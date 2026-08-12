#!/bin/bash
# M3 (c): HEAD's own edge/feature/chain dumps for the P3 pre-K5 recall study.
# All three env taps are read-only sidecars (LSTEvent.dev.cc): with them unset the binary is
# byte-identical, so this changes no physics and makes no timing claim.
#   arm A: PU200RelVal, 100 events, one process (replaces round1's 2-commits-back reference)
#   arm B: the jet sample, ONE PROCESS PER EVENT (the 4 GiB truncation kills a batch), on the
#          events that survive, smallest first.
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
D=$S/m3_ref/dumps
mkdir -p $D
cd $S || exit 9
source setup.sh > /dev/null 2>&1
eval $(scramv1 runtime -sh) 2>/dev/null
source setup.sh > /dev/null 2>&1

if [ "${1:-both}" = "pu" ] || [ "${1:-both}" = "both" ]; then
  echo "=== arm A: PU200RelVal 100 evt, HEAD dumps ==="
  LST_CHAIN_FEAT_DUMP=$D/pu_edgefeat.bin \
  LST_CHAIN_EDGE_DUMP=$D/pu_edges.bin \
  LST_CHAIN_CHAIN_DUMP=$D/pu_chains.bin \
    ./bin/lst_cpu -i PU200RelVal -n 100 -s 1 -p 0.8 -v 1 -w 0 > $D/pu_dump.log 2>&1
  echo "pu rc=$? $(ls -la $D/pu_*.bin | awk '{print $9, $5}')"
fi

if [ "${1:-both}" = "jet" ] || [ "${1:-both}" = "both" ]; then
  for i in 8 7 1 9 2 4 6 3 0; do
    echo "=== arm B: jet event $i ==="
    LST_CHAIN_FEAT_DUMP=$D/jet${i}_edgefeat.bin \
    LST_CHAIN_EDGE_DUMP=$D/jet${i}_edges.bin \
    LST_CHAIN_CHAIN_DUMP=$D/jet${i}_chains.bin \
      /usr/bin/time -v ./bin/lst_cpu -i jet_ref/trackingNtuple_jets_1000.root -x $i -n -1 \
        -s 1 -v 2 -w 0 > $D/jet${i}_dump.log 2> $D/jet${i}_dump.err
    echo "jet $i rc=$? $(du -sh $D/jet${i}_edgefeat.bin 2>/dev/null)"
    df -h /mnt/data1 | tail -1
  done
fi
echo ALLDONE
