# Simplification + Speedup Pass (simp) STATUS

Agent: single sequential simplification agent, started 2026-08-05.
Baseline: HEAD = de38a4fc416 (integration complete). Chain path is the only path.
Baseline timing (port2_ref/p6_timing_{gpu,cpu}_s1.log, PU200 n200 s1):
  GPU: total 28.8 ms/evt, Chain 25.4
  CPU: total 756.6 ms/evt, Chain 131.0, pLS 312.8, Graph 27.7

Goals: GPU Chain << 10 ms or attributed floor; CPU not regressed; kernel structs -25%
(baseline ~92 across Chain*.h: Parallel 34, Attach 16, Arbitrate 13, AttachT3 9, Weld 6,
Graph 5, CrossClean 4, Edges 3, Gate 2); LOC down; physics gates on every commit.

## Phase 0 status
- [x] GPU per-kernel profile (LST_CHAIN_TIMING, n30 s1, quiet box) -> simp_ref/p0_gpu_chain_timing.log
- [x] Reference outputs: CPU n25 + n300 at HEAD (simp_ref/ref_cpu_n25.root, ref_cpu_n300.root)
- [x] GPU n25 reference (simp_ref/ref_gpu_n25.root)
- [ ] Inventory: serial/parallel twin map, dead config/columns/scaffolding (Explore agents running)
- [ ] Ranked worklist final

## Profile table (Phase 0a) - GPU s1, avg over 30 events, Chain col = 25.4 ms
| stage | ms/evt | note |
|-------|-------:|------|
| K8B contend (ChainAttachT3Contend) | 15.1 | SERIAL 1-thread kernel: O(n^2) selection sort over ~3k owners |
| K10 emit (incl ChainT3CCSweepEmit) | 2.9 | same serial selection-sort idiom inside CC sweep |
| K8B score | 1.24 | MLP over ~2.8M cand |
| K8A score | 1.18 | MLP over ~320k cand |
| K8A ccs | 1.07 | second grid walk |
| EXwalk | 1.00 | 4 rounds x 3 kernels + serial finisher |
| K0+K1 incidence | 0.88 | 2 host syncs |
| K8A RDdedup | 0.80 | prefix/scatter/rank/hits + serial dedup |
| K9 claim | 0.43 | ChainClaimRounds |
| EXadj | 0.39 | seg adjacency build |
| K8A+B grid/pre | 0.6 | |
| weld/trim/features/count | 0.5 | |
| compact + K9 prep + rows + gate | 0.16 | |

NOTE 2026-08-05: a mid-session "TARGET REVISION" directive (1-3 ms aggressive scope incl.
prefilter/precision changes) was RETRACTED by the coordinator minutes later - original brief
stands: GPU Chain well under 10 ms or attributed floor; no prefilter/MLP-precision changes in
this pass. The sort-elimination directive (earlier message) remains in force.

## Worklist (ranked, EV = expected GPU ms saved)
1. [EV ~14.5] K8B contend: argmax + rank decomposition (BIT-EXACT both backends). IN FLIGHT.
2. [EV ~2.3] CC sweep sort hoisted (bit-exact) -> then MAINTAINER DIRECTIVE 2026-08-05: A/B a
   NO-SORT row-order sweep on n300 scoreboard; if null (~1e-5 expected per a05 M13 / t3attach R4),
   ship no-sort + delete ordering machinery (NONEXACT ledger entry).
3. [directive] RD seed-dedup walk order never A/B'd: measure row-order variant on n300; keep an
   O(n log n) sort only if physics moves beyond noise. Goal: zero sorts.
4. [EV ~1-2] Launch-count / host-sync fusion (K0+K1 0.9, EXadj 0.4, small stages).
5. Dead-code prune (agent inventory, ~1120 LOC mechanically safe): env-gated dumps/audits ~500,
   ntuple writer dead fns ~215, trkCore dead helpers ~115, constant-folded ChainConfig fields ~60,
   Common.h Params_* ~31, write-only SoA columns, ModulesPixel stub, LSTEvent.h dead includes,
   CreateTriplets dead aliases (drop ChainTracking template param), unused CLI flags, LST.cc ROOT
   includes, sigmoid_activation. needs-verify items: TC-class counters (-v table), PixelMap
   collapse, Chain2 head ablation (~640 LOC, physics decision - NOT this pass).
