#!/bin/bash
# M3PH2: the jet physics arms in the BASELINE'S OWN MODE -- one process per event.
# Why: the cap-OFF arm of M3PH1 (batch, -s 4) SIGSEGVed after 20 of 100 events even WITH M1's
# guard (the guard did skip event 5 correctly and printed its census, then the process died anyway),
# so a batch cap-off arm cannot be the uncapped physics reference. Per-event isolation is the mode
# the [COORDINATOR 12:50] baseline itself used and is known to complete 98/100 at cap-off.
# All three arms are run in the SAME mode so the arms are comparable, and the capped arms then also
# provide an isolation-vs-batch control against the M3PH1 files.
# Physics only (-w 1 -J), 100 events max, one binary (M1's frozenvar) so no layout band between arms.
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
V=$S/m1_ref/frozenvar
M=$S/m3_ref
IN=$S/jet_ref/trackingNtuple_jets_1000.root
cd $V || exit 9
md5sum -c $V/MD5 || { echo "FROZEN BINARY MD5 MISMATCH -- job voided"; exit 8; }
cd $S || exit 9
source setup.sh > /dev/null 2>&1
eval $(scramv1 runtime -sh) 2>/dev/null
source setup.sh > /dev/null 2>&1
export LD_LIBRARY_PATH=$V:$LD_LIBRARY_PATH
export IN V M

run_one() {   # $1 = event index ; $2 = arm name ; $3 = cap
  env LST_CHAIN_DEG_CAP=$3 $V/lst_cpu -i "$IN" -x $1 -n -1 -s 1 -v 2 -w 1 -J \
      -o $M/pe100_$2/evt$1.root > $M/pe100_$2/evt$1.log 2>&1
  echo "$1 $?" >> $M/pe100_$2.rc
}
export -f run_one

for arm in "C256 256" "C512 512" "OFF 0"; do
  set -- $arm
  NAME=$1; CAP=$2
  mkdir -p $M/pe100_$NAME
  rm -f $M/pe100_$NAME.rc
  : > $M/pe100_$NAME.rc
  echo "=== arm $NAME (cap=$CAP), 100 events, one process each, 4 at a time ==="
  SECONDS=0
  seq 0 99 | xargs -P 4 -I{} bash -c "run_one {} $NAME $CAP"
  echo "arm $NAME wall ${SECONDS}s : ok=$(grep -c ' 0$' $M/pe100_$NAME.rc) of 100"
  echo "  nonzero rc:"; grep -v ' 0$' $M/pe100_$NAME.rc | sort -n | tr '\n' ' '; echo
  echo "  overflow-skipped events: $(grep -l 'CHAIN OVERFLOW' $M/pe100_$NAME/evt*.log 2>/dev/null | wc -l)"
  grep -h 'CHAIN OVERFLOW' $M/pe100_$NAME/evt*.log 2>/dev/null | head -3
  echo "  root files: $(ls $M/pe100_$NAME/evt*.root 2>/dev/null | wc -l)"
done
echo M3PH2DONE
