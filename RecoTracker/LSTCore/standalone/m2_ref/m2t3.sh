#!/bin/bash
# M2T3 -- timing and memory of the SHIPPED candidate, C = 16, on the benchmark 100 jet events, both
# backends, per-event isolated.  C=16 is the value the gates chose (35/35 on 100 PU200 events and on
# all 9 survivable jet events; R = 14 on PU200 so it is also proven there), and a shipped default has
# to be the value that was measured -- the C=8 and C=32 arms already on record are its neighbours.
#   CPU at `-v 2`  : the [COORDINATOR 12:50] baseline recipe exactly, so agg_pe.py differences it.
#   GPU at `-v 1`  : the eligible-row counter must be off on a GPU timing arm (200 M threads, one
#                    global atomicAdd), plus a short `-v 2` memory pass over the 12 heaviest events.
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
GPU=$S/bin/lst_cuda
A=$(md5sum $CPU | cut -d' ' -f1); B=$(awk '$2=="lst_cpu"{print $1}' $R/frozenC/MD5)
[ "$A" = "$B" ] || { echo "VOID: binary changed since the frozen copy ($A vs $B)"; exit 8; }
md5sum $CPU $GPU
BIG="5 25 28 42 64 71 74 81 85 91 92 97"
C=16

O=$R/peC$C; mkdir -p $O; : > $O/rc.txt
echo "=== CPU C=$C, -v 2 : 100 events, one process each ==="
for i in $(seq 0 99); do
  LST_CHAIN_NODE_TOPC=$C /usr/bin/time -v $CPU -i $J -x $i -n -1 -s 1 -v 2 -w 0 \
      > $O/evt$i.log 2> $O/evt$i.err
  echo "$i $?" >> $O/rc.txt
done
echo "  rc census: $(awk '{print $2}' $O/rc.txt | sort | uniq -c | tr '\n' ' ')"

O1=$R/peC${C}_v1; mkdir -p $O1; : > $O1/rc.txt
echo "=== CPU C=$C, -v 1 control (eligible-row counter OFF), the 12 heaviest ==="
for i in $BIG; do
  LST_CHAIN_NODE_TOPC=$C /usr/bin/time -v $CPU -i $J -x $i -n -1 -s 1 -v 1 -w 0 \
      > $O1/evt$i.log 2> $O1/evt$i.err
  echo "$i $?" >> $O1/rc.txt
done
echo "  rc census: $(awk '{print $2}' $O1/rc.txt | sort | uniq -c | tr '\n' ' ')"

G=$R/gpeC$C; mkdir -p $G; : > $G/rc.txt
echo "=== GPU C=$C, -v 1 : 100 events, one process each ==="
for i in $(seq 0 99); do
  LST_CHAIN_NODE_TOPC=$C /usr/bin/time -v $GPU -i $J -x $i -n -1 -s 1 -v 1 -w 0 \
      > $G/evt$i.log 2> $G/evt$i.err
  echo "$i $?" >> $G/rc.txt
done
echo "  rc census: $(awk '{print $2}' $G/rc.txt | sort | uniq -c | tr '\n' ' ')"
echo "  events that threw:"; awk '$2!=0{printf " %s(rc=%s)",$1,$2}' $G/rc.txt; echo

G2=$R/gpeC${C}_v2; mkdir -p $G2; : > $G2/rc.txt
echo "=== GPU C=$C, -v 2 memory pass, the 12 heaviest ==="
for i in $BIG; do
  LST_CHAIN_NODE_TOPC=$C /usr/bin/time -v $GPU -i $J -x $i -n -1 -s 1 -v 2 -w 0 \
      > $G2/evt$i.log 2> $G2/evt$i.err
  echo "$i $?" >> $G2/rc.txt
done
echo "  rc census: $(awk '{print $2}' $G2/rc.txt | sort | uniq -c | tr '\n' ' ')"
grep -h '\[MEM\] Total' $G2/evt*.log | sort -t: -k2 -rn | head -4
echo "DONE M2T3"
