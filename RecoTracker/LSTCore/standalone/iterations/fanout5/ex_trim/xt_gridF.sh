#!/bin/bash
# Grid F: push the winning -L 3.0 + -TT line found in Grid D.
# -TT holds d15 at 525/932 while -TA spends that track, so -L 3.0 stacks with -TT but not -TA.
P=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fanout5/ex_trim
R=$P/xt_run.sh

"$R" f_l3_tt175      -L 3.0 -TT 1.75            > "$P/drv_f_l3_tt175.out" 2>&1 &
"$R" f_l3bt45_tt15   -L 3.0 -BT 4.5 -TT 1.5     > "$P/drv_f_l3bt45_tt15.out" 2>&1 &
"$R" f_l3bt40_tt15   -L 3.0 -BT 4.0 -TT 1.5     > "$P/drv_f_l3bt40_tt15.out" 2>&1 &
"$R" f_l40_tt15      -L 4.0 -TT 1.5             > "$P/drv_f_l40_tt15.out" 2>&1 &
"$R" f_l25_tt20      -L 2.5 -TT 2.0             > "$P/drv_f_l25_tt20.out" 2>&1 &
"$R" f_l3_tt15_ta110 -L 3.0 -TT 1.5 -TA 1.10    > "$P/drv_f_l3_tt15_ta110.out" 2>&1 &
wait
touch $P/gridF.done
echo "GRIDF DONE"
