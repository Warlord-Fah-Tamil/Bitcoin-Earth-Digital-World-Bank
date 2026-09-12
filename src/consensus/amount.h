// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CONSENSUS_AMOUNT_H
#define BITCOIN_CONSENSUS_AMOUNT_H

#include <cstdint>
#include <string>

/** Amount in satoshis (Can be negative) */
typedef int64_t CAmount;

/** The amount of satoshis in one BTC. */
inline constexpr CAmount COIN{100'000'000};

/** Earth Digital World Bank Expansion Constants */
inline constexpr CAmount MAX_MONEY_BASE{21'000'000 * COIN};
inline constexpr CAmount MAX_MONEY_EXPANSION{63'000'000 * COIN};

/** 
 * WARLORD VAULT ARCHITECTURE (BLOCK 965,900 HORIZON)
 * Phase 1: Direct Minting into Primary One Coin Vault (21,000,000 BTC) -> warlord_one
 * Phase 2: Secondary Allocation into 3 Foundation Pillars (7,000,000 BTC Each)
 */
inline constexpr CAmount ONE_COIN_PRIMARY_VAULT{21'000'000 * COIN}; // หลักยิงเข้าก้อนนี้ก่อน
inline const std::string WARLORD_ONE_ADDRESS{"bc1qrjw50j6pqv0m5k2x780r5j5an4dvvy0a9ggaaq"}; // ล็อคเป้าหมายถาวร

// 3 Foundation Pillars (โครงสร้างสัดส่วนเป้าหมาย 7M + 7M + 7M)
inline constexpr CAmount ONE_COIN_RESERVE{7'000'000 * COIN};
inline constexpr CAmount HEALTH_COIN_RESERVE{7'000'000 * COIN};
inline constexpr CAmount FOOD_COIN_RESERVE{7'000'000 * COIN};
inline constexpr CAmount THREE_FOUNDATION_TOTAL{ONE_COIN_RESERVE + HEALTH_COIN_RESERVE + FOOD_COIN_RESERVE};

/** Default MAX_MONEY for global sanity check */
inline constexpr CAmount MAX_MONEY{MAX_MONEY_EXPANSION};

/** Dynamic MAX_MONEY Expansion for Earth Digital World Bank */
inline CAmount GetMaxMoney(int nHeight) {
    // ปรับ Anchor Height เป็น 966,600 รองรับ 21M Vault ล็อคมงเข้า warlord_one ถาวร
    if (nHeight >= 966600) {
        return MAX_MONEY_EXPANSION; // 63,000,000 BTC เมื่อถึง Height 966,600 เป็นต้นไป
    }
    return MAX_MONEY_BASE; // 21,000,000 BTC ประวัติศาสตร์เดิมก่อนบล็อก 966,600
}

inline bool MoneyRange(const CAmount& nValue) { return (nValue >= 0 && nValue <= MAX_MONEY); }

#endif // BITCOIN_CONSENSUS_AMOUNT_H