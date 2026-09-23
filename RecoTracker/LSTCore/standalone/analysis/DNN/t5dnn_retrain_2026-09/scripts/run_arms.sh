#!/bin/bash
# T5 DNN training arms on the training-build samples. Waits for sample generation, then trains 3 arms at a time.
T=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/displaced_ref/t5dnn
PY=$T/wt_train/RecoTracker/LSTCore/standalone/analysis/DNN/train_T5_DNN.py
pushd /mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone >/dev/null
source setup.sh >/dev/null 2>&1; eval $(scramv1 runtime -sh) 2>/dev/null; source setup.sh >/dev/null 2>&1
until grep -q "GEN DONE" $T/samples/gen.log; do sleep 60; done
S=$T/samples; OUT=$T/train; mkdir -p $OUT; cp $PY $OUT/train_T5_DNN.py.used
COMMON="--lab pu=$S/pu/chunk_*.root --lab jet=$S/jet/chunk_*.root --lab gun=$S/gun/chunk_*.root --share jet=0.25 --share gun=0.02 --flat gun --ref pu --out $OUT"
ALL=base,t3score,mddir,density,topo,dca
ARMS=("full --groups $ALL" "base --groups base" "noT3score --groups base,mddir,density,topo,dca"
      "noMddir --groups base,t3score,density,topo,dca" "noDensity --groups base,t3score,mddir,topo,dca"
      "noTopo --groups base,t3score,mddir,density,dca" "noDca --groups base,t3score,mddir,density,topo"
      "fullNoTier --groups $ALL --no-tier")
DEVS=(cuda:0 cuda:1 cuda:0)
i=0
for arm in "${ARMS[@]}"; do
  tag=${arm%% *}; rest=${arm#* }
  while [ $(jobs -rp | wc -l) -ge 3 ]; do sleep 30; done
  dev=${DEVS[$((i % 3))]}; i=$((i+1))
  echo "$(date '+%F %T') START $tag $dev" >> $OUT/arms.log
  ( /usr/bin/time -v python3 $PY $COMMON --tag $tag --device $dev $rest > $OUT/$tag.log 2>&1
    echo "$(date '+%F %T') END $tag rc=$?" >> $OUT/arms.log ) &
  sleep 5
done
wait
echo "$(date '+%F %T') ARMS DONE" >> $OUT/arms.log
