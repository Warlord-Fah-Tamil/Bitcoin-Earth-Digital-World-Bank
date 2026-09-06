// Copyright (c) 2026 The EDWB Sovereign Network
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/edwb_subsidy.h>

#include <cstddef>
#include <limits>
#include <memory>
#include <mutex>
#include <utility>

#include <dbwrapper.h>
#include <logging.h>
#include <util/fs.h>
#include <util/system.h>
#include <chain.h>
#include <chainparams.h>
#include <validation.h>

// ============================================================================
// PHASE B (3.8.2): PERSISTENT MAINNET MIRROR CHECKPOINT & STARTUP
//
// Purpose:
//   - Manage persistent checkpoints for the EDWB Mainnet Mirror.
//   - Keep Mirror data completely separate from chainstate / UTXO.
// ============================================================================

namespace {

static constexpr char WARLORD_MIRROR_DB_DIR[] = "edwb_mirror";
static constexpr uint8_t DB_MIRROR_CHECKPOINT = 'C';
static constexpr std::uint64_t WARLORD_MIRROR_DB_CACHE = 8ULL * 1024ULL * 1024ULL; // 8 MiB

std::mutex g_warlord_checkpoint_mutex;
std::unique_ptr<CDBWrapper> g_warlord_mirror_db;
MainnetMirrorCheckpoint g_current_checkpoint;

// Flag to track whether startup-load has been attempted.
bool g_checkpoint_loaded_from_disk = false;

bool EnsureMirrorDatabaseOpen()
{
    if (g_warlord_mirror_db) {
        return true;
    }

    try {
        const fs::path mirror_db_path = GetDataDir() / WARLORD_MIRROR_DB_DIR;

        DBParams params{
            .path = mirror_db_path,
            .cache_bytes = WARLORD_MIRROR_DB_CACHE,
            .memory_only = false,
            .wipe_data = false,
            .obfuscate = true,
            .bloom_filter = true,
        };

        g_warlord_mirror_db = std::make_unique<CDBWrapper>(params);

        LogPrintf(
            "Warlord Mirror: Persistent database opened at %s\n",
            fs::PathToString(mirror_db_path)
        );

        return true;

    } catch (const std::exception& e) {

        LogPrintf(
            "Warlord Mirror ERROR: failed to open persistent database: %s\n",
            e.what()
        );

        g_warlord_mirror_db.reset();
        return false;
    }
}

// Internal helper for startup loading without redundant mutex acquisition if already locked
bool LoadMirrorCheckpointInternal(MainnetMirrorCheckpoint& checkpoint_out)
{
    if (!EnsureMirrorDatabaseOpen()) {
        return false;
    }

    MainnetMirrorCheckpoint checkpoint;

    if (!g_warlord_mirror_db->Read(DB_MIRROR_CHECKPOINT, checkpoint)) {
        g_current_checkpoint = MainnetMirrorCheckpoint{};
        LogPrint(
            BCLog::NET,
            "Warlord Mirror: no persistent checkpoint found; starting from height -1\n"
        );
        return false;
    }

    if (!checkpoint.IsValid()) {
        LogPrintf(
            "Warlord Mirror Checkpoint ERROR: persistent checkpoint is invalid "
            "(version=%u height=%d block_hash=%s payload_hash=%s)\n",
            checkpoint.version,
            checkpoint.height,
            checkpoint.block_hash.ToString(),
            checkpoint.payload_hash.ToString()
        );
        g_current_checkpoint = MainnetMirrorCheckpoint{};
        return false;
    }

    g_current_checkpoint = checkpoint;
    checkpoint_out = checkpoint;

    LogPrint(
        BCLog::NET,
        "Warlord Mirror Checkpoint loaded from disk: "
        "version=%u height=%d block_hash=%s payload_hash=%s\n",
        checkpoint.version,
        checkpoint.height,
        checkpoint.block_hash.ToString(),
        checkpoint.payload_hash.ToString()
    );

    return true;
}

} // namespace

// ============================================================================
// 3.8.2 LOAD CHECKPOINT FROM DISK
// ============================================================================

bool LoadMirrorCheckpoint(MainnetMirrorCheckpoint& checkpoint_out)
{
    std::lock_guard<std::mutex> lock(g_warlord_checkpoint_mutex);

    g_checkpoint_loaded_from_disk = true;
    if (!EnsureMirrorDatabaseOpen()) {
        return false;
    }

    return LoadMirrorCheckpointInternal(checkpoint_out);
}

// ============================================================================
// 3.8.2 SAVE CHECKPOINT TO DISK (WITH REGRESSION & SAME-HEIGHT CONFLICT CHECK)
// ============================================================================

