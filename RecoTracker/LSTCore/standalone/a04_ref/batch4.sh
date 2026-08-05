S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
B=$S/a04_ref/chainproto_base
BASE="-XC 3 -XCT 4 -T3E 1 -CC 1 -CCN 1 -CCR 2"
BIN=$B bash $S/a04_ref/a04_run.sh F_N2     $BASE -CCN 2 &
BIN=$B bash $S/a04_ref/a04_run.sh F_N2X3   $BASE -CCN 2 -XCT 3 &
BIN=$B bash $S/a04_ref/a04_run.sh F_N2X2   $BASE -CCN 2 -XCT 2 &
BIN=$B bash $S/a04_ref/a04_run.sh F_AT35   $BASE -AT3 5 &
BIN=$B bash $S/a04_ref/a04_run.sh F_AT355  $BASE -AT3 5.5 &
BIN=$B bash $S/a04_ref/a04_run.sh F_MRIL   $BASE -MRI -1.5 &
wait
echo BATCH4_DONE
