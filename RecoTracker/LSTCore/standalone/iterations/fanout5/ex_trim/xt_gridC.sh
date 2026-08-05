#!/bin/bash
# Grid C: resolve the -TA floor edge (between 1.25 pass and 1.5 eff-fail), and test whether
# the trim lever stacks with RECON-1's free length levers -L 3.0 (T1a) and -BT 4.5 (T1c).
P=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fanout5/ex_trim
R=$P/xt_run.sh

"$R" c_ta130         -TA 1.30                 > "$P/drv_c_ta130.out" 2>&1 &
"$R" c_ta135         -TA 1.35                 > "$P/drv_c_ta135.out" 2>&1 &
"$R" c_ta140         -TA 1.40                 > "$P/drv_c_ta140.out" 2>&1 &
"$R" c_l3_ta125      -L 3.0 -TA 1.25          > "$P/drv_c_l3_ta125.out" 2>&1 &
"$R" c_l3_ta15       -L 3.0 -TA 1.5           > "$P/drv_c_l3_ta15.out" 2>&1 &
"$R" c_l3bt45_ta125  -L 3.0 -BT 4.5 -TA 1.25  > "$P/drv_c_l3bt45_ta125.out" 2>&1 &
wait
touch $P/gridC.done
echo "GRIDC DONE"
