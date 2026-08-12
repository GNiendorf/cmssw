#!/bin/bash
# M3 (a)+(b): JET PHYSICS of OUR pipeline on the first 100 jet events, three cap arms out of ONE
# binary (M1's frozen guard+cap build), so there is no binary-to-binary layout band between arms.
# Physics only: -w 1 -J, no timing quoted from this job. 100-EVENT MAX rule respected.
#   arm OFF  : LST_CHAIN_DEG_CAP=0    (cap off = today's enumeration; events 5 and 85 are SKIPPED
#              by the guard instead of SIGSEGV, so they carry no chain candidates)
#   arm C256 : LST_CHAIN_DEG_CAP=256  (M1's ship default)
#   arm C512 : LST_CHAIN_DEG_CAP=512  (the provably PU200-safe cap)
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
V=$S/m1_ref/frozenvar
M=$S/m3_ref
IN=$S/jet_ref/trackingNtuple_jets_1000.root
mkdir -p $M/ph1
cd $V || exit 9
md5sum -c $V/MD5 || { echo "FROZEN BINARY MD5 MISMATCH -- job voided"; exit 8; }
cd $S || exit 9
source setup.sh > /dev/null 2>&1
eval $(scramv1 runtime -sh) 2>/dev/null
source setup.sh > /dev/null 2>&1
export LD_LIBRARY_PATH=$V:$LD_LIBRARY_PATH

run_arm() {
  NAME=$1; CAP=$2
  echo "=== arm $NAME : LST_CHAIN_DEG_CAP=$CAP , 100 jet events, -s 4 ==="
  /usr/bin/time -v env LST_CHAIN_DEG_CAP=$CAP $V/lst_cpu -i "$IN" -n 100 -s 4 -v 2 -w 1 -J \
      -o $M/ph1/jets100_$NAME.root > $M/ph1/jets100_$NAME.log 2> $M/ph1/jets100_$NAME.err
  RC=$?
  echo "arm $NAME rc=$RC"
  echo "  overflow-skipped events: $(grep -c 'CHAIN OVERFLOW' $M/ph1/jets100_$NAME.log $M/ph1/jets100_$NAME.err | paste -sd, )"
  grep -h 'CHAIN OVERFLOW' $M/ph1/jets100_$NAME.log $M/ph1/jets100_$NAME.err | head -8
  echo "  peak RSS: $(grep 'Maximum resident' $M/ph1/jets100_$NAME.err)"
  echo "  wall: $(grep 'Elapsed (wall' $M/ph1/jets100_$NAME.err)"
  ls -la $M/ph1/jets100_$NAME.root
}

run_arm C256 256
run_arm C512 512
run_arm OFF  0

echo "=== [CHAIN] E / Euncapped census, per arm (first 6 lines each) ==="
for a in OFF C256 C512; do echo "-- $a"; grep -h 'Euncapped' $M/ph1/jets100_$a.log | head -6; done
echo M3PH1DONE
