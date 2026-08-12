# FINDINGS_JET.md

Recon of the LST Chain pipeline on the hard-QCD dijet (JET) sample. Measurement and
diagnosis only, no code changes. Artifacts in `standalone/jetrecon_ref/`.

Binary under test: main tree at `81a9afe2d00` ("Integrate the NN-loop pipeline: 4 networks
-> 3"), `standalone/bin/lst_cpu` + `lst_cuda` built 2026-08-12 08:36. `src/` and `interface/`
verified clean (`git status --porcelain`), so the binaries are HEAD. `.make.log` grepped for
`error:` -> none.
Master reference: worktree `g3` at `b42d8f97ad5`, `lst_cpu` only (no CUDA build there).

Samples (local copies under `standalone/jet_ref/`; the read-only originals under
`/mnt/data1/kk829` were never touched):
`trackingNtuple_jets_10.root` (10 evt), `trackingNtuple_jets_1000.root` (1000 evt).

---

## [JR 10:45] HEADLINE: the current binary CRASHES on the jet sample, on both backends

Not a slowdown. A hard failure, and it is a **regression introduced by HEAD itself**.

```
lst_cpu  -i jet_ref/trackingNtuple_jets_10.root -n 10 -s 1 -v 1 -w 0
  -> SIGSEGV on event 5 (rc 139), inside alpaka_serial_sync::lst::ChainBuildEdges
lst_cuda -i jet_ref/trackingNtuple_jets_10.root -n 10 -s 1 -v 2 -w 0
  -> abort (rc 134), cudaErrorIllegalAddress, same event
```
1 of the first 10 events dies. The process dies, so **no multi-event and no multi-stream run
over the jet sample completes at all** with this binary. Every jet number below therefore had
to be taken with one isolated process per event (`-x <i>`), which is why this recon carries a
`pe1000/` directory of 1000 single-event runs instead of one timing table.

### Mechanism: a 32-bit byte-size overflow in the ChainEdges allocation

Exact arithmetic, not a guess. Event 5 of the jet sample:

```
[CHAIN] nodes(nT3)=377082 E1=214163878 E2=7164830 E=221328708
[MEM]  ChainEdges: 221328708 allocated (352.9 MB)      <-- WRONG BY 4 GiB
```
`ChainEdgesSoA` is 21 B/row (`inner` u32, `outer` u32, `type` u8, `logOdds` f32, `tie` u32,
`weldBar` f32). With per-column 128 B padding, 221,328,708 rows need
**4,647,903,488 B = 4647.9 MB**. What got allocated was 352.9 MB, and
`4,647,903,488 - 2^32 = 352,936,192`: the byte extent was truncated modulo 2^32. The buffer
is then 13x too small and `ChainBuildEdges` writes past the end of it -> SIGSEGV on CPU,
`cudaErrorIllegalAddress` on GPU.

The truncation site is the platform contract, not our code:
`HeterogeneousCore/AlpakaInterface/interface/config.h:14` -> `using Idx = uint32_t;`
Every alpaka buffer in CMSSW carries a **32-bit byte extent**, so *any* single SoA collection
has a silent 4 GiB ceiling. (`cms::soa::byte_size_type` is `std::size_t`, so `computeDataSize`
is fine; the loss happens when that value becomes a buffer extent.) Nothing checks it and
nothing throws.

**Hard ceiling for ChainEdges: 2^32 / 21 = 204,522,252 edges. Event 5 wants 221,328,708** --
8% over the wall.

### Why this is new in HEAD

`git log -S weldBar -- interface/ChainEdgesSoA.h` -> `81a9afe2d00`, i.e. HEAD. The S1
`weldBar` column added the 4 bytes that took the row from 17 B to 21 B:

| binary | row width | event-5 ChainEdges bytes | vs 2^32 |
|---|---|---|---|
| pre-loop 4-NN (prior recon 2026-08-11) | 17 B | 3.76 GB | fits, 12% headroom |
| HEAD `81a9afe2d00` | 21 B | 4.65 GB | **overflows by 8%** |

Consistent with the prior recon seeing event 5 as merely "worst event 32.5 s" instead of a
crash, and with its `[MEM] ChainEdges: 800 MB` quote (that was event 0 at 47.1 M edges:
17 B x 47.1 M = 800 MB then, 21 B x 47.1 M = 988 MB now -- both confirm the 17 -> 21 B
widening). HEAD had ~0.5 GB of headroom on the worst jet event and spent it. The next column
added to `ChainEdgesSoA` lowers the wall to 2^32/25 = 172 M edges.

**PU200 is nowhere near this cliff**: PU200 tops out at E = 622,599 edges over 150 events =
13.1 MB, a **328x margin** on this ceiling (and 82x on the tighter GPU one found below).
Jet-only.

---

## [JR 10:55] The graph anatomy: the OUTPUT is PU200-sized, the INTERMEDIATE is 380x bigger

Per-event, same binary, `-v 2`. (`-v 2` was verified to cost nothing measurable: every stage
time in the `-v 1` and `-v 2` single-event runs agrees to <1%, so all jet runs below carry
both the timing table and the `[MEM]`/`[CHAIN]` statistics.)

### jets, 10-event file, single stream, one isolated process per event

| evt | nHits | nMD | MD keys used | nT3 | mean deg | E1 | E2 | maxDegIn | maxDegOut | ChainEdges | chains | welded edges |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 0 | 5176 | 1972 | 980 | 154378 | 157.5 | 44,542,034 | 2,517,160 | 1280 | 1456 | 988 MB | 8151 | 8578 |
| 1 | 7744 | 2548 | 1000 | 41251 | 41.3 | 3,361,487 | 282,112 | 394 | 403 | 77 MB | 5465 | 6587 |
| 2 | 4676 | 1491 | 658 | 63298 | 96.2 | 8,087,622 | 594,263 | 723 | 520 | 182 MB | 5148 | 6034 |
| 3 | 8504 | 2619 | 898 | 97977 | 109.1 | 17,512,720 | 1,194,054 | 799 | 923 | 393 MB | 5517 | 5681 |
| 4 | 7494 | 2306 | 901 | 78490 | 87.1 | 12,184,756 | 763,702 | 812 | 740 | 272 MB | 6121 | 6597 |
| **5** | **15721** | **5519** | **1839** | **377082** | **205.0** | **214,163,878** | **7,164,830** | **2303** | **1972** | **4648 MB -> CRASH** | - | - |
| 6 | 7605 | 2522 | 1013 | 111749 | 110.3 | 14,400,056 | 1,107,753 | 1248 | 1370 | 326 MB | 8521 | 9477 |
| 7 | 4527 | 1366 | 550 | 15099 | 27.5 | 681,982 | 87,552 | 198 | 439 | 16 MB | 2276 | 2774 |
| 8 | 6090 | 2042 | 544 | 9002 | 16.5 | 334,360 | 45,075 | 180 | 164 | 8 MB | 1536 | 1940 |
| 9 | 10760 | 3274 | 951 | 42848 | 45.1 | 4,104,853 | 286,289 | 504 | 466 | 92 MB | 5255 | 6184 |

### PU200 control, SAME binary, same session

| evt | MD keys | LS keys | nT3 | E1 | E2 | E | ChainEdges | chains | welded edges |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 0 | 43970 | 139957 | 47297 | 85,121 | 39,299 | 124,420 | 2.6 MB | 7882 | 10370 |
| 1 | 36710 | 95328 | 26437 | 30,978 | 19,110 | 50,088 | 1.1 MB | 4573 | 6177 |
| 2 | 42528 | 122558 | 36671 | 46,054 | 25,733 | 71,787 | 1.5 MB | 6166 | 8240 |
| 3 | 35537 | 93970 | 28309 | 34,377 | 20,234 | 54,611 | 1.1 MB | 4896 | 6519 |
| 4 | 47553 | 157077 | 53153 | 99,994 | 41,563 | 141,557 | 3.0 MB | 8550 | 11026 |

### The one comparison that matters

| quantity | PU200 (median of 150) | jets (median of 1000) | jets worst | ratio worst/PU200 |
|---|---:|---:|---:|---:|
| nT3 | 42,485 | 58,755 | 601,655 | 14x |
| **MD keys used** | **20,378** | **940** | 2,210 | **0.046x (22x FEWER)** |
| mean per-MD degree nT3/K | 2.08 | 62.5 | 272 | 131x |
| **E1** | **69,603** | **6,728,747** | **382,407,452** | **5500x** |
| E2 | ~31,700 | 536,928 | 16,522,641 | 520x |
| chains out | ~7,900 | ~6,000 | - | 0.8x |
| **welded edges out** | **~10,400** | **~6,500** | - | **0.6x** |

**The output is the same size on both samples. Only the intermediate explodes.** On jet
event 0, 8,578 of 47,059,194 enumerated edges are welded: **0.018% yield**. On PU200 event 0
it is 10,370 of 124,420: **8.3%**. The jet edge list is 460x less productive per row and
99.982% of it is enumerated, MLP-scored, and thrown away.

### Why: jets are collimated, so K (the number of shared MDs) does not grow

`E1 = sum over shared-MD keys of degIn(k) * degOut(k)`. With mean degree nT3/K,
`E1 ~ nT3^2 / K`. A jet dumps ~1000 tracks into a few hundred modules, so **K saturates
around 900-2000 while nT3 grows unbounded**; PU200 spreads its tracks over 20,000-48,000
distinct shared MDs so its mean degree stays near 2. That single ratio, mean degree 2.08 vs
62.5-272, squares into the whole 5500x.

Measured fit over all 1000 jet events (log-log):

```
E1 = 3.75 * (nT3^2 / K)^0.952        R^2 = 0.964     K = MD keys with a nonzero degree
E1 = 4.82e-2 * nT3^1.709             R^2 = 0.965
E1 = 4.66e+2 * nHits^1.063           R^2 = 0.052     <- nHits predicts NOTHING
E1 / (nT3^2/K) : median 1.845, p10 1.24, p90 2.76    (1.0 = uniform degrees; the excess is
                                                      the degree variance, which is large)
```
So **nT3 is the only useful predictor and hit count is useless** -- a 5k-hit jet event
outproduces a 230k-hit PU200 event by 400x in edges. Do not size any buffer off nHits.

E2 (the LS-keyed / T4 family) is 5-6% of E1 on jets and never the problem. The whole
pathology is E1, the MD-keyed T5 relation.

---

## [JR 10:58] Is the T3 count itself pathological, or is it only our edge enumeration?

**Both, and the T3 half is inherited from LST master, bit for bit.** `g3` at `b42d8f97ad5`
on the same 10 jet events reports exactly our numbers at every shared stage:

| evt | MDs (master = ours) | LSs (master = ours) | **T3s (master = ours)** | master T5s | master T4s |
|---:|---:|---:|---:|---:|---:|
| 0 | 1448 | 15830 | **154,378** | 59,007 | 4,734 |
| 5 | 2999 | 32636 | **377,082** | 112,002 | 9,282 |
| 8 | 882 | 2985 | **9,002** | 3,026 | 45 |

Every row matches our `[MEM]`/`[CHAIN]` counts. So the T3 explosion (nT3/nLS = 9.8 on jets
vs 0.34 on PU200, a 29x multiplicity) is **shared upstream LST code**, not ours, and E1 is
not our invention either: E1 is precisely the candidate set master's T5 builder iterates
over to make its 59,007 T5s. The difference is only in *how*: master loops it, we
materialize it.

### And master is FAR worse on jets than we are

`lst_cpu -n 10 -s 1 -v 2 -w 0` on the jet 10-event file, ms/event:

| binary | worst event (evt 5) | avg over 10 | where the time goes | peak RSS |
|---|---:|---:|---|---:|
| **LST master b42d8f97ad5** | **116,887** | **16,909** | T5 14,404 + T4 2,268 + pT3 115 | 519 MB |
| **ours 81a9afe2d00** | crash (was 32,500 on the 4-NN binary) | 3,516 (median 1,319) | Graph 3,436 | 4,200 MB max |

Master survives event 5 and takes **117 seconds** on it. We die on it instead, and on the
events we do survive we are **3.4x faster than master overall** and 4.8x faster on the
matched heavy events. Master's T5/T4 stages spend 16.7 s of its 117 s in T4 alone.

So the two designs fail in opposite currencies on the same combinatorics:

* **master: time-bound.** Nested loops, nothing materialized, 519 MB peak. Never crashes,
  takes 2 minutes on one event.
* **ours: memory-bound.** One flat 21 B/edge array, 0.176 us/edge, 4.2 GB peak, and a silent
  4 GiB truncation at 204 M edges that ends the process.

This is worth stating plainly for the round: **we did not create the jet pathology and we are
already the faster of the two implementations of it. What we added is a hard failure mode.**

---

## [JR 11:05] Timing: the chain block is EXACTLY linear in the edge count, on both backends

Fitted over the 9 survivable events of the 10-event jet file (E from 0.38 M to 47 M edges),
single stream, one isolated process per event:

```
CPU   Graph_ms = 171.8 ns/edge * E - 83.5 ms      corr 0.9996
GPU   Graph_ms =   0.718 ns/edge * E + 0.72 ms    corr 0.9995
                  --------------------
                  CPU is 239x slower per edge
```
That is the whole story of the jet cost. There is no separate "jets are hard" effect: **feed
the same pipeline N edges and it costs 172 ns each on one CPU core and 0.72 ns each on an
L40.** Everything else (Hits, MD, LS, T3, pLS, Chain, TC) is 1-60 ms and irrelevant.

Per-edge work, for context on why 172 ns: K5 gathers two 13-float node rows, builds the 14
edge features, standardizes 40 inputs and runs a 40 -> 32 -> 32 -> 3 MLP = 2400 MACs, then
writes a 21 B row. The edge list is also re-read `2 * kChainWeldSweeps = 6` more times by
K6a/K6b.

### CPU, single stream

| | jets, 10-evt file (9 survivable events) | jets, per-event sweep (see below) | PU200, 175 evt, same binary+session |
|---|---:|---:|---:|
| Total ms/evt | 2,112 mean / 1,466 median / 8,206 max | 3,014 mean / 1,213 median | **780.9** |
| Graph ms/evt | 2,056 mean, 8,080 max | 2,934 mean, 33,383 max | **30.2** |
| T3 ms/evt | 38.3 | 54.2 | 62.7 |
| peak RSS | 1,333 MB | 4,293 MB max | 2,296 MB |

**Graph is 114x the PU200 value on average and 1,100x on the worst survivable event.** Note
the PU200 Graph is 30.2 ms at E ~ 124k, i.e. 240 ns/edge -- so PU200's Graph is NOT
edge-dominated at all, it is dominated by the fixed scan over 44k MD keys / 140k LS keys.
Jets invert that completely.

### GPU (L40), single stream

| | jets per-event (9 events) | PU200 175 evt |
|---|---:|---:|
| Total(short) ms/evt | 3.8 - 41.9 | **4.3** |
| Graph ms/evt | 1.19 - 34.37 | **0.6** |
| peak device memory | 1,997 MiB (s=1, evts 0-4) | 853 MiB |

**On the GPU the jet sample is a non-event time-wise**: the worst survivable event of the
first ten costs 41.9 ms, only 10x a PU200 event, and even the 399 M-edge maximum would be only
399 M x 0.718 ns = **286 ms** if it could be allocated at all -- which it cannot (see the
ceilings section: the GPU refuses 9.0% of jet events outright). **The GPU's problem on jets is
100% memory and 0% time.**

### Stream scaling (only events 0-4 could be used -- event 5 kills any batch)

CPU, `-n 5`:

| streams | peak RSS | ms/evt |
|---:|---:|---:|
| 1 | 1,333 MB | 3,122 |
| 2 | 1,623 MB | 3,123 |
| 4 | 1,987 MB | 3,155 |

GPU, `-n 5`:

| streams | peak device MiB | ms/evt |
|---:|---:|---:|
| 1 | 1,997 | 19.1 |
| 2 | 2,299 | 31.5 |
| 4 | 2,983 | 51.5 |
| 8 | 3,139 | 77.9 |

Both scale sub-linearly here only because 5 events cannot fill 4-8 streams and the caching
allocator reuses the big block. `free -g` was checked before and after every step: no swap,
no pressure (376 GB box, 359 GB available throughout).

### GPU memory ceiling, per stream

`ChainEdges` is 21 B/edge and it dominates the per-event footprint
(`[MEM] Total` = ChainEdges + ~15-50 MB of everything else). With ~45 GiB usable on the L40
and ~500 MiB of context/baseline:

| streams | max E per stream that fits the L40 | max E per stream that fits the L4 (23 GB) |
|---:|---:|---:|
| 1 | 2.1 G edges | 1.06 G edges |
| 2 | 1.06 G | 530 M |
| 4 | 530 M | 265 M |
| 8 | 265 M | 133 M |

**But the 4 GiB per-buffer `Idx = uint32_t` wall bites first and it is per-stream-independent:
204.5 M edges, full stop.** So the real table is:

| streams | jet events that fit (of the 1000 measured) | limiting factor |
|---:|---|---|
| 1-4 | see the ceilings section: 9.0% of jet events cannot be allocated on GPU at ANY stream count | the 1 GiB allocator bin, not the device |
| 8 | 8 x 1 GiB = 8.6 GB worst case (the bin caps each stream at 1 GiB) -- fits both cards | the 1 GiB allocator bin |

Concretely, and this was measured rather than reasoned (see the ceilings section below): the
47 M-edge event (event 0) needs 988 MB and fits at 8 streams (7.9 GB) on either card; the
188 M-edge event (event 42) and the 315 M-edge event (event 85) **cannot be allocated on the GPU
at all, at any stream count**, because a single 1 GiB allocation is the policy limit. Device
capacity never becomes the binding constraint on this workload.

---

## [JR 11:12] The attack surface: degree caps, measured on both samples

Method: `LST_CHAIN_NODE_DUMP` (already in the binary, no code change) writes every chain
node's three MD hit rows. From that the exact per-shared-MD `degIn` / `degOut` arrays are
reconstructed offline (`jetrecon_ref/degrees.py`, `degrees2.py`, `nodecap.py`). Validation:
the recomputed E1 and E2 match the `[CHAIN]` log lines **exactly, digit for digit, on every
event** (e.g. event 0: 44,542,034 and 2,517,160), so these are not estimates.

* jets: the 9 survivable events of the 10-event file (event 5 cannot be dumped -- the dump
  call sits at the end of `buildChainEdges()`, after the crash point). Pooled E1 = 105.2 M.
* PU200: 150 events, from the existing `nnloop_ref/round1/nodes.bin` (dumped at
  `4ea386942`, one commit back; the node set is upstream-T3-derived so it is the right
  reference, and its E1 range 24.8k-545k brackets the direct measurement on the current
  binary, 31k-100k). Pooled E1 = 12.4 M.

### Where E1 lives

| | top-10 MD keys | top-100 MD keys |
|---|---:|---:|
| jets (median over 9) | 23% of E1 | **98% of E1** |
| PU200 (median over 12) | 10% of E1 | 38% of E1 |

On jets, **98% of E1 comes from 100 shared MDs**. That is the whole target.

### Family A -- per-shared-MD degree cap (keep at most C in- and C out-triplets per MD)

`kept(k) = min(degIn,C) * min(degOut,C)`, quadratic in C.

| cap C | **jets: E1 removed** | **PU200: E1 removed (pooled)** | PU200 p90 event | PU200 **worst** event |
|---:|---:|---:|---:|---:|
| 32 | 0.9897 | 0.1764 | 0.2256 | 0.7646 |
| 64 | 0.9631 | 0.0789 | 0.0689 | 0.5868 |
| **128** | **0.8767** | 0.0198 | 0.0156 | 0.1915 |
| **256** | **0.6640** | **0.00074** | 0.00020 | 0.0243 |
| **512** | **0.2931** | **0.0000005** | 0 | 0 |
| 1024 | 0.0306 | 0 | 0 | 0 |
| 2048 | 0 | 0 | 0 | 0 |

Max per-MD degree ever seen: **PU200 554** (1 event in 150 above 512; 22/150 above 256;
98/150 above 128) vs **jets 2303**. So:

* **C = 512 is the largest cap that provably never bites PU200** (1 event in 150 loses
  5e-7 of E1) and it only buys **1.4x** on jets. Not enough on its own.
* **C = 256** costs PU200 0.07% of E1 pooled / 2.4% on its single worst event and buys
  **3.0x** on jets (and 6.0x on the big events -- event 0 goes 44.5 M -> 7.46 M).
* **C = 128** buys **8.1x** on jets (event 0 -> 2.15 M) but takes 19% of E1 off one PU200
  event in 150. Only viable if the loss is shown to be irrelevant downstream (see below).

### Family B -- per-NODE top-C cap (each triplet keeps its C best out- and C best in-edges)

`kept(k) = degIn*min(degOut,C) + min(degIn,C)*degOut - min(degIn,C)*min(degOut,C)`, i.e.
**linear in C** instead of quadratic. This is strictly the better family:

| cap C | **jets: E1 removed** | jets: pooled kept | **PU200: E1 removed** | PU200 worst event |
|---:|---:|---:|---:|---:|
| 4 | 0.9774 | 2.38 M | 0.3164 | 0.8098 |
| **8** | **0.9552** | **4.71 M** | 0.1779 | 0.7298 |
| 16 | 0.9122 | 9.23 M | 0.0925 | 0.6174 |
| 32 | 0.8310 | 17.8 M | 0.0425 | 0.4468 |
| 128 | 0.4572 | 57.1 M | 0.00059 | 0.0134 |
| 256 | 0.1920 | 85.0 M | 0 | 0 |

At C = 8 the worst dumped jet event goes **44,542,034 -> 1,212,177 edges (37x, 988 MB ->
25 MB)** and the whole jet sample fits in a 25 MB buffer instead of a 6.6 GB one.

### Why Family B at small C should be EXACTLY lossless -- read the weld

`src/alpaka/ChainWeld.h` K6a/K6b is a **mutual-best matching on `logOdds`**, run
`kChainWeldSweeps = 3` times, and `interface/ChainConfig.h:269` confirms the 3. Each node has
exactly one out-weld slot and one in-weld slot; K6a takes an `atomicMax` of the packed
`(logOdds, tie)` key over that node's eligible incident edges and K6b applies only mutual-best
pairs. **A node can therefore only ever consume its top few eligible edges per direction --
at most one per sweep, so at most 3 ranks deep.** Everything below rank ~3 in a node's own
ordering is enumerated, MLP-scored, re-read 6 more times by the weld sweeps, and discarded.

The receipts, from `[MEM] Chains` minus the chain count:

| | enumerated edges E | **welded edges** | yield |
|---|---:|---:|---:|
| jets evt 0 | 47,059,194 | 8,578 | **0.018%** |
| jets evt 6 | 15,507,809 | 9,477 | 0.061% |
| PU200 evt 0 | 124,420 | 10,370 | 8.3% |
| PU200 evt 4 | 141,557 | 11,026 | 7.8% |

**This is the key result of the recon.** The E1-removed columns above are the WRONG success
metric -- they measure how much of a discard pile you discard earlier. The right metric is
whether the *welded* edge set changes, and a per-node top-C cap with C >= 8 (2.7x the sweep
depth) cannot plausibly change it. The verification is a welded-chain-set diff on PU200
(`LST_CHAIN_EDGE_DUMP` + the existing p25 tooling already do exactly this comparison), not an
E1 count. **If that diff is empty at C = 8, the 5500x jet blow-up disappears at zero physics
cost and PU200 is untouched by construction.**

Implementation catch, stated honestly: "top-C by logOdds per node" needs the scores, which is
what the enumeration produces -- so it cannot be a pre-filter as written. It has to become
either (a) a **tiled** enumerate-score-reduce loop that keeps a per-node top-C heap and never
materializes more than one tile of edges (memory O(C * nT3), time still O(E)), or (b) a cheap
pre-score (geometry only, no MLP) used to pick the C survivors per node before K5 runs (cuts
time as well as memory). (a) is exact; (b) needs the pre-score to correlate with the MLP logit.

---

## [JR 11:50] DEFINITIVE distribution: the full 1000-event jet sample, per-event isolated

`jetrecon_ref/sweep1000.sh`: 1000 single-event `lst_cpu -x i -n -1 -s 1 -v 2 -w 0` runs, one
process each, 63 minutes wall. Raw logs in `jetrecon_ref/pe1000/`, aggregation
`jetrecon_ref/sweepstats.py`, summary + per-event CSV `jetrecon_ref/sweep_summary.txt`.
(Caveat: a handful of these events overlapped a few short stats-only runs of mine; medians and
percentiles are unaffected, the max could be optimistic by ~1%.)

**992 completed, 8 crashed (0.8%): events 5, 85, 217, 306, 496, 515, 646, 972.**

### CPU ms/event, single stream, 992 completed events

| stage | mean | median | p90 | p99 | max |
|---|---:|---:|---:|---:|---:|
| Hits | 1.18 | 1.17 | 1.45 | 1.65 | 1.85 |
| MD | 1.01 | 0.98 | 1.38 | 1.74 | 2.31 |
| LS | 3.20 | 2.94 | 5.07 | 7.70 | 10.81 |
| T3 | 54.2 | 37.8 | 117.2 | 282.4 | 381.4 |
| **Graph** | **2934.4** | **1156.2** | **7277.5** | **25960.1** | **33382.6** |
| pLS | 0.40 | 0.33 | 0.77 | 1.40 | 2.59 |
| Chain | 19.3 | 10.2 | 43.4 | 135.5 | 313.7 |
| TC | 0.37 | 0.37 | 0.65 | 0.97 | 1.39 |
| Reset | 27.0 | 10.5 | 68.8 | 233.5 | 337.7 |
| **Total** | **3014.1** | **1213.4** | **7400.9** | **26459.6** | **33908.1** |

PU200 on the same binary in the same session: **780.9 ms/evt**, Graph 30.2 ms.

**The median jet event costs 1.55x a PU200 event. The mean costs 3.9x. The worst costs 43x.**
This is a pure tail problem: **the top 100 events (10%) carry 53% of all edges and 50.4% of
the 2911 s of Graph time in the sample.** Fixing the tail fixes essentially everything.

### Graph size distribution (all 1000, crashers included)

| | mean | median | p90 | p99 | max |
|---|---:|---:|---:|---:|---:|
| nHits | 7,818 | 7,685 | 11,254 | 13,978 | 15,721 |
| nMD | 2,565 | 2,506 | 3,743 | 4,687 | 5,519 |
| MD keys used (K) | 977 | 940 | 1,458 | 1,840 | 2,210 |
| nT3 | 81,663 | 58,755 | 175,836 | 397,944 | **601,655** |
| **E1** | 18,410,974 | 6,728,747 | 43,482,099 | 175,608,057 | **382,407,452** |
| E2 | 1,047,708 | 536,928 | 2,438,586 | 8,134,950 | 16,522,641 |
| **E = E1+E2** | 19,458,682 | 7,302,680 | 45,606,130 | 184,335,732 | **398,930,093** |
| ChainEdges bytes | 409 MB | 153 MB | 958 MB | 3,871 MB | **8,377 MB** |
| peak RSS | - | 494 MB | 1,226 MB | - | 4,293 MB |

Fitted scaling law over all 1000:

```
E1 = 3.75 * (nT3^2 / K)^0.952         R^2 = 0.964    <- USE THIS to size buffers
E1 = 4.82e-2 * nT3^1.709              R^2 = 0.965
E1 = 4.66e+2 * nHits^1.063            R^2 = 0.052    <- nHits is worthless as a predictor
E1 / (nT3^2/K):  median 1.845, p10 1.237, p90 2.760
CPU  Graph_ms = 0.1741 us/edge * E - 90.6 ms         corr 0.9998
```

### The eight crashing events

| evt | nT3 | E1 | E2 | E | ChainEdges needed |
|---:|---:|---:|---:|---:|---:|
| 972 | 601,655 | 382,407,452 | 16,522,641 | 398,930,093 | **8,377 MB** |
| 85 | 457,806 | 305,912,527 | 9,182,795 | 315,095,322 | 6,617 MB |
| 646 | 475,090 | 293,917,028 | 11,581,431 | 305,498,459 | 6,416 MB |
| 496 | 471,250 | 291,824,409 | 12,380,364 | 304,204,773 | 6,388 MB |
| 217 | 440,654 | 236,144,098 | 10,147,299 | 246,291,397 | 5,172 MB |
| 5 | 377,082 | 214,163,878 | 7,164,830 | 221,328,708 | 4,648 MB |
| 306 | 476,399 | 210,467,457 | 8,967,486 | 219,434,943 | 4,608 MB |
| 515 | 424,028 | 207,137,730 | 9,274,888 | 216,412,618 | 4,545 MB |

Largest survivor: event 776, E = 192,357,534, 4,039 MB, i.e. **within 6% of the CPU wall**.

---

## [JR 11:55] There are THREE ceilings, and the one that bites hardest is on the GPU

| # | ceiling | value | edges (21 B/row) | failure mode | jet events over it |
|---|---|---|---|---|---|
| 1 | **CMS device caching allocator max bin** (`AllocatorConfig.h:19`, `maxBin = 30`, and the header says explicitly "allocations larger than binGrowth^maxBin are set to fail") | **1 GiB** | **51,130,563** | `std::runtime_error` -> abort, clean message | **90 / 1000 = 9.0%** |
| 2 | **alpaka `Idx = uint32_t` buffer byte extent** (`config.h:14`) | 4 GiB | 204,522,252 | **silent truncation -> heap/device corruption -> SIGSEGV / cudaErrorIllegalAddress** | 8 / 1000 = 0.8% |
| 3 | `edgeProdPrefix` / `nEdgesExact` are `uint32_t` (`ChainIncidenceSoA.h:31-32`) | 4.29 G edges | 4,294,967,295 | silent wrap of the COUNT -> wrong-size buffer, no crash | 0 / 1000 (max 383 M, 11x margin) |

Ceiling 1 verified by bisection on real events:

```
evt 643  E = 51,011,733  ->  1,071.2 MB  OK
evt 389  E = 51,237,406  ->  what(): Requested allocation size 1076244992 bytes is too large
                             for the caching detail with maximum bin 1073741824 bytes
evt 776  E = 192,357,534 ->  same throw at 4039103104 bytes
```
So **the GPU cannot run 9% of jet events at all, at any stream count, on any card**, because
1 GiB is a policy limit and not a device limit. The L40's 46 GB is irrelevant. This also means
the ceiling applies identically inside the CMSSW framework path, not just standalone.

Ceiling 2 is the dangerous one: it does not throw, it corrupts. It is also the one HEAD moved
(17 -> 21 B/row). If the next round adds a column to `ChainEdgesSoA`, ceiling 2 drops to
2^32/25 = 172 M edges and 12 more events of the 1000 start corrupting memory silently.

PU200 margin against all three: max E over 150 events = 622,599 rows = 13.1 MB, i.e. **1.2% of
ceiling 1 and 0.3% of ceiling 2. An 82x margin on the tightest ceiling.** Nothing proposed
below can be justified by PU200 headroom -- PU200 has all of it.

---

## [JR 12:00] Where the per-edge cost actually goes (LST_CHAIN_TIMING, jet event 0, E = 47.06 M)

The env var is already in the binary; no rebuild needed.

| kernel | CPU ms | CPU ns/edge | CPU share | GPU ms | GPU ns/edge | GPU share |
|---|---:|---:|---:|---:|---:|---:|
| K0+K1 incidence | 5.1 | - | 0.1% | 1.17 | - | 3% |
| K3 node features | 18.4 | - | 0.2% | 0.07 | - | 0.2% |
| K2 build edges | 867.3 | 18.4 | 10.8% | 2.56 | 0.054 | 7% |
| **K5 edge inference (the MLP)** | **5611.0** | **119.2** | **69.6%** | 8.44 | 0.179 | 23% |
| **K6ab weld (3 sweeps x 2 kernels)** | **1514.1** | **32.2** | **18.8%** | **20.13** | **0.428** | **55%** |
| K6cd+K6e+K6f+K7 | 10.5 | - | 0.1% | 1.28 | - | 3% |
| K8/K9/K10 attach+claim+emit | 37.6 | - | 0.5% | 3.05 | - | 8% |
| Graph stage subtotal (K0..K7) | 8026.5 | 170.6 | | 33.64 | 0.715 | |
| **total incl. attach** | **8064.1** | **171.4** | | **36.69** | **0.780** | |

The 170.6 ns/edge Graph subtotal matches the independent 174.1 ns/edge fit over 1000 events, and
the 0.715 ns/edge matches the 0.718 ns/edge GPU fit. So this attribution is the whole cost.

**The bottleneck is a different kernel on each backend:**

* **CPU: K5, 70%.** 2400 MACs of a 40 -> 32 -> 32 -> 3 MLP per edge, plus two 13-float node-row
  gathers and 40 standardizations. Anything that removes an edge *before* K5 pays 119 ns.
* **GPU: K6ab, 55%.** Not arithmetic -- six full passes over the edge array with two
  `atomicMax` per eligible edge. Anything that shrinks the array pays 6x.

For reference the same run on PU200 event 0: K5 15.3 ms over 124,378 edges = **123 ns/edge,
the same per-edge rate.** PU200 is cheap only because it has 380x fewer edges, and its Graph
cost is dominated by fixed per-key scans (K3 6.75 ms, K0+K1 3.34 ms, K7a 4.41 ms) rather than
by edges at all. Also visible: **on PU200 the single most expensive chain kernel is K8 attach
at 152.7 ms**, which is 5.5x the entire jet-side attach. That is a PU200 finding, out of scope
here, but it is sitting in the same log.

---

## [JR 12:10] PRIORITIZED ATTACK LIST for the speed/memory round

Baseline to beat (all measured above, same binary, same box):

```
jets  1000 evt CPU s=1 : Total mean 3014 ms / median 1213 / max 33908 ; Graph mean 2934
jets  1000 evt GPU s=1 : Graph = 0.715 ns/edge * E -> mean 13.9 ms, max would be 285 ms
jets  crashes           : 8/1000 CPU (0.8%)  ,  90/1000 GPU (9.0%)
jets  peak buffer       : 8,377 MB on one event ; peak RSS 4,293 MB
PU200 175 evt CPU s=1  : 780.9 ms/evt , Graph 30.2 ms , peak RSS 2,296 MB
PU200 175 evt GPU s=1  : 4.3 ms/evt , Graph 0.6 ms , 853 MiB device
PU200 margin to the tightest ceiling : 82x
```

### P0 -- Make the overflow LOUD (correctness, not performance)

Guard the `ChainEdges` (and any other event-sized) allocation against both the 1 GiB allocator
bin and the 4 GiB `Idx = uint32_t` extent, before `emplace`. Right now ceiling 2 silently
truncates and corrupts. Nothing else on this list is safe to evaluate until a run either works
or says why it did not.
**Expected saving: 0. Expected PU200 risk: 0.** Ships alone. Do it first.

### P1 -- Per-node top-C edge retention via a tiled enumerate-score-reduce loop

Never materialize more than one tile of edges; keep a per-node top-C (out) and top-C (in) list
and weld only over the union. `ChainWeld.h` K6a/K6b is a mutual-best matching run
`kChainWeldSweeps = 3` times, one edge per node-slot, so C = 8 is 2.7x the depth the weld can
possibly reach.

* **Memory: both ceilings gone permanently, on every sample.** Peak becomes
  `tile + 2*C*nT3` rows: at C = 8 and the worst observed nT3 = 601,655 that is 9.6 M rows =
  **202 MB** even before choosing a tile size, against 8,377 MB today (**41x**), and the bound is
  exact and sample-independent rather than merely observed.
* Time CPU: the weld stops being O(E) -> the mean chain block goes 3304 -> 2732 ms (**1.21x**).
  Modest, because K5 still sees every edge.
* Time GPU: the weld is **55%** of the GPU chain block -> mean 12.9 -> 5.3 ms (**2.4x**).
* **Expected PU200 risk: zero by construction** at C >= 8, and it is directly falsifiable:
  dump the welded chain set with `LST_CHAIN_EDGE_DUMP` before and after and require a byte-identical
  diff on PU200 and on the survivable jet events. No retraining, no weights touched, no cut moved.
* Cost: the largest code change on this list (K2/K5/K6 restructured into a tile loop).

### P2 -- Per-shared-MD degree cap in K1, C = 256 (or 512)

Truncate the incidence CSR before any edge exists. Cuts K2, K5, K6 *and* the allocation at
once, and it is the cheapest thing here to implement: it is a clamp on `degIn`/`degOut` in the
K1b prefix, with a keep rule ordered by an already-available per-T3 score.

| C | jets E1 removed (pooled / on the big events) | PU200 E1 removed (pooled / p90 / worst of 150) | max per-MD degree ever seen on PU200 |
|---:|---|---|---|
| 512 | 29.3% / 48% | **5e-7 / 0 / 0** | 554 (1 event of 150 above 512) |
| 256 | 66.4% / 83% | 0.074% / 0.02% / 2.4% | 22 of 150 above 256 |
| 128 | 87.7% / 95% | 1.98% / 1.56% / **19.2%** | 98 of 150 above 128 |

* **C = 512 is the only cap that provably never bites PU200** and it buys 1.4-1.9x -- real, free,
  and NOT enough on its own (the worst event still needs 4.4 GB).
* **C = 256 is the recommended first cut**: 3.0x pooled / 6.0x on the tail, and the PU200 cost is
  0.074% of a discard pile whose weld yield is 8.3%. Verify with the same chain-set diff as P1.
* C = 128 clears both ceilings outright but takes 19.2% of E1 off one PU200 event in 150, so it
  must not ship on an E1-count argument alone.
* **Expected saving at C = 256** (extrapolating the measured per-event keep fraction,
  `kept_E1 = 98.3 * E1^0.662`, over all 1000 events): **mean E 19.46 M -> 6.13 M (3.2x), mean chain
  block 3388 -> 1068 ms, worst event 8,377 MB -> 1,340 MB. CPU crashes 0.8% -> 0.0%; GPU crashes
  9.0% -> 0.3% (3 events still over the 1 GiB bin).** So P2 alone fixes the CPU outright and takes
  the GPU to within 3 events of clean -- which P4 then finishes.
* Ships without touching the MLP, the weights, or any working point.

### P3 -- A cheap pre-score gate in front of K5 (the CPU time fix)

K5 is **70% of the CPU chain block at 119 ns/edge** (2400 MACs + two 13-float node gathers + 40
standardizations). Every edge that dies before K5 pays 119 ns. If a geometry-only pre-score
(a subset of the 14 edge features -- `dKappaRel`, `kinkPhi`, `centerDistRel` are the obvious
candidates) can select the top-C per node without the MLP, K5 runs on `2*C*nT3` edges instead
of E: at C = 8, jets go from 19.5 M edges/evt to ~1 M.

* **Expected saving** (K2 still over all E, K5 and the weld over the capped set, C = 8, measured
  keep fraction `kept_E1 = 27.0 * E1^0.616`): **mean E 19.46 M -> 1.69 M (11.5x); mean CPU chain
  block 3304 -> 613 ms (5.4x); mean GPU 12.9 -> 2.1 ms (6.2x); worst event 8,377 -> 457 MB.**
  Both ceilings cleared with 2x margin. This is the biggest single win on the list.
* Risk is a *recall* risk, not a rate risk: the pre-score must not drop an edge the MLP would
  have ranked in a node's top ~3. **Measure this offline first** -- `LST_CHAIN_FEAT_DUMP` already
  writes all 14 edge features plus the logit, so the recall curve of any candidate pre-score is a
  pure Python study on an existing dump. No build needed to decide whether P3 is viable.
* PU200 risk: the same recall question, same verification.

### P4 -- Narrow the edge row from 21 B (cheap headroom, not a fix)

`weldBar` is a table lookup on `(family, wpBin[inner])` and `tie` is
`stableId[inner] ^ stableId[outer]`; both are recomputable in K6 from data K6 already reads.
Dropping either takes the row to 17 B, dropping both to 13 B.

* Measured ceiling / crash-rate sensitivity to the row width, over the 1000 events:

| row | GPU wall | GPU events over | CPU wall | CPU events over |
|---:|---:|---:|---:|---:|
| 13 B | 82.6 M | 4.5% | 330.4 M | 0.1% |
| 17 B | 63.2 M | 6.3% | 252.6 M | 0.4% |
| **21 B (today)** | **51.1 M** | **9.0%** | **204.5 M** | **0.8%** |
| 25 B | 42.9 M | 10.7% | 171.8 M | 1.5% |

  Bit-identical physics, but on its own it only halves the crash rate -- it is a multiplier for
  P1/P2/P3, and the last row is the warning.
* It is also the reason NOT to add another column: HEAD already spent its headroom this way
  (17 -> 21 B is what turned event 5 from slow into a crash), and a 25 B row would take the silent
  CPU-corruption rate from 0.8% to 1.5% and the GPU abort rate from 9.0% to 10.7%.

### P5 -- Compact the eligible edges before the weld (GPU-specific)

K6ab makes **six full passes over the whole edge array** with two `atomicMax` per eligible edge
and it is 55% of the GPU chain block. The first thing to measure is the **eligible fraction**
(`logOdds >= weldBar`) -- it is not instrumented today and I could not get it without a code
change. If eligibility is a few percent, one compaction pass followed by six passes over the
compacted array is a large GPU win and it also helps PU200.
**Expected saving: unknown until the eligible fraction is measured; upper bound is ~50% of the
GPU chain block. Do the measurement in the first hour of the round.**

### P6 -- Buffer sizing policy

Stop sizing anything off `nHits` (`E1 = 4.7e2 * nHits^1.06`, R^2 = **0.052**). The measured
predictor is

```
E1 = 3.75 * (nT3^2 / K)^0.952      R^2 = 0.964      (K = MD keys with nonzero degree)
E1 / (nT3^2/K) : p90 = 2.76        -> use 2.8x the mean-field value as the safe bound
```
and under a cap the bound becomes exact and sample-independent: `K*C^2` for P2, `2*C*nT3` for P1.
Prefer the exact bound; it is what makes the ceilings unreachable rather than merely distant.

### Ranking summary

| | fixes CPU crash | fixes GPU crash | jets CPU speedup | jets GPU speedup | PU200 risk | effort |
|---|---|---|---|---|---|---|
| **P0** guard | makes it loud | makes it loud | - | - | none | tiny |
| **P1** tiled top-C weld | **yes** | **yes** | 1.2x | **2.4x** | none at C>=8, diff-verifiable | large |
| **P2** per-MD cap 256 | **yes** | 9.0% -> 0.3% | **3.2x** | 3.2x | 0.074% of E1, diff-verifiable | **small** |
| **P3** pre-score before K5 | **yes** | **yes** | **5.4x** | **6.2x** | recall study first | medium |
| **P4** 21 B -> 13 B row | 0.8% -> 0.1% | 9.0% -> 4.5% | - | 1.1x | none (bit-identical) | small |
| **P5** compact before weld | no | no | 1.2x | up to 2x | none | medium |

Recommended sequencing: **P0, then P2 at C = 256 (small, ships alone, 3x), then P3's offline
recall study (free, decides the big win), then P1 or P3 as the structural fix.**

### Caveats to carry with any of these numbers

* **No jet physics was measured.** Efficiency / fake / duplicate rate on the jet sample is
  entirely unmeasured in this recon -- it is a pure speed and memory characterization. Any cap
  must be validated on jet physics too, and the jet ntuple's truth matching has not been checked.
* Every jet number is from **one isolated process per event** (`-x i`), forced by the crash. Isolated
  runs pay a cold caching allocator, so single-event totals run a few percent high versus a batch
  (event 0: 8,206 ms isolated vs 6,838 ms in a batch on the older binary).
* Stream scaling on jets could only be measured on events 0-4, because any batch containing one
  of the 8 crashing events dies. The 0.8% / 9.0% crash rates are what a real multi-stream jet run
  would hit, and one crash takes the whole process with it.
* The PU200 degree distributions come from `nnloop_ref/round1/nodes.bin`, dumped one commit back
  at `4ea386942`. Its E1 range (24.8k-545k over 150 events) brackets the direct measurement on
  HEAD (31k-100k over 5 events), so it is the right reference, but it is not HEAD's own dump.
* The prior recon's "PU200 E1 ~ 3.3 M" is **wrong by ~40x**; measured PU200 E1 is 31k-545k. The
  PU200/jet edge ratio is 400x at the median and 4,400x at the tail, not 13x.

---

## [JR 12:20] Artifact index (`standalone/jetrecon_ref/`)

| file | what |
|---|---|
| `cpu_jets10_s1.log/.time` | the crash, first reproduction, `/usr/bin/time -v` |
| `cpu_jets10_v2.log/.err` | `-v 2` batch run: the `[MEM]`/`[CHAIN]` lines for events 0-5 and the stack trace |
| `pe10_v1/`, `pe10_v2/` | per-event isolated CPU runs of the 10-event file (v1 = timing, v2 = timing + stats) |
| `pe10_gpu/` | per-event isolated GPU runs + `nvidia-smi` samples |
| `pe1000/` | **the 1000 single-event CPU runs** (`evt<i>.log` + `evt<i>.err`) |
| `sweep1000.sh`, `sweep1000.rc` | the sweep driver and its per-event exit codes (`DONE` sentinel at the end) |
| `sweepstats.py`, `sweep_summary.txt` | sweep aggregation: distributions, ceiling census, fits, per-event CSV |
| `degrees.py`, `degrees2.py`, `cap_analysis.txt` | exact per-shared-MD degree reconstruction from node dumps; family-A cap curves |
| `nodecap.py`, `nodecap.txt` | family-B (per-node top-C) cap curves, both samples |
| `nodes/jets_evt*.bin`, `nodes/jets_all.bin` | `LST_CHAIN_NODE_DUMP` output for the 9 dumpable jet events |
| `cpu_pu200_s1.log`, `cpu_pu200_v2_n5.log`, `gpu_pu200_s1.log` | the PU200 controls, same binary and session |
| `master_jets10_v2.log/.err` | LST master (`g3`, `b42d8f97ad5`) on the same 10 jet events |
| `cpu_jets5_s{1,2,4}.*`, `gpu_jets5_s{1,2,4,8}.*` | stream scaling on events 0-4 |
| `gpu_big{42,776,972}.err`, `gpuwall_{643,389,736}.log/.err` | the 1 GiB device-allocator ceiling, bracketed |
| `perevent.sh` | the generic per-event isolation driver |

Reproduce the two headline failures:

```bash
pushd /mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone && \
  source setup.sh && eval $(scramv1 runtime -sh) && source setup.sh
# CPU: silent 4 GiB truncation -> SIGSEGV
./bin/lst_cpu  -i jet_ref/trackingNtuple_jets_10.root -x 5   -n -1 -s 1 -v 2 -w 0
# GPU: 1 GiB allocator bin -> clean throw (this event is fine on CPU)
./bin/lst_cuda -i jet_ref/trackingNtuple_jets_1000.root -x 389 -n -1 -s 1 -v 2 -w 0
```
