# 🌍 ฺBitcoin-Earth Digital World Bank (EDWB)

## Sovereign Consensus Expansion Protocol

> **Data is Eternal. Consensus is Ours. One Coin for All Life in This Universe.**

Earth Digital World Bank (EDWB) is a Bitcoin-derived sovereign consensus architecture designed to introduce a deterministic monetary expansion framework while preserving the historical Bitcoin issuance before the EDWB activation boundary.

The protocol defines a transition from the legacy Bitcoin issuance schedule into a new **63,000,000 COIN** monetary architecture, consisting of three 21,000,000 COIN components:

1. **Legacy Core Issuance**
2. **EDWB Global Reserve**
3. **Normalized Fibonacci Expansion**

The system is implemented as deterministic consensus code. Monetary issuance is calculated from block height and consensus parameters without floating-point arithmetic.

---

## 1. Protocol Overview

EDWB activates at:

* **Activation Height:** `966,600`
* **Anchor Height:** `966,599`
* **Expansion End Height:** `2,466,600`

The activation boundary is anchored to the preceding block:

* **Anchor Height:** `966,599`
* **Anchor Hash:** `39da3e5d9022592992f92a7d734e4354f7d3e405b9ef4d3b333af0de408aa46f`

At block `966,600`, the EDWB monetary architecture becomes active, entering a deterministic **1,500,000-block** expansion phase.

---

## 2. The 63,000,000 COIN Architecture

EDWB defines a total monetary architecture of **63,000,000 COIN** structured into three major 21,000,000 COIN components.

### 🟦 21,000,000 COIN — Legacy Core

The original Bitcoin issuance prior to EDWB activation remains preserved. Blocks before `966,600` continue to follow the established Bitcoin subsidy and halving mechanism. EDWB therefore does not retroactively rewrite historical Bitcoin issuance.

### 🟨 21,000,000 COIN — EDWB Global Reserve

At activation height `966,600`, exactly `21,000,000 COIN` is introduced as the EDWB Global Reserve allocation. The activation block is required by consensus to reference the predetermined EDWB anchor.

* **Primary EDWB Destination:** `bc1qrjw50j6pqv0m5k2x780r5j5an4dvvy0a9ggaaq`
* **Coinbase Marker:** `EARTH_DIGITAL_WORLD_BANK_PHASE1_21M_VAULT`

The reserve architecture supports strategic sub-reserves:

* 🟡 **ONE** — Reserve monetary foundation
* 🔴 **HEALTH** — Healthcare resilience reserve
* 🟢 **FOOD** — Food and agricultural security reserve

### 🟩 21,000,000 COIN — Normalized Fibonacci Expansion

From block `966,601` through `2,466,600`, EDWB executes a deterministic Fibonacci-based monetary expansion.

* **Repeating Fibonacci Sequence:** `1, 1, 2, 3, 5, 8, 13, 21, 34, 55`
* **Complete Cycle Weight:** `143`
* **Total Expansion Blocks:** `1,500,000` blocks (`150,000` cycles)
* **Raw Fibonacci Weight:** `143 × 150,000 = 21,450,000`

The raw sequence is normalized against the exact target expansion supply (`21,000,000 COIN`) using integer arithmetic and `__int128` multiplication to avoid floating-point consensus errors.

---

## 3. Normalized Fibonacci Consensus

The normalized expansion is calculated using cumulative allocation:

$$\text{Cumulative Reward} = \left\lfloor \frac{\text{Cumulative Fibonacci Weight} \times 21,000,000}{21,450,000} \right\rfloor$$

The subsidy for an individual block is the difference between its cumulative allocation and the previous block's cumulative allocation, yielding an initial expansion subsidy of approximately **`0.97902097 COIN`** for block `966,601`.

---

## 4. Consensus Issuance Rules

