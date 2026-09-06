// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <node/miner.h>

#include <chain.h>
#include <chainparams.h>
#include <common/args.h>
#include <consensus/amount.h>
#include <consensus/consensus.h>
#include <consensus/merkle.h>
#include <consensus/params.h>
#include <consensus/tx_verify.h>
#include <consensus/validation.h>
#include <interfaces/types.h>
#include <node/blockstorage.h>
#include <node/kernel_notifications.h>
#include <node/mining_args.h>
#include <node/mining_types.h>
#include <policy/feerate.h>
#include <policy/policy.h>
#include <pow.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <sync.h>
#include <tinyformat.h>
#include <txgraph.h>
#include <txmempool.h>
#include <uint256.h>
#include <util/check.h>
#include <util/feefrac.h>
#include <util/log.h>
#include <util/result.h>
#include <util/signalinterrupt.h>
#include <util/time.h>
#include <util/translation.h>
#include <validation.h>
#include <validationinterface.h>
#include <versionbits.h>
#include <key_io.h>
#include <algorithm>
#include <compare>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <numeric>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <logging.h>
#include <util/strencodings.h>

// === EDWB & WARLORD INCLUDES & MIRROR FUNCTION ===
#include <consensus/edwb_subsidy.h>

extern bool PopLatestMainnetBlock(
    uint256& hash_out,
    std::vector<uint8_t>& data_out);
// ==============================================

