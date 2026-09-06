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

namespace Consensus {

// ============================================================================
// EDWB SOVEREIGN CONSENSUS CONSTANTS & MONETARY ENGINE
// ============================================================================

static constexpr int EDWB_ANCHOR_HEIGHT = 965799;
static constexpr int EDWB_ACTIVATION_HEIGHT = 965800;
static constexpr int EDWB_EXPANSION_LIMIT = 1500000;
static constexpr int EDWB_EXPANSION_END_HEIGHT = 2465800;

inline const uint256 EDWB_ANCHOR_HASH = uint256{"0000000000000000000000000000000000000000000000000000000000000000"};
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

#endif // BITCOIN_CONSENSUS_EDWB_SUBSIDY_H