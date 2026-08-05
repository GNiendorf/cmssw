# B01 -- PER-ETA-BAND WORKING POINTS (band WP table)

Workspace: `S/protoB01` (copy of protoFINAL, rebuilt). Runner: `S/b01_ref/b01_run.sh`
(byte-identical to synth_ref/syn_run.sh except OUTDIR=b01_ref and BIN=protoB01).
CHAINFINAL override line: `-T3F 0.10 -XC4 1 -RPSA 5.5 -EXR 4.0 -a 6.0`.
Table tool: `b01_ref/b01_table.py r_G0.json <others...>`.

## Milestones
- [x] protoB01 copied + built
- [x] NO-OP GATE (pre-code-change binary): r_G0 vs synth_ref/r_D1 = 33 IDENTICAL, 0 DIFFER, 0 MISSING
- [x] batch1: XCT band scan on 300 -- SEE RESULT BELOW
- [x] code: -RPSA2 / -RPSA3 eta bins added (exactly the -XCT2/-XCT3 pattern), built
- [ ] batch2 no-op gate G1 (new binary) + RPSA band scan + finer XCT points + combo C1
- [ ] combined WP + 977 confirmation

## BATCH 1 RESULT (300 evt, deltas vs CHAINFINAL)
THE BANDS ARE EXACTLY SEPARABLE. -XCT touches ONLY barrel metrics, -XCT2 ONLY transition,
-XCT3 ONLY endcap; every displaced band except vxy[1,5) is BIT-IDENTICAL, and vxy[1,5)
moves in quanta of 0.00073 = ONE sim. Track length RISES on every tightening (the retired
rows are short bare-pLS rows), so nothing here can regress length.

| tag | flags | eff | dupB | dupT | dupE | nh |
|-----|-------|-----|------|------|------|----|
| G0  | CHAINFINAL         | .81054 | .03127 | .02828 | .07204 | 6.499 |
| T30 | -XCT2 3.0          | -.00040 | -.00021 | -.00890 | -.00003 | +.007 |
| T25 | -XCT2 2.5          | -.00075 | -.00023 | -.01162 | -.00007 | +.009 |
| T20 | -XCT2 2.0          | -.00137 | -.00025 | -.01393 | -.00009 | +.012 |
| B35 | -XCT 3.5 (b-only)  | -.00053 | -.00495 | -.00008 | 0 | +.007 |
| B30 | -XCT 3.0 (b-only)  | -.00133 | -.00870 | -.00012 | 0 | +.013 |
| B25 | -XCT 2.5 (b-only)  | -.00208 | -.01179 | -.00013 | 0 | +.018 |
| E50 | -XCT3 5.0 (RELAX)  | +.00018 | 0 | +.00034 | +.00513 | -.012 |
| E60 | -XCT3 6.0 (RELAX)  | +.00027 | 0 | +.00055 | +.00922 | -.021 |

EXCHANGE RATES (band dup per 1e-4 of overall efficiency):
  transition -XCT2: 22.2 (4.0->3.5 step) .. 10.2 (deep)   BEST VALUE
  barrel     -XCT :  9.3 (4.0->3.5 step) ..  5.7 (deep)
  endcap RELAX -XCT3: buys efficiency at 28 units of dupE per 1e-4 -- i.e. the endcap
  SELLS efficiency at 3x the price the barrel BUYS it for. The "spend endcap dup headroom
  to fund the barrel" idea is DEAD: measured, rejected (E50/E60).
