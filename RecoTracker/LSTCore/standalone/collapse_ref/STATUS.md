# Twin Collapse pass (collapse_ref) STATUS

Agent: single sequential collapse agent, 2026-08-05 night.
Baseline HEAD = 0d83340d20e (tree clean; stash@{0} = ungated parallel-CAS XC change, LEFT STASHED).
Kernel census at baseline: **86 kernels** across src/alpaka/Chain*.h
(Parallel 31, Attach 13, Arbitrate 12, AttachT3 10, Weld 6, Graph 5, CrossClean 4, Edges 3, Gate 2;
the six non-kernel structs AttachPlsPre / ChainXcPair / AttachTargetPre / ChainTCRowPayload /
ChainOrderKeyRec / ChainFit are not counted).

References generated at HEAD (collapse_ref/):
- lst_cpu_head, lst_cuda_head (saved binaries)
- ref_cpu_n25.root, ref_gpu_n25.root (25 events, -s 1)
- ref_cpu_chaintiming.log, ref_gpu_chaintiming.log (LST_CHAIN_TIMING, n10 s1) - per-stage baseline:
  CPU: compact 0.003 | K9 prep 0.292 | K9 claim 0.773 | K8 attach 159.2 | EXadj 1.392 | EXwalk 0.469 |
       K10 rows 0.010 | K10 emit 0.471 | T3CC 0.073 | XC 1.389 | suppress 0.049 | total 164.1
  GPU: compact 0.065 | K9 prep 0.023 | K9 claim 0.449 | K8 attach 6.384 | EXadj 0.356 | EXwalk 1.135 |
       K10 rows 0.044 | K10 emit 0.022 | T3CC 0.554 | XC 1.061 | suppress 0.068 | total 10.161

## THE DUPLICATION MAP (from LSTEvent.dev.cc, the 7 `if constexpr (kChainSerialArb)` sites)

| # | stage | serial kernel (CPU path) | parallel cascade (device path) | decision |
|---|-------|--------------------------|--------------------------------|----------|
| 1 | -RT5 carried-row compaction (LSTEvent:1345) | ChainCompactCarriedTCs | ChainTCKeepCompact + ChainSegPrefix + ChainTCGather + ChainTCScatter + ChainTCFinishCompact | **(a)** keep parallel, delete serial |
| 2 | K9a/b/c claim (LSTEvent:1431) | ChainArbitrateSerial | ChainClaimBands, ChainCandFlags, prefix, ChainCandScatter, ChainClaimRank, ChainClaimTieCensus, ChainPreClaimPixels, ChainClaimRounds | **(a)** keep parallel, delete serial |
| 3 | EX extension (LSTEvent:1653) | ChainExtendSerial | ChainExtendReach + (ResetMinPos, MinPos, Round) x4 + ChainExtendFinish | **(a)** keep parallel, delete serial |
| 4 | K10 row assignment (LSTEvent:1801) | ChainAssignTCRows | ChainRowFlags + prefix + ChainRowAssign + ChainRowFinish | **(a)** keep parallel, delete serial |
| 5 | K8d carried-row retirement (LSTEvent:2013) | ChainSuppressCarriedTCs | ChainTCKeepSuppress + prefix + Gather + Scatter + ChainTCFinishSuppress | **(a)** keep parallel, delete serial |
| 6 | K8-0b stage-A target list (LSTEvent:2177) | ChainAttachSelectTargets | ChainTargetFlags + prefix + ChainTargetScatter | **(a)** keep parallel, delete serial |
| 7 | K8c stage-A contend + -RD dedup (LSTEvent:2335) | ChainAttachContend | ChainAttachInitPls, Argmax, Resolve, prefix, OwnerScatter, OwnerHits, SeedDedup, Publish, Count | **(a)** keep parallel, delete serial |

Order-dependent kernels that are ALREADY single-copy (one single-thread kernel shared by both
backends) and are therefore NOT twins - left alone, rule (b) by construction:
ChainAttachSelectAux, ChainAttachSeedDedup, ChainAttachT3Dedup, ChainExtendFinish,
ChainT3CCSweepEmit, ChainXcAnchorHits, ChainTCFinish*, ChainRowFinish.

## FUSIONS (same cascade, no cross-thread dependency between the two launches)

| fusion | into | -kernels |
|--------|------|---------:|
| ChainTCFinishCompact + ChainTCFinishSuppress | ChainTCFinish (classCounts==nullptr selects compact tail) | 1 |
| ChainBuildClaimHits + ChainOrderAndSelect + ChainClaimBands + ChainCandFlags | ChainClaimPrep (all four are per-chain elementwise, mutually independent) | 3 |
| ChainClaimTieCensus | ChainClaimRounds prologue (stats-only, order[] final) | 1 |
| ChainExtendResetMinPos | ChainExtendMinPos (`bool reset`; identical reach-set walk, different write) | 1 |
| ChainAttachInitPls | host memset of plsKey (plsOwned is already memset there) | 1 |
| ChainBuildPlsOwnerChain | ChainAttachPublish (one per-chain pass writes plsOwned + plsOwnerChain + stats[4]) | 1 |
| ChainAttachCount | ChainAttachCcsScore prologue (`chains.nAttached() = stats[4]`) | 1 |
| ChainTargetScatter + ChainAttachOwnerScatter + ChainAttachT3Scatter | ChainCompactScatter (one generic keep/offs compaction, src==nullptr means "emit the index") | 2 |
| ChainAttachT3StageOwners | ChainAttachOwnerHits (stage A's owner list becomes position-keyed like stage B's) | 1 |
| ChainAttachT3Resolve | ChainAttachResolve (`targets==nullptr` skips the chain-row publish) | 1 |
| ChainT3CCPreclaim | ChainEmitTCs (same loop, same `tcRow >= 0` test, same mdItems walk) | 1 |

Expected census: 86 - 7 (serial twins) - 15 (fusions) = **64**.

## GROUPS / COMMITS
1. TC compaction + suppression cascade (twins 1, 5 + TCFinish fusion)
2. K9 claim cascade (twin 2 + ClaimPrep + TieCensus)
3. Extension rounds (twin 3 + MinPos/Reset)
4. K10 row assignment (twin 4)
5. Attach stage-A contention/dedup + target selection (twins 6, 7 + attach fusions + stage A/B unification)
6. Generic compaction scatter + T3CC preclaim fold

Per group: CPU build green + lst_cpu -n 5 object-count check (attribution net, cheap).
Final gate: CPU+CUDA build, lst_cpu -n 25 bit-compare vs ref_cpu_n25.root, lst_cuda -n 25 row
agreement, one CPU+GPU timing pair (n200 s1 -w 0, sequential).

## LEDGER
(filled in as groups land)
