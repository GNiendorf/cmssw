#!/bin/bash
# Grid G: combine the recentered band lever (-ZM4D 1.2, which keeps the exempt-T4
# branch alive instead of erasing it) with the recentered trim (-TT 1.2/1.5).
P=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fanout4/compose
cd "$P" || exit 1
./cs2_run.sh gm12tt12 -ZM4D 1.2 -ZM4 -0.5 -TT 1.2 &
./cs2_run.sh gm12tt15 -ZM4D 1.2 -ZM4 -0.5 -TT 1.5 &
./cs2_run.sh gm18     -ZM4D 1.8 -ZM4 -0.5 &
./cs2_run.sh gm08     -ZM4D 0.8 -ZM4 -0.5 &
./cs2_run.sh gm12z0   -ZM4D 1.2 -ZM4 0.0 &
./cs2_run.sh gm12z0t12 -ZM4D 1.2 -ZM4 0.0 -TT 1.2 &
wait
echo GRIDG_DONE
