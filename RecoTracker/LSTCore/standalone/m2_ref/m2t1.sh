#!/bin/bash
# M2T1 -- jets CPU timing + memory, the FIRST 100 events, per-event isolated, exactly the
# [COORDINATOR 12:50] baseline recipe (`-x i -n -1 -s 1 -v 2 -w 0` under /usr/bin/time -v, one
# process per event) so the delta is not confounded by the isolation mode.
# Arms: C = ${ARMS:-"8 32"} .  The cap-off arm is the baseline itself (jetrecon_ref/pe1000),
# already measured at 81a9afe2d00 -- it is NOT re-run here; agg_pe.py reads both.
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
CPU=$S/bin/lst_cpu
md5sum $CPU $S/LST/liblst_cpu.so
if [ -f $R/frozenC/MD5 ]; then
  A=$(md5sum $CPU | cut -d' ' -f1); B=$(awk '$2=="lst_cpu"{print $1}' $R/frozenC/MD5)
  [ "$A" = "$B" ] || { echo "VOID: binary changed since the frozen copy ($A vs $B)"; exit 8; }
fi
# CONTROL ARM, and it matters: at `-v 2` my ChainTopCInsert does one atomicAdd per ELIGIBLE ROW to
# count the eligible population (`objectsStatistics_`), which the baseline binary never paid. So the
# 12 heaviest events of the 100 are also run at `-v 1`, where that counter is off, to bound how much
# of the capped arm's cost is the instrument rather than the cap.
BIG="5 25 28 42 64 71 74 81 85 91 92 97"   # E over the 1 GiB GPU bin (M1's base100.csv)
for C in ${ARMS:-8 32}; do
  O=$R/peC$C; mkdir -p $O; : > $O/rc.txt
  echo "=== arm C=$C, -v 2 (baseline recipe) : 100 events, one process each -> $O ==="
  for i in $(seq 0 99); do
    LST_CHAIN_NODE_TOPC=$C /usr/bin/time -v $CPU -i $J -x $i -n -1 -s 1 -v 2 -w 0 \
        > $O/evt$i.log 2> $O/evt$i.err
    echo "$i $?" >> $O/rc.txt
  done
  echo "  rc census: $(awk '{print $2}' $O/rc.txt | sort | uniq -c | tr '\n' ' ')"
  O1=$R/peC${C}_v1; mkdir -p $O1; : > $O1/rc.txt
  echo "=== control arm C=$C, -v 1 (eligible-row counter OFF), the 12 heaviest events ==="
  for i in $BIG; do
    LST_CHAIN_NODE_TOPC=$C /usr/bin/time -v $CPU -i $J -x $i -n -1 -s 1 -v 1 -w 0 \
        > $O1/evt$i.log 2> $O1/evt$i.err
    echo "$i $?" >> $O1/rc.txt
  done
  echo "  rc census: $(awk '{print $2}' $O1/rc.txt | sort | uniq -c | tr '\n' ' ')"
done
# TILE SIZE, a pure locality/footprint knob: every tiling enumerates the same global rows in the
# same order and therefore inserts the same keys, so this arm cannot change a result -- only the
# transient tile allocation (rows x 21 B) and the cache behaviour. Run on the 12 heaviest events at
# `-v 1` so the eligible-row counter is out of the way.
C=${TILEC:-8}
for T in 262144 1048576 4194304; do
  OT=$R/peC${C}_tile$T; mkdir -p $OT; : > $OT/rc.txt
  echo "=== tile sweep C=$C tile=$T rows ($(python3 -c "print(f'{$T*21/1e6:.0f}')") MB), 12 heaviest events ==="
  for i in $BIG; do
    LST_CHAIN_NODE_TOPC=$C LST_CHAIN_EDGE_TILE=$T /usr/bin/time -v $CPU -i $J -x $i -n -1 -s 1 -v 1 -w 0 \
        > $OT/evt$i.log 2> $OT/evt$i.err
    echo "$i $?" >> $OT/rc.txt
  done
  echo "  rc census: $(awk '{print $2}' $OT/rc.txt | sort | uniq -c | tr '\n' ' ')"
done
echo "DONE M2T1"
