#!/bin/bash
# Grid X: decompose the -Z family under BASE2 + recenter -ZM4D itself
# (grid A showed ALL six points breach the dxy[5,10) floor).
P=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fanout4/compose
cd "$P" || exit 1
./cs2_run.sh x1 -ZBA 20 &
./cs2_run.sh x2 -ZBA 20 -ZM4D 99 &
./cs2_run.sh x3 -ZBA 20 -ZR 1.3 &
./cs2_run.sh x4 -ZBA 20 -ZM4D 1.2 -ZR 1.3 -ZM4 -0.5 &
./cs2_run.sh x5 -ZBA 20 -ZM4D 2.5 -ZR 1.3 -ZM4 -0.5 &
./cs2_run.sh x6 -ZBA 20 -ZM4D 99 -ZR 0.7 -ZM4 -0.5 &
wait
echo GRIDX_DONE
