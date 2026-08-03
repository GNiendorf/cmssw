#!/bin/bash
# P2.5 attribution floor: is the GPU run-to-run TC set already unstable with chain tracking OFF?
# Two identical flag-OFF CUDA runs, compared on the tc_* ntuple rows (LST_CHAIN_TC_DUMP only fires
# inside arbitrateChains, so the sidecar cannot be used for this leg).
# Also the flag-ON leg through the same ntuple comparator, so the two are directly comparable.
STANDALONE=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
REF="$STANDALONE/p25_ref"
N="${1:-10}"

cd "$STANDALONE" && source setup.sh > /dev/null 2>&1
eval $(scramv1 runtime -sh) > /dev/null 2>&1
source setup.sh > /dev/null 2>&1

set -e
for leg in A B; do
  rm -f "$REF/floor_off_${leg}.root" "$REF/floor_on_${leg}.root"
  echo "[floor] gpu flag-OFF leg $leg"
  lst_cuda -i PU200RelVal -n "$N" -s 1 -o "$REF/floor_off_${leg}.root" > "$REF/floor_off_${leg}.log" 2>&1
  echo "[floor] gpu flag-ON  leg $leg"
  lst_cuda -i PU200RelVal -n "$N" -s 1 --use_chain_tracking \
    -o "$REF/floor_on_${leg}.root" > "$REF/floor_on_${leg}.log" 2>&1
done

echo
echo "########## GPU run-to-run, chain tracking OFF (the pre-existing LST floor) ##########"
python3 "$REF/p25_tcntuple.py" "$REF/floor_off_A.root" "$REF/floor_off_B.root" || true
echo
echo "########## GPU run-to-run, chain tracking ON ##########"
python3 "$REF/p25_tcntuple.py" "$REF/floor_on_A.root" "$REF/floor_on_B.root" || true