namespace node {

int64_t GetMinimumTime(const CBlockIndex* pindexPrev, const int64_t difficulty_adjustment_interval)
{
    int64_t min_time{pindexPrev->GetMedianTimePast() + 1};
    const int height{pindexPrev->nHeight + 1};
    if (height % difficulty_adjustment_interval == 0) {
        min_time = std::max<int64_t>(min_time, pindexPrev->GetBlockTime() - MAX_TIMEWARP);
    }
    return min_time;
}

int64_t UpdateTime(CBlockHeader* pblock, const Consensus::Params& consensusParams, const CBlockIndex* pindexPrev)
{
    int64_t nOldTime = pblock->nTime;
    int64_t nNewTime{std::max<int64_t>(GetMinimumTime(pindexPrev, consensusParams.DifficultyAdjustmentInterval()),
                                        TicksSinceEpoch<std::chrono::seconds>(NodeClock::now()))};

    if (nOldTime < nNewTime) {
        pblock->nTime = nNewTime;
    }

    if (consensusParams.fPowAllowMinDifficultyBlocks) {
        pblock->nBits = GetNextWorkRequired(pindexPrev, pblock, consensusParams);
    }

    return nNewTime - nOldTime;
}

void RegenerateCommitments(CBlock& block, ChainstateManager& chainman)
{
    // 1. ดึงข้อมูล Mainnet Block Mirror ที่มี Height ชัดเจน
    MainnetBlockMirror mirror;
    if (PopNextMainnetBlock(mirror)) {
        CMutableTransaction tx{*block.vtx.at(0)};
        
        // ลบ Witness Commitment เดิมออกก่อน (ถ้ามี) เพื่อป้องกันตำแหน่ง index เพี้ยน
        int witness_index = GetWitnessCommitmentIndex(block);
        if (witness_index != -1 && witness_index < (int)tx.vout.size()) {
            tx.vout.erase(tx.vout.begin() + witness_index);
        }

        // คำนวณ Payload Hash ด้วยมาตรฐาน double-sha256 (Hash() ใน Bitcoin Core คือ SHA256d)
        uint256 payload_hash = Hash(mirror.vchData);

        // สร้าง EDWB Protocol Mirror OP_RETURN script
        CScript mirror_script;
        uint32_t edwb_proto_version = 1;

        mirror_script << OP_RETURN
                      << std::vector<unsigned char>{'E', 'D', 'W', 'B'}
                      << edwb_proto_version
                      << static_cast<uint32_t>(mirror.height)
                      << ToByteVector(mirror.hash)
                      << ToByteVector(payload_hash);

        // เพิ่ม Mirror OP_RETURN ลงใน Coinbase vout (value = 0 ไม่สร้าง UTXO ขยะใน LevelDB)
        tx.vout.push_back(CTxOut(0, mirror_script));

        // อัปเดตทรานแซกชัน Coinbase กลับเข้าบล็อก
        block.vtx.at(0) = MakeTransactionRef(tx);
    }

    // 2. เรียกใช้ฟังก์ชันมาตรฐานของ Bitcoin Core สำหรับสร้าง Witness Commitment ต่อท้าย
    const CBlockIndex* prev_block = WITH_LOCK(::cs_main, return chainman.m_blockman.LookupBlockIndex(block.hashPrevBlock));
    chainman.GenerateCoinbaseCommitment(block, prev_block);

    // 3. คำนวณ Merkle Root ใหม่ทั้งบล็อกหลังการเปลี่ยนแปลง Coinbase เสร็จสมบูรณ์
    block.hashMerkleRoot = BlockMerkleRoot(block);
}
BlockAssembler::BlockAssembler(Chainstate& chainstate,
                               const CTxMemPool* mempool,
                               BlockCreateOptions options)
    : chainparams{chainstate.m_chainman.GetParams()},
      m_mempool{options.use_mempool ? mempool : nullptr},
      m_chainstate{chainstate},
      m_options{[&] {
          if (auto result{CheckMiningOptions(options, /*use_argnames=*/false)}; !result) {
              throw std::runtime_error(util::ErrorString(result).original);
          }
          return FlattenMiningOptions(std::move(options));
      }()}
{
}

void BlockAssembler::resetBlock()
{
    nBlockWeight = *Assert(m_options.block_reserved_weight);
    nBlockSigOpsCost = m_options.coinbase_output_max_additional_sigops;
    nBlockTx = 0;
    nFees = 0;
}

std::unique_ptr<CBlockTemplate> BlockAssembler::CreateNewBlock()
{
    const auto time_start{SteadyClock::now()};

    resetBlock();

    pblocktemplate.reset(new CBlockTemplate());
    CBlock* const pblock = &pblocktemplate->block;

    pblock->vtx.emplace_back();

    LOCK(::cs_main);
    CBlockIndex* pindexPrev = m_chainstate.m_chain.Tip();
    assert(pindexPrev != nullptr);
    nHeight = pindexPrev->nHeight + 1;

    // --- Warlord Phase 1: Signaling Bit (Bit 29) ---
    static constexpr int32_t WARLORD_SIGNAL_BIT = (1 << 29);
    pblock->nVersion = m_chainstate.m_chainman.m_versionbitscache.ComputeBlockVersion(pindexPrev, chainparams.GetConsensus()) | WARLORD_SIGNAL_BIT;

    if (chainparams.MineBlocksOnDemand()) {
        pblock->nVersion = gArgs.GetIntArg("-blockversion", pblock->nVersion);
    }

    pblock->nTime = TicksSinceEpoch<std::chrono::seconds>(NodeClock::now());
    m_lock_time_cutoff = pindexPrev->GetMedianTimePast();

    if (m_mempool) {
        LOCK(m_mempool->cs);
        m_mempool->StartBlockBuilding();
        addChunks();
        m_mempool->StopBlockBuilding();
    }
    const auto time_1{SteadyClock::now()};

    m_last_block_num_txs = nBlockTx;
    m_last_block_weight = nBlockWeight;

    // Create coinbase transaction.
    CMutableTransaction coinbaseTx;
    coinbaseTx.vin.resize(1);
    coinbaseTx.vin[0].prevout.SetNull();

    // ============================================================================
    // EDWB CONFIGURATION CONSTANTS & DEFAULTS
    // ============================================================================
    const CAmount block_reward{
            nFees + Consensus::GetBlockSubsidy(nHeight, chainparams.GetConsensus())
        };

        // จัดการสร้างโครงสร้าง Coinbase ตามกฎ EDWB Sovereign Consensus
        if (nHeight == Consensus::EDWB_ACTIVATION_HEIGHT) {
            // Phase 1: Block 965900 (21M COIN Vault)
            coinbaseTx.vout.clear();

            // สร้าง ScriptPubKey สำหรับ Vault (ใช้ P2PKH หรือ Script มาตรฐานตามโครงสร้างโปรเจกต์)
            CScript vaultScript = GetScriptForDestination(WitnessV0KeyHash{CKeyID()}); // หรือปรับตามฟังก์ชันกระเป๋าเงินของคุณ
            coinbaseTx.vout.push_back(
                CTxOut((21000000 * COIN) + nFees, vaultScript)
            );

            const std::string edwb_magic = "EARTH_DIGITAL_WORLD_BANK_PHASE1_21M_VAULT";
            const CScript scriptEDWB = CScript() << OP_RETURN << std::vector<unsigned char>(edwb_magic.begin(), edwb_magic.end());
            coinbaseTx.vout.push_back(CTxOut(0, scriptEDWB));
    }
        else if (nHeight > Consensus::EDWB_ACTIVATION_HEIGHT && nHeight <= Consensus::EDWB_EXPANSION_END_HEIGHT) {
        // Phase 2: Fibonacci Expansion + ล็อคมงเข้ากระเป๋า warlord_one ถาวร
        coinbaseTx.vout.clear();

        const CAmount nSubsidy = Consensus::GetBlockSubsidy(nHeight, chainparams.GetConsensus());
        const CAmount nVaultSubsidy = Consensus::GetEDWBVaultSubsidy(nHeight);
        const CAmount nMinerSubsidy = nSubsidy - nVaultSubsidy;

        // แปลง Address warlord_one ให้เป็น ScriptPubKey ตายตัวตรงนี้เลย
        CTxDestination warlordDest = DecodeDestination("bc1qrjw50j6pqv0m5k2x780r5j5an4dvvy0a9ggaaq");
        CScript warlordScript = GetScriptForDestination(warlordDest);

        // ยัดเข้า Miner Subsidy แบบไร้รอยต่อ
        coinbaseTx.vout.push_back(
            CTxOut(nMinerSubsidy + nFees, warlordScript)
        );
        // ส่วน Vault Subsidy ยังวิ่งเข้าที่เดิมตามระบบ
        coinbaseTx.vout.push_back(
            CTxOut(nVaultSubsidy, Consensus::GetEDWBVaultScriptPubKey())
        );
    }
    else if (nHeight > Consensus::EDWB_EXPANSION_END_HEIGHT) {
        // Phase 3: Zero Subsidy (Fees Only)
        coinbaseTx.vout.clear();
        coinbaseTx.vout.push_back(
            CTxOut(nFees, m_options.coinbase_output_script)
        );
    }
    else {
        // Pre-Activation (Original Bitcoin)
        coinbaseTx.vout.resize(1);
        coinbaseTx.vout[0].scriptPubKey = m_options.coinbase_output_script;
        coinbaseTx.vout[0].nValue = block_reward;
    }

    coinbaseTx.vin[0].scriptSig = CScript() << nHeight;
    if (nHeight <= 16) {
        coinbaseTx.vin[0].scriptSig << OP_0;
    }
    Assert(nHeight > 0);
    coinbaseTx.nLockTime = static_cast<uint32_t>(nHeight - 1);

    // ========================================================================
    // FINALIZING COINBASE & BLOCK HEADERS
    // ========================================================================
    pblock->vtx[0] = MakeTransactionRef(std::move(coinbaseTx));
    m_chainstate.m_chainman.GenerateCoinbaseCommitment(*pblock, pindexPrev);

    const CTransactionRef& final_coinbase{pblock->vtx[0]};
    if (final_coinbase->HasWitness()) {
        const auto& witness_stack{final_coinbase->vin[0].scriptWitness.stack};
        Assert(witness_stack.size() == 1 && witness_stack[0].size() == 32);
    }
    if (const int witness_index = GetWitnessCommitmentIndex(*pblock); witness_index != NO_WITNESS_COMMITMENT) {
        Assert(witness_index >= 0 && static_cast<size_t>(witness_index) < final_coinbase->vout.size());
    }

    LogInfo("CreateNewBlock(): block weight: %u txs: %u fees: %ld sigops %d\n", GetBlockWeight(*pblock), nBlockTx, nFees, nBlockSigOpsCost);

    pblock->hashPrevBlock  = pindexPrev->GetBlockHash();
    UpdateTime(pblock, chainparams.GetConsensus(), pindexPrev);
    pblock->nBits         = GetNextWorkRequired(pindexPrev, pblock, chainparams.GetConsensus());
    static uint32_t custom_nonce = 0;
    pblock->nNonce = ++custom_nonce;

    if (m_options.test_block_validity) {
        if (BlockValidationState state{TestBlockValidity(m_chainstate, *pblock, /*check_pow=*/false, /*check_merkle_root=*/false)}; !state.IsValid()) {
            throw std::runtime_error(strprintf("TestBlockValidity failed: %s", state.ToString()));
        }
    }
    const auto time_2{SteadyClock::now()};

    LogDebug(BCLog::BENCH, "CreateNewBlock() chunks: %.2fms, validity: %.2fms (total %.2fms)\n",
             Ticks<MillisecondsDouble>(time_1 - time_start),
             Ticks<MillisecondsDouble>(time_2 - time_1),
             Ticks<MillisecondsDouble>(time_2 - time_start));

    return std::move(pblocktemplate);
} // <--- ปิดปีกกาของฟังก์ชัน CreateNewBlock() ตรงนี้ให้เด็ดขาด ห้ามให้ฟังก์ชันอื่นหลุดเข้าไปข้างใน!

// ========================================================================
// นอกเหนือจากนี้คือเมธอดระดับคลาส BlockAssembler ตัวอื่นๆ (อยู่นอกฟังก์ชันหลัก)
// ========================================================================
bool BlockAssembler::TestChunkBlockLimits(FeePerWeight chunk_feerate, int64_t chunk_sigops_cost) const
{
    Assert(m_options.block_max_weight);
    if (nBlockWeight + chunk_feerate.size >= *m_options.block_max_weight) {
        return false;
    }
    if (nBlockSigOpsCost + chunk_sigops_cost >= MAX_BLOCK_SIGOPS_COST) {
        return false;
    }
    return true;
}

bool BlockAssembler::TestChunkTransactions(const std::vector<CTxMemPoolEntryRef>& txs) const
{
    for (const auto tx : txs) {
        if (!IsFinalTx(tx.get().GetTx(), nHeight, m_lock_time_cutoff)) {
            return false;
        }
    }
    return true;
}

void BlockAssembler::AddToBlock(const CTxMemPoolEntry& entry)
{
    pblocktemplate->block.vtx.emplace_back(entry.GetSharedTx());
    pblocktemplate->vTxFees.push_back(entry.GetFee());
    pblocktemplate->vTxSigOpsCost.push_back(entry.GetSigOpCost());
    nBlockWeight += entry.GetTxWeight();
    ++nBlockTx;
    nBlockSigOpsCost += entry.GetSigOpCost();
    nFees += entry.GetFee();

    if (*m_options.print_modified_fee) {
        LogInfo("fee rate %s txid %s\n",
                CFeeRate(entry.GetModifiedFee(), entry.GetTxSize()).ToString(),
                entry.GetTx().GetHash().ToString());
    }
}

void BlockAssembler::addChunks()
{
    const int64_t MAX_CONSECUTIVE_FAILURES = 1000;
    constexpr int32_t BLOCK_FULL_ENOUGH_WEIGHT_DELTA = 4000;
    int64_t nConsecutiveFailed = 0;

    std::vector<CTxMemPoolEntry::CTxMemPoolEntryRef> selected_transactions;
    selected_transactions.reserve(MAX_CLUSTER_COUNT_LIMIT);
    FeePerWeight chunk_feerate;

    chunk_feerate = m_mempool->GetBlockBuilderChunk(selected_transactions);
    FeePerVSize chunk_feerate_vsize = ToFeePerVSize(chunk_feerate);

    while (selected_transactions.size() > 0) {
        if (ByRatio{chunk_feerate_vsize} < ByRatio{m_options.block_min_fee_rate->GetFeePerVSize()}) {
            return;
        }

        int64_t chunk_sig_ops = 0;
        for (const auto& tx : selected_transactions) {
            chunk_sig_ops += tx.get().GetSigOpCost();
        }

        if (!TestChunkBlockLimits(chunk_feerate, chunk_sig_ops) || !TestChunkTransactions(selected_transactions)) {
            m_mempool->SkipBuilderChunk();
            ++nConsecutiveFailed;

            Assert(m_options.block_max_weight);
            if (nConsecutiveFailed > MAX_CONSECUTIVE_FAILURES && nBlockWeight +
                    BLOCK_FULL_ENOUGH_WEIGHT_DELTA > *m_options.block_max_weight) {
                return;
            }
        } else {
            m_mempool->IncludeBuilderChunk();

            nConsecutiveFailed = 0;
            for (const auto& tx : selected_transactions) {
                AddToBlock(tx);
            }
            pblocktemplate->m_package_feerates.emplace_back(chunk_feerate_vsize);
        }

        selected_transactions.clear();
        chunk_feerate = m_mempool->GetBlockBuilderChunk(selected_transactions);
        chunk_feerate_vsize = ToFeePerVSize(chunk_feerate);
    }
}

void AddMerkleRootAndCoinbase(CBlock& block, CTransactionRef coinbase, uint32_t version, uint32_t timestamp, uint32_t nonce)
{
    if (block.vtx.size() == 0) {
        block.vtx.emplace_back(coinbase);
    } else {
        block.vtx[0] = coinbase;
    }
    block.nVersion = version;
    block.nTime = timestamp;
    block.nNonce = nonce;
    block.hashMerkleRoot = BlockMerkleRoot(block);

    block.m_checked_witness_commitment = false;
    block.m_checked_merkle_root = false;
    block.fChecked = false;
}

namespace {
class SubmitBlockStateCatcher final : public CValidationInterface
{
public:
    uint256 m_hash;
    bool m_found{false};
    BlockValidationState m_state;

