#!/bin/bash
# run_all.sh <workdir>: the whole on-policy retrain in one command.
#   1. cache: stream the training RAW files into compact local-reco + truth caches (skipped when PTT_CACHE_ROOT is given)
#   2. dumps of the policy's tuples with the classifier cut off (cfg/dump_cfg.py on PTT_MENU)
#   3. train.py, 4. export.py, 5. fit_wp.py, 6. install the model in the area,
#   7. C++ parity: a dump with the shipped settings as a second deployed clone, then parity.py
# env: PTT_AREA (scram area: branch + LocalVal/PttTruth + LocalVal/PttDump built), PTT_MENU (full-menu CPU config with the
#      CA policy), PTT_CACHE_ROOT (dir with ttbar/ and qcd/ caches) or PTT_LFN_TTBAR + PTT_LFN_QCD (RAW lists to stream),
#      PTT_MODEL_NAME (pixel_track_classifier_noOT.pt), PTT_EPS4 (0.99), PTT_EPS3 (0.95), PTT_HP_FAKE (3.0),
#      PTT_WIDTHS (32,32), PTT_EPOCHS (200), PTT_DEVICE (cuda), SLOT/SLOTDIR/NSLOTS (job slots), X509_USER_PROXY
set -e
D=$(cd $(dirname $0)/.. && pwd)
W=$(readlink -m ${1:?workdir}); mkdir -p $W
export PTT_DIR=$D
C=${PTT_CACHE_ROOT:-$W/cache}
if [ -z "$PTT_CACHE_ROOT" ]; then
  bash $D/scripts/make_cache.sh ttbar ${PTT_LFN_TTBAR:?} $C/ttbar
  bash $D/scripts/make_cache.sh qcd ${PTT_LFN_QCD:?} $C/qcd
fi
for s in ttbar qcd; do bash $D/scripts/make_dump.sh $s $C/$s $W/dump/$s; done
source /cvmfs/cms.cern.ch/cmsset_default.sh >/dev/null 2>&1; export SCRAM_ARCH=el9_amd64_gcc14
pushd $PTT_AREA/src >/dev/null; eval $(scramv1 runtime -sh); popd >/dev/null
N=${PTT_MODEL_NAME:-pixel_track_classifier_noOT.pt}
python3 $D/train.py --dump $W/dump --out $W/train --widths ${PTT_WIDTHS:-32,32} --epochs ${PTT_EPOCHS:-200} --device ${PTT_DEVICE:-cuda} > $W/train.log 2>&1
mkdir -p $W/model
python3 $D/export.py --train $W/train --dump $W/dump --out $W/model/$N > $W/export.log 2>&1
python3 $D/fit_wp.py --train $W/train --dump $W/dump --eps4 ${PTT_EPS4:-0.99} --eps3 ${PTT_EPS3:-0.95} --hp-fake ${PTT_HP_FAKE:-3.0} --out $W/wp.json > $W/fit_wp.log 2>&1
I=$PTT_AREA/src/RecoTracker/FinalTrackSelectors/data/PixelTrackTorchHighPuritySelector; mkdir -p $I; cp $W/model/$N $I/$N
cat > $W/dep1.py <<EOT
import json
exec(open('$D/cfg/deploy_block.py').read())
def ptt_dep1(m):
    wp = json.load(open('$W/wp.json'))
    m.model = cms.FileInPath('RecoTracker/FinalTrackSelectors/data/PixelTrackTorchHighPuritySelector/$N')
    m.inferenceHalf = cms.bool(False)
    m.passThreeHitTracks = cms.bool(False)
    m.wpPtEdges = cms.vdouble(*wp['ptEdges']); m.wpAbsEtaEdges = cms.vdouble(*wp['absEtaEdges'])
    m.wpThresholds = cms.vdouble(*wp['thresholds']); m.wpThresholdsThreeHit = cms.vdouble(*wp['thresholdsThreeHit'])
EOT
for s in ttbar qcd; do PTT_DEP1=$W/dep1.py PTT_FILES_LIMIT=2 bash $D/scripts/make_dump.sh $s $C/$s $W/parity/$s 2; done
python3 $D/parity.py --dump $W/parity --model $W/model/$N --wp $W/wp.json --out $W/parity.json > $W/parity.log 2>&1
cat $W/parity.log
