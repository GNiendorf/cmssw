#!/bin/bash
O=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
W=$O/nnloop_ref/s3_work
S=/mnt/data1/gsn27/here/gpu_wt/g7/src/RecoTracker/LSTCore/standalone
cd $S && source setup.sh >/dev/null 2>&1 && eval $(scramv1 runtime -sh) >/dev/null 2>&1 && source setup.sh >/dev/null 2>&1
cd $S
bash /mnt/data1/gsn27/here/gpu_wt/broker/buildlock.sh lst_make_tracklooper -m -C > $W/logs/build_SHIPVERIFY.log 2>&1
L=$(ls -t $S/.make.log.* | head -1)
echo "SHIPVERIFY BUILD: $L  error: count = $(grep -c 'error:' $L)"
md5sum $S/bin/lst_cpu
./bin/lst_cpu -i PU200RelVal -n 1000 -s 8 -p 0.8 -o $W/runs/S3A2_SHIPVERIFY.root > $W/runs/S3A2_SHIPVERIFY.log 2>&1
echo "RUN_EXIT=$?" >> $W/runs/S3A2_SHIPVERIFY.log
python3 $O/d3_ref/pu_judge.py $W/runs/S3A2_SHIPVERIFY.root --json $W/runs/S3A2_SHIPVERIFY.json > $W/runs/S3A2_SHIPVERIFY.judge 2>&1
echo SHIPVERIFY_DONE
