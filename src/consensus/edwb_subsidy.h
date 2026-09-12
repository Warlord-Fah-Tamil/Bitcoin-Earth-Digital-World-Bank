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

static constexpr int EDWB_ANCHOR_HEIGHT = 966599;
static constexpr int EDWB_ACTIVATION_HEIGHT = 966600;
static constexpr int EDWB_EXPANSION_LIMIT = 1500000;
static constexpr int EDWB_EXPANSION_END_HEIGHT = 2466600;

inline const uint256 EDWB_ANCHOR_HASH("39da3e5d9022592992f92a7d734e4354f7d3e405b9ef4d3b333af0de408aa46f");

inline constexpr std::array<int64_t, 10> EDWB_FIB_SEQUENCE{
    1, 1, 2, 3, 5, 8, 13, 21, 34, 55
};

inline CAmount GetNormalizedFibonacciSubsidy(int nHeight)
{
    if (nHeight <= EDWB_ACTIVATION_HEIGHT || nHeight > EDWB_EXPANSION_END_HEIGHT) {
        return 0;
    }

    const int64_t nStep = static_cast<int64_t>(nHeight) - static_cast<int64_t>(EDWB_ACTIVATION_HEIGHT);

    static constexpr CAmount EDWB_EXPANSION_SUPPLY = 21000000LL * COIN;
    static constexpr int64_t EDWB_FIB_TOTAL_WEIGHT = 21450000LL;

    const int64_t nCompletedCycles = (nStep - 1) / 10;
    const int64_t nPositionInCycle = (nStep - 1) % 10;

    static constexpr int64_t fibPrefix[10] = {
        1, 2, 4, 7, 12, 20, 33, 54, 88, 143
    };

    const int64_t nCumulativeWeight =
        nCompletedCycles * 143LL + fibPrefix[nPositionInCycle];

    const CAmount nCumulativeReward = static_cast<CAmount>(
        (static_cast<__int128>(nCumulativeWeight) * static_cast<__int128>(EDWB_EXPANSION_SUPPLY))
        / EDWB_FIB_TOTAL_WEIGHT
    );

    const int64_t nPreviousCumulativeWeight =
        nCompletedCycles * 143LL +
        (nPositionInCycle == 0 ? 0 : fibPrefix[nPositionInCycle - 1]);

    const CAmount nPreviousCumulativeReward = static_cast<CAmount>(
        (static_cast<__int128>(nPreviousCumulativeWeight) * static_cast<__int128>(EDWB_EXPANSION_SUPPLY))
        / EDWB_FIB_TOTAL_WEIGHT
    );

    return nCumulativeReward - nPreviousCumulativeReward;
}

inline CAmount GetBlockSubsidy(int nHeight, const Params& params)
{
    if (nHeight == EDWB_ACTIVATION_HEIGHT) {
        return 21000000LL * COIN;
    }

    if (nHeight > EDWB_ACTIVATION_HEIGHT && nHeight <= EDWB_EXPANSION_END_HEIGHT) {
        return GetNormalizedFibonacciSubsidy(nHeight);
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
    return GetNormalizedFibonacciSubsidy(nHeight);
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