# E1 -- the maximum-reach combiner. What is here and how to reproduce it.

## The build (one binary, every D-arm lever, inert at its defaults)

Worktree `/mnt/data1/gsn27/here/gpu_wt/g1`, branch `e1_combine`, based on `d9368700c2d`
(= `d950e4315be` + C3's 53-knob measurement instrument, which D3 verified inert), plus

  * `d2_ref/d2_mechanism_only.patch`  -- C6/D2's per-edge-family weld bar `thetaEdgeE1/E2`
    (1e30 = inherit `thetaEdge`), verbatim
  * the `ChainGate.h` half of `c4_ref/c4_all_d950.patch` -- C4/A3/D6's far-dca cell
    `dcaSplit2` / `m3Theta4D2` / `t4FarMaxResid`, verbatim
  * a hand-written flat T4-class dcaXY floor at the gate (`t4DcaFloor`, D1's mechanism in its
    all-eta form, which D1 measured as better than master's eta banding on both dup and fake)
  * NEW, mine: `t4MaxResid` / `t5MaxResid` -- a FIT-QUALITY CEILING on `chains.features()[c][16]`
    (maxXyResid), applied at the gate to the T4 class and to the 5+ classes separately

Env knobs added on top of C3's: `LST_CHAIN_{E1,E2,X2,M4D2,FR,T4F,R4,R5}`.

BASELINE VERIFICATION (mandatory, and it passed on both legs):
  * PU200RelVal 1000 evt, no env: eff .8099 / dup .0479 / fake .0470 / fakB .0516 /
    dxy .5815 .2463 .0317 / vxy .8027 .7227 .7128, n TC(pt>0.9) 1592804, n T4cl 24766 --
    every contract metric equal to `win_ref/all4_rv1000_hists.root` to 4 decimals.
  * cube50 5000 evt, no env: all six bands and every matched/class count identical to
    `cube_ref/cube50_ours.root` (dxy[10,30) 52/9353, T4cl 20).

## Drivers

    e1_ref/run_pu.sh   <arm> <nev> <streams> [VAR=val ...]
    e1_ref/run_cube.sh <arm> <sample> <nev> <streams> [driver args] -- [VAR=val ...]
    e1_ref/ledger.py   <arm> ...       goal ledger from the .judge files
    e1_ref/ntc.py      <file.root>     total TC rows (compare_ab's `n TC`)
    e1_ref/mkpatch.sh  <out.patch> "field:value" ...   builds the deliverable against d950e4315be
    e1_ref/graft.py    (used by mkpatch.sh; copies the mechanism blocks by anchor text)

Both drivers retry at `-s 4` then `-s 2` when the ntuple writer segfaults under box load.

## The deliverables

    e1_candidate.patch          **THE NOMINATION (B2)**: eff .8102 / dup .0452 / fake .0455 /
                                PU200 dxy[10,30) .0494 (91% of master) / ALL SIX cube50 bands at or
                                above master within noise / both protected cube50_highPt goals MET
                                with margin. Cost: PU200 dxy[1,5) -.0302 (2.2 sigma).
    e1_candidate_Y1.patch       same reach, fake .0464, dxy[1,5) -.0214, best held-out numbers.
    e1_candidate_U3.patch       fake .0450, reach 79%, dxy[1,5) -.0149. The fake-goal-first pick.
    e1_candidate_T3.patch       fake .0500, reach 83%, EVERY retain band within 0.25 sigma.
    e1_mechanisms_inert.patch   the two mechanisms only, strictly inert. For stacking.
    E1_ALL_ARMS.txt             all 44 PU200 1000-evt arms, one row each (env lines in pu/<arm>.cmd)
    timing/                     the CPU A/B (first for any candidate in this project)
    pu/ cube/ meas/             every ntuple, judge and census quoted in any [E1] post

All four candidate patches carry the SAME code (the typed weld bar and the far-dca cell) and differ
only in constants, so they are alternatives, not a stack. `git apply` verified for all five at
`d950e4315be`; the constants were additionally verified by BAKING them into the build and reproducing
the env arm to +0.0000 on PU200RelVal 1000, cube50 5000 and cube50_highPt 5000 (`pu/BAKED.judge`).

## Falsified here, with numbers (do not spend a slot)

  * A fit-quality (maxXyResid) ceiling on the whole T4 class or on the 5+ classes: R4 1e9 -> 0.1 buys
    fake -.0005 and costs dxy[1,5) -.0019; R4 = 0.5 removes THREE TCs of 1.6M. C4/D6's residual
    separation is conditional on the mD bar being switched off inside the far cell.
  * `ccMinShared = 1`: byte-identical to base in every metric and in n TC to the row.
  * D1's T4 dcaXY floor stacked on a configuration with `m3Theta4 >= 2`: fake -.0000, eff -.0005.
    The floor and `m3Theta4` are the same cut and `m3Theta4` is the free one.
  * `maxClaimedMDs = 0` on a deep gate: fake .0406 but dxy[1,5) -.0438 and vxy[10,30) -.0460.
  * `m3Theta4 = 4.0` (shipped) on a deep gate: same fake as 2.0 for eff -.0006. 2.0 is an interior optimum.
  * `m3Theta4D` at its shipped -1.2 with the far cell (arm Z6): best PU200 numbers of anything I
    measured and it FAILS cube50_highPt dxy[5,10) at .0150 vs the protected .0177.
