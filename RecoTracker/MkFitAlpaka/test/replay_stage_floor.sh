#!/bin/bash
# replay_stage_floor.sh <sample> <outdir> [maxEvents]: per-stage D-M4 floor (harness lane). Two jobs on the same
# events: stock stage dumps with the production (x86-64-v3) libraries and with the x86-64-v2 mkFit libraries, then
# test/replay_stage_compare.py v3 vs v2 (env V2LIB=<dir>: v2 mkFit libraries from <dir> instead of the release) (table + JSON per stage for test/replay_floor_check.py).
SMP=$1; OUT=$(readlink -f $2); N=${3:-100}
D=$(dirname $(readlink -f $0)); mkdir -p $OUT && cd $OUT || exit 1
cmsRun $D/replay_stage_floor_cfg.py sample=$SMP maxEvents=$N prefix=$OUT/v3_${SMP}_ > v3_$SMP.log 2>&1 || { echo FAIL v3; exit 1; }
V2=${V2LIB:-$CMSSW_RELEASE_BASE/lib/$SCRAM_ARCH/scram_x86-64-v2};  # env V2LIB: local v2 mkFit
 mkdir -p $OUT/v2mkfit
for f in libRecoTrackerMkFit.so libRecoTrackerMkFitCMS.so libRecoTrackerMkFitCore.so pluginRecoTrackerMkFitPlugins.so; do
  ln -sf $V2/$f $OUT/v2mkfit/$f; done
grep '^pluginRecoTrackerMkFitPlugins.so ' $V2/.edmplugincache > $OUT/v2mkfit/.edmplugincache
LD_LIBRARY_PATH=$OUT/v2mkfit:$LD_LIBRARY_PATH cmsRun $D/replay_stage_floor_cfg.py sample=$SMP maxEvents=$N \
  prefix=$OUT/v2_${SMP}_ > v2_$SMP.log 2>&1 || { echo FAIL v2; exit 1; }
grep 'stagesSelfCheck\] events identical' v3_$SMP.log v2_$SMP.log
python3 $D/replay_stage_compare.py $OUT/v3_${SMP}_ $OUT/v2_${SMP}_ --json $OUT/floor_${SMP}_ | tee floor_$SMP.txt
