#!/bin/bash
# P4 referee run driver.  $1 = tag, $2 = what (tune|hold|cube|cubehp|jets|jetstune)
# P4_BIN  = directory holding lst_cpu (default = the gc6 reference build at ec08aba9e5b)
# Every arm is judged with EXACTLY these commands; only P4_BIN changes.
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
G=/mnt/data1/gsn27/here/gpu_wt/gc6
R=$S/p4_ref/meas
RELVAL=/data2/segmentlinking/CMSSW_12_5_0_pre3/RelValTTbar_14TeV_CMSSW_12_5_0_pre3
JETS=$S/jet_ref/trackingNtuple_jets_1000.root
TAG=$1; WHAT=${2:-tune}
mkdir -p $R
# default = the FROZEN copy of the gc6 reference build at ec08aba9e5b (md5 a1b9c2e8...),
# kept in p4_ref/refbin so a later gc6 rebuild can never silently change the reference.
BINDIR=${P4_BIN:-$S/p4_ref/refbin/bin}
# Run in the environment of the tree the BINARY came from, not gc6's: an arm's lst_cpu is linked
# against ITS OWN liblst_cpu.so and the two are one unit.
ARMSA=$(dirname $BINDIR)
[ -f $ARMSA/setup.sh ] && pushd $ARMSA > /dev/null || pushd $G/src/RecoTracker/LSTCore/standalone > /dev/null
source setup.sh > /dev/null 2>&1; eval $(scramv1 runtime -sh) > /dev/null 2>&1; source setup.sh > /dev/null 2>&1
export LD_LIBRARY_PATH=$ARMSA/LST:$LD_LIBRARY_PATH
BIN=$BINDIR/lst_cpu
echo "BIN $BIN  md5 $(md5sum $BIN | cut -d' ' -f1)"
# LIBRARY PROVENANCE GATE.  A missing/renamed liblst_cpu.so in the arm's tree makes the loader fall
# through to some OTHER tree's copy; the binary then runs SHIPPED kernels against a patched
# ChainConfig layout -- which segfaults on jets and, worse, could silently mis-measure elsewhere.
# (This actually happened at 17:33: P2's CUDA build had just cleaned g4/LST, and three jet runs of
# their binary died with SIGSEGV against gc6's library.  Never again: hard fail instead.)
RESOLVED=$(ldd $BIN 2>/dev/null | awk '$1 == "liblst_cpu.so" {print $3}')
echo "liblst_cpu.so -> ${RESOLVED:-NOT FOUND}  md5 $(md5sum ${RESOLVED:-/dev/null} 2>/dev/null | cut -d' ' -f1)"
case "$RESOLVED" in
  $ARMSA/LST/*) : ;;
  *) echo "ABORT: $BIN resolves liblst_cpu.so to '${RESOLVED:-NOTHING}', not to $ARMSA/LST/."
     echo "       The arm's own CPU library is missing (a CUDA rebuild cleans it) -- rebuild -mC or"
     echo "       hand me a patch and I will build it in gc6.  Refusing to produce a number."
     exit 8 ;;
esac
run() {  # $1 input  $2 nev  $3 out  $4 streams  $5 extra
  rm -f $3
  /usr/bin/time -f "WALL %e s" $BIN -i $1 -n $2 -p 0.8 -s ${4:-8} -v 1 $5 -o $3 > ${3%.root}.log 2>&1
  RC=$?
  grep -E "WALL" ${3%.root}.log | tail -1
  echo "rc=$RC  events=$(grep -c 'Event' ${3%.root}.log 2>/dev/null)"
  [ $RC -ne 0 ] && { echo "RUN FAIL $TAG $WHAT rc=$RC"; return $RC; }
  if [ -n "$6" ]; then    # jet arm: the dR-band gate, not the PU judge
    python3 $S/p4_ref/jetgate.py --split holdout $3 --labels $TAG > ${3%.root}.jetjudge 2>&1
  else
    python3 $S/d3_ref/pu_judge.py $3 --json ${3%.root}.json > ${3%.root}.judge 2>&1 \
      || { echo "JUDGE FAIL $TAG"; return 3; }
    head -19 ${3%.root}.judge
  fi
}
case $WHAT in
  tune) run $RELVAL/event_1000.root 1000 $R/${TAG}_tune.root 8 ;;
  hold) run $RELVAL/event_2000.root 1000 $R/${TAG}_hold.root 8 ;;
  # P4_STREAMS overrides the stream count.  The cube50_highPt WRITER segfaults at high TC count
  # (documented trap: try -s 4 -> -s 2 -> -s 1); an arm that keeps more chains alive crosses that
  # threshold where SHIP does not, so the fallback has to be available -- and when it is used, the
  # REFERENCE arm must be re-run at the same stream count (proven equivalent, see FINDINGS).
  cube)   run cube50        5000 $R/${TAG}_cube.root   ${P4_STREAMS:-32} ;;
  cubehp) run cube50_highPt 5000 $R/${TAG}_cubehp.root ${P4_STREAMS:-32} ;;
  jets)   run $JETS 1000 $R/${TAG}_jets.root 16 "-J" jet ;;
esac
echo "P4 DONE $TAG $WHAT"
