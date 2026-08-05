S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
B=$S/a04_ref/chainproto_base
BASE="-XC 3 -XCT 4 -T3E 1 -CC 1 -CCN 1 -CCR 2"
BIN=$B bash $S/a04_ref/a04_run.sh H_A55  $BASE -a 5.5 &
BIN=$B bash $S/a04_ref/a04_run.sh H_A625 $BASE -a 6.25 &
BIN=$B bash $S/a04_ref/a04_run.sh H_A45  $BASE -a 4.5 &
BIN=$B bash $S/a04_ref/a04_run.sh H_A6X45 $BASE -a 6 -XCT 4.5 &
wait
echo BATCH5_DONE
