#!/bin/bash
# Grid E: (i) resolve the -TA edge between 1.40 (pass) and 1.50 (eff fail);
# (ii) -L 3.0 costs the same single d15 track that -TA does, so they collide at 523/932.
#      Test whether a SMALLER -L keeps its length gain without spending that track.
P=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fanout5/ex_trim
R=$P/xt_run.sh

"$R" e_ta145      -TA 1.45           > "$P/drv_e_ta145.out" 2>&1 &
"$R" e_l3         -L 3.0             > "$P/drv_e_l3.out" 2>&1 &
"$R" e_l15_ta140  -L 1.5 -TA 1.40    > "$P/drv_e_l15_ta140.out" 2>&1 &
"$R" e_l20_ta140  -L 2.0 -TA 1.40    > "$P/drv_e_l20_ta140.out" 2>&1 &
"$R" e_l15_ta130  -L 1.5 -TA 1.30    > "$P/drv_e_l15_ta130.out" 2>&1 &
"$R" e_l20_ta125  -L 2.0 -TA 1.25    > "$P/drv_e_l20_ta125.out" 2>&1 &
wait
touch $P/gridE.done
echo "GRIDE DONE"
