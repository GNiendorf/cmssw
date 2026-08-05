# a08_ref -- artifact index (A08 displaced audit)

## Tools (analysis only; they read ROOT files and touch no physics)
| file | what it does |
|---|---|
| `a08_run.sh` | run driver. Same frozen flag prefix as `fin_ref/fin_run.sh`; only BIN (protoA08) and the artifact dir differ. `[NEV=] [LSTN=] [BASEHISTS=] a08_run.sh <TAG> <overrides>` |
| `a08_bands.py` | band audit from `createPerfNumDenHists` output WITH numerators, DENOMINATORS and binomial sigma; `--multi tag=file ...` for side-by-side. Adds the `[30,inf)` tails the round scoreboard drops. |
| `a08_simdiff.py` | TRACK-LEVEL LOST/GAINED between a prototype output and LST (or between two prototype outputs), profiled by matched-TC type, nhitOT, pt, eta, vxy, dxy, pdgId. Reproduces the histogram selection exactly. |
| `a08_matrix.py` | efficiency in every (displacement band x eta region) cell, proto vs LST, with cell denominators. |
| `a08_977pass.py` | single pass over the 977 for the vxy>30 tail and the d1030 loss profile. |
| `a08_tab.py` | full scoreboard row (headline + displaced + per region + length) for any tag, falling back to fin_ref / xc_ref / rebase_ref. |

## Measurements
| file | content |
|---|---|
| `gate_check.txt` | the no-op gate: A08GATE == fin_ref/r_FINBASE, 33/33 branches identical |
| `bands_FINBASE_300.txt`, `bands_W_X4_977.txt` | the displaced audit with denominators, 300 and 977 |
| `bands_ablations_300.txt` | FINBASE / GATE(-T3E 0) / -XC 0 / -CC 0 side by side, per band |
| `bands_977_multi.txt` | same invariance check on the full 977 |
| `sens_map_300.txt`, `sens_map_prior.txt` | all 111 prior 300-evt configurations with their displaced bands |
| `matrix_FINBASE_300.txt`, `matrix_W_X4_977.txt` | band x region matrices |
| `simdiff_exactsel_300.txt` | track-level profiles, exact selection (d1030, d510, v1030) |
| `simdiff_d1030_977.txt`, `simdiff_dxy_300.txt`, `simdiff_vxy_300.txt` | earlier profiles on the looser cut |
| `vxy_tail_300.txt`, `a08_977pass.txt` | the vxy>30 tail, finely binned |
| `displaced_length_300.txt` | track length + matched-TC composition per displaced band |
| `displaced_significance.txt` | binomial significance of every displaced band, both samples |
| `d1030_partial_300.txt` | VOID -- a vacuous check, kept only so nobody repeats it |

## Runs (300 evts, all from the assembled baseline)
`r_<TAG>.{root,_hists.root,json,log,cmd}` + `r_agg_<TAG>.txt` for
A08GATE, P_M4D25, P_M4DOFF, P_ZM4D0, P_MR25, T_MR10, T_MRI05.

## Added in the second half of the round
| file | content |
|---|---|
| `a08_distinct.py` | **the correct unit**: DISTINCT displaced sims (displacement = max(vxy,\|dxy\|)), so the vxy and dxy projections are not double counted. Tiers >=1/5/10/30 cm, proto vs LST and proto vs proto. |
| `distinct_displaced_300.txt`, `distinct_displaced_977.txt` | the displaced headline restated in distinct tracks (977: 8512 vs LST 7782, +730) and the -M4D / -MR probes in the same unit |
| `probe_batch_full.txt` | all six chain-layer probes + the full -M4D bracket in one scoreboard |
| `bands_bracket_300.txt` | the bracket with numerators and denominators per band |
| `m4d_bracket_mechanism_300.txt` | T4-class / T5-class populations and the marginal purity of each -M4D step |
| `simdiff_m4d16_vs_base_300.txt` | per-band one-sidedness of the recommended knee (gained/lost 4/1, 3/0, 5/0, 6/0, 8/0, 5/0, 0/0) |
| `simdiff_d1030_m4doff_300.txt` | proof that d1030 is NOT an admission-cut ceiling (kill fully off recovers 1 of 12) |

## Runs added
300 evts: `P_M4D16` (-M4D -1.6), `P_M4D20` (-M4D -2.0).
977 evts: `W_M4D16`, `W_M4D25`, `W_MR10` -- full-sample confirmation of the knee, the far
end of the bracket, and the protection warning.
