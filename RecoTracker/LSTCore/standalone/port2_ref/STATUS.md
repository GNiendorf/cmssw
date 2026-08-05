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
