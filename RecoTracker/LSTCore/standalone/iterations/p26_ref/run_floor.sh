#!/bin/bash
# The GPU run-to-run identity FLOOR of the PRE-CHANGE binary: two identical invocations of
# p26_ref/bin_before/lst_cuda. It is what the P2.6a before-vs-after TC number has to be compared
# against, because LST's own upstream stages are not reproducible on the device (P2.5 finding).
STANDALONE=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
REF="$STANDALONE/p26_ref"

cd "$STANDALONE" && source setup.sh > /dev/null 2>&1
eval $(scramv1 runtime -sh) > /dev/null 2>&1
source setup.sh > /dev/null 2>&1

for leg in A B; do
  rm -f "$REF/floor_${leg}.root" "$REF/floor_${leg}_tc.bin" "$REF/floor_${leg}_ch.bin"
  LST_CHAIN_TC_DUMP="$REF/floor_${leg}_tc.bin" LST_CHAIN_CHAIN_DUMP="$REF/floor_${leg}_ch.bin" \
    "$REF/bin_before/lst_cuda" -i PU200RelVal -n 10 -s 1 --allobj --use_chain_tracking \
    -o "$REF/floor_${leg}.root" > "$REF/floor_${leg}.log" 2>&1
done
{
  echo "################ GPU run-to-run, PRE-CHANGE binary (the floor) ################"
  python3 "$STANDALONE/p25_ref/p25_repro.py" chain "$REF/floor_A_ch.bin" "$REF/floor_B_ch.bin"
  python3 "$STANDALONE/p25_ref/p25_repro.py" tc    "$REF/floor_A_tc.bin" "$REF/floor_B_tc.bin"
} > "$REF/out_floor.txt" 2>&1
grep -E "IDENTITY|####" "$REF/out_floor.txt"
echo FLOOR_DONE
