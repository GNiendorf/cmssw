#!/bin/bash
# P2.5 GPU run-to-run reproducibility on a larger sample (the 5-event leg is too small to be
# convincing once the identity is 100%). Two identical CUDA invocations, 30 events, -s 1.
STANDALONE=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
REF="$STANDALONE/p25_ref"
N="${1:-30}"

cd "$STANDALONE" && source setup.sh > /dev/null 2>&1
eval $(scramv1 runtime -sh) > /dev/null 2>&1
source setup.sh > /dev/null 2>&1

set -e
for leg in A B; do
  rm -f "$REF/g30_${leg}.root" "$REF/g30_${leg}_ch.bin" "$REF/g30_${leg}_tc.bin"
  echo "[repro30] gpu leg $leg"
  LST_CHAIN_CHAIN_DUMP="$REF/g30_${leg}_ch.bin" LST_CHAIN_TC_DUMP="$REF/g30_${leg}_tc.bin" \
    lst_cuda -i PU200RelVal -n "$N" -s 1 --allobj --use_chain_tracking \
    -o "$REF/g30_${leg}.root" > "$REF/g30_${leg}.log" 2>&1
done

echo
echo "################ GPU run-to-run, $N events ################"
python3 "$REF/p25_repro.py" chain "$REF/g30_A_ch.bin" "$REF/g30_B_ch.bin" || true
python3 "$REF/p25_repro.py" tc    "$REF/g30_A_tc.bin" "$REF/g30_B_tc.bin" || true
