#!/bin/bash
# gpumem2.sh <outdir> <outfile>: every 2 s, the device memory (MiB) used by the cmsRun processes whose cwd is under <outdir> or $TMPDIR/multirun* (this
# benchmark's jobs only; other lanes' GPU jobs excluded), summed, + the GPU total; runs until killed. D7-f budget check.
O=$1; F=$2
while true; do
  s=0; n=0
  while IFS=, read -r pid mem; do pid=${pid// /}; mem=${mem//[^0-9]/}; [ -n "$pid" ] || continue
    c=$(readlink /proc/$pid/cwd 2>/dev/null); [[ $c == $O* || $c == $TMPDIR/multirun* ]] && { s=$((s + mem)); n=$((n + 1)); }
  done < <(nvidia-smi --query-compute-apps=pid,used_memory --format=csv,noheader,nounits 2>/dev/null)
  echo "$(date +%s) mine_MiB $s procs $n gpu0_total_MiB $(nvidia-smi --query-gpu=memory.used --format=csv,noheader,nounits -i 0)" >> $F
  sleep 2
done
