#!/bin/bash
# M1JC2 -- THE JET GATE, CPU, BATCH MODE: `-n 100 -s 1`, ONE process for all 100 events.
#
# This run DOES NOT EXIST at 81a9afe2d00 in any form. Event 5 is inside the first ten and its
# 4,648 MB edge request has its byte extent truncated modulo 2^32 to 353 MB, so ChainBuildEdges
# writes 4 GiB past the end of the buffer and the process dies -- taking the other 99 events with
# it. So there is no batch baseline to divide by here; the baseline IS the per-event-isolated table
# (job M1JC1), and this job's job is to establish (a) that the batch run completes at all, (b) the
# peak RSS of a real batch run, which per-event isolation cannot measure because each process only
# ever holds one event, and (c) the isolation-mode bridge factor, by running the same 100 events in
# batch at the same cap as M1JC1's arms.
#
# Three caps in one binary via the env override, so no binary-to-binary layout band separates them:
#   0 (off, the guard alone) -> 256 (ship) -> 512 (the largest cap that provably never bites PU200).
# Ordered so that the two arms that matter land first if the slot is cut short.
# NO `set -u`: standalone/setup.sh legitimately reads unset variables, and with nounset a
# sourced setup.sh aborts the whole job with status 1 and its message swallowed by the
# redirect -- which is exactly how the first submission of this script died, silently.
M=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
R=$M/m1_ref
V=$R/frozenvar
G=/mnt/data1/gsn27/here/gpu_wt/g2/src/RecoTracker/LSTCore/standalone
J=$M/jet_ref/trackingNtuple_jets_1000.root
cd $G || exit 9
source setup.sh > /dev/null 2>&1
eval $(scramv1 runtime -sh) > /dev/null 2>&1
source setup.sh > /dev/null 2>&1

echo "=== FROZEN BINARY PROOF (void on mismatch) ==="
( cd $V && md5sum -c MD5 ) || { echo "*** MD5 MISMATCH -- VOID ***"; exit 1; }

arm () {   # arm <label> <cap>
  local lbl=$1 cap=$2
  local t0=$SECONDS
  /usr/bin/time -v env LST_CHAIN_DEG_CAP=$cap LD_LIBRARY_PATH=$V:$LD_LIBRARY_PATH \
      $V/lst_cpu -i $J -n 100 -s 1 -v 1 -w 0 > $R/jc2_$lbl.log 2> $R/jc2_$lbl.err
  echo "  arm $lbl (cap=$cap) rc=$? wall $((SECONDS-t0)) s"
  echo -n "    avg line: "; grep -E '^\s+avg' $R/jc2_$lbl.log | tail -1
  echo    "    per-event timing rows: $(grep -cE '^ +[0-9]+ +[0-9]' $R/jc2_$lbl.log)"
  echo    "    [CHAIN OVERFLOW] skips: $(grep -h -c 'CHAIN OVERFLOW' $R/jc2_$lbl.log $R/jc2_$lbl.err | awk '{s+=$1} END {print s}')"
  grep -h 'CHAIN OVERFLOW' $R/jc2_$lbl.log $R/jc2_$lbl.err | sed 's/^/      /' | head -20
  echo    "    peak RSS: $(( $(grep 'Maximum resident' $R/jc2_$lbl.err | tr -dc '0-9') / 1024 )) MB"
  grep -E 'Elapsed \(wall clock\)' $R/jc2_$lbl.err | sed 's/^/    /'
}

echo
echo "=== BATCH, 100 events, one process, s=1 ==="
arm off 0
arm c256 256
arm c512 512

echo
echo "=== per-event distributions and the paired off -> c256 comparison ==="
python3 $R/perev.py $R/jc2_off.log $R/jc2_c256.log
echo
python3 $R/perev.py $R/jc2_c512.log
echo DONE M1JC2
