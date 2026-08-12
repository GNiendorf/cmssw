#!/bin/bash
# M2P1 -- the PU200 cost of the cap, ONE binary, env-toggled arms, PALINDROME order
# (discipline rule 3: same-binary env A/B, alternate arms, never one pair; check the pLS column
# first because CPU totals carry a +/-45 ms layout band -- here both arms ARE the same binary, so
# any band is scheduling noise only).
# 100 events per arm, per the [COORDINATOR 12:50] 100-event rule.
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
GPU=$S/bin/lst_cuda
md5sum $CPU $GPU

run () {  # run <bin> <label> <C>
  printf "  %-12s " "$2"
  env LST_CHAIN_NODE_TOPC=$3 $1 -i PU200 -n 100 -v 1 -w 0 -s 1 2>&1 | grep -E '^\s+avg' | tail -1
}
# EVERY arm at `-v 1`, including the C=0 reference: at `-v 2` the capped path pays one atomicAdd per
# eligible row for its eligible-population counter, which the cap-off path does not, so a v2-vs-v1
# pair would be measuring my instrument. C=16 is the arm that matters (R = 14 on PU200, so C >= 16 is
# lossless by the induction argument); 8 and 32 are the neighbours.
A=${ARM:-16}
echo "=== CPU PU200 100 evt s=1 : Hits MD LS T3 Graph pLS Chain TC Reset | Total Total(short) ==="
run $CPU C0_a   0
run $CPU CA_a   $A
run $CPU C0_b   0
run $CPU CA_b   $A
run $CPU C8     8
run $CPU C32    32
echo; echo "=== GPU PU200 100 evt s=1 ==="
run $GPU C0_a   0
run $GPU CA_a   $A
run $GPU C0_b   0
run $GPU CA_b   $A
run $GPU C8     8
run $GPU C32    32
echo; echo "=== per-kernel attribution, 20 evt, LST_CHAIN_TIMING (attribution only) ==="
for b in cpu cuda; do
  case $b in cpu) BIN=$CPU;; cuda) BIN=$GPU;; esac
  for C in 0 $A; do
    env LST_CHAIN_TIMING=1 LST_CHAIN_NODE_TOPC=$C $BIN -i PU200 -n 20 -v 1 -w 0 -s 1 \
        > $R/pk_${b}_C$C.log 2>&1
    echo "  --- $b C=$C ---"
    grep -m3 'CHAIN TIMING' $R/pk_${b}_C$C.log
  done
done
echo "DONE M2P1"
