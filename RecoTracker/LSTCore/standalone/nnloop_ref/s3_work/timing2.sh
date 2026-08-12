#!/bin/bash
O=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
W=$O/nnloop_ref/s3_work
for pair in "GM12F g6" "S3A2 g7" "GM12Fb g6" "S3A2b g7"; do
  set -- $pair
  S=/mnt/data1/gsn27/here/gpu_wt/$2/src/RecoTracker/LSTCore/standalone
  cd $S && source setup.sh >/dev/null 2>&1 && eval $(scramv1 runtime -sh) >/dev/null 2>&1 && source setup.sh >/dev/null 2>&1
  cd $S
  ./bin/lst_cpu -i PU200RelVal -n 200 -s 1 -v 1 -w 0 > $W/logs/t2_$1.log 2>&1
  echo "TAG=$1 tree=$2 md5=$(md5sum bin/lst_cpu|cut -c1-12) exit=$?"
done
echo T2DONE
