#!/bin/bash
# B01 batch 2: new-code no-op gate + -RPSA band scan + finer XCT band points + first combo
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
R="$S/b01_ref/b01_run.sh"
CF="-T3F 0.10 -XC4 1 -RPSA 5.5 -EXR 4.0 -a 6.0"
bash $R G1    $CF                                          &
bash $R T35   $CF -XCT2 3.5                                &
bash $R B375  $CF -XCT 3.75 -XCT2 4.0 -XCT3 4.0            &
bash $R B20   $CF -XCT 2.0  -XCT2 4.0 -XCT3 4.0            &
bash $R R50   $CF -RPSA 5.0 -RPSA2 5.5 -RPSA3 5.5          &
bash $R R45   $CF -RPSA 4.5 -RPSA2 5.5 -RPSA3 5.5          &
bash $R R40   $CF -RPSA 4.0 -RPSA2 5.5 -RPSA3 5.5          &
bash $R RT50  $CF -RPSA2 5.0                               &
bash $R RT45  $CF -RPSA2 4.5                               &
bash $R C1    $CF -XCT 3.5 -XCT2 3.0 -XCT3 4.0             &
wait
echo "BATCH2 DONE"
