#!/bin/bash
# Per-event isolated CPU sweep over the 1000-event jet ntuple.
# Isolated so that the events which overflow the ChainEdges allocation (SIGSEGV)
# do not take the rest of the sample with them. -v 2 costs nothing measurable
# (verified event-by-event against -v 1), so every run carries both the stage
# timing table and the [MEM]/[CHAIN] graph statistics.
set -u
cd /mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
OUT=jetrecon_ref/pe1000
mkdir -p $OUT
: > jetrecon_ref/sweep1000.rc
for i in $(seq 0 999); do
  /usr/bin/time -v ./bin/lst_cpu -i jet_ref/trackingNtuple_jets_1000.root \
      -x $i -n -1 -s 1 -v 2 -w 0 > $OUT/evt$i.log 2> $OUT/evt$i.err
  echo "$i $?" >> jetrecon_ref/sweep1000.rc
done
echo DONE >> jetrecon_ref/sweep1000.rc
