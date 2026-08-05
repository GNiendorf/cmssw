#!/bin/bash
# DESCENT batch 2 -- the last 4 of the 12-run budget, driven by batch 1.
# tt03 : batch 1 showed -TT 0.5 -TA 0.5 is the eff frontier (eff .8137, dup .0653,
#        both constraints still met). Push the same axis one more notch to find where
#        dup crosses .0660 -- this is the effmax end-stop probe.
# dd2k : batch 1's -DD 2 is the single biggest rate lever in the program (fake .0473
#        -> .0391, dup .0642 -> .0565) but it breaks three displaced floors. -DDK 1
#        changes the survivor from "K9-best" to "longest chain", which is the only
#        softening knob that can plausibly keep the displaced representative.
# bal  : rm1 had the best track length anywhere (9.922/9.863/3.559 = endcap at LST
#        parity) and dup .0613, but missed d510 by ONE track. Drop the two components
#        that batch 1 flagged as displaced-negative (-BT 3, -ZR6) and keep the trim
#        volume + the dup-tight -ZM4 0.0.
# rmin : dd2k on top of the displaced-friendly trim pack, i.e. spend tt05's displaced
#        surplus on buying the -DD 2 floors back.
P=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fanout4/descent
cd "$P" || exit 1
./ds_run.sh tt03 -TT 0.3 -TA 0.2                    &
./ds_run.sh dd2k -DD 2 -DDK 1                       &
./ds_run.sh bal  -TT 1.5 -TA 3.0 -ZM4 0.0           &
./ds_run.sh rmin -DD 2 -DDK 1 -TT 0.5 -TA 0.5       &
wait
echo DS_BATCH2_DONE
