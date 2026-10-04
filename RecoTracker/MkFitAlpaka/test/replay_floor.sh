#!/bin/bash
# replay_floor.sh <sample> <variant> <outdir> [maxEvents] [inputFile] [rawFiles]: the D-M4 noise floor (harness lane).
#   ref job: stock replay + mkFit final fit with the production (x86-64-v3) libraries -> <outdir>/ref_<sample>.root
#   tgt job: the same events again with other libraries, per-seed comparison vs the ref job + MTV of both.
#   variant  mkfit : only stock mkFit is x86-64-v2 (libRecoTrackerMkFit{Core,CMS,}.so + pluginRecoTrackerMkFitPlugins.so
#                    of lib/<arch>/scram_x86-64-v2): the rounding sensitivity of mkFit alone, the D-M4 floor
#            all   : the whole release is x86-64-v2 (hit CPEs etc. too): informational
#            v3    : production libraries again (must be bit-identical: checks the recipe)
# env V2LIB=<dir>: take the v2 mkFit libraries from <dir> (e.g. r6_ref/floors/v2mkfit) instead of the release.
# Run inside a CMSSW environment of an area that has RecoTracker/MkFitAlpaka built. env MTV=0: no truth (no RAW
# secondaries: with many RAW parents + 8 streams the secondary-file switching can crash, seen on qcd500; env STREAMS=1
# avoids it).
# Outputs: <outdir>/{ref,tgt_<variant>}_<sample>.log, cand/fit comparator JSONs, DQM files, loaded-library list.
SMP=$1; VAR=$2; OUT=$(readlink -f $3); N=${4:--1}; INF=${5:-}; RAWF=${6:-}
[ -n "$CMSSW_BASE" ] || { echo "no CMSSW environment"; exit 1; }
mkdir -p $OUT && cd $OUT || exit 1
CFG=$(dirname $(readlink -f $0))/replay_floor_cfg.py
ARCHLIB=lib/$SCRAM_ARCH
V2DIR=$OUT/v2mkfit
EXTRA=""; [ -n "$INF" ] && EXTRA="inputFiles=$INF"; [ -n "$RAWF" ] && EXTRA="$EXTRA rawFiles=$RAWF"

libs_of() {  # wait until the mkFit libraries are mapped, then list them
  local pid=$1 f=$2 i
  for i in $(seq 1 600); do
    kill -0 $pid 2>/dev/null || break
    if grep -q libRecoTrackerMkFitCore /proc/$pid/maps 2>/dev/null; then
      sleep 2; grep -E 'MkFit|libRecoLocalTracker|libRecoTrackerTransientTrackingRecHit' /proc/$pid/maps | awk '{print $6}' | sort -u > $f; return
    fi
    sleep 1
  done
}

if [ ! -s ref_$SMP.root ]; then
  cmsRun $CFG role=ref streams=${STREAMS:-0} sample=$SMP maxEvents=$N out=file:$OUT/ref_$SMP.root mtv=${MTV:-1} dqmFile=$OUT/DQM_ref_$SMP.root $EXTRA \
    > ref_$SMP.log 2>&1 &
  pid=$!; libs_of $pid ref_$SMP.libs; wait $pid || { echo "FAIL ref $SMP"; exit 1; }
fi

case $VAR in
  mkfit)
    mkdir -p $V2DIR
    V2=${V2LIB:-$CMSSW_RELEASE_BASE/$ARCHLIB/scram_x86-64-v2}   # env V2LIB: a locally built x86-64-v2 mkFit (new reference)
    for f in libRecoTrackerMkFit.so libRecoTrackerMkFitCMS.so libRecoTrackerMkFitCore.so pluginRecoTrackerMkFitPlugins.so; do
      ln -sf $V2/$f $V2DIR/$f
    done
    grep '^pluginRecoTrackerMkFitPlugins.so ' $V2/.edmplugincache > $V2DIR/.edmplugincache
    export LD_LIBRARY_PATH=$V2DIR:$LD_LIBRARY_PATH ;;
  all)
    export LD_LIBRARY_PATH=$(echo $LD_LIBRARY_PATH | sed "s#$CMSSW_RELEASE_BASE/$ARCHLIB#$CMSSW_RELEASE_BASE/$ARCHLIB/scram_x86-64-v2:$CMSSW_RELEASE_BASE/$ARCHLIB#") ;;
  v3) ;;
  *) echo "variant: mkfit | all | v3"; exit 1 ;;
esac
T=tgt_${VAR}${TAG:-}_$SMP     # env TAG: suffix for a repeated tgt job; env PAIRED=1: paired truth comparison
cmsRun $CFG role=tgt streams=${STREAMS:-0} paired=${PAIRED:-0} sample=$SMP maxEvents=$N inputFiles=file:$OUT/ref_$SMP.root mtv=${MTV:-1} dqmFile=$OUT/DQM_$T.root \
  summaryPrefix=$OUT/${T}_ $([ -n "$RAWF" ] && echo rawFiles=$RAWF) > $T.log 2>&1 &
pid=$!; libs_of $pid $T.libs; wait $pid || { echo "FAIL tgt $VAR $SMP"; exit 1; }
grep '^\[compare\|^\[paired' $T.log
echo "libraries (tgt): $(grep -c scram_x86-64-v2 $T.libs) of $(wc -l < $T.libs) from scram_x86-64-v2"
