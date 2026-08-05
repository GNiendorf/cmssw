# T3ATTACH-BUILD STATUS

Workspace: standalone/t3attach_ref/   (durable milestone log; API may 529 and resume)
Code tree being extended: standalone/prototype/   (== fanout5/final M19 freeze, verified by md5)

## M0 -- SURVEY (DONE)

KEY FINDING: `standalone/prototype/` IS `standalone/fanout5/final` source-identical
(md5 match on main.cc, PixelAttach.{cc,h}, PixelAttachPairs.h, AttachDelivery.{cc,h},
DumpWriter.cc), and it ALREADY CARRIES the full M16 general-attach machinery. The
fanout3/m16 copies of AttachDelivery.{cc,h} and main.cc are OLDER (different md5).
Nothing had to be "ported" for eligibility / delivery / threshold / dump.

Resident before I started (task capabilities 1, 4, 5, 6):
  * `k8BuildBareT3Mask`                  PixelAttach.cc -- eligibility mask
  * `k8EnumeratePrefilteredPairsGeneral` PixelAttach.cc -- ttype 0 chain / ttype 1 bare T3
  * `makeT3Pre` / `makeT3PreGeom`        PixelAttach.cc -- T3 target-side feature mapping
  * `gaStageT3`                          AttachDelivery.cc -- stage B delivery + contention
  * flags -AT3 -RT3 -RPS -PDT -PDS in main.cc, echoed in the run summary
  * pairdump bare-T3 target branch
  attach_mlp_weights.h resident head = r2 (eb4e1bf7...), the M19 freeze head.
Only `train_attach_gen.py` was missing; copied from fanout3/m16 (2026-08-03).

## M1 -- REPRO GATE (PASS)
Frozen binary (md5 8f84320f..., built from the untouched tree), 300 evts:
  eff .8129  dup .0518  fake .0463  dxy[5,10) .2526 == 72/285  nTC 614213
EXACT match to FREEZE_RECORD c4. Wall 115.6 s.
Artifacts: t_FREEZE.{log,root,json,cmd}, t_agg_FREEZE.txt; binary bin/chainproto_frozen.

## M2 -- NEW CAPABILITIES BUILT (compile clean; NO-OP GATE PASS)
New files:  prototype/PixelAttachCand.h   (design + superset proof + file format)
            prototype/PixelAttachCand.cc  (map-candidate ingestion, text + binary)
Modified:   PixelAttach.h/.cc (candidate-finder hook, binned index, superset audit),
            main.cc (flags, per-event index build, reporting, -CC crossclean, -T3E).
NO-OP GATE: 30 evts, all new flags unset -> per-event counter lines BYTE-IDENTICAL to the
frozen binary; only timestamps/paths differ in the summary.

## M3 -- P2.5 STABLE TIE-BREAK PORT (maintainer task, DONE, in validation)
Ported from production commit f41c6abb8a4:
  * `chainNodeStableId` / `chainMix32`  -> K6Weld.cc (constants + fold order verbatim)
  * weld comparator `beats()`           -> tie = stableId(inner)^stableId(outer), LARGER wins
  * `Chains::stableKey`                 -> stableId of the PRE-trim head node (Stages.h),
                                           COPIED through Trim.cc at all 4 emission sites
  * K9 claim order (both passes)        -> key desc, stableKey asc, chain index asc
  * attach -RD dedup order              -> logit desc, stableKey asc, chain index asc
  * extension argmin (Extend.cc)        -> exact-residual tie on
                                           hitKey=(anchorHit<<32)|outerHit, SMALLER wins

## M3 RESULT -- TIE-BREAK CONVERGENCE (PASS, EXACT)
300 evts, frozen flag line. "PROD" = the maintainer-supplied production current numbers.

  metric        FROZEN(old)   P2.5 PROTO (NEW BASELINE)   PROD current   proto-PROD
  eff overall     0.81294            0.81303                0.81303        0.00000
  vxy [0,1)       0.84582            0.84610                0.84610        0.00000
  vxy [1,5)       0.80495            0.80283                0.80283        0.00000
  vxy [5,10)      0.72871            0.72713                0.72713        0.00000
  vxy [10,30)     0.71732            0.71732                0.71732        0.00000
  dxy [1,5)       0.56223            0.56009                0.56009        0.00000
  dup             0.05183            0.05188                0.051883       0.00000
  fake            0.04633            0.04636                0.046360       0.00000
  nTC             614213             614277
EVERY band the maintainer flagged CONVERGED EXACTLY. No residual semantic difference.

RESIDUAL WORTH KNOWING (band not quoted by the maintainer): dxy[5,10) moved
0.25263 -> 0.24912, i.e. the d510 numerator 72/285 -> 71/285 (ONE track). The M19
"d510 = 72" claim is NOT held at the new baseline; the campaign's d510 reference is 71.

