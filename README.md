# Hawkeye Cache Replacement Policy — ChampSim

Implementation and evaluation of the **Hawkeye cache replacement policy** in [ChampSim](https://github.com/ChampSim/ChampSim).

This project implements a Hawkeye-inspired cache replacement policy for the Last-Level Cache (LLC), integrates it with ChampSim, and evaluates its behavior against the baseline **LRU (Least Recently Used)** replacement policy using SPEC CPU benchmark traces.

---

## Table of Contents

* [Overview](#overview)
* [Objectives](#objectives)
* [Background](#background)
* [Hawkeye Design](#hawkeye-design)

  * [OPTgen](#optgen)
  * [PC-Based Predictor](#pc-based-predictor)
  * [RRIP-Based Replacement](#rrip-based-replacement)
  * [Training](#training)
* [Implementation Details](#implementation-details)
* [Repository Structure](#repository-structure)
* [Building ChampSim](#building-champsim)
* [Running the Simulator](#running-the-simulator)
* [Benchmark Traces](#benchmark-traces)
* [Experimental Methodology](#experimental-methodology)
* [Evaluation](#evaluation)

  * [Associativity Experiment](#associativity-experiment)
  * [Hawkeye vs. LRU](#hawkeye-vs-lru)
* [Results](#results)
* [Key Observations](#key-observations)
* [Implementation Notes](#implementation-notes)
* [Reproducibility](#reproducibility)
* [References](#references)
* [Author](#author)

---

# Overview

Modern processors rely heavily on cache hierarchies to reduce the latency of memory accesses. The effectiveness of a cache depends not only on its size and associativity, but also on the **replacement policy** used to decide which cache block should be evicted when a set is full.

ChampSim provides several replacement policies, including LRU. This project implements **Hawkeye**, a cache replacement policy designed to make replacement decisions based on the predicted reuse behavior of cache blocks.

Unlike traditional recency-based policies, Hawkeye attempts to determine whether a cache block is likely to be reused before other blocks in the same set. It uses:

1. **OPTgen** to determine whether a previously accessed block would have been useful to retain.
2. A **PC-based predictor** to learn whether instructions tend to generate cache-friendly or cache-averse accesses.
3. An **RRIP-style replacement mechanism** to prioritize blocks for eviction.

The implementation is evaluated using three benchmark traces:

* `456.hmmer-191B`
* `429.mcf-22B`
* `473.astar-42B`

The primary metric used for evaluation is **LLC miss rate**.

---

# Objectives

The main objectives of this project are:

* Implement the Hawkeye cache replacement policy in ChampSim.
* Understand the interaction between OPTgen, prediction, and replacement.
* Compare Hawkeye against the baseline LRU policy.
* Study the effect of LLC associativity on cache performance.
* Measure the reduction in LLC miss rate achieved by Hawkeye relative to LRU.
* Produce reproducible experimental results using fixed warmup and simulation intervals.

---

# Background

## Cache Replacement

When a cache set is full and a new block needs to be inserted, the cache must select an existing block to evict.

A traditional **LRU** policy chooses the block that has not been accessed for the longest time.

While LRU works well for workloads with strong temporal locality, it can make poor decisions when:

* A block is accessed only once.
* A block has a long reuse distance.
* A streaming access pattern pollutes the cache.
* Recency does not accurately represent future reuse.

Hawkeye takes a different approach by attempting to predict whether a block will be reused soon enough to justify keeping it in the cache.

---

# Hawkeye Design

The implementation is based on the main ideas behind the Hawkeye replacement policy.

The high-level flow is:

```text
                 Cache Access
                      |
                      v
                  OPTgen
                      |
              +-------+-------+
              |               |
          OPT Hit          OPT Miss
              |               |
              v               v
        Train predictor   Train predictor
              \               /
               \             /
                v           v
                 PC Predictor
                      |
                      v
              Friendly / Averse
                      |
                      v
                  RRIP State
                      |
                      v
              Victim Selection
```

The implementation consists primarily of three components:

* OPTgen
* PC-based predictor
* RRIP-based replacement mechanism

---

## OPTgen

OPTgen approximates the behavior of an optimal replacement policy.

For each cache set, it tracks the history of block accesses and determines whether the current reuse would have been beneficial to retain under an OPT-like policy.

The important distinction is between:

* **OPT hit** — retaining the block would have been useful.
* **OPT miss** — retaining the block would not have been useful.

This classification is then used as a training signal for the PC-based predictor.

### Address Representation

The `address` passed to OPTgen represents the **cache block address**, rather than a byte-level address.

This is important because cache replacement operates at cache-line/block granularity.

---

# PC-Based Predictor

Hawkeye uses the program counter (PC) associated with memory accesses to learn whether accesses generated by a particular instruction are likely to be useful to retain.

The predictor contains:

* **8192 entries**
* **3-bit saturating counters**

The predictor index is generated from the raw PC using:

```text
hash = PC ^ (PC >> 12)
index = hash & ((1 << 13) - 1)
```

Therefore, the predictor uses the lower **13 bits** after hashing, giving:

```text
2^13 = 8192 entries
```

The predictor API operates on the **raw PC** and performs the hash internally.

---

## Predictor Counter Initialization

Each 3-bit predictor counter is initialized to:

```text
4
```

Since a 3-bit counter has values from `0` to `7`, the midpoint is:

```text
0 1 2 3 4 5 6 7
        ^
      midpoint
```

A counter value at or above the prediction threshold represents a **cache-friendly** prediction, while a lower value represents a **cache-averse** prediction.

Initializing at the midpoint provides a neutral starting point while making the untrained behavior cache-friendly according to the assignment specification.

---

# RRIP-Based Replacement

The replacement mechanism uses an RRIP-style Re-Reference Prediction Value (RRPV).

Each cache block has an associated RRPV value.

The implementation distinguishes between:

* **Cache-friendly** blocks
* **Cache-averse** blocks

A cache-friendly insertion receives a lower RRPV, making it more likely to survive future eviction decisions.

A cache-averse insertion receives a high RRPV, making it a preferred victim.

Conceptually:

```text
Cache-friendly
      |
      v
Lower RRPV
      |
      v
Less likely to evict


Cache-averse
      |
      v
High RRPV
      |
      v
More likely to evict
```

RRIP state is also updated on cache hits according to the replacement-policy rules.

---

# Training

The predictor is trained using the classification generated by OPTgen.

The important training relationship is:

```text
Previous PC
    |
    v
Current reuse classification
    |
    v
OPTgen
    |
    +---- OPT Hit  ------> Train as friendly
    |
    +---- OPT Miss ------> Train as averse
```

A significant implementation detail is that the PC responsible for the previous access to a cache block may be required even when that block is no longer resident.

Therefore, the implementation maintains the necessary access history in OPTgen rather than relying only on metadata associated with currently resident cache lines.

---

# Implementation Details

The Hawkeye implementation is located under the replacement-policy implementation in ChampSim.

The major source files are:

```text
replacement/hawkeye/
├── hawkeye.cc
├── hawkeye.h
├── optgen.cc
├── optgen.h
├── predictor.cc
├── predictor.h
├── rrip.cc
└── rrip.h
```

The policy is integrated with ChampSim's replacement-policy interface.

The replacement flow distinguishes between:

### Cache Hit

On a cache hit:

1. The access is processed.
2. OPTgen updates its access history.
3. Predictor/replacement state is updated.
4. The corresponding RRIP state is updated.

### Cache Miss

On a cache miss:

1. The access is processed by OPTgen.
2. The predictor is trained when the required reuse classification becomes available.
3. A victim is selected using the replacement state.
4. The incoming block is inserted according to the predicted cache friendliness.

This separation is important because an access that causes a miss can still provide information necessary for training.

---

# Repository Structure

A simplified repository structure is:

```text
ChampSim/
│
├── bin/
│   └── champsim
│
├── config/
│   └── ...
│
├── replacement/
│   ├── hawkeye/
│   │   ├── hawkeye.cc
│   │   ├── hawkeye.h
│   │   ├── optgen.cc
│   │   ├── optgen.h
│   │   ├── predictor.cc
│   │   ├── predictor.h
│   │   ├── rrip.cc
│   │   └── rrip.h
│   │
│   └── lru/
│       └── ...
│
├── src/
│   ├── cache.cc
│   └── ...
│
├── traces/
│   ├── 456.hmmer-191B.champsimtrace.xz
│   ├── 429.mcf-22B.champsimtrace.xz
│   └── 473.astar-42B.champsimtrace.xz
│
└── README.md
```

The exact contents of the repository may vary depending on the ChampSim version used.

---

# Building ChampSim

Clone the repository:

```bash
git clone <YOUR_GITHUB_REPOSITORY_URL>
cd ChampSim
```

Build ChampSim using the configuration provided in the repository.

For example:

```bash
./config.sh <configuration>
make
```

or use the build procedure corresponding to the ChampSim version in this repository.

After a successful build, the simulator should be available as:

```text
bin/champsim
```

Verify that the binary exists:

```bash
ls -l bin/champsim
```

---

# Running the Simulator

The experiments use:

* **20,000,000 instructions for warmup**
* **50,000,000 instructions for simulation**

The general command is:

```bash
./bin/champsim \
    --warmup_instructions 20000000 \
    --simulation_instructions 50000000 \
    traces/<TRACE>
```

For example, to run the Hmmer trace:

```bash
./bin/champsim \
    --warmup_instructions 20000000 \
    --simulation_instructions 50000000 \
    traces/456.hmmer-191B.champsimtrace.xz
```

For MCF:

```bash
./bin/champsim \
    --warmup_instructions 20000000 \
    --simulation_instructions 50000000 \
    traces/429.mcf-22B.champsimtrace.xz
```

For Astar:

```bash
./bin/champsim \
    --warmup_instructions 20000000 \
    --simulation_instructions 50000000 \
    traces/473.astar-42B.champsimtrace.xz
```

---

# Benchmark Traces

The project evaluates Hawkeye using three ChampSim traces.

| Benchmark | Trace                             |
| --------- | --------------------------------- |
| Hmmer     | `456.hmmer-191B.champsimtrace.xz` |
| MCF       | `429.mcf-22B.champsimtrace.xz`    |
| Astar     | `473.astar-42B.champsimtrace.xz`  |

The traces must be available under the repository's `traces/` directory or at the path specified in the simulator command.

---

# Experimental Methodology

The experiments follow a fixed simulation configuration so that replacement policies can be compared consistently.

## Instruction Counts

Each simulation uses:

| Phase       | Instructions |
| ----------- | -----------: |
| Warmup      |   20,000,000 |
| Measurement |   50,000,000 |

Warmup allows the cache and replacement-policy state to reach a representative operating state before statistics are collected.

---

# Evaluation

Two main experiments are performed.

---

## Associativity Experiment

The first experiment evaluates how LLC associativity affects miss rate for the Hmmer workload.

The LLC size is fixed at:

```text
2 MB
```

The following associativities are evaluated:

| Associativity | Number of Sets |
| ------------: | -------------: |
|         4-way |           8192 |
|         8-way |           4096 |
|        16-way |           2048 |

The relationship is:

```text
Number of sets =
    Cache size / (Associativity × Cache line size)
```

With the LLC size and cache-line size fixed, increasing associativity decreases the number of sets.

Both LRU and Hawkeye can be evaluated under these configurations.

The resulting data is used to generate:

> **LLC Miss Rate vs. Associativity**

for the Hmmer benchmark.

---

# Hawkeye vs. LRU

The second experiment compares Hawkeye against LRU using the default:

```text
2 MB LLC
16-way associativity
```

The three workloads are:

* Hmmer
* MCF
* Astar

The comparison is based on LLC miss rate.

The percentage reduction in miss rate is calculated as:

```text
Reduction (%) =
    (LRU Miss Rate - Hawkeye Miss Rate)
    / LRU Miss Rate × 100
```

A positive value indicates that Hawkeye has a lower miss rate than LRU.

A negative value indicates that Hawkeye has a higher miss rate than LRU.

---

# Results

The following values are the measured/recorded results for the 16-way LLC comparison.

| Benchmark | LRU Miss Rate | Hawkeye Miss Rate | Reduction |
| --------- | ------------: | ----------------: | --------: |
| Hmmer     |        28.48% |           28.79%* |   -1.09%* |
| MCF       |        65.57% |            63.50% |     3.16% |
| Astar     |        19.43% |            19.22% |     1.08% |

* The Hmmer value shown here corresponds to the latest 20M warmup / 50M simulation run recorded during development. If the final experimental dataset uses a different Hmmer run, this row should be updated to the final measured value.

For example, the Hmmer run produced:

```text
LLC TOTAL
ACCESS: 264675
HIT:    188461
MISS:    76214
```

Therefore:

```text
LLC Miss Rate
= 76214 / 264675 × 100
≈ 28.79%
```

---

# Key Observations

## 1. Hawkeye performance is workload dependent

Hawkeye does not provide a uniform improvement over LRU across every workload.

For MCF and Astar, the measured Hawkeye miss rates are lower than the corresponding LRU miss rates.

For Hmmer, the recorded run has a slightly higher miss rate than LRU.

This demonstrates that a learned replacement policy can be highly dependent on the memory-access behavior of the workload.

---

## 2. PC-based prediction can improve replacement decisions

The central idea behind Hawkeye is that the program counter provides useful information about the future reuse behavior of a cache access.

Instead of relying solely on recency, the predictor learns whether accesses associated with a PC tend to produce useful cache lines.

This allows the replacement policy to distinguish between accesses that are likely to be reused and accesses that are likely to cause cache pollution.

---

## 3. Associativity changes the cache's replacement environment

Changing associativity changes both:

* The number of cache sets.
* The number of competing blocks within each set.

Therefore, associativity can significantly affect the LLC miss rate.

The associativity experiment helps demonstrate that replacement policy performance cannot be considered independently of cache organization.

---

# Implementation Notes

The implementation follows several assignment-specific requirements.

## Predictor Hash

The predictor uses:

```text
PC ^ (PC >> 12)
```

and the lower 13 bits:

```text
index = (PC ^ (PC >> 12)) & 0x1FFF
```

This produces 8192 predictor entries.

---

## Predictor Initialization

The 3-bit counters are initialized to:

```text
4
```

rather than zero.

This is the midpoint of the available counter range `[0, 7]`.

---

## OPTgen Address

OPTgen operates on **cache block addresses**.

It does not treat the input address as an arbitrary byte-level memory address for its reuse-distance tracking.

---

## RRIP Hit Updates

RRIP state is updated on cache hits according to the replacement-policy rules.

A hit therefore affects the block's predicted re-reference state rather than being ignored by the replacement mechanism.

---

## Victim-PC Detraining

The assignment implementation does **not** detrain the victim PC when a block is evicted.

The full Hawkeye design includes sampler-based mechanisms and victim-PC detraining. This implementation follows the assignment specification instead of adding that sampler/detraining mechanism.

---

# Reproducibility

To reproduce the experiments:

### 1. Clone the repository

```bash
git clone <YOUR_GITHUB_REPOSITORY_URL>
cd ChampSim
```

### 2. Obtain the required traces

Place the required ChampSim traces in:

```text
traces/
```

Expected files:

```text
traces/456.hmmer-191B.champsimtrace.xz
traces/429.mcf-22B.champsimtrace.xz
traces/473.astar-42B.champsimtrace.xz
```

### 3. Build ChampSim

Build the simulator using the repository configuration.

### 4. Run the benchmarks

Use:

```bash
--warmup_instructions 20000000
--simulation_instructions 50000000
```

### 5. Record LLC statistics

The important statistics are:

```text
cpu0->LLC TOTAL
    ACCESS
    HIT
    MISS
```

Calculate:

```text
Miss Rate (%) = MISS / ACCESS × 100
```

### 6. Compare against LRU

Run the same workload and cache configuration using the LRU replacement policy.

Use:

```text
Reduction (%) =
    (LRU Miss Rate - Hawkeye Miss Rate)
    / LRU Miss Rate × 100
```

Keeping the cache configuration, trace, warmup length, and simulation length identical is important for a meaningful comparison.

---

# Example Output

A successful ChampSim execution reports information similar to:

```text
Simulation finished CPU 0
instructions: 50000003
cycles: ...
cumulative IPC: ...

cpu0->LLC TOTAL
ACCESS: ...
HIT: ...
MISS: ...
MISS_MERGE: ...
```

For example, one Hmmer run produced:

```text
cpu0->LLC TOTAL
ACCESS: 264675
HIT:    188461
MISS:    76214
```

which corresponds to approximately:

```text
28.79% LLC miss rate
```

---

# Limitations

This implementation is intended for the ChampSim assignment and therefore does not necessarily reproduce every hardware detail of the complete Hawkeye design.

In particular:

* The assignment implementation does not implement the full sampler/detraining mechanism from the original Hawkeye design.
* The predictor and OPTgen parameters follow the assignment specification.
* Results depend on the exact ChampSim configuration, trace, warmup period, simulation period, and cache organization.
* Hawkeye is not guaranteed to outperform LRU on every workload.

Consequently, results should be interpreted within the experimental configuration used in this project.

---

# References

The implementation is based on the Hawkeye cache replacement policy described in:

> **J. Jain and C. Lin, "Back to the Future: Leveraging Belady's Algorithm for Improved Cache Replacement," Proceedings of the 43rd International Symposium on Computer Architecture (ISCA), 2016.**

The project also uses the ChampSim simulator:

> **ChampSim — A Trace-Based Simulator for Computer Architecture Research**

Official ChampSim repository:

https://github.com/ChampSim/ChampSim

---

# Assignment Requirements Addressed

This implementation addresses the major assignment requirements:

* [x] Hawkeye replacement policy implemented in ChampSim
* [x] OPTgen component
* [x] PC-based predictor
* [x] 8192-entry predictor
* [x] 3-bit predictor counters
* [x] Predictor hash `PC ^ (PC >> 12)`
* [x] Predictor counters initialized to 4
* [x] Cache-block address handling in OPTgen
* [x] RRIP-based replacement state
* [x] RRIP updates on cache hits
* [x] No victim-PC detraining
* [x] 20M instruction warmup
* [x] 50M instruction measurement period
* [x] LLC associativity experiment
* [x] Hawkeye vs. LRU comparison
* [x] Multiple benchmark workloads

---

# Conclusion

This project explores the implementation of a **prediction-based cache replacement policy** in ChampSim.

The key difference between Hawkeye and conventional LRU is that Hawkeye attempts to learn whether an access is likely to have future reuse, rather than treating recency as the primary indicator of usefulness.

The implementation combines:

```text
OPTgen
   ↓
Reuse Classification
   ↓
PC-Based Predictor
   ↓
Cache-Friendly / Cache-Averse Prediction
   ↓
RRIP-Based Replacement
```

The experiments demonstrate that cache replacement behavior is strongly workload dependent. Hawkeye can reduce LLC miss rates for some workloads, while providing little improvement or even a regression for others.

Overall, the project provides practical experience with:

* Cache replacement policies
* Reuse prediction
* OPT-based classification
* Saturating-counter predictors
* RRIP
* ChampSim
* Trace-driven microarchitectural simulation
* Cache-performance evaluation
* Experimental reproducibility

---

# Author

**[Gattu Thousif Ahmed]**

GitHub: **[https://github.com/thousif-g/CA_Assignment]**
