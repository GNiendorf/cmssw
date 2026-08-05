#!/bin/bash
# Grid F: is -ZM4D 99 (full exempt-T4 wipeout in band) over-tightened under BASE2's
# looser -M4D -1.2? Milder values + one -W 0.60 liveness check + trim/MRI combination.
P=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fanout4/compose
cd "$P" || exit 1
./cs2_run.sh fm4d12  -ZM4D 1.2 -ZM4 -0.5 &
./cs2_run.sh fm4d25  -ZM4D 2.5 -ZM4 -0.5 &
./cs2_run.sh fm4d50  -ZM4D 5.0 -ZM4 -0.5 &
./cs2_run.sh ftt12nz -TT 1.2 &
./cs2_run.sh fcombo  -ZM4D 99 -ZM4 -0.5 -TT 1.2 -MRI -0.75 &
./cs2_run.sh fw060   -ZM4D 99 -ZM4 -0.5 -W 0.60 &
wait
echo GRIDF_DONE
