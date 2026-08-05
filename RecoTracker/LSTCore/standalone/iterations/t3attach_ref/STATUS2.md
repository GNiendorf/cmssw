# T3DEDUP-1 STATUS (exercise campaign; picks up from STATUS.md M9)

Gate: P25BASE eff .81303 | vxy01 .84610 | v15 .80283 | v510 .72713 | v1030 .71732 |
d15 .56009 | d510 .24912 (71/285) | dup .05188 | fake .04636 | nTC 614277.
TARGET eff >= .81303, dup <= .052, fake <= .047, displaced bands not degraded.

## N0 -- ORIENTATION (code read before any run)

1. STATUS.md's "THE FLAG SURFACE" section is STALE for the -CC family. `-CCT` appears in
   usage() (main.cc:421) but is NOT parsed (the parse chain at main.cc:1064-1075 has
   -CCG/-CCN/-CCP/-CCK/-CC/-RDT and no -CCT). The fraction rule was replaced by the
   COUNTING rule: `-CCG 1` (MD units, default) / `-CCG 0` (OT hit rows), `-CCN <n>` =
   number of already-claimed units that kills a delivery (default 2). ta_dedup.sh is
   written against the CORRECT (new) surface.

2. THE pLS-RELEASE FORK IS ALREADY DECIDED BY THE CODE, ANALYTICALLY. On revoke, main.cc
   sets `ga.plsOwned[p] = 0` and the comment claims m16RefreshSupp() "un-retires" the
   type-8 row. It cannot: m16RefreshSupp (main.cc:3194) skips rows that are already
   suppressed and ONLY EVER ADDS. Worse, under `-RPS 1` (the flagship line) the drop
   predicate is
     drop = plsOwned!=0 || plsBestChainLogit>=thetaAttach || plsBestT3Logit>=thetaAttachT3
   and `plsBestT3Logit` is recorded for EVERY scored pair (AttachDelivery.cc:131), not
   only for owners. A revoked delivery's pLS therefore still satisfies
   plsBestT3Logit >= AT3, so its type-8 row stays retired whether or not plsOwned is
   released. => at -RPS 1 the release is a NO-OP for the bare-pLS universe, so the dup win
   from -CC is NOT eroded by resurrected pLS rows. To be confirmed empirically (a -CCR
   flag) rather than left as a code-reading claim.

## N1 -- ROUND 1 MATRIX LAUNCHED (9 points, 300 evts each, parallel)
Base line for all: `-RT3 1 -CF 1 -CFC 1 -AT3 6` on the frozen flagship flags.
  d_none  -RDT 0                       no dedup at all
  d_ot    -RDT 0 -CC 1                 OT-only, MD granularity, N=2
  d_both  -RDT 1 -CC 1                 pixel + OT
  d_px    -RDT 1                       pixel only (== s_a6 control)
  d_n1    -RDT 1 -CC 1 -CCN 1          any single shared MD kills
  d_hit   -RDT 1 -CC 1 -CCG 0 -CCN 1   hit granularity, strictest
  d_nopre -RDT 1 -CC 1 -CCP 0          no pre-claim from already-delivered TCs
  d_otn1  -RDT 0 -CC 1 -CCN 1          OT-only, strictest   (added)
  d_k1    -RDT 1 -CC 1 -CCK 1          keep-best by pLS pt  (added, fork 4b)
