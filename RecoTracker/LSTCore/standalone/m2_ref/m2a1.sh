#!/bin/bash
# M2A1 -- the DECISION slot for the per-node top-C weld cap, PU200 side only.
#   [1] INERTNESS : my binary at C=0 vs the frozen HEAD binary (m1_ref/frozenbase), CPU 100 evt.
#                   This is what proves the K2 windowing refactor and the five new kernels moved
#                   nothing when the cap is off.
#   [2] R on PU200: the argmax-rank census, cap OFF -- the number that decides the safe C.
#   [3] THE GATE  : C = 4 / 8 / 16 / 32 / 64 vs C = 0, ntuples for an offline branch diff.
# Only lst_cpu runs here; every branch comparison is offline python on the written files.
# 100 events, per [COORDINATOR 12:50] rule 1.
# NO `set -u`: setup.sh trips on unbound variables and would abort the script with rc=1 and no output.
S=/mnt/data1/gsn27/here/gpu_wt/g4/src/RecoTracker/LSTCore/standalone
H=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
R=$H/m2_ref
cd $S || exit 9
source setup.sh > /dev/null 2>&1
eval $(scramv1 runtime -sh) > /dev/null 2>&1
source setup.sh > /dev/null 2>&1
source $H/m2_ref/freshcheck.sh || exit 7
CPU=$S/bin/lst_cpu
B=$H/m1_ref/frozenbase

echo "=== binaries under test ==="
md5sum $CPU $S/LST/liblst_cpu.so $B/lst_cpu $B/liblst_cpu.so
# The frozen copy of MY binary: every later M2 job re-checks this md5, so a rebuild cannot
# silently change which binary a number came from.
mkdir -p $R/frozenC
cp -f $CPU $S/LST/liblst_cpu.so $R/frozenC/ 2>/dev/null
(cd $R/frozenC && md5sum lst_cpu lst_cuda liblst_cpu.so liblst_cuda.so > MD5)  # basenames only: `md5sum frozenC/*` also hashed MD5 itself and broke the exact-name lookup
cat $R/frozenC/MD5

echo; echo "############ [1] INERTNESS: mine C=0 vs frozen HEAD, PU200 100 evt s=1 ############"
rm -f $R/pu_C0.root $R/pu_HEAD.root
LST_CHAIN_NODE_TOPC=0 $CPU -i PU200 -n 100 -s 1 -v 1 -w 1 -o $R/pu_C0.root > $R/pu_C0.log 2>&1
echo "  mine C=0  rc=$? size=$(stat -c%s $R/pu_C0.root 2>/dev/null)"
LD_LIBRARY_PATH=$B:${LD_LIBRARY_PATH:-} $B/lst_cpu -i PU200 -n 100 -s 1 -v 1 -w 1 -o $R/pu_HEAD.root > $R/pu_HEAD.log 2>&1
echo "  HEAD      rc=$? size=$(stat -c%s $R/pu_HEAD.root 2>/dev/null)"

echo; echo "############ [2] R on PU200: rank census, cap OFF, 100 evt ############"
LST_CHAIN_RANK_CENSUS=1 LST_CHAIN_NODE_TOPC=0 $CPU -i PU200 -n 100 -s 1 -v 0 -w 0 > $R/rank_pu200.log 2>&1
echo "  rc=$?  [CHAIN RANK] lines: $(grep -c 'CHAIN RANK' $R/rank_pu200.log)"

echo; echo "############ [3] THE GATE: ntuples at C = 4/8/16/32/64, PU200 100 evt s=1 ############"
for C in 4 8 16 32 64; do
  rm -f $R/pu_C$C.root
  LST_CHAIN_NODE_TOPC=$C $CPU -i PU200 -n 100 -s 1 -v 2 -w 1 -o $R/pu_C$C.root > $R/pu_C$C.log 2>&1
  echo "  C=$C rc=$? size=$(stat -c%s $R/pu_C$C.root 2>/dev/null)"
  grep -m1 'CHAIN TOPC' $R/pu_C$C.log
done

echo; echo "=== timing tails (context only, this job is not a timing arm) ==="
for f in pu_C0 pu_C8 pu_C32; do grep -E '^\s+avg' $R/$f.log | tail -1; done
echo "DONE M2A1"