bool SaveMirrorCheckpoint(const MainnetMirrorCheckpoint& checkpoint_in)
{
    if (!checkpoint_in.IsValid()) {
        LogPrintf(
            "Warlord Mirror Checkpoint ERROR: attempted to save invalid checkpoint "
            "(version=%u height=%d block_hash=%s payload_hash=%s)\n",
            checkpoint_in.version,
            checkpoint_in.height,
            checkpoint_in.block_hash.ToString(),
            checkpoint_in.payload_hash.ToString()
        );
        return false;
    }

    std::lock_guard<std::mutex> lock(g_warlord_checkpoint_mutex);

    // Force startup load check if not yet performed before evaluating conflicts
    if (!g_checkpoint_loaded_from_disk) {
        g_checkpoint_loaded_from_disk = true;
        MainnetMirrorCheckpoint dummy;
        LoadMirrorCheckpointInternal(dummy);
    }

    if (!EnsureMirrorDatabaseOpen()) {
        return false;
    }

    // ------------------------------------------------------------
    // Prevent checkpoint regression & same-height hash conflicts.
    // ------------------------------------------------------------
    if (g_current_checkpoint.IsValid()) {
        if (checkpoint_in.height < g_current_checkpoint.height) {
            LogPrintf(
                "Warlord Mirror Checkpoint WARNING: rejecting checkpoint regression "
                "(current_height=%d requested_height=%d)\n",
                g_current_checkpoint.height,
                checkpoint_in.height
            );
            return false;
        }

        if (checkpoint_in.height == g_current_checkpoint.height &&
            checkpoint_in.block_hash != g_current_checkpoint.block_hash) {
            LogPrintf(
                "Warlord Mirror Checkpoint ERROR: rejecting same-height conflict! "
                "Height %d already recorded with block_hash=%s, but attempted write has block_hash=%s\n",
                g_current_checkpoint.height,
                g_current_checkpoint.block_hash.ToString(),
                checkpoint_in.block_hash.ToString()
            );
            return false;
        }
    }

    // ------------------------------------------------------------
    // Persistent synchronous write.
    // ------------------------------------------------------------
    try {
        g_warlord_mirror_db->Write(
            DB_MIRROR_CHECKPOINT,
            checkpoint_in,
            true
        );
    } catch (const std::exception& e) {
        LogPrintf(
            "Warlord Mirror Checkpoint ERROR: persistent write failed: %s\n",
            e.what()
        );
        return false;
    }

    // ------------------------------------------------------------
    // Update in-memory state after successful disk write.
    // ------------------------------------------------------------
    g_current_checkpoint = checkpoint_in;

    LogPrint(
        BCLog::NET,
        "Warlord Mirror Checkpoint persisted: "
        "height=%d block_hash=%s payload_hash=%s\n",
        checkpoint_in.height,
        checkpoint_in.block_hash.ToString(),
        checkpoint_in.payload_hash.ToString()
    );

    return true;
}

// ============================================================================
// 3.8.2 GET CURRENT CHECKPOINT (WITH AUTOMATIC STARTUP LOAD)
// ============================================================================

MainnetMirrorCheckpoint GetCurrentMirrorCheckpoint()
{
    std::lock_guard<std::mutex> lock(g_warlord_checkpoint_mutex);

    if (!g_checkpoint_loaded_from_disk) {
        g_checkpoint_loaded_from_disk = true;
        MainnetMirrorCheckpoint dummy;
        LoadMirrorCheckpointInternal(dummy);
    }

    return g_current_checkpoint;
}

// ============================================================================
// PHASE B (3.9): HISTORICAL BACKFILL
//
// Purpose:
//   Reconstruct the EDWB Mainnet Mirror checkpoint from historical
//   Bitcoin Mainnet blocks.
//
// IMPORTANT:
//   - Does NOT modify chainstate.
//   - Does NOT modify the UTXO set.
//   - Does NOT modify consensus state.
//   - Does NOT modify the block index.
//   - Writes only to the EDWB persistent mirror database.
//   - Uses the persistent checkpoint from Phase B (3.8.2).
//
// Resume model:
//
//   checkpoint.height = H
//         |
//         v
//   resume from H + 1
//
// If the process stops unexpectedly, the last successfully persisted
// checkpoint remains the recovery point.
//
// ============================================================================

