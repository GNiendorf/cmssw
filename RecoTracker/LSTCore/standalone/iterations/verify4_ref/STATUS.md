# VERIFIER-4 STATUS
Claims: A04 (-a 6.875 -> 6), A09 (-RPS 3 -AT3 7), A14 (negative, baseline unchanged).
Tools written (verifier-owned, never the agents' tables):
  v4_run.sh   frozen prefix transcribed from fin_run.sh (verified line-identical)
  v4_tab.py   full compare_ab metric table for N hists files
  v4_prec.py  5-decimal headline + deltas vs a reference
  v4_bands.py per-band NUMERATOR/DENOMINATOR sim counts (tracks, not rates)
  v4_judge.py matched-duplicate-rate judge, global frontier RE-DERIVED from raw hists

## M1 BINARY PROVENANCE (done)
protoFIN 519b0abc = a04_ref/chainproto_base = protoA09/bin/chainproto. A04 physics runs all
used chainproto_base (checked every r_*.cmd). protoA09/bin/chainproto_a09 fc636849,
protoA14/bin/chainproto(_v1) 1f9a3f13. All md5 claims true.

## M2 NO-OP GATES -- ALL THREE PASS, BIT-IDENTICAL (done)
cmp_branches.py vs fin_ref/r_FINBASE.root, 300 evts: a04 GATE300, a09 G2, a14 AGATE each
33 IDENTICAL / 0 DIFFER / 0 MISSING. Metric table identical incl nTC 618793.
Also confirmed A09's four gates: A_R0==A_R0CCR1, B_R3==B_R3C1, B_R3==B_R4, A_NOOP==G2.

## M3 SCOREBOARD RE-DERIVATION FROM RAW HISTS (done)
A04 -a ladder, A09 batches 2-4 curves, A14 all 28 runs, 977 runs: EVERY quoted number
reproduced exactly. A04 five-for-five Pareto: confirmed at XCT 3.5/4/4.5 and AT3 6.5/7.
A14 global -XCT frontier hardcoded in judge.py: re-derived from hists, exact.

## M4 EVERY BAND (done) -- see allbands.txt
FINDING: A09's "displaced untouched to the last digit in all 29 configs" is FALSE.
 - vxy[1,5) moves in most A09 configs; -8 tracks (.79459 -> .78874) at the RECOMMENDED
   -RPS 3 -AT3 7. (Their per-run table does print v15; the blanket claim does not.)
 - vxy[5,10) is -1 track (.73356 -> .73187) in all 7 -XCG 1 configs, incl. the SECONDARY
   recommendation -RPS 2 -XCG 1.
 The -8 on v15 is an -AT3 7 effect: fin_ref E_A7X4 at -RPS 1 shows the same -8.
A04's displaced accounting is exact and complete (v1030 -3, d15 -2, all others 0).
A14's "bit-identical across all 16 runs" is TRUE for the 16 it had (mtimes checked).

## M5 MECHANISM LEDGERS (done)
A04: type-7 upgrades 575.9->705.1 (+129), XC chain arm 359.4->156.9, OT revoke 330.5->220.7,
 types: pT5 531->638 (LST 707), T5 328->226 (LST 112), pixel-slice subset OK. All exact.
A09: -UM map and overlap block exact. Two-law curve points exact. Crossover ~.0670 verified.
 NOTE: "RPSblock/surviveA literally unchanged 154.0->154.0, 29.8->29.8" is a 6-evt -RPS 2
 measurement quoted in support of the -RPS 3 recommendation; at 300 evts -RPS 2 holds
 exactly (166.3/36.1 both) but -RPS 3 does NOT (187.9->183.8, 35.3->35.6).
A14: RGD splits 55.0/20.1/24.9 vs LST 56.2/22.4/21.3 exact; 485 of 640 endcap revoked exact;
 band denominators 8787/3928/9026 and 143359/77392/259558 exact; 892 sims in over/underflow.

## M6 FRESH RE-RUNS (batch1.sh, in flight)
V4_A04_GATE, V4_A04_A6, V4_A09_GATE, V4_A09_BEST, V4_A09_BESTNX, V4_A14_GATE

## M6 FRESH RE-RUNS -- ALL SIX BIT-IDENTICAL (done)
My own runner (v4_run.sh), my own workspace, from scratch:
  V4_A04_GATE == FINBASE          33/33   V4_A09_GATE == FINBASE   33/33
  V4_A14_GATE == FINBASE          33/33
  V4_A04_A6   == a04_ref/r_E_A6   33/33   (.81032/.05909/.05508, nTC 617584)
  V4_A09_BEST == a09_ref/r_B_R3A7 33/33   (.81019/.07137/.04960, nTC 616333)
  V4_A09_BEST == V4_A09_BESTNX    33/33   -> -XCD 2 inert at the OPERATING POINT too
A04's a04_diff.py on MY fresh outputs: gate 228 loss/229 gain/churn 2.49%; -a 6
219 loss/229 gain, pT3/barrel 47 and pLS/endcap 37. Exactly as claimed.
A04 prefilter closure: volume 2.26M -> 6.56M pairs, delivery line character-identical,
Q_R015/Q_R030 bit-identical to G2_30 at 30 evts. Genuine negative.

## FINAL VERDICT
A04 CONFIRMED. A14 CONFIRMED. A09 ISSUES (displaced-untouched claim falsified for
vxy[1,5) at its own recommended point and for vxy[5,10) in the -XCG 1 family; the
"pure delivery knob" ledger numbers are 6-evt -RPS 2 values used to support -RPS 3).
No processes left running. Nothing written outside verify4_ref/.
