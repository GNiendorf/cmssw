#!/bin/bash
# chain.sh: run the validation waves one after another (<= 2 cmsRun of this lane at a time)
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/mem_ref/lanes/mkfit_alpaka/r7_menu/menu/scripts
for w in "$@"; do [ -e $S/../STOPCHAIN ] && break; echo "WAVE $w START $(date '+%-I:%M %p') load $(cut -d' ' -f1 /proc/loadavg)"; $S/wave.sh $w; done
echo "CHAIN DONE $(date '+%-I:%M %p')"