bool BackfillEDWBHistoricalData(int target_height)
{
    // ------------------------------------------------------------------------
    // 1. Load the current persistent checkpoint.
    // ------------------------------------------------------------------------

    const MainnetMirrorCheckpoint current_ckpt =
        GetCurrentMirrorCheckpoint();

    int start_height = 0;

    if (current_ckpt.IsValid()) {

        start_height = current_ckpt.height + 1;

        LogPrintf(
            "Warlord Mirror Backfill: "
            "resuming from checkpoint height %d (next=%d)\n",
            current_ckpt.height,
            start_height
        );

    } else {

        LogPrintf(
            "Warlord Mirror Backfill: "
            "no valid checkpoint found, starting from height 0\n"
        );
    }

    // ------------------------------------------------------------------------
    // 2. Snapshot the current active-chain tip.
    //
    // IMPORTANT:
    //   cs_main is held only while reading chain state.
    //   It is NOT held during block disk I/O or LevelDB writes.
    // ------------------------------------------------------------------------

    int max_height = -1;

    {
        LOCK(cs_main);

        max_height = ::ChainActive().Height();
    }

    if (max_height < 0) {

        LogPrintf(
            "Warlord Mirror Backfill ERROR: "
            "active chain is not available\n"
        );

        return false;
    }

    // ------------------------------------------------------------------------
    // 3. Apply optional target height.
    //
    // target_height < 0 means:
    //   backfill up to the current active-chain tip.
    // ------------------------------------------------------------------------

    if (target_height >= 0 &&
        target_height < max_height) {

        max_height = target_height;
    }

    // ------------------------------------------------------------------------
    // 4. Validate the requested range.
    // ------------------------------------------------------------------------

    if (start_height > max_height) {

        LogPrint(
            BCLog::NET,
            "Warlord Mirror Backfill: "
            "already up to date (start=%d tip=%d)\n",
            start_height,
            max_height
        );

        return true;
    }

    LogPrintf(
        "Warlord Mirror Backfill: "
        "starting historical sync from height %d to %d\n",
        start_height,
        max_height
    );

    // ------------------------------------------------------------------------
    // 5. Process historical blocks sequentially.
    // ------------------------------------------------------------------------

    for (int h = start_height; h <= max_height; ++h) {

        CBlockIndex* pindex = nullptr;

        // --------------------------------------------------------------------
        // Obtain the active-chain block index entry.
        // --------------------------------------------------------------------

        {
            LOCK(cs_main);

            pindex = ::ChainActive()[h];

            if (pindex == nullptr) {

                LogPrintf(
                    "Warlord Mirror Backfill ERROR: "
                    "active-chain block index missing at height %d\n",
                    h
                );

                return false;
            }
        }

        const uint256 block_hash =
            pindex->GetBlockHash();

        // --------------------------------------------------------------------
        // Read historical block from disk.
        //
        // This operation does NOT connect the block and does NOT modify
        // chainstate / UTXO state.
        // --------------------------------------------------------------------

        CBlock block;

        if (!ReadBlockFromDisk(
                block,
                *pindex,
                Params().GetConsensus())) {

            LogPrintf(
                "Warlord Mirror Backfill ERROR: "
                "failed to read historical block at height %d "
                "(hash=%s)\n",
                h,
                block_hash.ToString()
            );

            return false;
        }

        // --------------------------------------------------------------------
        // 6. Verify that the block read from disk matches the block index.
        // --------------------------------------------------------------------

        if (block.GetHash() != block_hash) {

            LogPrintf(
                "Warlord Mirror Backfill ERROR: "
                "block hash mismatch at height %d "
                "(index=%s disk=%s)\n",
                h,
                block_hash.ToString(),
                block.GetHash().ToString()
            );

            return false;
        }

        // --------------------------------------------------------------------
        // 7. Construct EDWB Mirror payload hash.
        //
        // This is metadata integrity for the Mirror.
        //
        // It is NOT Bitcoin consensus validation.
        // It does NOT modify UTXO state.
        // --------------------------------------------------------------------

        CHashWriter ss(
            SER_GETHASH,
            PROTOCOL_VERSION
        );

        ss << block.GetHash();
        ss << block.hashMerkleRoot;
        ss << block.nTime;

        const uint256 payload_hash =
            ss.GetHash();

        // --------------------------------------------------------------------
        // 8. Construct the next persistent checkpoint.
        // --------------------------------------------------------------------

        MainnetMirrorCheckpoint next_ckpt;

        next_ckpt.version =
            WARLORD_MIRROR_CHECKPOINT_VERSION;

        next_ckpt.height = h;

        next_ckpt.block_hash =
            block_hash;

        next_ckpt.payload_hash =
            payload_hash;

        // --------------------------------------------------------------------
        // 9. Persist checkpoint.
        //
        // SaveMirrorCheckpoint() already provides:
        //
        //   - validity check
        //   - regression protection
        //   - same-height conflict protection
        //   - synchronous LevelDB persistence
        //   - in-memory state update after successful disk write
        //
        // If persistence fails, stop immediately.
        // The previous checkpoint remains the recovery point.
        // --------------------------------------------------------------------

        if (!SaveMirrorCheckpoint(next_ckpt)) {

            LogPrintf(
                "Warlord Mirror Backfill ERROR: "
                "failed to persist checkpoint at height %d "
                "(hash=%s)\n",
                h,
                block_hash.ToString()
            );

            return false;
        }

        // --------------------------------------------------------------------
        // 10. Progress logging.
        // --------------------------------------------------------------------

        if ((h % 1000) == 0 ||
            h == max_height) {

            LogPrintf(
                "Warlord Mirror Backfill progress: "
                "height %d/%d processed successfully\n",
                h,
                max_height
            );
        }
    }

    // ------------------------------------------------------------------------
    // 11. Completed successfully.
    // ------------------------------------------------------------------------

    LogPrintf(
        "Warlord Mirror Backfill completed successfully "
        "up to height %d\n",
        max_height
    );

    return true;
}