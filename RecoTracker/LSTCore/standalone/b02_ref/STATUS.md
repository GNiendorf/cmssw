# B02 -- BARREL ATTACH CONVERSION -- STATUS

Workspace: `standalone/protoB02` (copy of protoFINAL). Artifacts: `standalone/b02_ref`.
Runner: `b02_ref/b02_run.sh <TAG> <overrides>` (= synth_ref/syn_run.sh with BIN=protoB02).
Table: `python3 b02_ref/b02_tab.py <tags...>` (first tag = reference for the delta rows).

## M0 -- CODE (done 2026-08-05 09:41)

New flags `-a2` / `-a3`: ETA-BAND BINS of the CHAIN-target attach delivery margin `-a`,
following the existing `-XCT` / `-XCT2` / `-XCT3` pattern exactly.
  `-a`  = |eta| < 1.1   (barrel bin; unchanged name, so old command lines keep meaning)
  `-a2` = 1.1 <= |eta| < 1.7
  `-a3` = |eta| >= 1.7
1e9 = unset = follow `-a` (bit-identical default).

Files touched (5 edits, ~30 lines):
  `main.cc`   : declare aThetaT/aThetaE; pre-scan parse (`-a2`/`-a3` MUST be consumed
                before getopt -- getopt's "a:" would read "-a2" as -a with value "2");
                sentinel resolution next to the -RPSA/-RPST resolution; gap assignment.
  `AttachDelivery.h`  : two fields on GeneralAttachParams.
  `AttachDelivery.cc` : band lookup on |pLS_eta| at the ONE acceptance site
                        (`if (lo < thetaAttach) continue;` in gaStageChains).

Blast radius verified at source level: `thetaAttach` reaches the live -A 4 path ONLY
through `gap.pref.thetaAttach` -> AttachDelivery.cc gaStageChains. The pair PREFILTER /
enumeration never reads it (grep: no `thetaAttach` in PixelAttachPairs.h /
PixelAttachCand.*), so a band split cannot change which pairs exist -- only which are
accepted. main.cc:3118 (`apar.thetaAttach`) is the attachMode 1/2/3 path, dead under
-A 4; main.cc:4411 is the ATTACHCM diagnostic. `-RPSA` is set explicitly by CHAINFINAL,
so the `rpsThetaChain = thetaAttach` fallback is not in play.

The band lookup runs on EVERY pair, not only when the bins differ, so the default command
line exercises the new code and the no-op gate is a real gate.

## M0b -- SECOND FLAG `-a4` (stage A2): 4-LAYER CHAINS MAY ATTACH

Found while reading the -A 4 block: `PixelAttach.cc` gates chain targets at
`minChainLayers` (default 5), so an accepted 4-LAYER chain TC can NEVER become
pixel-anchored. A15's `-XC4` opened that hole for the pair LOG only (score-only, it
explicitly writes neither plsOwned nor plsBestChainLogit), i.e. a 4-layer chain can
DELETE a duplicate seed but never ABSORB it. `-a4` (+ band bins `-a42` / `-a43`) opens
the third exit: a second `gaStageChains` call over the accepted nLayers<5 chains, with
its own per-class margin (the same design that gives 3-layer bare-T3 targets `-AT3`).
Two guards keep it composable: `skipOwnedPls` (stage A1's grants are final) and
`writeBestLogit=false` (plsBestChainLogit untouched, so `-RPSA` is bit-unmoved and the
CONVERSION and DELETION mechanisms remain independently priceable). recordPairs is
suppressed for the call so `-XC4` does not log those pairs twice. 1e9 = OFF.

## M1 -- NO-OP GATES PASS

`B02G0` (CHAINFINAL, no new flags) and `B02G1` (CHAINFINAL + `-a2 6.0 -a3 6.0`) both:
`python3 rebase_ref/cmp_branches.py synth_ref/r_B02G0.root synth_ref/r_D1.root`
-> **33 IDENTICAL, 0 DIFFER, 0 MISSING; ADDED in new (0)**.
(`B02G2` re-gates the later binary that also carries `-a4`.)

## M2 -- BARREL ATTACH MARGIN SCAN (frozen 300)

reference `B02G0` = CHAINFINAL. `-a2 6.0 -a3 6.0` pinned, so ONLY the barrel moves --
and dT / dE / eE / fE come back EXACTLY zero-delta, which is the band split working.

| -a (barrel) | eff | effB | dupB | fakB | nhB | v510 | v1030 | dxy15 |
|---|---|---|---|---|---|---|---|---|
| 6.0 (CHAINFINAL) | .81054 | .92739 | .03127 | .05447 | 9.9256 | -- | -- | -- |
| 5.5 | (batch 2) | | | | | | | |
| 5.0 | +.00022 | +.00057 | **-.00229** | -.00012 | +.018 | 0 | -4 sims | -2 sims |
| 4.5 | (batch 2) | | | | | | | |
| 4.0 | +.00004 | +.00011 | **-.00424** | +.00071 | +.031 | -4 | -11 | -9 |
| 3.5 | (batch 2) | | | | | | | |
| 3.0 | -.00106 | -.00273 | -.01320 | +.00287 | +.098 | -5 | -24 | -17 |
| 2.0 | -.00283 | -.00728 | -.01924 | +.00690 | +.148 | -8 | -61 | -43 |

300-evt displaced denominators: vxy[1,5)=1368 vxy[5,10)=593 vxy[10,30)=1249 dxy[1,5)=897;
in-cut eff denom 22633; dup/fake denom B 141458 / T 76899 / E 257400.

HEADLINE: the CONVERSION route is far cheaper than the diagnosis frontier priced the
DELETION knobs. `-a 5.0` is a FREE win -- efficiency UP, barrel efficiency UP, barrel
dup -.0023, barrel fake DOWN, length UP -- and `-a 4.0` buys -.0042 of dupB at eff
+.00004. Both leave transition/endcap bit-untouched.

</content>
</invoke>
