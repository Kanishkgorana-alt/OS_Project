# Multi-Threaded Design for Text Retrieval

[![C99](https://img.shields.io/badge/Language-C99-blue.svg)](https://en.wikipedia.org/wiki/C99)
[![POSIX Threads](https://img.shields.io/badge/Concurrency-Pthreads-orange.svg)](https://pubs.opengroup.org/onlinepubs/9699919799/basedefs/pthread.h.html)
[![Platform](https://img.shields.io/badge/Platform-Linux%20%2F%20POSIX-lightgrey.svg)](https://www.kernel.org/)
[![Corpus](https://img.shields.io/badge/Dataset-IIT%20Bombay%20Parallel%20Corpus-green.svg)](https://www.cfilt.iitb.ac.in/~parallelcorp/iitb_en_hi_parallel/)

A high-performance multithreaded text retrieval architecture developed in C using **POSIX Threads (Pthreads)**, evaluated on a quality-filtered subset of **1,030,875 aligned English–Hindi sentence pairs** (quality score > 0.65) from the IIT Bombay English–Hindi Parallel Corpus.

This project investigates concurrency paradigms, workload partitioning strategies, memory hierarchy dynamics, and algorithmic pruning mechanisms to balance parallel throughput against thread synchronization and memory overhead.

---

## 👥 Authors (Operating Systems Project — Group 18)

- **Kanishk Gorana** (S20240030378)
- **Kartik Kumthe Sanjay** (S20240030383)
- **Manas Jain** (S20240030387)

Full project report: [OSProject_Group-18.pdf](OSProject_Group-18.pdf)

---

## 📌 Executive Summary & Key Results

- **Substantial Latency Reductions**: Decreases search times from **1.922 s to 0.405 s (4.75× speedup)** for `government` and from **1.072 s to 0.333 s (3.22× speedup)** for `the` across 1 to 16 threads. In controlled warm-cache trials, 16-thread search kernels reach **~0.135 s (over 5.6× speedup)**.
- **Two-Phase Algorithmic Pruning**: Converts exhaustive dual-language scans ($O(N \cdot L_E + N \cdot L_H)$) into a parallel primary scan followed by targeted $O(1)$ random seeks via a 64-bit byte-offset index, reducing secondary Hindi verification work from $O(N)$ to $O(|K|)$ candidate matches.
- **Lock-Free Concurrency**: Eliminates mutex contention and cache bouncing in the critical search path by pairing a shared read-only offset table with thread-isolated `FILE*` streams, independent parsing line buffers, and private dynamic result buffers.
- **Mitigating Workload Imbalance (Straggler Effect)**: Evaluates static 1D sentence-count partitioning against **byte-balanced block partitioning** (`Block_indexing.c`), snapping boundaries to sentence delimiters while equalizing byte volume across workers.
- **Amdahl's Law & Scaling Limits**: Profiles parallel efficiency inflection points (declining from ~81–85% at 2 threads to <30% at 16 threads), driven by memory bus bandwidth saturation, straggler idle time, kernel lifecycle overhead, and serial master-thread result merging.

---

## 🏗 System Architecture

```
                      +---------------------------------------+
                      | Raw Bilingual Corpus (.en, .hi files) |
                      +---------------------------------------+
                                          |
                                 [offset_file_gen.c]
                                          v
                      +---------------------------------------+
                      | 64-bit Byte-Offset Index (RAM)        |
                      | (Shared Read-Only Memory, ~16.5 MB)   |
                      +---------------------------------------+
                                          |
                   +----------------------+----------------------+
                   |                                             |
       [Equal-Sentence Splitting]                      [Block_indexing.c]
                   |                                             |
                   v                                             v
       +-----------------------+                     +-----------------------+
       | Static 1D Partitions  |                     | Byte-Balanced Blocks  |
       +-----------------------+                     +-----------------------+
                   \                                             /
                    \                                           /
                     v                                         v
       +---------------------------------------------------------------------+
       | Worker Thread Pool (POSIX Threads)                                  |
       |  - Private FILE* streams (zero POSIX stream lock contention)        |
       |  - Thread-isolated dynamic line buffers (char* line_buf)            |
       |  - Lock-free thread-local result vectors (int* local_results)       |
       +---------------------------------------------------------------------+
                                          |
                                    pthread_join()
                                          v
       +---------------------------------------------------------------------+
       | Master Thread Result Aggregation                                    |
       |  - Sequential array concatenation & K-way merge                     |
       |  - Deduplication & strict bilingual alignment verification          |
       +---------------------------------------------------------------------+
                                          |
                                          v
                      +---------------------------------------+
                      | Mini-Dataset Export / Benchmark CSV   |
                      +---------------------------------------+
```

### 1. Memory Isolation & Lock-Free Design
Acquiring mutual exclusion locks inside inner token-matching loops introduces severe cache-coherence traffic (MESI invalidations) and lock contention. The architecture decouples threads completely:
1. **Shared Read-Only State**: The 64-bit byte-offset index table (~16.5 MB) is loaded into heap memory before thread dispatch and mapped read-only across all workers. It fits inside modern CPU Last-Level Caches (L3 / LLC, 16–32 MB).
2. **Private File Descriptors**: Each worker opens its own dedicated `FILE*` handle via `fopen()`. Sharing file pointers across threads triggers internal POSIX stream locking and serializes I/O.
3. **Thread-Local Buffers**: Parsing line buffers and dynamic match arrays (`IDList`) are thread-private, eliminating false sharing on 64-byte CPU cache lines.
4. **Post-Join Merge**: Workers execute asynchronously without cross-thread coordination. The master thread aggregates results sequentially after `pthread_join()`.

### 2. Byte-Balanced Block Decomposition (`Block_indexing.c`)
Sentence length in natural text exhibits high variance (titles < 20 bytes vs. dense administrative text > 500 bytes). Dividing $N$ sentences equally creates severe stragglers:
$$\text{Workload Imbalance Ratio } I = \frac{\max_j T_j}{\frac{1}{p} \sum_{j=0}^{p-1} T_j}$$
`Block_indexing.c` computes an ideal byte quota $B_{\text{target}} = B_{\text{total}} / p_{\text{blocks}}$ and snaps boundaries to the nearest sentence end delimiter, equalizing byte volumes across workers and synchronizing thread completion.

---

## 📂 Codebase & File Organization

| Category | Source File | Description |
| :--- | :--- | :--- |
| **Preprocessing & Indexing** | [`offset_file_gen.c`](offset_file_gen.c) | Scans raw `.en` and `.hi` corpora to produce `line_index.txt` with 64-bit byte offsets (`line_id`, `en_offset`, `hi_offset`). |
| | [`Block_indexing.c`](Block_indexing.c) | Decomposes the corpus into 8 contiguous byte-balanced partitions snapped to sentence boundaries (`en_partitions.txt`, `hi_partitions.txt`). |
| **Monolingual Search** | [`search_seq.c`](search_seq.c) | Baseline single-threaded sequential linear scan ($O(N)$ reference). |
| | [`search_par_debugged.c`](search_par_debugged.c) | Static 1D range-partitioned parallel scan for up to 16 concurrent worker threads. |
| | [`search_par_modified.c`](search_par_modified.c) | Range-partitioned parallel scan tuned for 8 concurrent worker threads. |
| | [`search_block.c`](search_block.c) | Block-oriented parallel search utilizing byte-balanced partition metadata. |
| | [`search_2level.c`](search_2level.c) | Hierarchical 2-level decomposition (8 macro-blocks $\times$ 2 micro-chunks = 16 workers). |
| **Bilingual Filtering** | [`search_two_thread_bilingual_and.c`](search_two_thread_bilingual_and.c) | Two-thread pipeline evaluating concurrent English and Hindi conditions. |
| | [`search_8thread_bilingual_and.c`](search_8thread_bilingual_and.c) | 8-worker bilingual conjunctive (AND) retrieval engine. |
| | [`search_2level_bilingual_and.c`](search_2level_bilingual_and.c) | Hierarchical block/chunk bilingual conjunctive retrieval. |
| | [`search_two_phase.c`](search_two_phase.c) | **Two-Phase Pruning**: Parallel English scan followed by targeted $O(1)$ Hindi offset seeks on candidate IDs. |
| | [`search_two_phase_or.c`](search_two_phase_or.c) | Two-phase disjunctive (OR) search with thread-local deduplication. |
| | [`search_bidirectional_bilingual_and.c`](search_bidirectional_bilingual_and.c) | Evaluates English-first versus Hindi-first candidate pruning selectivity. |
| **Sequential Baselines** | [`sequential_or.c`](sequential_or.c) | Baseline dual-language disjunctive (OR) scan with linear deduplication. |
| | [`sequential_retrieval.c`](sequential_retrieval.c) | Monolingual sequential text retrieval baseline. |
| | [`sequential_two_word.c`](sequential_two_word.c) | Baseline sequential two-word vocabulary search. |
| **Documentation & Data** | [`OSProject_Group-18.pdf`](OSProject_Group-18.pdf) | Complete academic research paper and empirical analysis report. |
| | `IITB_score_gt_065.en` | Quality-filtered English parallel corpus (1,030,875 sentences, score > 0.65). |
| | `IITB_score_gt_065.hi` | Quality-filtered Hindi parallel corpus (1,030,875 sentences, score > 0.65). |

---

## 📊 Empirical Benchmarks & Analysis

### 1. Monolingual Search Scaling ($p \in \{1, 2, 4, 8, 16\}$)

Kernel search timings ($T_{\text{search}}$), speedup $S(p)$, and parallel efficiency $E(p)$:

| Thread Count ($p$) | `government` Latency | Speedup $S(p)$ | Efficiency $E(p)$ | `the` Latency | Speedup $S(p)$ | Efficiency $E(p)$ |
| :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **1 (Baseline)** | 1.922 s | 1.00× | 100.0% | 1.072 s | 1.00× | 100.0% |
| **2** | 1.188 s | 1.62× | 80.9% | 0.634 s | 1.69× | 84.6% |
| **4** | 0.678 s | 2.84× | 70.9% | 0.394 s | 2.72× | 68.1% |
| **8** | 0.463 s | 4.15× | 51.9% | 0.352 s | 3.04× | 38.1% |
| **16** | 0.405 s | 4.75× | 29.7% | 0.333 s | 3.22× | 20.1% |

> In repeated warm-cache trials across the word frequency spectrum, 16-thread search kernels achieve an asymptotic floor of **~0.133–0.139 s**, yielding **over 5.6× speedup** over single-threaded warm baselines (~0.758 s).

### 2. Query Selectivity vs. Word Rarity

Linear text scanning inspects every token across all 1.03M sentences, causing raw search latency to remain virtually constant across rare terms and stopwords:

| Query Token | Semantic Class | Corpus Matches ($|R|$) | Match Density ($\rho$) | 1-Thread $T_{\text{search}}$ | 16-Thread $T_{\text{search}}$ | Amortized Time / Match ($\tau$) | Post-Processing Overhead |
| :--- | :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| `eye` | Highly Rare Content Noun | 1,298 | 0.126% | 0.758 s | 0.136 s | 1,351.5 µs | 0.226 s |
| `government` | Moderate Topical Noun | 17,420 | 1.690% | 0.760 s | 0.139 s | 1,565.2 µs | 0.231 s |
| `is` | Common Auxiliary Verb | 197,091 | 19.119% | 0.760 s | 0.133 s | 100.1 µs | 0.248 s |
| `the` | Ubiquitous Stopword | 498,661 | 48.373% | 0.774 s | 0.135 s | 5.9 µs | 0.280 s |

- **Word Rarity Invariance**: A 384-fold increase in match volume (`eye` vs. `the`) increases 1-thread search time by only 2.1% (16 ms). Raw character scanning dominates >95% of execution time.
- **Hyperbolic Cost Per Match**: Amortized cost per match $\tau_{\text{match}} = T_{\text{search}} / |R|$ scales inversely with frequency ($1/|R|$), demonstrating the inherent wastefulness of brute-force scans for low-frequency queries.

### 3. Bilingual Boolean Retrieval & Hierarchical Models

| Algorithmic Strategy | Implementation File | Worker Threads | Mean Execution Time | Relative Speedup | Algorithmic Mechanism |
| :--- | :--- | :---: | :---: | :---: | :--- |
| **Sequential OR** | `sequential_or.c` | 1 | 2.775 s | 1.00× | Baseline dual-language scan with linear deduplication. |
| **Two-Thread OR** | `search_par_modified.c` | 2 | 1.564 s | 1.77× | Concurrent language scanning; 43.6% latency reduction. |
| **Two-Thread AND** | `search_two_thread_bilingual_and.c` | 2 | 0.957 s | 1.00× | Concurrent dual-language scan with join synchronization. |
| **Eight-Thread AND** | `search_8thread_bilingual_and.c` | 8 | 0.295 s | 3.25× | 1D range partitioning over aligned pairs; 69.2% latency reduction. |
| **Hierarchical 2-Level AND** | `search_2level_bilingual_and.c` | 16 (8 × 2) | 0.357 s | 2.68× | Nested block/chunk decomposition; degraded by thread oversubscription. |

> **Oversubscription Penalty**: The 2-level hierarchical variant ran slower (0.357 s) than the 8-thread single-level engine (0.295 s). Finer chunking without a bounded worker pool introduced scheduling contention, L1 cache thrashing, and context switches without reducing computational work.

---

## ⚙️ Compilation & Usage Guide

### Prerequisites
- GCC supporting C99 or later
- POSIX Threads library (`libpthread`)
- Linux / POSIX-compliant environment (tested on Ubuntu x86_64)

### Step 1: Generate the 64-Bit Byte-Offset Index
Configure paths in `offset_file_gen.c` to point to `IITB_score_gt_065.en` and `IITB_score_gt_065.hi`, then compile and run:
```bash
gcc -O2 -Wall offset_file_gen.c -o offset_file_gen
./offset_file_gen
```
*Output: `line_index.txt` (64-bit offsets for all 1,030,875 sentence boundaries).*

### Step 2: Generate Byte-Balanced Partitions
```bash
gcc -O2 -Wall Block_indexing.c -o Block_indexing
./Block_indexing
```
*Output: `en_partitions.txt` and `hi_partitions.txt` (8 byte-balanced partition boundaries).*

### Step 3: Compile and Execute Search Engines

```bash
# Baseline sequential search
gcc -O2 -Wall search_seq.c -o search_seq
./search_seq

# 1D range-partitioned parallel search (up to 16 threads)
gcc -O2 -Wall -pthread search_par_debugged.c -o search_par_debugged
./search_par_debugged

# Byte-balanced block parallel search (8 threads)
gcc -O2 -Wall -pthread search_block.c -o search_block
./search_block

# Hierarchical two-level search (8 blocks x 2 chunks = 16 threads)
gcc -O2 -Wall -pthread search_2level.c -o search_2level
./search_2level

# Eight-thread bilingual conjunctive (AND) retrieval
gcc -O2 -Wall -pthread search_8thread_bilingual_and.c -o search_8thread_bilingual_and
./search_8thread_bilingual_and

# Two-phase candidate pruning bilingual search
gcc -O2 -Wall -pthread search_two_phase.c -o search_two_phase
./search_two_phase

# Bidirectional candidate pruning selectivity evaluation
gcc -O2 -Wall -pthread search_bidirectional_bilingual_and.c -o search_bidirectional_bilingual_and
./search_bidirectional_bilingual_and
```

---

## 🔬 Validation Invariants & Correctness Guarantees

All parallel implementations enforce automated validation against the single-threaded baseline:
1. **Set Equivalence**: $\text{sort}(\text{unique}(R_{\text{par}}(q, p))) \equiv \text{sort}(R_{\text{seq}}(q))$
2. **Partition Completeness & Disjointness**: $\bigcup_{j=0}^{p-1} [\text{start}_j, \text{end}_j) = [0, N)$ with $[\text{start}_j, \text{end}_j) \cap [\text{start}_k, \text{end}_k) = \emptyset \quad \forall j \neq k$
3. **Boundary Safety**: Enforcing $\text{end}_j = \text{start}_{j+1}$ eliminates omitted or duplicated boundary sentence IDs.
4. **Zero Duplication**: Disjunctive (OR) queries deduplicate candidate IDs across both language streams before emission.

---

## 📖 References

1. A. Kunchukuttan, P. Mehta, and P. Bhattacharyya, *"The IIT Bombay English-Hindi Parallel Corpus,"* in Proc. 11th International Conference on Language Resources and Evaluation (LREC), Miyazaki, Japan, May 2018. [ACL Anthology](https://aclanthology.org/L18-1548/).
2. Center for Indian Language Technology (CFILT), IIT Bombay, *"IIT Bombay English-Hindi Corpus,"* [Official Corpus Page](https://www.cfilt.iitb.ac.in/~parallelcorp/iitb_en_hi_parallel/).
3. S. Jung, D.-J. Chang, and J. W. Park, *"Large Scale Document Inversion using a Multi-threaded Computing System,"* Applied Computing Review, vol. 17, no. 2, pp. 27–35, June 2017.
4. C. Bonacic, C. García, M. Marín, M. Prieto-Matias, and F. Tirado, *"Building Efficient Multi-Threaded Search Nodes,"* in Proc. 19th ACM Conference on Information and Knowledge Management (CIKM), 2010.
5. G. M. Amdahl, *"Validity of the Single Processor Approach to Achieving Large Scale Computing Capabilities,"* in Proc. AFIPS Spring Joint Computer Conference, 1967, pp. 483–485.
