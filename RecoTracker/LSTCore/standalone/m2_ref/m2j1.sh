#!/bin/bash
# M2J1 -- the jet side of the DECISION: R on jets, and the bit-identity C-sweep against
# UNCAPPED, on the 10-event jet file (9 of its 10 events survive with the cap off; event 5 is
# the 4 GiB Idx-extent SIGSEGV).  Per-event isolated, one process each, because the cap-off arm
# cannot survive a batch.  Every arm writes an ntuple so the diff is a branch diff and not a
# count comparison.
#   [1] cap OFF (C=0)      : the reference ntuples, 9 events
#   [2] rank census cap OFF: R on jets, the number that decides the safe C
#   [3] C = 4/8/16/32/64   : ntuples for the same 9 events + event 5 as a liveness check
# NO `set -u`: setup.sh trips on unbound variables and would abort the script with rc=1 and no output.
S=/mnt/data1/gsn27/here/gpu_wt/g4/src/RecoTracker/LSTCore/standalone
H=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
R=$H/m2_ref
J=$H/jet_ref/trackingNtuple_jets_10.root
cd $S || exit 9
source setup.sh > /dev/null 2>&1
eval $(scramv1 runtime -sh) > /dev/null 2>&1
source setup.sh > /dev/null 2>&1
source $H/m2_ref/freshcheck.sh || exit 7
CPU=$S/bin/lst_cpu
mkdir -p $R/j10
md5sum $CPU $S/LST/liblst_cpu.so
if [ -f $R/frozenC/MD5 ]; then
  A=$(md5sum $CPU | cut -d' ' -f1); B=$(awk '$2=="lst_cpu"{print $1}' $R/frozenC/MD5)
  [ "$A" = "$B" ] || { echo "VOID: binary changed since the frozen copy ($A vs $B)"; exit 8; }
fi
SURV="0 1 2 3 4 6 7 8 9"

echo "############ [1] cap OFF reference ntuples, 9 survivable events ############"
for i in $SURV; do
  rm -f $R/j10/C0_evt$i.root
  LST_CHAIN_NODE_TOPC=0 $CPU -i $J -x $i -n -1 -s 1 -v 2 -w 1 -o $R/j10/C0_evt$i.root \
      > $R/j10/C0_evt$i.log 2>&1
  echo "  evt $i rc=$? $(grep -m1 -oE 'E1=[0-9]+ E2=[0-9]+ E=[0-9]+' $R/j10/C0_evt$i.log)"
done

echo; echo "############ [2] R on jets: rank census, cap OFF, same 9 events ############"
rm -f $R/rank_jets.log
for i in $SURV; do
  LST_CHAIN_RANK_CENSUS=1 LST_CHAIN_NODE_TOPC=0 $CPU -i $J -x $i -n -1 -s 1 -v 0 -w 0 \
      >> $R/rank_jets.log 2>&1
  echo "  evt $i rc=$?"
done
echo "  [CHAIN RANK] lines: $(grep -c 'CHAIN RANK' $R/rank_jets.log)"

echo; echo "############ [3] capped arms: C = 4/8/16/32/64, the 9 events + event 5 ############"
for C in 4 8 16 32 64; do
  for i in $SURV 5; do
    rm -f $R/j10/C${C}_evt$i.root
    LST_CHAIN_NODE_TOPC=$C $CPU -i $J -x $i -n -1 -s 1 -v 2 -w 1 -o $R/j10/C${C}_evt$i.root \
        > $R/j10/C${C}_evt$i.log 2>&1
    printf "  C=%-3s evt %-2s rc=%s  %s\n" $C $i $? "$(grep -m1 -oE 'C=[0-9]+ enumerated=[0-9]+ eligible=[0-9]+ kept=[0-9]+' $R/j10/C${C}_evt$i.log)"
  done
done

echo; echo "=== event 5 (today's SIGSEGV) at every C, [MEM]/[CHAIN] lines ==="
for C in 4 8 16 32 64; do
  echo "  --- C=$C ---"
  grep -E '\[CHAIN\] nodes|CHAIN TOPC|\[MEM\] ChainTopC|\[MEM\] ChainEdges|\[MEM\] Total' $R/j10/C${C}_evt5.log
done
echo "DONE M2J1"
