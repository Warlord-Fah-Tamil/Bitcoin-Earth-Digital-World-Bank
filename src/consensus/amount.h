// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CONSENSUS_AMOUNT_H
#define BITCOIN_CONSENSUS_AMOUNT_H

#include <cstdint>

/** Amount in satoshis (Can be negative) */
typedef int64_t CAmount;

/** The amount of satoshis in one BTC. */
inline constexpr CAmount COIN{100'000'000};

/** Earth Digital World Bank Expansion Constants */
inline constexpr CAmount MAX_MONEY_BASE{21'000'000 * COIN};
inline constexpr CAmount MAX_MONEY_EXPANSION{63'000'000 * COIN};

/** Default MAX_MONEY for global sanity check */
inline constexpr CAmount MAX_MONEY{MAX_MONEY_EXPANSION};

/** Dynamic MAX_MONEY Expansion for Earth Digital World Bank */
inline CAmount GetMaxMoney(int nHeight) {
    if (nHeight >= 964000) {
        return MAX_MONEY_EXPANSION; // 63,000,000 BTC เมื่อถึง Height อนาคต
    }
    return MAX_MONEY_BASE; // 21,000,000 BTC ประวัติศาสตร์เดิม
}

inline bool MoneyRange(const CAmount& nValue) { return (nValue >= 0 && nValue <= MAX_MONEY); }

#endif // BITCOIN_CONSENSUS_AMOUNT_H
