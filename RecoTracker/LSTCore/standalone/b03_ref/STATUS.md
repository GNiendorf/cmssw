# B03 -- BARREL + TRANSITION FAKE (exploration agent, round of 2026-08-05)

Workspace `standalone/protoB03` (copy of protoFINAL + band-split exempt bars).
Artifacts `standalone/b03_ref`.

## M0 SETUP -- DONE
* protoB03 built from protoFINAL, UNMODIFIED binary reproduced CHAINFINAL on the frozen
  300: `synth_ref/r_B03GATE` vs `synth_ref/r_D1` = **33 IDENTICAL, 0 DIFFER, 0 MISSING,
  0 ADDED**.

## M1 ASSIGNED SURFACES -- MEASURED AND CLOSED (see below)
Barrel fake TCs/evt by class on the CHAINFINAL 977 (`b00_ref/cf977.pkl`, 26.3 fake/evt):
seedless-chain br3 12.58 (48%), br1 6.94 (26%), br2 2.84 (11%), seeded chain (t7) 1.84
(7%), pT3-class 1.06 (4%), bare pLS 0.84 (3%).
=> `-T3F` / `-AT3` / `-CCN` reach the pT3-class + bare-T3 pool, which is <= 7% of the
barrel fake AND is already a CREDIT vs LST (pT3cls+seeded .006 vs LST .672). No band
split of those flags can pay for itself. ANGLE PIVOTED to the branch that carries 74%.

## M2 THE REAL HANDLE -- THE EXEMPT-BRANCH BARS ARE GLOBAL, THEIR PURITY IS NOT
`tc_dbgMD` / `tc_dbgMP` are the GATE MARGINS mD / mP (not counts -- b00_fakecells.py read
them as "mdMax" and cut them in the WRONG DIRECTION). The two displaced-exempt admission
bars are:
  br1 (4-layer exempt) : kill iff mD < -M4D                (global -1.2)
  br3 (5+   exempt)    : kill iff mD < -MD && mX < -MR      (-MD 1e9 => kill iff mX < -1.8)
Both GLOBAL, while branch purity is strongly eta-dependent (br1 fake%: B 67 / T 42 / E 19;
br3 fake%: B 26 / T 17 / E 9).

## M3 CODE -- BAND-SPLIT BARS (the blessed -XCT2/-XCT3 pattern)
`protoB03/main.cc`: `-MRB`/`-MRT` (barrel / transition value of -MR) and `-M4B`/`-M4T`
(barrel / transition value of -M4D). Each defaults to its global flag => unset is bit
exact. Bands reuse the EXISTING -ZE1/-ZE2 edges (1.1 / 1.7): NO new eta constants. Band
keyed on the innermost member-T3 |eta|, the same quantity the -Z levers use.
