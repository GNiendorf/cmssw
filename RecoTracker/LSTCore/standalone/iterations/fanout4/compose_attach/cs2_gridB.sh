#!/bin/bash
# Grid B: claim/rescue knobs under the new ordering, anchored on the best floor-passing
# A-family point y1 = -ZBA 20 -ZM4D 99 -ZM4 -0.5. Also re-checks -ZBA inertness in
# combination and two cheap band add-ons.
P=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fanout4/compose
cd "$P" || exit 1
Y1="-ZBA 20 -ZM4D 99 -ZM4 -0.5"
./cs2_run.sh mri025 $Y1 -MRI -0.25 &
./cs2_run.sh mri075 $Y1 -MRI -0.75 &
./cs2_run.sh mri000 $Y1 -MRI 0.0 &
./cs2_run.sh nozba  -ZM4D 99 -ZM4 -0.5 &
./cs2_run.sh yr6    $Y1 -ZR6 1.3 &
./cs2_run.sh yri05  $Y1 -ZRI 0.5 &
wait
echo GRIDB_DONE
