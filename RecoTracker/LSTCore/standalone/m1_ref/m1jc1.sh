#!/bin/bash
# M1JC1 -- THE JET GATE, CPU, IN THE BASELINE'S OWN MEASUREMENT MODE.
#
# The [COORDINATOR 12:50] baseline is the FIRST 100 jet events, PER-EVENT ISOLATED, at 81a9afe2d00:
# 98/100 completed (events 5 and 85 SIGSEGV), Total 3621 mean / 1333 median / 8198 p90 / 33630 max,
# Graph 3538. That table is reproduced from the raw pe1000 logs by m1_ref/base100.py, so this job
# does not have to re-run the baseline -- it only has to run the two VARIANT arms in the SAME mode,
# one process per event, so the delta is not confounded by the isolation-mode change.
#
#   arm OFF  = the patch with the cap disabled: the guard ALONE. Isolates what P0 buys (events 5
#              and 85 stop corrupting memory and are skipped with a census) from what P2 buys.
#   arm C256 = the shipping default.
#
# Per-event isolation costs a cold caching allocator (JR: ~+7% on single-event totals), which is
# exactly why both arms here are isolated too: the comparison is like-for-like with the baseline.
# The batch-mode numbers -- the thing the guard newly makes possible -- are the separate job M1JC2.
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
  local lbl=$1 cap=$2 d=$R/pe100_$1
  rm -rf $d; mkdir -p $d
  local t0=$SECONDS ncrash=0 nskip=0 maxrss=0
  for i in $(seq 0 99); do
    /usr/bin/time -f 'RSSKB %M' \
      env LST_CHAIN_DEG_CAP=$cap LD_LIBRARY_PATH=$V:$LD_LIBRARY_PATH \
      $V/lst_cpu -i $J -x $i -n -1 -s 1 -v 2 -w 0 > $d/evt$i.log 2> $d/evt$i.err
    local rc=$?
    [ $rc -ne 0 ] && { ncrash=$((ncrash+1)); echo "  *** evt $i rc=$rc"; }
    grep -q 'CHAIN OVERFLOW' $d/evt$i.log $d/evt$i.err && nskip=$((nskip+1))
    local r=$(grep -h RSSKB $d/evt$i.err | awk '{print $2}' | sort -n | tail -1)
    [ -n "$r" ] && [ "$r" -gt "$maxrss" ] && maxrss=$r
  done
  echo "  arm $lbl (cap=$cap): wall $((SECONDS-t0)) s, NONZERO EXIT: $ncrash / 100, [CHAIN OVERFLOW] skips: $nskip, peak RSS $((maxrss/1024)) MB"
  grep -h 'CHAIN OVERFLOW' $d/evt*.log $d/evt*.err | sed 's/^/    /' | head -20
}

echo
echo "=== ARM OFF: the GUARD ALONE (LST_CHAIN_DEG_CAP=0), 100 isolated events ==="
arm off 0
echo
echo "=== ARM C256: the shipping default, 100 isolated events ==="
arm c256 256

echo
echo "=== AGGREGATE vs the OURS-100 baseline (offline, same 100 events, same isolation mode) ==="
python3 $R/pe100.py $R/pe100_off $R/pe100_c256
echo DONE M1JC1
