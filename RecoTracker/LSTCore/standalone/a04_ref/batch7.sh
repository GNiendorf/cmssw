S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
B=$S/a04_ref/chainproto_base
BASE="-XC 3 -XCT 4 -T3E 1 -CC 1 -CCN 1 -CCR 2"
BIN=$B bash $S/a04_ref/a04_run.sh J_A6X5   $BASE -a 6 -XCT 5 &
BIN=$B bash $S/a04_ref/a04_run.sh J_A55X45 $BASE -a 5.5 -XCT 4.5 &
BIN=$B bash $S/a04_ref/a04_run.sh J_A625X4 $BASE -a 6.5 &
wait
echo BATCH7_DONE
