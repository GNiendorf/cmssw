#!/bin/bash
# VOID_1000_EVENT -- kept as the record of what was submitted before the [COORDINATOR 12:50]
# restart, NOT to be re-run: it is a 1000-event jet job and the rule is now 100-event max.
# The compliant replacements are m1jc1.sh (CPU isolated), m1jc2.sh (CPU batch), m1jg1.sh (GPU).
echo "VOID: 1000-event job, superseded by m1jc1.sh / m1jc2.sh / m1jg1.sh (100-event rule)"; exit 3
# M1J1 -- THE JET GATE, GPU side, plus the two cheap censuses.
#   [1] freeze proof.
#   [2] GPU 1000 jet events, ONE process, cap OFF then cap 256, both arms in this slot. Before this
#       patch NEITHER arm existed: 90 of 1000 events are over the 1 GiB device allocator bin and the
#       first of them ABORTS THE JOB, so there is no such thing as a 1000-event jet GPU run today.
#       Peak device memory is sampled from nvidia-smi throughout each arm.
#   [3] CPU 10-event jet file, -v 2, cap 256: the E vs Euncapped census per event (what the cap
#       actually removed on jets, measured in the binary rather than from an offline dump), plus the
#       [CHAIN OVERFLOW] lines, plus [MEM].
#   [4] the same 10 events with the cap OFF: the guard's census on the event JR's report dies on
#       (event 5, 221 M edges, 4,648 MB -> a truncated 353 MB buffer -> SIGSEGV). It must now say so
#       and keep going.
M=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
R=$M/m1_ref
B=$R/frozenbase
V=$R/frozenvar
G=/mnt/data1/gsn27/here/gpu_wt/g2/src/RecoTracker/LSTCore/standalone
J1000=$M/jet_ref/trackingNtuple_jets_1000.root
J10=$M/jet_ref/trackingNtuple_jets_10.root
cd $G || exit 9
source setup.sh > /dev/null 2>&1
eval $(scramv1 runtime -sh) > /dev/null 2>&1
source setup.sh > /dev/null 2>&1

echo "=== [1] FROZEN BINARY PROOF ==="
for D in $B $V; do ( cd $D && md5sum -c MD5 ) || { echo "*** MD5 MISMATCH in $D -- VOID ***"; exit 1; }; done

smon () {  # sample GPU 0 used memory until the sentinel file disappears
  local out=$1
  while [ -f $out.on ]; do
    nvidia-smi --query-gpu=index,memory.used --format=csv,noheader,nounits | head -1 >> $out
    sleep 0.2
  done
}

gpuarm () {  # gpuarm <label> <cap>
  local lbl=$1 cap=$2
  rm -f $R/j1_gpu_$lbl.smi; touch $R/j1_gpu_$lbl.smi.on
  smon $R/j1_gpu_$lbl.smi &
  local mpid=$!
  /usr/bin/time -v env LST_CHAIN_DEG_CAP=$cap LD_LIBRARY_PATH=$V:$LD_LIBRARY_PATH \
      $V/lst_cuda -i $J1000 -n 1000 -s 1 -v 1 -w 0 > $R/j1_gpu_$lbl.log 2> $R/j1_gpu_$lbl.err
  echo "  arm $lbl (cap=$cap) rc=$?"
  rm -f $R/j1_gpu_$lbl.smi.on; wait $mpid 2>/dev/null
  echo -n "    avg line: "; grep -E '^\s+avg' $R/j1_gpu_$lbl.log | tail -1
  echo    "    events with a per-event timing row: $(grep -cE '^ +[0-9]+ +[0-9]' $R/j1_gpu_$lbl.log)"
  echo    "    [CHAIN OVERFLOW] skips: $(grep -c 'CHAIN OVERFLOW' $R/j1_gpu_$lbl.log $R/j1_gpu_$lbl.err | awk -F: '{s+=$2} END {print s}')"
  echo    "    peak device MiB (nvidia-smi, GPU 0): $(sort -t, -k2 -n $R/j1_gpu_$lbl.smi 2>/dev/null | tail -1)"
  echo    "    peak host RSS kB: $(grep 'Maximum resident' $R/j1_gpu_$lbl.err | tr -dc '0-9')"
}

echo
echo "=== [2] GPU, 1000 JET EVENTS, ONE PROCESS, s=1. Cap OFF is the BASELINE arm (guard only). ==="
gpuarm OFF 0
gpuarm C256 256
gpuarm OFF2 0
echo "  (OFF2 repeats the first arm at the end of the slot: the in-slot repeatability of these"
echo "   totals, without which the OFF -> C256 delta has no floor.)"

echo
echo "=== [3] CPU 10-evt jet file, -v 2, CAP 256: per-event E vs Euncapped ==="
env LST_CHAIN_DEG_CAP=256 LD_LIBRARY_PATH=$V:$LD_LIBRARY_PATH $V/lst_cpu -i $J10 -n 10 -s 1 -v 2 -w 0 \
    > $R/j1_cpu10_c256.log 2>&1
echo "  rc=$?"
grep -E "^\[CHAIN\] (MD/E1|LS/E2|nodes)|^\[MEM\] ChainEdges|CHAIN OVERFLOW" $R/j1_cpu10_c256.log

echo
echo "=== [4] the same 10 events with the CAP OFF -- the guard on JR's crashing event 5 ==="
env LST_CHAIN_DEG_CAP=0 LD_LIBRARY_PATH=$V:$LD_LIBRARY_PATH $V/lst_cpu -i $J10 -n 10 -s 1 -v 2 -w 0 \
    > $R/j1_cpu10_off.log 2>&1
echo "  rc=$? (139 = the SIGSEGV this patch exists to remove)"
grep -E "^\[CHAIN\] nodes|^\[MEM\] ChainEdges|CHAIN OVERFLOW" $R/j1_cpu10_off.log
echo "  per-event timing rows: $(grep -cE '^ +[0-9]+ +[0-9]' $R/j1_cpu10_off.log)"
echo DONE M1J1
