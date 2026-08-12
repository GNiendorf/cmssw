#!/bin/bash
# deploy_s3.sh <worktree> <TAG> <model.pt> <bars.json> <runargs...>
# Installs the retrained 22-input attach head + the refitted attach bars into a deploy tree that
# ALREADY carries the S3 deletion (base + armG + GM12F + the S3 C++ edits), rebuilds CPU-only under
# the shared build lock, greps the FRESH make log for `error:` WITH the colon, then runs the gate.
# no `set -u`: setup.sh dereferences unset vars and would abort the shell
G=$1; TAG=$2; MODEL=$3; BARS=$4; shift 4
O=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
W=$O/nnloop_ref/s3_work
S=$G/src/RecoTracker/LSTCore

cd $O && source setup.sh > /dev/null 2>&1 && eval $(scramv1 runtime -sh) > /dev/null 2>&1 && source setup.sh > /dev/null 2>&1
cd $O
echo "=== $TAG: head $MODEL, bars $BARS into $G"
python3 $O/b1_ref/port_hdr.py --model $MODEL --norm ${MODEL%.pt}_norm.json \
        --out $W/hdr_$TAG.h > $W/logs/port_$TAG.log 2>&1 || { tail -5 $W/logs/port_$TAG.log; exit 3; }
grep -q "PORT VERIFIED" $W/logs/port_$TAG.log || { echo "PORT NOT VERIFIED"; exit 3; }
cp $W/hdr_$TAG.h $S/src/alpaka/AttachNetworkWeights.h
python3 $W/setbars_s3.py $S/interface/ChainConfig.h --restore > /dev/null || exit 4
python3 $W/setbars_s3.py $S/interface/ChainConfig.h $W/$BARS || exit 4

cd $S/standalone && source setup.sh > /dev/null 2>&1 && eval $(scramv1 runtime -sh) > /dev/null 2>&1 && source setup.sh > /dev/null 2>&1
cd $S/standalone
bash /mnt/data1/gsn27/here/gpu_wt/broker/buildlock.sh lst_make_tracklooper -C > $W/logs/build_$TAG.log 2>&1
L=$(ls -t $S/standalone/.make.log.* | head -1)
NE=$(grep -c 'error:' "$L")
echo "$TAG BUILD: $L  error: count = $NE"
if [ "$NE" != "0" ]; then grep 'error:' "$L" | head -20; exit 5; fi
md5sum $S/standalone/bin/lst_cpu

R=$W/runs
mkdir -p $R
./bin/lst_cpu "$@" -o $R/$TAG.root > $R/$TAG.log 2>&1
echo "RUN_EXIT=$?" >> $R/$TAG.log
python3 $O/d3_ref/pu_judge.py $R/$TAG.root --json $R/$TAG.json > $R/$TAG.judge 2>&1
echo "$TAG DONE"
grep -E "eff_overall_incut|eff_dxy_10_30|dup_overall|fake_overall|n_tc " $R/$TAG.judge | head -6
