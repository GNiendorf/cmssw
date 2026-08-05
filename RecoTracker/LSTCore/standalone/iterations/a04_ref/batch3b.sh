S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
B=$S/a04_ref/chainproto_base
BASE="-XC 3 -XCT 4 -T3E 1 -CC 1 -CCN 1 -CCR 2"
BIN=$B bash $S/a04_ref/a04_run.sh E_ANOA $BASE -a 1e9 &
BIN=$B bash $S/a04_ref/a04_run.sh E_A5   $BASE -a 5 &
wait
echo BATCH3B_DONE
