#!/bin/bash
# P2.5 node/edge sidecar legs: the weld-tie uniqueness census and the CPU-vs-GPU attribution.
# usage: run_audit.sh <nevents>
STANDALONE=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
REF="$STANDALONE/p25_ref"
N="${1:-10}"

cd "$STANDALONE" && source setup.sh > /dev/null 2>&1
eval $(scramv1 runtime -sh) > /dev/null 2>&1
source setup.sh > /dev/null 2>&1

set -e
for bk in cpu cuda; do
  rm -f "$REF/aud_${bk}_nodes.bin" "$REF/aud_${bk}_edges.bin" "$REF/aud_${bk}_ch.bin" "$REF/aud_${bk}.root"
  echo "[audit] $bk, $N events"
  LST_CHAIN_NODE_DUMP="$REF/aud_${bk}_nodes.bin" \
  LST_CHAIN_EDGE_DUMP="$REF/aud_${bk}_edges.bin" \
  LST_CHAIN_CHAIN_DUMP="$REF/aud_${bk}_ch.bin" \
    "lst_$bk" -i PU200RelVal -n "$N" -s 1 -w 0 --use_chain_tracking \
    -o "$REF/aud_${bk}.root" > "$REF/aud_${bk}.log" 2>&1
done

echo
echo "############ weld tie uniqueness census (CPU) ############"
python3 "$REF/p25_nodes.py" tie "$REF/aud_cpu_nodes.bin" "$REF/aud_cpu_edges.bin" || true
echo
echo "############ weld tie uniqueness census (GPU) ############"
python3 "$REF/p25_nodes.py" tie "$REF/aud_cuda_nodes.bin" "$REF/aud_cuda_edges.bin" || true
echo
echo "############ CPU-vs-GPU chain attribution ############"
python3 "$REF/p25_nodes.py" attrib "$REF/aud_cpu_nodes.bin" "$REF/aud_cpu_ch.bin" \
                                   "$REF/aud_cuda_nodes.bin" "$REF/aud_cuda_ch.bin" || true
