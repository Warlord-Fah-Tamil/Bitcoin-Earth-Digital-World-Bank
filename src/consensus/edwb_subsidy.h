// Copyright (c) 2026 The EDWB Sovereign Network
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CONSENSUS_EDWB_SUBSIDY_H
#define BITCOIN_CONSENSUS_EDWB_SUBSIDY_H

#include <consensus/amount.h>
#include <consensus/params.h>
#include <uint256.h>
#include <script/script.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Consensus {

static constexpr int EDWB_ANCHOR_HEIGHT = 965899;
static constexpr int EDWB_ACTIVATION_HEIGHT = 965900;
static constexpr int EDWB_EXPANSION_LIMIT = 1500000;
static constexpr int EDWB_EXPANSION_END_HEIGHT = 2465900;

inline const uint256 EDWB_ANCHOR_HASH("0000000000000000000000000000000000000000000000000000000000000000");
inline constexpr std::array<int64_t, 10> EDWB_FIB_SEQUENCE{
    1, 1, 2, 3, 5, 8, 13, 21, 34, 55
};

static constexpr CAmount EDWB_PHASE2_SUBSIDY = 100 * COIN;

inline CAmount GetBlockSubsidy(int nHeight, const Params& params)
{
    if (nHeight == EDWB_ACTIVATION_HEIGHT) {
        return 21000000 * COIN;
    }

    if (nHeight > EDWB_ACTIVATION_HEIGHT && nHeight <= EDWB_EXPANSION_END_HEIGHT) {
        return EDWB_PHASE2_SUBSIDY;
    }

    if (nHeight > EDWB_EXPANSION_END_HEIGHT) {
        return 0;
    }

    const int halvings = nHeight / params.nSubsidyHalvingInterval;
    if (halvings >= 64) {
        return 0;
    }

    CAmount nSubsidy = 50 * COIN;
    nSubsidy >>= halvings;
    return nSubsidy;
}

inline CAmount GetEDWBVaultSubsidy(int nHeight)
{
    if (nHeight <= EDWB_ACTIVATION_HEIGHT || nHeight > EDWB_EXPANSION_END_HEIGHT) {
        return 0;
    }

    const std::size_t index = static_cast<std::size_t>(
        (nHeight - EDWB_ACTIVATION_HEIGHT - 1) % EDWB_FIB_SEQUENCE.size());

    return CAmount{EDWB_FIB_SEQUENCE[index]} * COIN;
}

inline CScript GetEDWBVaultScriptPubKey()
{
    return CScript() << OP_TRUE;
}

} // namespace Consensus

// ============================================================================
// EDWB MIRROR STORAGE, QUEUE & BACKPRESSURE INTERFACES
// ============================================================================

struct MainnetBlockMirror {
    int height;
    uint256 hash;
    std::vector<uint8_t> vchData;
};

bool IsMainnetMirrorQueueFull();
bool PushMainnetMirrorBlock(MainnetBlockMirror mirror_item);
bool PopNextMainnetBlock(MainnetBlockMirror& mirror_out);
std::size_t GetMainnetMirrorQueueSize();
std::size_t GetMainnetMirrorQueueBytes();

// ============================================================================
// PHASE A / B (3.8): MAINNET MIRROR CHECKPOINT
// ============================================================================

static constexpr uint32_t WARLORD_MIRROR_CHECKPOINT_VERSION = 1;

struct MainnetMirrorCheckpoint
{
    uint32_t version{WARLORD_MIRROR_CHECKPOINT_VERSION};

    // -1 = no valid checkpoint established.
    int height{-1};

    // Mainnet block hash at checkpoint height.
    uint256 block_hash;

    // Hash of the raw Mainnet block payload.
    uint256 payload_hash;

    SERIALIZE_METHODS(MainnetMirrorCheckpoint, obj)
    {
        READWRITE(
            obj.version,
            obj.height,
            obj.block_hash,
            obj.payload_hash
        );
    }

    bool IsValid() const
    {
        return
            version == WARLORD_MIRROR_CHECKPOINT_VERSION &&
            height >= 0 &&
            !block_hash.IsNull() &&
            !payload_hash.IsNull();
    }
};

// ---------------------------------------------------------------------------
// Phase A / B checkpoint interface
// ---------------------------------------------------------------------------

// Load persistent Mirror checkpoint.
//
// Returns false when no valid checkpoint exists yet.
bool LoadMirrorCheckpoint(
    MainnetMirrorCheckpoint& checkpoint_out);

// Save Mirror checkpoint.
//
// The implementation persists the checkpoint to the dedicated
// edwb_mirror database.
bool SaveMirrorCheckpoint(
    const MainnetMirrorCheckpoint& checkpoint_in);

// Return the currently loaded checkpoint.
//
// If no checkpoint exists, height == -1.
MainnetMirrorCheckpoint GetCurrentMirrorCheckpoint();

// ============================================================================
// 3.9 HISTORICAL BACKFILL API
//
// Resumes automatically from the current persistent Mirror checkpoint
// and processes historical Mainnet blocks up to target_height.
//
// target_height = -1:
//     Continue until the current Active Chain tip.
//
// target_height >= 0:
//     Continue until the specified target height.
//
// The function never modifies Bitcoin Core chainstate, UTXO state,
// block index, or consensus state.
// ============================================================================

bool BackfillEDWBHistoricalData(int target_height = -1);

#endif // BITCOIN_CONSENSUS_EDWB_SUBSIDY_H