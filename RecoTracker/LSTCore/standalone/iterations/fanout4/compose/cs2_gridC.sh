#!/bin/bash
# Grid C: trim volume vs dup/length at the best floor-passing band point
# (nozba = -ZM4D 99 -ZM4 -0.5, i.e. g3's band lever with -ZR and -ZBA dropped).
# Track length is an explicit TARGET and -TT 0.8 is BASE2's biggest length cost.
P=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fanout4/compose
cd "$P" || exit 1
NZ="-ZM4D 99 -ZM4 -0.5"
./cs2_run.sh ctt15   $NZ -TT 1.5 &
./cs2_run.sh ctt30   $NZ -TT 3.0 &
./cs2_run.sh ctt30a3 $NZ -TT 3.0 -TA 3.0 &
./cs2_run.sh ctr0    $NZ -TR 0 &
./cs2_run.sh zm4_00  -ZM4D 99 -ZM4 0.0 &
./cs2_run.sh zm4_10  -ZM4D 99 -ZM4 -1.0 &
wait
echo GRIDC_DONE
