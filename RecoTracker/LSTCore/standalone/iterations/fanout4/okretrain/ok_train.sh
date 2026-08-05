#!/bin/bash
# okretrain: train one 2-class ORDER-KEY head variant.
#   ok_train.sh <tag> [extra train_okey.py flags...]
# Recipe mirrors the resident a2 head (hidden 32, tiered displaced weights 8/16,
# 200 epochs / patience 15, seed 42, lr 1e-3, bs 16384) on the M18 dump; the
# variant-specific flags come from the caller.
TAG="$1"; shift
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
P="$S/fanout4/okretrain"
pushd "$S" > /dev/null && source setup.sh > /dev/null 2>&1
cmsenv > /dev/null 2>&1
source setup.sh > /dev/null 2>&1
export CUDA_VISIBLE_DEVICES=0
python3 "$P/train_okey.py" \
  --input "$P/chains_m18.root" \
  --survivor-npz "$P/ok_surv_m18.npz" \
  --out-model "$P/chain_mlp_${TAG}.pt" \
  --out-norm "$P/chain_norm_${TAG}.json" \
  --out-report "$P/report_${TAG}.json" \
  --state-file "$P/state_${TAG}.pt" \
  --seed 42 --hidden 32 --epochs 200 --patience 15 --lr 1e-3 --batch-size 16384 \
  --displaced-weight-mid 8 --displaced-weight-hi 16 \
  --no-perm-importance \
  "$@" > "$P/train_${TAG}.log" 2>&1
echo "rc=$? tag=$TAG"
grep -E "best checkpoint|SURVIVOR-RESTRICTED" -A 12 "$P/train_${TAG}.log" | tail -20