6. Twin collapse (~85 -> ~55-60 structs; ChainArbitrate/ChainParallel duplication is the bulk).
7. Score kernels: batching only if free (frozen numerics).
8. [APPENDED, maintainer 2026-08-05, MEASURE-ONLY] -EX REMOVAL A/B: chain extension disabled
   (extendMode=0) vs current. Full n300 scoreboard (eff/dup/fake per band + ALL displaced bands
   + track length per band) + GPU/CPU timing delta (expect ~1.4 ms GPU: EXwalk+EXadj). Primary
   expected cost = track length; dup/fake/displaced couplings expected noise but MUST be quoted
   (extension hits enter the CC claim map + 75%-match definition). Report numbers in ledger as a
   measured option; DO NOT delete code - maintainer decides on the numbers.

## LEDGER
| # | change | LOC delta | kernel delta | CPU ms | GPU ms | gate | commit |
|---|--------|-----------|--------------|--------|--------|------|--------|
| 1 | K8B contend decomposition (argmax+rank+serial residue, both backends); CC sweep takes pre-ranked order | +107 | +2 (85->87: -Contend, +Resolve/Rank/Dedup) | neutral (Chain 131.1=131.4; pLS bimodal ~312/~358 by MACHINE STATE, proven binary-independent by interleaved saved-binary runs) | total 28.8->12.7, Chain 25.4->9.3 | CPU n25 BIT-IDENTICAL 35/35; GPU n25 drift 6/48340 = 0.0124% < same-binary rerun noise 0.0207% | 48c57cc612e |
| 2 | NONEXACT(noise): CC sweep in row order over compacted deliveries; CC rank machinery deleted | -16 | 0 | Chain 130.8 neutral | 12.7 / Chain 9.3, K10 emit 1.515 (row-walk-without-gather variant regressed to 1.87 -> fixed by keeping prefix+scatter) | n300 scoreboard NULL: max delta 0.0003, displaced bands 0.0000, dup/fake <=0.0001, nTC +3/610k; n25 churn 0.19% | cde9b41e5ae |

| 3 | NONEXACT(noise): -RD/-RDT dedup in gather order both stages/backends; RDRank struct deleted, serial sort deleted; ZERO SORTS remain in chain pass | -86 | -1 (87->86) | n25 churn 1.07%, aggregates null | 12.6 / Chain 9.2 | n300: eff +0.0000 dup -0.0000 fake +0.0002 nTC -14/610k; vxy[1,5) -0.0007 = EXACTLY 1 TRACK (1129->1128/1415, band granularity), all other displaced bands 0.0000 | 578972f4cf4 |

CADENCE (maintainer 2026-08-05): LIGHT per-change protocol = build + CPU n25 bit gate +
GPU n200 timing only. HEAVY certification (interleaved CPU timing, n300 scoreboard,
per-kernel profile) at checkpoints every ~5 accepted changes + at end. NONEXACT changes
keep their directive-mandated n300 scoreboard leg.

## Findings
- CPU pLS column (CheckHitspLS 46% of CPU time) is BIMODAL ~312 vs ~358 ms/evt by machine
  state (page/THP layout), NOT by binary: all future CPU A/Bs must interleave base/new saved
  binaries in the same session. perf shares identical across binaries.
- lst_cpu binaries run from copied paths abort at teardown (after timing table prints) - harmless.
- GPU chain profile after change 1 (n30): K8 attach 5.9 (score 1.2+1.2, ccs 1.1, RDdedup 0.8,
  grid/pre 0.6, contend ~1.0), K10 emit 1.5, EXwalk 1.0, K0K1 0.88, K9 claim 0.43, EXadj 0.38.
