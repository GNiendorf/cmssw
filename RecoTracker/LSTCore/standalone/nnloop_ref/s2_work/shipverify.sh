#!/bin/bash
# shipverify.sh <TAG> <verify-worktree> [prereq.patch]
# Rebuild the delivered patch from a PRISTINE tree at 4f1846078d9 (+ the arm's prerequisite patch)
# and prove the judge is bit-identical to the measured arm on every field.
O=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
W=$O/nnloop_ref/s2_work
TAG=$1; V=$2; PRE=${3:-}
S=$V/src/RecoTracker/LSTCore/standalone
cd $V/src && git checkout -- RecoTracker/LSTCore 2>/dev/null
if [ -n "$PRE" ]; then git -C $V/src apply "$PRE" || { echo "PREREQ APPLY FAILED"; exit 2; }; fi
git -C $V/src apply $W/s2_$TAG.patch || { echo "PATCH APPLY FAILED"; exit 2; }
cd $S && source setup.sh >/dev/null 2>&1 && eval $(scramv1 runtime -sh) >/dev/null 2>&1 && source setup.sh >/dev/null 2>&1
cd $S
bash /mnt/data1/gsn27/here/gpu_wt/broker/buildlock.sh lst_make_tracklooper -C > $W/logs/verify_build_$TAG.log 2>&1
L=$(ls -t $S/.make.log.* | head -1)
echo "VERIFY BUILD $TAG: $L  error: count = $(grep -c 'error:' $L)"
./bin/lst_cpu -i PU200RelVal -n 1000 -s 8 -p 0.8 -o $W/runs/${TAG}_SHIPVERIFY.root > $W/runs/${TAG}_SHIPVERIFY.log 2>&1
echo "RUN_EXIT=$?" >> $W/runs/${TAG}_SHIPVERIFY.log
python3 $O/d3_ref/pu_judge.py $W/runs/${TAG}_SHIPVERIFY.root --json $W/runs/${TAG}_SHIPVERIFY.json > $W/runs/${TAG}_SHIPVERIFY.judge 2>&1
python3 - "$W/runs/${TAG}.json" "$W/runs/${TAG}_SHIPVERIFY.json" <<'PY'
import json,sys
a=json.load(open(sys.argv[1])); b=json.load(open(sys.argv[2]))
ks=sorted(set(a)|set(b)); bad=[k for k in ks if repr(a.get(k))!=repr(b.get(k))]
print("SHIP-VERIFY: %d/%d judge fields BIT-IDENTICAL at full float precision%s"
      % (len(ks)-len(bad), len(ks), "" if not bad else "  MISMATCH: "+", ".join(bad)))
PY
