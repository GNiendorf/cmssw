#!/bin/bash
# Grid D: (i) do the two trim dials -TT and -TA stack or overlap? (ii) does -TT stack with -L 3.0?
P=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fanout5/ex_trim
R=$P/xt_run.sh

"$R" d_tt20_ta125  -TT 2.0 -TA 1.25   > "$P/drv_d_tt20_ta125.out" 2>&1 &
"$R" d_tt15_ta125  -TT 1.5 -TA 1.25   > "$P/drv_d_tt15_ta125.out" 2>&1 &
"$R" d_tt175       -TT 1.75           > "$P/drv_d_tt175.out" 2>&1 &
"$R" d_tt25        -TT 2.5            > "$P/drv_d_tt25.out" 2>&1 &
"$R" d_l3_tt20     -L 3.0 -TT 2.0     > "$P/drv_d_l3_tt20.out" 2>&1 &
"$R" d_l3_tt15     -L 3.0 -TT 1.5     > "$P/drv_d_l3_tt15.out" 2>&1 &
wait
touch $P/gridD.done
echo "GRIDD DONE"
