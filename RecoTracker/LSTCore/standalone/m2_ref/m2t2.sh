#!/bin/bash
# M2T2 -- jets GPU timing + device memory, first 100 events, per-event isolated.
# Two arms, same binary, env-toggled:
#   C=0   : today's behaviour. 12 of these 100 events are over the 1 GiB caching-allocator bin
#           (M1's base100.csv: 5,25,28,42,64,71,74,81,85,91,92,97), so this arm is also the
#           GPU crash census.
#   C=<A> : the cap. Expect every event to run and the peak device allocation to be bounded by
#           tile + 2*C*nNodes + kept rows.
# nvidia-smi is sampled once per event AFTER the run (peak comes from the [MEM] lines, which are
# exact allocations rather than a sampled residency).
# NO `set -u`: setup.sh trips on unbound variables and would abort the script with rc=1 and no output.
S=/mnt/data1/gsn27/here/gpu_wt/g4/src/RecoTracker/LSTCore/standalone
H=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
R=$H/m2_ref
J=$H/jet_ref/trackingNtuple_jets_1000.root
cd $S || exit 9
source setup.sh > /dev/null 2>&1
eval $(scramv1 runtime -sh) > /dev/null 2>&1
source setup.sh > /dev/null 2>&1
source $H/m2_ref/freshcheck.sh || exit 7
GPU=$S/bin/lst_cuda
md5sum $GPU $S/LST/liblst_cuda.so
if [ -f $R/frozenC/MD5 ]; then
  A=$(md5sum $GPU | cut -d' ' -f1); B=$(awk '$2=="lst_cuda"{print $1}' $R/frozenC/MD5)
  [ -z "$B" ] || [ "$A" = "$B" ] || { echo "VOID: binary changed since the frozen copy"; exit 8; }
fi
# TIMING RUNS AT `-v 1`, DELIBERATELY. At `-v 2` ChainTopCInsert adds one global atomicAdd per
# eligible row for the eligible-population counter; on a GPU that is 200 M threads serializing on one
# address, which would be a measurement of my instrument and not of the cap. The memory numbers come
# from a separate short `-v 2` pass over the 12 heaviest events, where they are exact allocation
# sizes rather than timings.
nvidia-smi --query-gpu=index,name,memory.used --format=csv,noheader
BIG="5 25 28 42 64 71 74 81 85 91 92 97"
for C in ${ARMS:-0 8}; do
  O=$R/gpeC$C; mkdir -p $O; : > $O/rc.txt
  echo "=== GPU arm C=$C, -v 1 : 100 events, one process each -> $O ==="
  for i in $(seq 0 99); do
    LST_CHAIN_NODE_TOPC=$C /usr/bin/time -v $GPU -i $J -x $i -n -1 -s 1 -v 1 -w 0 \
        > $O/evt$i.log 2> $O/evt$i.err
    echo "$i $?" >> $O/rc.txt
  done
  echo "  rc census: $(awk '{print $2}' $O/rc.txt | sort | uniq -c | tr '\n' ' ')"
  echo "  events that threw:"; awk '$2!=0{printf " %s(rc=%s)",$1,$2}' $O/rc.txt; echo
  O2=$R/gpeC${C}_v2; mkdir -p $O2; : > $O2/rc.txt
  echo "=== GPU arm C=$C, -v 2 (memory census only) : the 12 heaviest events ==="
  for i in $BIG; do
    LST_CHAIN_NODE_TOPC=$C /usr/bin/time -v $GPU -i $J -x $i -n -1 -s 1 -v 2 -w 0 \
        > $O2/evt$i.log 2> $O2/evt$i.err
    echo "$i $?" >> $O2/rc.txt
  done
  echo "  rc census: $(awk '{print $2}' $O2/rc.txt | sort | uniq -c | tr '\n' ' ')"
  grep -h -E '\[MEM\] (Total|ChainEdges|ChainTopC)' $O2/evt*.log | sort | uniq -c | sort -rn | head -8
done
echo "DONE M2T2"