    explicit SubmitBlockStateCatcher(const uint256& hash) : m_hash{hash} {}

protected:
    void BlockChecked(const std::shared_ptr<const CBlock>& block, const BlockValidationState& state) override
    {
        if (block->GetHash() != m_hash) return;
        m_found = true;
        m_state = state;
    }
};
} // namespace

bool SubmitBlock(ChainstateManager& chainman, const std::shared_ptr<const CBlock>& block, std::string& reason, std::string& debug)
{
    reason.clear();
    debug.clear();

    auto sc = std::make_shared<SubmitBlockStateCatcher>(block->GetHash());
    CHECK_NONFATAL(chainman.m_options.signals)->RegisterSharedValidationInterface(sc);
    bool new_block;
    bool accepted = chainman.ProcessNewBlock(block, /*force_processing=*/true, /*min_pow_checked=*/true, /*new_block=*/&new_block);
    CHECK_NONFATAL(chainman.m_options.signals)->UnregisterSharedValidationInterface(sc);

    if (!new_block && accepted) {
        reason = "duplicate";
    } else if (!accepted && (!sc->m_found || sc->m_state.IsValid())) {
        reason = "inconclusive";
    } else if (!sc->m_found) {
        reason = "inconclusive";
    } else if (!sc->m_state.IsValid()) {
        reason = sc->m_state.GetRejectReason();
        debug = sc->m_state.GetDebugMessage();
    }
    const bool result{accepted && new_block && reason.empty()};
    CHECK_NONFATAL(result == reason.empty());
    return result;
}

void InterruptWait(KernelNotifications& kernel_notifications, bool& interrupt_wait)
{
    LOCK(kernel_notifications.m_tip_block_mutex);
    interrupt_wait = true;
    kernel_notifications.m_tip_block_cv.notify_all();
}

std::unique_ptr<CBlockTemplate> WaitAndCreateNewBlock(ChainstateManager& chainman,
                                                    KernelNotifications& kernel_notifications,
                                                    CTxMemPool* mempool,
                                                    const std::unique_ptr<CBlockTemplate>& block_template,
                                                    const BlockWaitOptions& wait_options,
                                                    const BlockCreateOptions& create_options,
                                                    bool& interrupt_wait)
{
    CAmount current_fees = -1;
    auto now{NodeClock::now()};
    const auto deadline = now + wait_options.timeout;
    const MillisecondsDouble tick{1000};
    const bool allow_min_difficulty{chainman.GetParams().GetConsensus().fPowAllowMinDifficultyBlocks};

    do {
        bool tip_changed{false};
        {
            WAIT_LOCK(kernel_notifications.m_tip_block_mutex, lock);
            kernel_notifications.m_tip_block_cv.wait_until(lock, std::min(now + tick, deadline), [&]() EXCLUSIVE_LOCKS_REQUIRED(kernel_notifications.m_tip_block_mutex) {
                AssertLockHeld(kernel_notifications.m_tip_block_mutex);
                const auto tip_block{kernel_notifications.TipBlock()};
                tip_changed = Assume(tip_block) && tip_block != block_template->block.hashPrevBlock;
                return tip_changed || chainman.m_interrupt || interrupt_wait;
            });
            if (interrupt_wait) {
                interrupt_wait = false;
                return nullptr;
            }
        }

        if (chainman.m_interrupt) return nullptr;

        LOCK(::cs_main);

        if (!tip_changed && allow_min_difficulty) {
            const NodeClock::time_point tip_time{std::chrono::seconds{chainman.ActiveChain().Tip()->GetBlockTime()}};
            if (now > tip_time + 20min) {
                tip_changed = true;
            }
        }

        if (wait_options.fee_threshold < MAX_MONEY || tip_changed) {
            auto new_tmpl{BlockAssembler{
                chainman.ActiveChainstate(),
                mempool,
                create_options
                }.CreateNewBlock()};

            if (tip_changed) return new_tmpl;

            if (current_fees == -1) {
                current_fees = std::accumulate(block_template->vTxFees.begin(), block_template->vTxFees.end(), CAmount{0});
            }

            const CAmount new_fees = std::accumulate(new_tmpl->vTxFees.begin(), new_tmpl->vTxFees.end(), CAmount{0});
            Assume(wait_options.fee_threshold != MAX_MONEY);
            if (new_fees >= current_fees + wait_options.fee_threshold) return new_tmpl;
        }

        now = NodeClock::now();
    } while (now < deadline);

    return nullptr;
}

std::optional<BlockRef> GetTip(ChainstateManager& chainman)
{
    LOCK(::cs_main);
    CBlockIndex* tip{chainman.ActiveChain().Tip()};
    if (!tip) return {};
    return BlockRef{tip->GetBlockHash(), tip->nHeight};
}

bool CooldownIfHeadersAhead(ChainstateManager& chainman, KernelNotifications& kernel_notifications, const BlockRef& last_tip, bool& interrupt_mining)
{
    uint256 last_tip_hash{last_tip.hash};

    while (const std::optional<int> remaining = chainman.BlocksAheadOfTip()) {
        const int cooldown_seconds = std::clamp(*remaining, 3, 20);
        const auto cooldown_deadline{MockableSteadyClock::now() + std::chrono::seconds{cooldown_seconds}};

        {
            WAIT_LOCK(kernel_notifications.m_tip_block_mutex, lock);
            kernel_notifications.m_tip_block_cv.wait_until(lock, cooldown_deadline, [&]() EXCLUSIVE_LOCKS_REQUIRED(kernel_notifications.m_tip_block_mutex) {
                const auto tip_block = kernel_notifications.TipBlock();
                return chainman.m_interrupt || interrupt_mining || (tip_block && *tip_block != last_tip_hash);
            });
            if (chainman.m_interrupt || interrupt_mining) {
                interrupt_mining = false;
                return false;
            }

            const auto tip_block = kernel_notifications.TipBlock();
            if (tip_block && *tip_block != last_tip_hash) {
                last_tip_hash = *tip_block;
                continue;
            }
        }

        if (MockableSteadyClock::now() >= cooldown_deadline) break;
    }

    return true;
}

std::optional<BlockRef> WaitTipChanged(ChainstateManager& chainman, KernelNotifications& kernel_notifications, const uint256& current_tip, MillisecondsDouble& timeout, bool& interrupt)
{
    Assume(timeout >= 0ms);
    if (timeout < 0ms) timeout = 0ms;
    if (timeout > std::chrono::years{100}) timeout = std::chrono::years{100};
    auto deadline{std::chrono::steady_clock::now() + timeout};
    {
        WAIT_LOCK(kernel_notifications.m_tip_block_mutex, lock);
        kernel_notifications.m_tip_block_cv.wait(lock, [&]() EXCLUSIVE_LOCKS_REQUIRED(kernel_notifications.m_tip_block_mutex) {
            return kernel_notifications.TipBlock() || chainman.m_interrupt || interrupt;
        });
        if (chainman.m_interrupt || interrupt) {
            interrupt = false;
            return {};
        }
        kernel_notifications.m_tip_block_cv.wait_until(lock, deadline, [&]() EXCLUSIVE_LOCKS_REQUIRED(kernel_notifications.m_tip_block_mutex) {
            return Assume(kernel_notifications.TipBlock()) != current_tip || chainman.m_interrupt || interrupt;
        });
        if (chainman.m_interrupt || interrupt) {
            interrupt = false;
            return {};
        }
    }

    return GetTip(chainman);
}

} // namespace node