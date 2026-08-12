#!/bin/bash
# M2 build of worktree g4 (the per-node top-C weld cap), CPU + CUDA, incremental.
# Run it through broker/buildlock.sh so it never overlaps a measurement.
# The wrapper says "compilation successful" even when a TU failed, so the freshly BACKED UP
# .make.log.<timestamp> is grepped for 'error:' at the end and that is the verdict.
# NO `set -u`: setup.sh trips on unbound variables and would abort the script with rc=1 and no output.
S=/mnt/data1/gsn27/here/gpu_wt/g4/src/RecoTracker/LSTCore/standalone
cd $S || exit 9
source setup.sh > /dev/null 2>&1
eval $(scramv1 runtime -sh) > /dev/null 2>&1
source setup.sh > /dev/null 2>&1
BEFORE=$(ls -t $S/.make.log.* 2>/dev/null | head -1)
date '+start %F %T'
lst_make_tracklooper -c 2>&1 | tail -5
date '+end   %F %T'
AFTER=$(ls -t $S/.make.log.* 2>/dev/null | head -1)
echo "newest .make.log backup before=$BEFORE after=$AFTER"
for L in $AFTER $S/.make.log; do
  echo "--- $L : $(grep -c 'error:' $L) error: lines ---"
  grep -n 'error:' $L | head -20
done
ls -la --time-style=+%F_%T $S/bin/lst_cpu $S/bin/lst_cuda $S/LST/liblst_cpu.so $S/LST/liblst_cuda.so
md5sum $S/bin/lst_cpu $S/bin/lst_cuda $S/LST/liblst_cpu.so $S/LST/liblst_cuda.so
echo "chain symbols in liblst_cuda.so: $(nm -D --defined-only $S/LST/liblst_cuda.so 2>/dev/null | grep -c Chain)"
echo "BUILD DONE"
