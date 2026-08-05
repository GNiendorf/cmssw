#!/bin/bash
# The 300-event scoreboard NOISE FLOOR on the device: the PRE-CHANGE binary against itself.
# LST's own upstream stages are not reproducible on CUDA (P2.5), so the GPU scoreboard moves
# between two runs of the same binary; this measures by how much, which is what the P2.6a
# before-vs-after GPU scoreboard has to be read against.
STANDALONE=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
PROTO="$STANDALONE/prototype"
REF="$STANDALONE/p26_ref"

cd "$STANDALONE" && source setup.sh > /dev/null 2>&1
eval $(scramv1 runtime -sh) > /dev/null 2>&1
source setup.sh > /dev/null 2>&1

set -e
rm -f "$REF/sbf_cuda.root" "$REF/sbf_cuda_hists.root" "$REF/sbf_cuda.json"
"$REF/bin_before/lst_cuda" -i PU200RelVal -n 300 -s 4 --use_chain_tracking \
  -o "$REF/sbf_cuda.root" > "$REF/sbf_cuda.log" 2>&1
createPerfNumDenHists -i "$REF/sbf_cuda.root" -o "$REF/sbf_cuda_hists.root" >> "$REF/sbf_cuda.log" 2>&1
python3 "$PROTO/compare_ab.py" --proto "$REF/sbf_cuda_hists.root" --base "$PROTO/base300_hists.root" \
  --json "$REF/sbf_cuda.json" > /dev/null

echo "################ cuda 300-event scoreboard NOISE FLOOR: BEFORE vs BEFORE ################"
python3 "$STANDALONE/p25_ref/p25_scoreboard.py" "$REF/sb_cuda_before.json" "$REF/sbf_cuda.json"
echo SBFLOOR_DONE
