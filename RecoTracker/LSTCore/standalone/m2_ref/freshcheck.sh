# Sourced by every M2 job: refuse to measure a binary that is older than the sources it came from.
# The parked g4 tree had exactly this failure (a 12:26 build against a 12:32 edit), and a stale
# binary is worse than no measurement because the number looks fine.
G=/mnt/data1/gsn27/here/gpu_wt/g4/src/RecoTracker/LSTCore
for f in $G/interface/ChainConfig.h $G/src/alpaka/ChainEdges.h $G/src/alpaka/ChainWeld.h \
         $G/src/alpaka/LSTEvent.dev.cc; do
  for b in $G/standalone/bin/lst_cpu $G/standalone/bin/lst_cuda; do
    if [ ! "$b" -nt "$f" ]; then
      echo "VOID: $b is not newer than $f -- the build has not finished. Requeue this job."
      exit 7
    fi
  done
done
echo "freshness OK: both binaries are newer than all four modified sources"
