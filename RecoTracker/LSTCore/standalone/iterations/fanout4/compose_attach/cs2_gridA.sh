#!/bin/bash
# Grid A: g3 recentering on the t5-shifted margins.
P=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fanout4/compose
cd "$P" || exit 1
Z="-ZM4D 99 -ZBA 20"
./cs2_run.sh a1 $Z -ZR 1.3  -ZM4 -0.5 &
./cs2_run.sh a2 $Z -ZR 1.3  -ZM4 -1.0 &
./cs2_run.sh a3 $Z -ZR 1.55 -ZM4 -0.5 &
./cs2_run.sh a4 $Z -ZR 1.55 -ZM4 -1.0 &
./cs2_run.sh a5 $Z -ZR 1.8  -ZM4 -0.5 &
./cs2_run.sh a6 $Z -ZR 1.8  -ZM4 -1.0 &
wait
echo GRIDA_DONE