NEW PROTOTYPE BASELINE FOR ALL CAMPAIGN A/Bs: tag P25BASE
  eff .81303  vxy01 .84610  v15 .80283  v510 .72713  v1030 .71732
  d15 .56009  d510 .24912 (71/285)  dup .05188  fake .04636  nTC 614277
  wall 126.0 s / 300 evts
Binary: t3attach_ref/bin/chainproto_p25.  Artifacts t_P25BASE.*.

## M4 -- SUPERSET AUDIT (PASS) + VOLUME, 10 evts, -RT3 1 -AT3 6
  -CF 0 analytic full scan : wall 48.8 s | pairs 25,577,679 | T3-attached 3529
  -CF 1 binned prefilter   : wall 14.3 s | pairs 25,577,679 | T3-attached 3529  (IDENTICAL)
  targets 26,107/evt | full-scan pairs 4.47e8/evt
  examined  1.96e7/evt = 0.0438x of full scan        (bare-T3 only, -CFC 0)
  examined  6.16e6/evt = 0.0138x of full scan        (+ chain targets, -CFC 1)
  emitted / analytic 2.56e6/evt | index build 78 ms/evt
  SUPERSET AUDIT: MISSING = 0 in BOTH configurations. PASS.
  Bins: rt 17 x 8.0 cm | tanLambda 134 x 0.6 (|t|<=40) | phi 15 x 0.419 rad | pad 0.02
  wild seeds 0, wild targets 0.
Stronger than required: the binned path does not merely contain the analytic set, it
emits the IDENTICAL pair list, so mode 1 is a pure speedup with no physics delta.

## M5 -- CANDIDATE FINDER DOES NOT PERTURB THE FROZEN LINE (PASS, 300 evts)
tag IDCF = P25BASE flag line + `-CF 1 -CFC 1` (binned prefilter on BOTH target kinds).
  every scoreboard metric IDENTICAL to P25BASE to 5 decimals, nTC 614277 = 614277,
  and all 300 PER-EVENT COUNTER LINES BYTE-IDENTICAL.
  wall 101.6 s vs P25BASE 126.0 s -- the prefilter makes the FROZEN line 19% faster too
  (chain-target attach 200 -> 40 ms/evt).
=> `-CF 1 -CFC 1` is safe to use as the campaign default.