Driver t3attach_ref/ta_dedup2.sh; scoreboard t3attach_ref/ta_board.py (adds the M20
pixel/OT/delivered ledger to ta_tab.py's metrics).
Parallel is safe: physics is load-independent, RSS ~530 MB/process, no timing claim made.

REFERENCE NUMBERS TAKEN FROM THE EXISTING s_a6 RUN (= d_px, OT dedup off), 300 evts:
  LST pT3 rows retired by -RT3 1 : 45511  = 151.7/evt   <== the real "LST ~150" reference
  LST pT5 rows retired by -RT5 1 : 241726 = 805.8/evt
  LST pLS rows retired by -RPS 1 : 9400   = 31.3/evt
  our bare-T3 deliveries          : 140022 = 466.7/evt  (after pixel dedup revoked 666.9/evt)
  scoreboard: eff .8140 (+.0010) dup .2829 (x5.45) fake .0667 nTC 712788 (+98511)
So the OT side has to take 466.7/evt down toward ~150/evt without taking the +.0010 with it.

## N2 -- ROUND 1 RESULT (all 9 points, 300 evts)   [ta_board.py]

tag           eff   vxy01     v15    v510   v1030     d15    d510     dup    fake     nTC  pxRev  otRev  deliv
P25BASE   0.81303 0.84610 0.80283 0.72713 0.71732 0.56009 0.24912 0.05188 0.04636  614277     -      -      -
d_none    0.81505 0.84828 0.80071 0.73028 0.71895 0.55901 0.24912 0.53862 0.06186  912579    0.0    0.0 1132.7
d_px      0.81404 0.84719 0.80071 0.73028 0.71814 0.55901 0.24912 0.28287 0.06672  712788  666.0    0.0  466.7
d_ot      0.80894 0.84203 0.79505 0.72555 0.71814 0.55901 0.24912 0.13360 0.06850  650091    0.0  875.0  257.8
d_both    0.80815 0.84123 0.79435 0.72555 0.71814 0.55901 0.24912 0.07151 0.06396  627226  666.0  285.2  181.5
d_k1      0.80824 0.84132 0.79435 0.72555 0.71814 0.55901 0.24912 0.07148 0.06413  627260  666.0  285.1  181.6
d_n1      0.80456 0.83744 0.79011 0.72555 0.71814 0.55901 0.24912 0.04682 0.05649  613420  666.0  331.2  135.5
d_otn1    0.80504 0.83796 0.79011 0.72555 0.71814 0.55901 0.24912 0.05241 0.05906  616614    0.0  986.5  146.2
d_hit     0.80385 0.83678 0.78799 0.72555 0.71814 0.55901 0.24912 0.04612 0.05546  611936  666.0  336.2  130.6
d_nopre   0.81390 0.84709 0.80000 0.73028 0.71814 0.55901 0.24912 0.27891 0.06599  709505  666.0   10.9  455.8
LST(base) 0.81355 0.84733 0.77314 0.65300 0.62908 0.49893 0.22807 0.05130 0.04550  610581

READINGS
 R1. THE DEDUP WORKS ON ROW COUNT AND ON DUP. 1132.7 -> 130.6 deliveries/evt is available,
     and dup .5386 -> .0461 with it. The M9 machinery is not broken.
 R2. THE PRE-CLAIM IS THE WHOLE MECHANISM. d_nopre (delivery-vs-delivery only) revokes
     10.9/evt against d_both's 285.2/evt and leaves dup at .2789. Contention among the
     deliveries themselves is worth almost nothing; contention against the ALREADY
     DELIVERED chain TCs is worth everything. -CCP 1 is not a variant, it is the feature.
 R3. OT-ONLY IS NOT AS GOOD AS PIXEL-ASSISTED at equal strictness, but it is close and it
     gets there ALONE: d_otn1 (no pixel dedup at all) = deliv 146.2, dup .05241, eff
     .80504 against d_n1's 135.5 / .04682 / .80456. The pixel side buys ~0.006 of dup and
     costs ~0.0005 of eff. Both miss the gate for the SAME reason (below), so "OT-only
     preferred" is still live and is not what is blocking.
 R4. -CCK 1 (pLS pt) IS A WASH. d_k1 vs d_both: eff +0.00009, dup -0.00003, deliv +0.1.
     The keep-best key is not where the physics is. Fork (b) answered: no.
 R5. FORK (a) ANSWERED, AND IT IS WORSE THAN ADVERTISED. `M16 suppression ... pLS=9400`
     is IDENTICAL in all nine configs, from 0 revocations to 1002/evt. The pLS release on
     revoke is a measured STRICT NO-OP: the -RPS predicate fires on plsBestT3Logit >= AT3,
     which is recorded for every scored pair, so a revoked seed loses its pT3-class row AND
     stays retired as a bare pLS. Every revocation currently DESTROYS a seed outright.
 R6. THE GATE IS MISSED, AND NOT BY dup. dup is solvable (d_n1 .04682 < .052 target). The
     two failures are EFFICIENCY and FAKE:
       * eff falls monotonically with dedup strength, .81505 (none) -> .80385 (hit,N=1).
         The -CC sweep costs about 0.010 of efficiency, i.e. ~640 sims/300evt, which is
         5x the +0.0020 the class was worth in the first place.
       * fake NEVER comes back to .04636. Even at 130.6 deliveries/evt (LESS than LST's
         151.7) fake is .05546 = 33938 fakes vs P25BASE's 28478. Our pT3-class rows are
         intrinsically dirtier than LST's, ~18 extra fakes/evt, and row count alone does
         not fix it -- the delivery THRESHOLD has to.
 R7. Displaced bands are FLAT across the entire matrix (d15 .55901, d510 .24912 everywhere,
     v510/v1030 move only between .72555/.73028 and .71814/.71895). The dedup does not
     touch displaced tracking either way. No displaced regression, no displaced tuning
     signal.

## N4 -- BARE-T3 PAIRDUMP: THE PATH WAS BROKEN, NOW FIXED
`-m pairdump -PDT 1` aborted immediately with "malloc(): invalid size (unsorted)" (heap
corruption; gdb put the abort in the first TTree::Fill, i.e. long after the real damage).
ROOT CAUSE, main.cc pairdump block: k8EnumeratePrefilteredPairsGeneral assigns targetOrd
over ITS OWN target list -- every accepted chain with nLayers >= 5, then the mask's bare
T3s -- but the local target list honours -PDT. At -PDT 1 the chain targets were missing
locally while still being enumerated, so targetOrd ran past nTgt and
`++pairBegin[pr.targetOrd + 1]` wrote off the end of the heap block. -PDT 0 was immune
because it suppresses its unwanted kind through bareMask, which the enumerator DOES see;
-PDT 2 was immune because both kinds are present. Only -PDT 1 -- the one the deliverable
asks for -- was broken, which is why the never-run path stayed broken.
FIX (main.cc, pairdump block only): hand the enumerator an EMPTY accepted-chain list when
-PDT 1, mirroring the bareMask trick; `accepted` itself is untouched so the bare-T3 mask
is still built from the real accepted set. Plus a hard guard that aborts with a message if
an ordinal ever lands outside the local list again.
VERIFIED 1 evt: targets 15873, pairs 1.16e6, written 24705 at -PDS 1000, 590 KB, 2.0 s.
Binary md5 bf79c848fd63c7cde0d5d3917c53666a. (The r2 runs were launched from
1b5a8e7a...; the delivery path is byte-identical between the two, only the pairdump block
and usage text changed.)

## N3 -- ROUND 2 LAUNCHED (12 points)
Fork (a) done properly (-CCR 2), the -CCN/-CCG curve filled in, and the -AT3 x dedup grid.

CODE ADDED THIS SESSION (flag-gated, default = previous behaviour):
  -CCR <0|1|2>  main.cc. 1 (default, = what M9 shipped) release plsOwned -- measured to be
              a strict no-op at -RPS 1; 0 keep the pLS retired; 2 ALSO erase the seed's
              plsBestT3Logit so the -RPS predicate stops firing and the carried type-8 row
              genuinely survives. -CCR 2 is the only version of "release" that releases.
  usage() -CCT block replaced by the real -CCG/-CCN/-CCR/-RDT text (-CCT was never parsed).
  Binary md5 1b5a8e7a097c0dba944191cc57a07df0 (round-1 binary was 45cb13c123eb...).
