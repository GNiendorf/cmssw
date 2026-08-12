#!/bin/bash
# M3PH3: the JET PHYSICS gate for M2's per-node top-C weld cap, same protocol as M3PH2 so the
# numbers drop straight into the same table. M2's g4 build of 13:11, frozen at m3_ref/frozenM2
# (md5 add74afb4a9fa1212141c16518879305 lst_cpu) so a later M2 rebuild cannot move my arms.
#   arm M2OFF  LST_CHAIN_NODE_TOPC=0   -- M2's "off = today byte for byte". Doubles as an
#              independent physics check of M2's refactor inertness on jets, and as a cross-check
#              against M3PH2's OFF arm (a DIFFERENT binary, M1's, also at cap-off): the two must
#              agree track for track if both patches are really inert with their knob off.
#   arm M2C8   the parked default (M3's offline label study: welded recall .99985 on jets)
#   arm M2C16 / M2C32   where M2 expects the PU200 bit-identity gate to land
# One process per event (M2's binary has no allocation guard, so events 5 and 85 SIGSEGV at C=0
# exactly as HEAD does; at C>0 M2 reports event 5 completes, which this job also tests).
# Physics only: -w 1 -J, no timing claimed.
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
V=$S/m3_ref/frozenM2
M=$S/m3_ref
IN=$S/jet_ref/trackingNtuple_jets_1000.root
cd $V || exit 9
md5sum -c $V/MD5 || { echo "FROZEN M2 BINARY MD5 MISMATCH -- job voided"; exit 8; }
cd $S || exit 9
source setup.sh > /dev/null 2>&1
eval $(scramv1 runtime -sh) 2>/dev/null
source setup.sh > /dev/null 2>&1
export LD_LIBRARY_PATH=$V:$LD_LIBRARY_PATH
export IN V M

run_one() {
  env LST_CHAIN_NODE_TOPC=$3 $V/lst_cpu -i "$IN" -x $1 -n -1 -s 1 -v 2 -w 1 -J \
      -o $M/pe100_$2/evt$1.root > $M/pe100_$2/evt$1.log 2>&1
  echo "$1 $?" >> $M/pe100_$2.rc
}
export -f run_one

for arm in "M2OFF 0" "M2C8 8" "M2C16 16" "M2C32 32"; do
  set -- $arm
  NAME=$1; C=$2
  mkdir -p $M/pe100_$NAME
  : > $M/pe100_$NAME.rc
  echo "=== arm $NAME (LST_CHAIN_NODE_TOPC=$C), 100 events, one process each, 4 at a time ==="
  SECONDS=0
  seq 0 99 | xargs -P 4 -I{} bash -c "run_one {} $NAME $C"
  echo "arm $NAME wall ${SECONDS}s : ok=$(grep -c ' 0$' $M/pe100_$NAME.rc) of 100"
  echo "  nonzero rc: $(grep -v ' 0$' $M/pe100_$NAME.rc | sort -n | tr '\n' ' ')"
  echo "  event 5 rc: $(grep '^5 ' $M/pe100_$NAME.rc), event 85 rc: $(grep '^85 ' $M/pe100_$NAME.rc)"
done
echo M3PH3DONE
