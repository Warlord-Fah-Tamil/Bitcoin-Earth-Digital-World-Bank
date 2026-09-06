#include <edwb_mirror.h>

#include <cstdio>
#include <cstddef>
#include <mutex>
#include <queue>
#include <utility>

// ============================================================================
// PHASE B (3.7): WARLORD MIRROR QUEUE & BACKPRESSURE
// ============================================================================

namespace {

static constexpr std::size_t WARLORD_MIRROR_MAX_QUEUE_BLOCKS = 256;

static constexpr std::size_t WARLORD_MIRROR_MAX_QUEUE_BYTES =
    512ULL * 1024ULL * 1024ULL; // 512 MiB

std::mutex g_warlord_mirror_mutex;

std::queue<MainnetBlockMirror> g_warlord_mirror_queue;

std::size_t g_warlord_mirror_queue_bytes = 0;

// Safe memory estimation avoiding vtx traversal to prevent EDWB serialization mismatch crashes
std::size_t EstimateBlockMemorySize(const CBlock& block)
{
    // Use a safe standard block memory footprint (e.g., 1 MiB per block)
    // to prevent unsafe pointer dereference in EDWB custom transaction structures.
    return 1024ULL * 1024ULL;
}

} // namespace

// ============================================================================
// 3.7.1 QUEUE STATUS
// ============================================================================

bool IsMainnetMirrorQueueFull()
{
    std::lock_guard<std::mutex> lock(g_warlord_mirror_mutex);

    return
        g_warlord_mirror_queue.size() >=
            WARLORD_MIRROR_MAX_QUEUE_BLOCKS
        ||
        g_warlord_mirror_queue_bytes >=
            WARLORD_MIRROR_MAX_QUEUE_BYTES;
}

// ============================================================================
// 3.7.2 PUSH / BACKPRESSURE
// ============================================================================

bool PushMainnetMirrorBlock(MainnetBlockMirror mirror_block)
{
    const std::size_t incoming_size = EstimateBlockMemorySize(mirror_block.block);

    std::lock_guard<std::mutex> lock(g_warlord_mirror_mutex);

    if (g_warlord_mirror_queue.size() >=
        WARLORD_MIRROR_MAX_QUEUE_BLOCKS) {

        std::fprintf(
            stderr,
            "Warlord Mirror Backpressure: "
            "queue block limit reached (%zu/%zu), "
            "rejecting block\n",
            g_warlord_mirror_queue.size(),
            WARLORD_MIRROR_MAX_QUEUE_BLOCKS);

        return false;
    }

    if (g_warlord_mirror_queue_bytes >
        WARLORD_MIRROR_MAX_QUEUE_BYTES - incoming_size) {

        std::fprintf(
            stderr,
            "Warlord Mirror Backpressure: "
            "queue byte limit reached (%zu/%zu), "
            "incoming=%zu, rejecting block\n",
            g_warlord_mirror_queue_bytes,
            WARLORD_MIRROR_MAX_QUEUE_BYTES,
            incoming_size);

        return false;
    }

    g_warlord_mirror_queue_bytes += incoming_size;
    g_warlord_mirror_queue.push(std::move(mirror_block));

    return true;
}

// ============================================================================
// 3.7.3 POP / DRAIN
// ============================================================================

bool PopNextMainnetBlock(MainnetBlockMirror& mirror_out)
{
    std::lock_guard<std::mutex> lock(g_warlord_mirror_mutex);

    if (g_warlord_mirror_queue.empty()) {
        return false;
    }

    MainnetBlockMirror& front = g_warlord_mirror_queue.front();
    const std::size_t payload_size = EstimateBlockMemorySize(front.block);

    if (payload_size <= g_warlord_mirror_queue_bytes) {
        g_warlord_mirror_queue_bytes -= payload_size;
    } else {
        g_warlord_mirror_queue_bytes = 0;
    }

    mirror_out = std::move(front);
    g_warlord_mirror_queue.pop();

    return true;
}