## M6 -- pT3-CLASS CROSSCLEAN (-CC) BUILT (maintainer redirect)
The blocker the sibling recon measured (bare-T3 deliveries never compete for hits, 472
rows/evt vs LST's ~150, 82% duplicates, dup x5.5) is addressed by a post-assembly
hit-overlap contention in main.cc, the CrossCleanpT3 analogue. Shared-hit structure only;
NO dR / dEta / embedding proximity anywhere (standing jet-safety rule).
  mechanism = (i) claimed-hit bitmap over ph2 rows pre-loaded from everything already
  delivered (chain TCs incl. the in-place type-7 upgrades + the carried pixel rows that
  survived the M16 suppression) -- the K9/Extend claim shape; (ii) greedy BEST-FIRST sweep
  over the deliveries -- the attach -RD dedup shape. A revoked delivery releases its pLS
  (plsOwned=0) BEFORE m16RefreshSupp(), so its carried type-8 row is un-retired rather
  than lost.
  flags: -CC 0|1 | -CCT <max claimed OT-hit fraction> | -CCP 0|1 preclaim | -CCK 0|1|2

## THE FLAG SURFACE THE CAMPAIGN DRIVES
(usage() in main.cc carries the same text; `chainproto -h` is authoritative.)

RESIDENT BEFORE M20 (M16, unchanged)
  -RT3 <0|1>   retire EVERY carried LST type-5 (pT3) row. Enables stage B by default.
  -AT3 <logit> bare-T3 attach margin, SEPARATE from the chain margin -a. Default 6.0.
  -RPS <0|1>   retire contested carried type-8 (bare pLS) rows.
  -RD  <0|1>   seed-family dedup of the attach owners (pixel-hit overlap, both stages).
  -PDT <0|1|2> pairdump target scope: 0 chains, 1 bare T3, 2 both.
  -PDS <N>     pairdump: keep 1 in N FAKE bare-T3 pairs (wgt records the stride).
  -PDN <first> pairdump: first LST entry, for chunked dumps.

M20 CANDIDATE FINDING (new)
  -CF  <0|1|2> 0 analytic full scan (frozen default) | 1 binned prefilter | 2 map file
  -CFC <0|1>   route CHAIN targets through the finder too. Proven output-identical and
               19% faster on the frozen line -> recommended campaign default `-CF 1 -CFC 1`.
  -CFA <0|1>   superset audit (runs the analytic scan alongside). MUST report MISSING=0.
               ~2.5x slower; audit runs only.
  -CFB <mult>  bin width as a multiple of the analytic window (default 1.0). Lower =
               tighter candidate volume, more cells.
  -CFR <cm>    rt bin width (default 8.0).   -CFP <rad> phi arc pad (default 0.02).
  -CFM <path>  candidate-pair file for -CF 2 (text or binary, auto-detected).
  -CFW <0|1>   -CF 2: also enforce the analytic windows on map candidates (default 0 --
               the map IS the prefilter; the windows stay as features).

M20 pT3-CLASS CROSSCLEAN (new; the PRIMARY tuning axis per the maintainer redirect)
  -CC  <0|1>   run the hit-overlap contention over the type-5 deliveries.
  -CCT <frac>  drop when the CLAIMED OT-hit fraction exceeds this. 6 OT hits per delivery,
               so the meaningful steps are 0 (any shared hit), 0.17 (>1), 0.34 (>2),
               0.5 (>3). Default 0 = strictest.
  -CCP <0|1>   1 (default) everything already delivered pre-claims its OT hits; 0 the
               deliveries contend only with each other.
  -CCK <0|1|2> keep-best key: 0 attach logit (default) | 1 pLS pt | 2 t3 row.
  -T3E <n>     stage-B enable: -1 follow -RT3 (default) | 0 force off | 1 force on with
               LST's pT3 rows still carried (DIAGNOSTIC ONLY -- double-counts the class).

## M7 -- MAP-CANDIDATE INGESTION (-CF 2) BUILT
New TU prototype/PixelAttachCand.cc. Two interchangeable encodings, auto-detected from the
first 8 bytes, both documented at the top of PixelAttachCand.h:
  TEXT   "E <run> <lumi> <event> <nPairs>" then one "<t3Row> <plsRow>" line per pair.
         '#' comments and blank lines ignored. Pair order free (the loader sorts + dedups).
  BINARY magic "T3PAIRS1", then per event int32 run,lumi,event,nPairs and nPairs x
         (int32 t3Row, int32 plsRow), little-endian.
Rows are production's OWN t3/pLS indices for the same event (the ntuple is written by the
same job), so no translation table is needed. Reference writer + exercise-file generator:
t3attach_ref/ta_makecands.py (emits both encodings from one pair list).
Events absent from the file are COUNTED (nMapEventsMissing) and reported, never silently
treated as "nothing to attach". A parse failure aborts the run.
-CFW 0 (default) = the map IS the prefilter, the analytic windows stay as FEATURES only;
-CFW 1 = enforce them too. -CFA works in map mode as a COVERAGE measurement of the
analytic window set (not a correctness gate there).

## M8 -- MAP MODE (-CF 2) VERIFIED END TO END (3 evts, exercise file)
  text  encoding: loaded "3 events, 30000 pairs" | examined 1.07e7/evt | emitted 196321
  binary encoding: IDENTICAL numbers -> auto-detection + both parsers agree
  -CFW 1 (windows enforced): emitted 173274 < 196321, as it must be
  missing file: run ABORTS with rc=1 (never silently "nothing to attach")
  bare targets with no candidate: correctly counted and reported (61873 of 64417 here,
  because the exercise file only covers t3Row < 200)
Artifacts m_txt/m_bin/m_txtW/m_bad.log, cands_test.{txt,bin}, ta_makecands.py.

## M9 -- DEDUP: CODE COMPLETE, NUMBERS NOT TAKEN  <-- WHERE THE NEXT AGENT PICKS UP
Mechanism implemented per the maintainer granularity brief (MD-granularity ownership map,
never pairwise, no proximity term). Compiles; NO-OP GATE PASSES against P25BASE (30/30
per-event lines identical with -CC unset).
NUMBERS: NONE. The variant matrix (ta_dedup.sh) was abandoned at d_none 54/300 on the
STOP instruction. The only dedup-relevant physics in hand is s_a6 = OT dedup OFF.
Ready-to-run matrix is in t3attach_ref/ta_dedup.sh, ~13 min per 300-evt point.

## ABANDONED / LOST
  * s_a6cc (old hit-granularity semantics) killed mid-run when the granularity brief
    arrived; superseded by ta_dedup.sh's d_hit point, which was never run.
  * ta_smoke.sh points s_a8 / s_a8cc / s_a10 / s_a4cc / s_a6cc2 never ran.
  * The bare-T3 PAIRDUMP was never run (deliverable 6 outstanding). The pairdump path IS
    wired to the binned prefilter (-CF 1) and to -CFA, so it should be ~4x faster than the
    M16 dump was; the stride flag is -PDS and the chunking flag -PDN.
