#!/bin/bash
# Grid Y: -ZM4D 99 is floor-safe under BASE2, -ZR is not. Replace the -ZR arm with
# (a) the per-length exempt split -ZR5/-ZR6 and (b) -ZRI, which recenters the IP-5+
# OR-rescue back to the anchor value in-band (BASE2 loosened -MRI 0.5 -> -0.5 globally).
P=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fanout4/compose
cd "$P" || exit 1
Z="-ZBA 20 -ZM4D 99"
./cs2_run.sh y1 $Z -ZM4 -0.5 &
./cs2_run.sh y2 $Z -ZR6 1.3 &
./cs2_run.sh y3 $Z -ZRI 1.0 &
./cs2_run.sh y4 $Z -ZRI 2.0 &
./cs2_run.sh y5 $Z -ZRI 1.0 -ZM4 -0.5 &
./cs2_run.sh y6 $Z -ZR5 1.3 &
wait
echo GRIDY_DONE
