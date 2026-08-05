#!/bin/bash
# B01 batch 1: no-op gate + eta-band XCT scan on the frozen 300
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
R="$S/b01_ref/b01_run.sh"
CF="-T3F 0.10 -XC4 1 -RPSA 5.5 -EXR 4.0 -a 6.0"
bash $R G0   $CF                                &
bash $R T30  $CF -XCT2 3.0                      &
bash $R T25  $CF -XCT2 2.5                      &
bash $R T20  $CF -XCT2 2.0                      &
bash $R B35  $CF -XCT 3.5 -XCT2 4.0 -XCT3 4.0   &
bash $R B30  $CF -XCT 3.0 -XCT2 4.0 -XCT3 4.0   &
bash $R B25  $CF -XCT 2.5 -XCT2 4.0 -XCT3 4.0   &
bash $R E50  $CF -XCT3 5.0                      &
bash $R E60  $CF -XCT3 6.0                      &
wait
echo "BATCH1 DONE"
