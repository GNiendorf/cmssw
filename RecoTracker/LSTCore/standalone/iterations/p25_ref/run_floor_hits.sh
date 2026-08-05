#!/bin/bash
# P2.5 reproducibility floor at HIT level. The TC sidecar now also covers the flag-OFF collection,
# so the pre-existing LST GPU nondeterminism can be measured with exactly the comparator the
# flag-ON number uses (type + outer-tracker hit rows), instead of the weaker kinematic one.
STANDALONE=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
REF="$STANDALONE/p25_ref"
N="${1:-10}"

cd "$STANDALONE" && source setup.sh > /dev/null 2>&1
eval $(scramv1 runtime -sh) > /dev/null 2>&1
source setup.sh > /dev/null 2>&1

set -e
for leg in A B; do
  for mode in off on; do
    rm -f "$REF/fh_${mode}_${leg}.bin" "$REF/fh_${mode}_${leg}.root"
  done
  echo "[floorhits] gpu OFF leg $leg"
  LST_CHAIN_TC_DUMP="$REF/fh_off_${leg}.bin" lst_cuda -i PU200RelVal -n "$N" -s 1 -w 0 \
    -o "$REF/fh_off_${leg}.root" > "$REF/fh_off_${leg}.log" 2>&1
  echo "[floorhits] gpu ON  leg $leg"
  LST_CHAIN_TC_DUMP="$REF/fh_on_${leg}.bin" lst_cuda -i PU200RelVal -n "$N" -s 1 -w 0 \
    --use_chain_tracking -o "$REF/fh_on_${leg}.root" > "$REF/fh_on_${leg}.log" 2>&1
done
echo "[floorhits] cpu OFF legs"
LST_CHAIN_TC_DUMP="$REF/fh_coff_A.bin" lst_cpu -i PU200RelVal -n "$N" -s 1 -w 0 \
  -o "$REF/fh_coff_A.root" > "$REF/fh_coff_A.log" 2>&1
LST_CHAIN_TC_DUMP="$REF/fh_coff_B.bin" lst_cpu -i PU200RelVal -n "$N" -s 1 -w 0 \
  -o "$REF/fh_coff_B.root" > "$REF/fh_coff_B.log" 2>&1

echo
echo "###### GPU run-to-run, chain tracking OFF (pre-existing LST floor, hit level) ######"
python3 "$REF/p25_repro.py" tc "$REF/fh_off_A.bin" "$REF/fh_off_B.bin" || true
echo
echo "###### GPU run-to-run, chain tracking ON (hit level) ######"
python3 "$REF/p25_repro.py" tc "$REF/fh_on_A.bin" "$REF/fh_on_B.bin" || true
echo
echo "###### CPU run-to-run, chain tracking OFF (control: should be 100%) ######"
python3 "$REF/p25_repro.py" tc "$REF/fh_coff_A.bin" "$REF/fh_coff_B.bin" || true
