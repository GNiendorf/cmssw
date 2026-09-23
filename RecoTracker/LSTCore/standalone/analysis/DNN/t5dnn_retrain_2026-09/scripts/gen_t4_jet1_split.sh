#!/bin/bash
# Jet chunk 1 (events i%5==1 of 0-499) stalls in RemoveDupQuadrupletsAfterBuild (quadratic T4 dedup with the T4 DNN
# off in one dense event). Rerun it as its two halves of a 10-way split (i%10 == 1 and 6) into samples_t4/jet10/,
# each with the stall watchdog; a half that still stalls is dropped (one outlier event among 50).
T=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/displaced_ref/t5dnn
until grep -q "GEN DONE" $T/samples_t4/gen.log; do sleep 30; done
source <(sed -n '/^T=/,/^export -f run_chunk/p' $T/gen_t4_samples.sh)
JET=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/jet_ref/trackingNtuple_jets_1000.root
mkdir -p $T/samples_t4/jet10
rm -f $T/samples_t4/jet/chunk_1.root
for k in 1 6; do run_chunk jet10 $JET 500 10 $k -J & done
wait
echo "SPLIT DONE"
