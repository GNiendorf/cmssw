# Replay with the mkFit final fit (trackingMkFitFit flavour) + track comparisons (harness lane).
#   cmsRun test/replay_fit_cfg.py sample=ttbar
# compares (a) replayed candidates vs the HLT's (fidelity), (b) HLT KF tracks (hltInitialStepTracks, process HLTX)
# vs the replayed mkFit-fit tracks (hltInitialStepTracksMkFitFit): two different fits of the same candidates.
import FWCore.ParameterSet.Config as cms
import sys
from RecoTracker.MkFitAlpaka.replay_cff import addTrackCompare
sys.argv = [a for a in sys.argv] + ['fit=1']
exec(open(__file__.replace('replay_fit_cfg.py', 'replay_cfg.py')).read())
addTrackCompare(process, 'compareTracksKFvsMkFitFit', reference='hltInitialStepTracks::HLTX',
                target='hltInitialStepTracksMkFitFit::REPLAY', paramTolerance=1e-3, maxPrint=2)
