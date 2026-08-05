S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
B=$S/a04_ref/chainproto_base
BASE="-XC 3 -XCT 4 -T3E 1 -CC 1 -CCN 1 -CCR 2"
BIN=$B bash $S/a04_ref/a04_run.sh I_A40   $BASE -a 4 &
BIN=$B bash $S/a04_ref/a04_run.sh I_A6X35 $BASE -a 6 -XCT 3.5 &
BIN=$B bash $S/a04_ref/a04_run.sh I_A5X35 $BASE -a 5 -XCT 3.5 &
BIN=$B bash $S/a04_ref/a04_run.sh I_A6T7  $BASE -a 6 -AT3 7 &
BIN=$B bash $S/a04_ref/a04_run.sh I_A6T65 $BASE -a 6 -AT3 6.5 &
wait
echo BATCH6_DONE
