#include <uint256.h>
#include <vector>
#include <mutex>
#include <queue>
#include <utility>

struct MainnetMirrorPayload {
    uint256 block_hash;
    std::vector<uint8_t> raw_data;
};

static std::mutex g_edwb_bridge_mutex;
static std::queue<MainnetMirrorPayload> g_mainnet_block_queue;

void PushMainnetBlockToBridge(const uint256& hash, const std::vector<uint8_t>& data) {
    std::lock_guard<std::mutex> lock(g_edwb_bridge_mutex);
    if (g_mainnet_block_queue.size() > 10) {
        g_mainnet_block_queue.pop();
    }
    g_mainnet_block_queue.push({hash, data});
}

bool PopLatestMainnetBlock(uint256& hash_out, std::vector<uint8_t>& data_out) {
    std::lock_guard<std::mutex> lock(g_edwb_bridge_mutex);
    if (g_mainnet_block_queue.empty()) {
        return false;
    }
    
    auto payload = g_mainnet_block_queue.front();
    g_mainnet_block_queue.pop();
    
    hash_out = payload.block_hash;
    data_out = std::move(payload.raw_data);
    return true;
}
