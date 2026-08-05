# PORT AGENT (phase 1 of 3: port -> strip -> parity+plots) -- STATUS

Artifact dir: standalone/port2_ref. Branch chain_tracking_proto. Remote "fork".
Goal: LST tree (P/src, P/src/alpaka, P/interface) at full CHAINFINAL2 physics, ADDITIVE and
flag-gated. Nothing deleted (that is phase 2). OFF must stay bit-identical to current LST.

## M0 -- BASELINE MEASURED (done)
* CPU build of the untouched tree: clean (.make.log.1785965915; the "error" grep hits are just
  `-Werror=` flags inside the compile command lines -- always check for `: error:` / `Error 1`).
* Reference runs, PU200, `-n 25 -s 1 -v 1` (single stream on purpose: makes the ntuple row order
  reproducible so bit-compares are meaningful):
  - `port2_ref/ref_off_n25.root`  master switch OFF -> **nTC 48335**
  - `port2_ref/ref_on_n25.root`   master switch ON  -> **nTC 48669**  (tree ON == M19 freeze hybrid)
  These two are the OFF-identity reference and the pre-port ON reference.

## M1 -- SCOPE AUDIT (done). WHAT IS AND IS NOT IN THE TREE
Tree state = **the M19 FREEZE, exactly**. Verified by grepping the flag names of every post-M19
prototype mechanism through P/src/alpaka + P/interface: `-CF -CFC -ZPF -ZP5 -ZP8 -XC -XCT -T3E
-CC -CCN -CCR -T3F -XC4 -RPSA -CCS -MRB -MRT -a2 -a3` -> **zero hits** for all of them.
Infrastructure that DOES exist and is reusable:
* master switch `useChainTracking_` (P/src/alpaka/LSTEvent.h:66), standalone
  `--use_chain_tracking` (standalone/bin/lst.cc:78), CMSSW `useChainTracking` +
  grouped `chainTracking` PSet (RecoTracker/LST/plugins/alpaka/LSTProducer.cc:141-212).
* `lst::ChainConfig` (P/interface/ChainConfig.h) -- the by-value config struct every chain kernel
  already takes. THIS is the flag surface to extend.
* full K0-K10 + attach pipeline: ChainGraph/ChainEdges/ChainWeld/ChainGate/ChainArbitrate/
  ChainAttach/ChainParallel (+ 4 weight headers) in P/src/alpaka.
* `ChainAttachT3.h` (untracked, 717 lines): bare-T3 stage B is FAR ALONG but is an
  env-var-gated MEASUREMENT instrument (LST_CHAIN_T3ATTACH / LST_CHAIN_T3REPLACE), not a config
  path. It already has: consumed-T3 mask, keep/prefix/scatter target selection with a `maxFake`
  knob (== -T3F) and a `maxClaimed` knob, target pre-records, stage-B scorer (feature 18 =
  T3 target type), serial contention + -RD seed dedup against stage A's hash table, ownership
  publish with the (-a - -AT3) shift trick, and `ChainEmitBareT3TCs` (type-5 rows).
* the retirement predicate exists in exactly TWO copies, both reading `cfg.attachTheta`:
  `ChainSuppressCarriedTCs` (ChainAttach.h:1056, serial) and `ChainTCKeepSuppress`
  (ChainParallel.h:148, parallel). -RPSA must add ONE field read by BOTH.
* carried-row class replacement: `ChainTCKeepCompact` (ChainParallel.h:122) /
  `ChainCompactCarriedTCs` (ChainArbitrate.h:862) key on `cfg.replacePT3` / `cfg.replacePT5`.

