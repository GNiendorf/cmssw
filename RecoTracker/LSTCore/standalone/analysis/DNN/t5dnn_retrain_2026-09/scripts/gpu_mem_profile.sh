#!/bin/bash
# GPU memory time series of one lst_cuda run: a continuous nvidia-smi logger (-lms 5, ~5 ms per sample) records the
# device's used memory while the job runs. Device-level memory == this job's only on an otherwise idle GPU, so the
# script refuses to start unless the GPU shows 0 MiB used. Never use a memory-profiled run for timing.
# usage: gpu_mem_profile.sh <arm (bin/<arm>/lst_cuda)> <input: e3000|jets|path> <nevents> [streams=4] [gpu=0]
# out: deploy/gpumem/<arm>_<ctx>.csv (t_s,used_MiB) + .log; plot with gpu_mem_plot.py
ARM=$1; CTX=$2; N=${3:-100}; ST=${4:-4}; GPU=${5:-0}
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
T=$S/displaced_ref/t5dnn; O=$T/deploy/gpumem; mkdir -p $O
case "$CTX" in
  e[0-9]*) IN=/data2/segmentlinking/CMSSW_12_5_0_pre3/RelValTTbar_14TeV_CMSSW_12_5_0_pre3/event_${CTX#e}.root; EX="" ;;
  jets) IN=$S/p3_ref/jets_hold500.root; EX="-J" ;;
  *) IN=$CTX; EX="" ;;
esac
# wait (up to 60 s) until the GPU has been at 0 MiB for 3 consecutive checks, so a previous job's release is not logged
ok=0; for i in $(seq 1 120); do
  USED=$(nvidia-smi -i $GPU --query-gpu=memory.used --format=csv,noheader,nounits)
  if [ "$USED" -eq 0 ]; then ok=$((ok+1)); [ $ok -ge 3 ] && break; else ok=0; fi; sleep 0.5
done
[ $ok -ge 3 ] || { echo "GPU $GPU not idle ($USED MiB used): refusing"; exit 3; }
export SCRAM_ARCH=el9_amd64_gcc13
pushd $S >/dev/null; source setup.sh >/dev/null 2>&1; eval $(scramv1 runtime -sh 2>/dev/null); source setup.sh >/dev/null 2>&1; popd >/dev/null
export LD_LIBRARY_PATH=$T/bin/$ARM:$LD_LIBRARY_PATH CUDA_VISIBLE_DEVICES=$GPU
TAG=${ARM}_$(basename $CTX .root)
nvidia-smi -i $GPU --query-gpu=timestamp,memory.used --format=csv,noheader,nounits -lms 5 > $O/$TAG.raw &
LOGGER=$!
sleep 0.2
$T/bin/$ARM/lst_cuda -i $IN $EX -n $N -p 0.8 -s $ST -v 1 -w 0 > $O/$TAG.log 2>&1
RC=$?
sleep 0.2; kill $LOGGER; wait $LOGGER 2>/dev/null
python3 - "$O/$TAG.raw" "$O/$TAG.csv" <<'EOF'
import sys, datetime
rows = []
for l in open(sys.argv[1]):
    p = l.strip().split(", ")
    if len(p) != 2 or not p[1].isdigit():
        continue
    rows.append((datetime.datetime.strptime(p[0], "%Y/%m/%d %H:%M:%S.%f"), int(p[1])))
t0 = rows[0][0]
with open(sys.argv[2], "w") as f:
    f.write("t_s,used_MiB\n")
    for t, m in rows:
        f.write(f"{(t - t0).total_seconds():.3f},{m}\n")
print(f"samples={len(rows)} peak_MiB={max(m for _, m in rows)}")
EOF
echo "rc=$RC" | tee -a $O/$TAG.log
rm -f $O/$TAG.raw