| Phase | Height Range | Description | Subsidy / Allocation |
| --- | --- | --- | --- |
| **Phase 0** | `< 966,600` | Legacy Bitcoin | Standard Bitcoin subsidy & halving rules |
| **Phase 1** | `= 966,600` | EDWB Activation | `21,000,000 COIN` (Anchor required) |
| **Phase 2** | `966,601 – 2,466,600` | Fibonacci Expansion | Normalized Fibonacci allocation (`21M` total) |
| **Phase 3** | `> 2,466,600` | Termination | `0 COIN` (Transaction fees only) |

---

## 5. Supply Summary

| Component | Supply |
| --- | --- |
| Legacy Bitcoin issuance | 21,000,000 COIN |
| EDWB Activation Reserve | 21,000,000 COIN |
| Normalized Fibonacci Expansion | 21,000,000 COIN |
| **Total** | **63,000,000 COIN** |

---

## 6. Consensus Determinism

EDWB monetary issuance is independently reproducible by every validating node, avoiding floating-point math, probabilistic issuance, centralized decisions, or ad-hoc corrections. Issuance depends deterministically on block height, consensus constants, Fibonacci sequence, and integer arithmetic.

---

## 7. Coinbase Architecture

* **Output 0:** `21,000,000 COIN + transaction fees` → EDWB primary reserve (`bc1qrjw50j6pqv0m5k2x780r5j5an4dvvy0a9ggaaq`)
* **Output 1:** `OP_RETURN` → `EARTH_DIGITAL_WORLD_BANK_PHASE1_21M_VAULT`

During Fibonacci expansion, allocations route through the EDWB Vault mechanism while transaction fees remain separately accounted for as miner compensation.

---

## 8. EDWB Anchor Security

The activation transition requires block `966,600` to directly follow anchor height `966,599` (`39da3e5d9022592992f92a7d734e4354f7d3e405b9ef4d3b333af0de408aa46f`). Chains attempting activation from invalid predecessors are strictly rejected.

---

## 9. Mainnet Mirror Architecture

EDWB includes a separate Mainnet Mirror subsystem designed for historical/reference processing via a mirror queue, backpressure, checkpoints, and resumable synchronization (`bool BackfillEDWBHistoricalData(int target_height = -1);`), maintaining strict isolation from Bitcoin chainstate, UTXO set, and consensus state.

---

## 10. Building and Deploying EDWB Node

To compile and run the EDWB Consensus node from source using MSYS2 (UCRT64) on Windows or a standard Linux development environment:

```bash
# 1. Clone the repository
git clone https://github.com/Warlord-Fah-Tamil/Bitcoin-Earth-Digital-World-Bank.git
cd Bitcoin-Earth-Digital-World-Bank

# 2. Configure the build directory using CMake & Ninja
cmake -B build -G Ninja

# 3. Compile the EDWB node
# On Linux:
cmake --build build -j$(nproc)

# On MSYS2/UCRT64 (Windows):
cmake --build build

# 4. Start the EDWB sovereign node
# Linux:
./build/bin/bitcoind -daemon

# MSYS2/UCRT64:
./build/bin/bitcoind.exe -daemon

# 5. Verify node operation
./build/bin/bitcoin-cli getblockchaininfo
# (or bitcoin-cli.exe on Windows)

```

---

## 11. Consensus Verification Constants

* `EDWB_ANCHOR_HEIGHT`: `966,599`
* `EDWB_ACTIVATION_HEIGHT`: `966,600`
* `EDWB_EXPANSION_LIMIT`: `1,500,000 blocks`
* `EDWB_EXPANSION_END_HEIGHT`: `2,466,600`
* `EDWB_ACTIVATION_SUPPLY`: `21,000,000 COIN`
* `EDWB_EXPANSION_SUPPLY`: `21,000,000 COIN`
* `EDWB_TOTAL_ARCHITECTURE`: `63,000,000 COIN`

---

## 12. Security and Consensus Disclaimer

EDWB is experimental open-source consensus software. Independent developers should audit consensus rules, subsidy calculations, coinbase validation, and chain parameters on isolated networks before production deployment.

> Data is Eternal. Consensus is Ours. One Coin for All Life in This Universe.
