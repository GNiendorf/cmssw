#!/bin/bash
# Grid G: resolve the -BT edge on the winning -L 3.0 + -TT 1.5 stack.
# -BT 4.5 passes (eff .81285), -BT 4.0 fails (eff .81263, d15 523). Find the max.
P=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fanout5/ex_trim
R=$P/xt_run.sh

"$R" g_bt425_tt15    -L 3.0 -BT 4.25 -TT 1.5           > "$P/drv_g_bt425_tt15.out" 2>&1 &
"$R" g_bt435_tt15    -L 3.0 -BT 4.35 -TT 1.5           > "$P/drv_g_bt435_tt15.out" 2>&1 &
"$R" g_bt45_tt175    -L 3.0 -BT 4.5  -TT 1.75          > "$P/drv_g_bt45_tt175.out" 2>&1 &
"$R" g_bt45_tt16     -L 3.0 -BT 4.5  -TT 1.6           > "$P/drv_g_bt45_tt16.out" 2>&1 &
"$R" g_bt45_tt15_ta11 -L 3.0 -BT 4.5 -TT 1.5 -TA 1.10  > "$P/drv_g_bt45_tt15_ta11.out" 2>&1 &
"$R" g_bt44_tt15     -L 3.0 -BT 4.4  -TT 1.5           > "$P/drv_g_bt44_tt15.out" 2>&1 &
wait
touch $P/gridG.done
echo "GRIDG DONE"
