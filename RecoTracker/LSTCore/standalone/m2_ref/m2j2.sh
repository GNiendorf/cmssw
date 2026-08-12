#!/bin/bash
# M2J2 -- the jet bit-identity gate on the BENCHMARK SET: the first 100 events of
# jet_ref/trackingNtuple_jets_1000.root, per-event isolated, cap-off reference ntuple vs the
# capped ntuple at C=$ARM.  Events 5 and 85 are excluded: the cap-off arm cannot produce a
# reference for them (they are the two 4 GiB Idx-extent SIGSEGVs), so the gate is on the 98
# survivors -- exactly the population the [COORDINATOR 12:50] baseline is quoted over.
# Branch diffs are offline (m2_ref/gate.py), not in this job.
# NO `set -u`: setup.sh trips on unbound variables and would abort the script with rc=1.
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
A=${ARM:-32}
O=$R/j100; mkdir -p $O; : > $O/rc.txt   # truncate: the loop below skips pairs that already exist,
                                        # so a requeue must not inherit the previous rc census
md5sum $CPU
echo "=== cap-off reference + C=$A, 98 events (5 and 85 skipped: no cap-off reference exists) ==="
for i in $(seq 0 99); do
  [ $i -eq 5 ] && continue
  [ $i -eq 85 ] && continue
  for C in 0 $A; do
    F=$O/C${C}_evt$i.root
    if [ ! -s $F ]; then
      rm -f $F
      # -J writes the genjet branches. It touches ONLY write_lst_ntuple.cc (verified: `ana.jet_branches`
      # has no reconstruction reader), so the arms stay comparable, the branch count is larger than
      # PU200's 35, and the same files can feed M3's m3_ref/jetphys.py if the gate needs physics
      # deltas instead of bit-identity.
      LST_CHAIN_NODE_TOPC=$C $CPU -i $J -x $i -n -1 -s 1 -v 2 -w 1 -J -o $F > $O/C${C}_evt$i.log 2>&1
      echo "$i $C $?" >> $O/rc.txt
    fi
  done
done
echo "  rc census: $(awk '{print $2" rc="$3}' $O/rc.txt | sort | uniq -c | tr '\n' ' ')"
echo "  pairs written: $(ls $O/C0_evt*.root 2>/dev/null | wc -l) ref / $(ls $O/C${A}_evt*.root 2>/dev/null | wc -l) cand"

# R ON THE BENCHMARK SET.  R is a sample statistic (14 over 100 PU200 events, 19 over 9 jet events),
# so the shipped C cannot be justified from nine events.  This pass measures it over all 98, cap off,
# `-w 0` (no ntuple), one process each.
rm -f $R/rank_jets100.log
for i in $(seq 0 99); do
  [ $i -eq 5 ] && continue
  [ $i -eq 85 ] && continue
  LST_CHAIN_RANK_CENSUS=1 LST_CHAIN_NODE_TOPC=0 $CPU -i $J -x $i -n -1 -s 1 -v 0 -w 0 \
      >> $R/rank_jets100.log 2>&1
done
echo "  [CHAIN RANK] lines: $(grep -c 'CHAIN RANK' $R/rank_jets100.log)  (expect 294 = 98 evt x 3 sweeps)"
grep -h 'CHAIN RANK' $R/rank_jets100.log | sed -E 's/.*maxRankOut=([0-9]+) maxRankIn=([0-9]+).*/\1 \2/' \
  | awk '{if($1>a)a=$1; if($2>b)b=$2} END{print "  R over the benchmark 98: maxRankOut="a" maxRankIn="b}'
echo "DONE M2J2"
