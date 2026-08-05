S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
B=$S/a04_ref/chainproto_base
BASE="-XC 3 -XCT 4 -T3E 1 -CC 1 -CCN 1 -CCR 2"
BIN=$B bash $S/a04_ref/a04_run.sh D_MAX   $BASE -XC 0 -RPS 0 -CC 0 -AT3 3 &
BIN=$B bash $S/a04_ref/a04_run.sh D_XC0   $BASE -XC 0 &
BIN=$B bash $S/a04_ref/a04_run.sh D_CC0   $BASE -CC 0 &
BIN=$B bash $S/a04_ref/a04_run.sh D_RPS0  $BASE -RPS 0 &
BIN=$B bash $S/a04_ref/a04_run.sh D_AT34  $BASE -AT3 4 &
BIN=$B bash $S/a04_ref/a04_run.sh D_XCT6  $BASE -XCT 6 &
wait
echo BATCH1_DONE
