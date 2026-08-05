#!/bin/bash
# ar_train.sh -- M19 attach-head variants. GPU 1 only (CUDA_VISIBLE_DEVICES=1).
#
# r0 CONTROL   : the g1 recipe (plain population-weighted BCE, no displaced upweight, no
#                type reweight, val AUC over both target types) run on the NEW 798-event
#                dump at the M19 STACK operating point. Its job is to separate "more +
#                better-matched data" from "new objective" in the attribution.
# r1 PRECISION : + one-sided focal on negatives (gamma_neg 2), val AUC on CHAIN pairs.
# r2 +DISPLACED: + true-pair upweight 8x for vxy [1,5), 16x for vxy >= 5.
# r3 +CHAINFOCUS: + 8x on every chain-target row (the universe -a actually cuts on; it is
#                a measured 0.368% of the wgt-weighted fake mass under r0/r1/r2).
#
# The shared standardization cache (cache_ret.npz, 10.4 GB) is already built, so every
# variant starts from the identical split / mu / sd and only the loss weights differ.
# GPU budget: each process pins Xtr (75.5M x 19 float32 = 5.7 GB) plus labels/weights,
# measured at ~6.3 GB; three fit in the L4's 23 GB, four do not -- hence the two waves.
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
P="$S/fanout5/attachretrain"
pushd "$S" > /dev/null && source setup.sh > /dev/null 2>&1
cmsenv > /dev/null 2>&1
source setup.sh > /dev/null 2>&1
cd "$P" || exit 1
export CUDA_VISIBLE_DEVICES=1

run() { # $1=tag, rest = objective flags
  local v="$1"; shift
  python3 train_attach_ret.py --out-model "$P/attach_mlp_${v}.pt" \
    --out-norm "$P/attach_norm_${v}.json" --out-testauc "$P/attach_${v}_testauc.json" \
    "$@" > "$P/train_${v}.log" 2>&1
  echo "train_${v} exit=$?"
}

echo "=== wave 1: r0 / r1 / r2 ==="
run r0 --gamma-neg 0 --val-metric all &
run r1 --gamma-neg 2.0 --val-metric chain &
run r2 --gamma-neg 2.0 --disp-mid 8 --disp-hi 16 --val-metric chain &
wait
echo "=== wave 2: r3 ==="
run r3 --gamma-neg 2.0 --disp-mid 8 --disp-hi 16 --chain-weight 8 --val-metric chain
echo "=== ALL TRAINING DONE ==="
for v in r0 r1 r2 r3; do echo "--- $v ---"; tail -2 "$P/train_${v}.log"; done
