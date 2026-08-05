S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
OLD=$S/a04_ref/chainproto_base
NEW=$S/protoA04/bin/chainproto
BASE="-XC 3 -XCT 4 -T3E 1 -CC 1 -CCN 1 -CCR 2"
NEV=30 BIN=$OLD bash $S/a04_ref/a04_run.sh G0_30 $BASE &
NEV=30 BIN=$NEW bash $S/a04_ref/a04_run.sh G2_30 $BASE &
NEV=30 BIN=$NEW bash $S/a04_ref/a04_run.sh Q_R015 $BASE -PWR 0.15 &
NEV=30 BIN=$NEW bash $S/a04_ref/a04_run.sh Q_R030 $BASE -PWR 0.30 &
wait
echo BATCH2_DONE
