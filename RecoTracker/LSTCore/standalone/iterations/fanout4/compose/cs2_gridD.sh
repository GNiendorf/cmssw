#!/bin/bash
# Grid D: -W probe at the winner + finer trim points on the eff/dup/length frontier.
P=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fanout4/compose
cd "$P" || exit 1
NZ="-ZM4D 99 -ZM4 -0.5"
./cs2_run.sh dw035      $NZ -W 0.35 &
./cs2_run.sh dw035tt15  $NZ -TT 1.5 -W 0.35 &
./cs2_run.sh ett12      $NZ -TT 1.2 &
./cs2_run.sh ett20      $NZ -TT 2.0 &
./cs2_run.sh ett15ta2   $NZ -TT 1.5 -TA 2.0 &
./cs2_run.sh ett15m400  -ZM4D 99 -ZM4 0.0 -TT 1.5 &
wait
echo GRIDD_DONE
