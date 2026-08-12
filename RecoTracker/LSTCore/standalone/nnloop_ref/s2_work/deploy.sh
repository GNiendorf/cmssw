#!/bin/bash
# deploy.sh <worktree> <ARMTAG> <head> <barjson> <variant> <runargs...>
# Installs the retrained gate header (affine pin baked in) + the refitted ChainConfig bars into a
# deploy tree, rebuilds CPU-only under the shared build lock, greps the FRESH make log for
# `error:` WITH the colon, then runs whatever gate args follow.
# no `set -u`: setup.sh dereferences unset vars and would abort the shell
G=$1; TAG=$2; HEAD=$3; BAR=$4; VAR=$5; shift 5
O=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
W=$O/nnloop_ref/s2_work
S=$G/src/RecoTracker/LSTCore

cd $O && source setup.sh > /dev/null 2>&1 && eval $(scramv1 runtime -sh) > /dev/null 2>&1 && source setup.sh > /dev/null 2>&1
cd $O

echo "=== $TAG: header from $HEAD, bars $BAR:$VAR into $G"
python3 $W/export3.py --model $W/models/chain3_$HEAD.pt --norm $W/models/chain3_norm_$HEAD.json \
        --barfit $W/$BAR --out $W/hdr_$TAG.h || exit 3
cp $W/hdr_$TAG.h $S/src/alpaka/Chain3NetworkWeights.h
cp $W/base_$(basename $G)_ChainConfig.h $S/interface/ChainConfig.h
python3 $W/setbars.py $S/interface/ChainConfig.h $W/$BAR $VAR || exit 4

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