### THE STRUCTURAL FINDING THAT DOMINATES THE ROUND
The prototype's CHAINFINAL2 numbers were measured on the **POSTDELP2 universe**, i.e. with the
prototype emulating the post-P2.7-deletion world:
`-ZPF 3` (zero t3_partOfPT5 / partOfPT3), `-ZP5 1` (pT5-side routing dead), `-ZP8 6`
(bare-pLS TC universe = the CheckHitspLS pass1+pass2 survivors, i.e. **LST's CrossCleanpLS is
gone**), `-RT3 1` (carried pT3 rows dropped). Those four are in the frozen prefix
(standalone/synth_ref/syn_run.sh: `POSTDELP2`), NOT in the tree.
The tree's ON state today still runs the whole LST TC sequence -- including `CrossCleanpLS`
(LST's pLS-embedding-DNN cleaning, LSTEvent.dev.cc:3203) -- and only then drops T5/T4/pT5 rows
and appends chain rows. So the tree ON state is the PRE-deletion hybrid: a *different* universe
from the one every post-M19 number was measured on, and one that leans on exactly the stripped-
LST code the maintainer's finish-line definition forbids relying on.
=> Porting CHAINFINAL2 therefore REQUIRES an ON-mode universe switch (bypass LST's
CrossCleanpLS, drop carried pT3, stop consulting partOfPT5/partOfPT3) as the FOUNDATION, before
any of the six new mechanisms can be scored against the prototype. This is additive and gated,
so OFF-identity is unaffected.

## PORT ORDER (dependency order)
0. ChainConfig surface: all new fields, defaults = CHAINFINAL2 winner; producer PSet entries.
1. ON-mode universe (-ZPF/-ZP5/-ZP8/-RT3 equivalents).
2. Stage-B pT3-class delivery ON (-T3E 1, -T3F 0.10, -AT3 6.0): promote ChainAttachT3.h from
   env-gated measurement to config-gated delivery.
3. -RPSA 5.5 in BOTH retirement-predicate copies (one resolution site).
4. Ported CrossCleanpLS (-XC 3, -XCT 3.75 / -XCT2 3.5, -XC4 1).
5. -CC 1 / -CCN 1 / -CCR 2 contention + revoke-release.
6. -CCS 6.0 / -CCS2 5.0 chain-loser suppression.
7. Pure retunes: -a 5.0 / -a2 5.0 / -a3 6.0, -EXR 4.0, -MRB -1.2 / -MRT -1.2.

## VERIFICATION LADDER
(a) OFF-identity vs ref_off_n25 (nTC exactly 48335 + metric identity).
(b) ON vs prototype CHAINFINAL2 977 record: eff .80957 dup .04812 fake .04637;
    dupB .02314 dupT .01690 dupE .07099; fakB .04869 fakT .05313 fakE .04311.
(c) CUDA build clean + short CUDA run agrees with CPU.

## LOG
* 2026-08-05: M0 + M1 done. Four spec-extraction agents dispatched onto the prototype
  (specs land in port2_ref/specs/: SPEC_XC.md, SPEC_T3E_CC.md, SPEC_CCS_MR.md, SPEC_POSTDEL.md).

## SCOPE CHANGE (coordinator, mid-session): ONE AGENT DOES THE WHOLE INTEGRATION
Phases now owned here: PORT -> ladder (a)(b)(c) -> (A) STRIP for real (delete the replaced LST
code: T5/T4/pT5/pT3 builders, their dedup+crossclean kernels incl. LST's CrossCleanpLS, both
pixel maps, T5/pT5/pT3/T4 DNNs + embedding nets; KEEP CheckHitspLS pass1+2, t3dnn/t3_fakeScore,
MD/LS/T3, pLS machinery) -> (B) post-strip parity vs pre-strip chain-ON -> (C) integrated
performance plots vs the M0 LST reference.

## INTEGRATION AGENT (resumed 2026-08-05): IMPLEMENTATION PLAN (M2)
Maintainer waived intermediate physics verification; the master-OFF no-op gate (nTC 48335 on
PU200 -n 25 -s 1) holds at every commit until the strip begins.

Design decisions taken after the tree audit (all spec-conformant):
* NEW ON-flow in arbitrateChains (prototype stage order transplanted to the tree's layout):
  compact(replacePT3=1 too) -> K9 -> attachPixels{stage A with BANDED -a thresholds read off the
  per-pLS pre-record + XC pass-1 filtered append for 5+ chain targets; aux 4-layer accepted
  targets join the GRID BOUNDS (hull only; superset argument unaffected, scored pairs of stage A
  provably identical) and get their own score-only pass; CCS restricted second pass -> per-chain
  ccsLoserLogit -> flags bit kChainFlagCcsSuppressed; contention+RD; stage B (production: live
  plsOwned, persistent plsBestT3 key array, cfg.t3FakeMax, maxClaimed DELETED, theta =
  cfg.attachThetaT3); RDT} -> EX -> K10 rows (skip CCS-flagged) -> ChainEmitTCs ->
  ChainT3CCPreclaim (MD map from emitted chains' post-extension mdItems CSR; carried pixel rows
  contribute NOTHING because replacePT5=replacePT3=1 dropped them all -- hit2md machinery not
  needed) -> ChainT3CCSweepEmit (serial, logit desc/T3 row asc, -CCN 1 on 3 MDs, -CCR 2 semantics
  hardcoded, FUSED type-5 emission, device-resident; replaces the host round trip + emitBareT3TCs)
  -> XC pixel arm (anchors = plsOwned!=0 final; candidates = carried type-8 rows only, the
  spec 2.4 candidacy restriction; hash set on hit idxs + brute anchor dR loop) + XC chain-arm
  pass 2 over the pass-1 buffer (tcRow>=0 && attachPls<0 -> retire) -> final
  ChainSuppressCarriedTCs MOVED HERE (two bars rpsThetaChain 5.5 / attachThetaT3 6.0 on two
  arrays, + xcRetired, chain rows at the tail kept verbatim + full 5-class recount).
* Single final retirement refresh == the prototype's two refreshes (proved: -CCR 2 releases only
  stage-B-owned seeds, which refresh#1 could not have suppressed; chain evidence untouched).
* tcEta/tcPhi for XC pass-1 computed in ChainAttachTargetPre from the innermost member T3 --
  exact at emission because extendMode=1 (outer only) never changes the innermost member.
* -M4B/-M4T omitted (winner-inert; legal while m3Theta4D stays one global constant).
* Env gates LST_CHAIN_T3ATTACH/T3REPLACE deleted; ON-state is the master switch.
* CrossCleanpLS launch wrapped in if(!useChainTracking_) (the -ZP8 6 universe).
* Writer fixes: 2475 map-insert guard for kBareT3TCMarker rows; bare-T3 emitted rows take the
  T3's eta/phi (recomputed writer-side from the T3's MD anchor hits).

COMMIT LADDER: P1-A config surface (+EXR 4.0, dropPartOfPT3=false, PSet) -> P1-B core delivery
(POSTDEL de-gate, stage B production, CC, retirement rework, flow restructure) -> P1-C bands
(-a/-a2/-a3, -MRB/-MRT) -> P1-D CCS+XC(+XC4) -> ON sanity run -> P2 STRIP (grouped deletions,
rebuild+commit each) -> P3 CUDA + physics + plots.

## M3 -- PORT IMPLEMENTED (P1-A..D landed as one coherent build, 2026-08-05)
All eight port items (a)-(h) implemented in one pass (they share the restructured flow, so the
intermediate commits would not each build):
* ChainConfig: attachTheta 5.0/attachThetaT 5.0/attachThetaE 6.0, rpsThetaChain 5.5, t3FakeMax
  0.10, ccMinShared 1, ccsTheta 6.0/5.0/1e9(OFF), xcTheta 3.75/3.5/3.75, xcDR2Pix 1e-6,
  xcDR2Chain 0.02, m3ThetaRB/RT -1.2, extendRzWindow 4.0, dropPartOfPT3 false; LSTProducer PSet
  surface matched.
* ChainGate.h: -MRB/-MRT band split of the exempt-5+ -MR floor (aEtaC fallback semantics kept);
  chainHitPhi moved to ChainGate.h.
* ChainAttach.h: AttachPlsPre gains eta/attachThr/xcThr/isQuad (banded thresholds resolved once
  per seed); AttachTargetPre gains tcEta/tcPhi (innermost-T3 = K10 TC direction, exact under
  outer-only extension); ChainAttachScore = banded -a + XC pass-1 filtered append;
  ChainBuildPlsOwnerChain + ChainAttachCcsScore (restricted second pass; 4L targets join the
  grid bounds -- hull-only, stage-A scored pairs provably unchanged -- and get their -XC4
  score-only enumeration there); ChainAttachSelectAux; ChainSuppressCarriedTCs reworked (two
  evidence arrays vs rpsThetaChain/attachThetaT3, xcRetired channel, chain-row tail kept, 5-class
  recount, pixelTriplets/pixelQuintuplets args dropped).
* ChainAttachT3.h: production stage B (env gates gone, live plsOwned, persistent plsBestT3,
  cfg.t3FakeMax gate, maxClaimed DELETED, hist/tgtBestAny stripped, CopyOwned+PublishOwnership
  shift trick DELETED); NEW ChainT3CCPreclaim + ChainT3CCSweepEmit (fused -CC sweep + type-5
  emission, -CCR 2 hardcoded).
* NEW ChainCrossClean.h: XC anchor list/hash, pixel arm (shared-hit + dR, candidacy = carried
  type-8 rows), chain arm pass 2.
* LSTEvent.dev.cc: CrossCleanpLS skipped under the master switch; arbitrateChains restructured
  (pls-side state hoisted; stage A -> CCS -> stage B -> EX -> K10(CCS skip) -> emit -> CC sweep
  -> XC -> final suppress, both backend forms); emitBareT3TCs + host round trip deleted.
* Writer: pt3_idx_map guard for kBareT3TCMarker rows; bare-T3 rows take the T3's eta/phi
  (writer-side recomputation, t3_eta/t3_phi conventions); bt3_* probe branches deleted.
* MASTER-OFF GATE: PASS. nTC 48335 and per-type {T5 3370, pT3 3673, pT5 19240, pLS 21184,
  T4 868} identical to ref_off_n25 (port2_ref/p1_off_n25.root).
Deferred edge (recorded): an event with ZERO welded chains skips stage B entirely (arbitrate
early-return); prototype would still deliver bare-T3 rows. Never occurs at PU200.
* -M4B/-M4T omitted (winner-inert while m3Theta4D stays global).
Commit c1d7f52ca7d, pushed to fork.

## M4 -- STRIP (in progress)
Maintainer addendum folded in: the standalone -v 1 timing table (trkCore.cc
printTimingInformation + runEvent timers) must lose the T5/T4/pT5/pT3 columns and gain
chain-path stage columns (per-stage attribution is the deliverable for the timing/memory
fan-out round). Plan: keep Hits/MD/LS/T3/pLS/TC/Reset, add a Chain column measured around
arbitrateChains in runEvent (finer attribution stays available via LST_CHAIN_TIMING).
Three read-only inventory agents dispatched (collections/xml/pixel-map + standalone harness +
kernel headers). Core strip order: LSTEvent+LST+TrackCandidate/Kernels -> SoAs/collections/xml
-> NeuralNetwork+weights+embed -> pixel maps -> harness/writer + timing table -> master-switch
removal. Rebuild + ON-run + commit per group.

STRIP EXECUTED (single sweep, building now):
* DELETED FILES: src/alpaka/{Quintuplet,Quadruplet,PixelTriplet,PixelQuintuplet}.h,
  {T5,T4,pT3}NeuralNetworkWeights.h, {T5Embed,pLSEmbed}NetworkWeights.h, and the 12 interface
  headers of the four collections (SoA + Host + Device).
* LSTEvent.dev.cc/-h: create/add/getters for the four classes deleted; createTrackCandidates
  chain-only (CrossCleanpT3/T5/T4/pLS gone, Add pT5/pT3/T5/T4 gone, dedups gone, CheckHitspLS
  pass 2 kept, CountSurvivingTCs = pLS only, allocation = pLS + nChainCount_ +
  kChainBareT3TCHeadroom 4096); LST_CHAIN_SKIP_DOOMED deleted; LST.cc sequence chain-only.
* Kernels.h 868->~100 lines (CheckHitspLS + rmPixelSegmentFromMemory); TrackCandidate.h
  852->~150 (addpLSTrackCandidateToMemory + pLS-only CountSurvivingTCs + AddpLSasTrackCandidate);
  NeuralNetwork.h 564->141 (t3dnn + shared primitives).
* Embedding: plsEmbed compute + column + partOfPT5 column deleted (Segment.h,
  PixelSegmentsSoA); TripletsSoA loses connectedMax/connectedLSMax/partOfPT5/partOfT5/partOfPT3;
  ChainOrderAndSelect pixel-consumed drop deleted; ChainConfig dropPixelConsumed/dropPartOf*
  deleted; ObjectRangesSoA loses all 10 T5/T4 fields; alpaka/Common.h dnn loses
  plsembdnn/t5dnn/pt3dnn/t4dnn.
* Pixel maps: PixelMap reduced to pixelModuleIndex only; getConnectedPixels + superbin fill +
  connectedPixels device fill deleted; pLS_map file loading + lstg.pixel_map consumption deleted
  (LSTESData.cc); modulesPixel block kept as 1-row stub so the multi-block layout is unchanged.
  (LSTInputSoA superbin/pixelType columns are now write-only -- deferred, input-format change.)
* Harness: AccessHelper 26k->8.7k (T4/T5/pT3/pT5 + TC-dispatch blocks gone); writer loses the
  t5/t4/pt3/pt5/t5dnn/t4dnn create+set+parse machinery (~66k chars), tc_*Idx reduced to
  tc_plsIdx, occupancy trimmed, parse switches chain+pLS only; trkCore loses the four doomed
  runners; timing table restructured to Hits/MD/LS/T3/Graph/pLS/Chain/TC/Reset with
  chainBuildMs_/chainTCMs_ hooks in LSTEvent (maintainer addendum); flags --t5/--pt3/--pt5/--t4/
  --t5dnn/--t4dnn removed.
* Dictionaries were already clean; RecoTracker/LST needs nothing (LSTObjType + TC counters kept).
Maintainer addendum 2 (recorded): after post-strip verification, run timing CPU 1-stream and GPU
1-stream (-n 200 -v 1 -w 0 -s 1, PU200), SEQUENTIALLY; report per-stage tables; no LST-baseline
comparison here.

## M5 -- STRIP COMPLETE (3 commits, all pushed to fork)
* c1d7f52ca7d PORT (OFF gate PASS 48335; ON 48341, ~116 pT3-class rows/evt)
* 7dff05be2ee STRIP (-12572 lines, 49 files; post-strip ON output IDENTICAL to pre-strip ON:
  nTC 48341, T5 5273 / pT3 2897 / pT5 18537 / pLS 20794 / T4 840)
* bbed527249a MASTER SWITCH REMOVED (chain-only path; output identical again, no flag)
The OFF no-op gate is retired (OFF path no longer exists).

## M6 -- PHASE 3 PLAN (verification + plots + timing)
(a) clean -mCG build; CUDA n25 vs CPU n25 count agreement (small drift acceptable, precedent).
(b) physics: PU200RelVal FIRST-300 run (-s 32) -> hists -> compare_ab vs port2_ref/
    rv_off_n300_hists.root (M0 LST baseline, same events) + lst_plot_performance --compare;
    PU200RelVal ALL-1000 run -> compare_ab --proto mine --base synth_ref/r_F3W977_hists.root
    (integrated vs prototype CHAINFINAL2; CAVEAT: 1000 vs 977 event sets, the 977 ntuple is
    missing 23 events, so ~2% sample delta is expected on top of port fidelity).
(c) timing: lst_cpu then lst_cuda, -i PU200 -n 200 -v 1 -w 0 -s 1, sequential; report the new
    Hits/MD/LS/T3/Graph/pLS/Chain/TC/Reset table.
