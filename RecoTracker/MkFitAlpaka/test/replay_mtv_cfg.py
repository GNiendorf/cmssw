# Replay (+ mkFit final fit) with MultiTrackValidator on track collections, sim truth from the RAW parent (harness lane).
#   cmsRun test/replay_mtv_cfg.py sample=ttbar [mtvLabels=hltInitialStepTracks::HLTX,hltInitialStepTracksMkFitFit] \
#          [dqmFile=DQM_replay_ttbar.root]
# then harvest:  test/replay_mtv_harvest.sh DQM_replay_ttbar.root  -> MTV histograms; replay_mtv_numbers.py prints
# efficiency / fake / duplicate rates per collection.
import sys
import FWCore.ParameterSet.Config as cms
from FWCore.ParameterSet.VarParsing import VarParsing
_extra = [a for a in sys.argv if a.startswith('mtvLabels=') or a.startswith('dqmFile=')]
sys.argv = [a for a in sys.argv if a not in _extra] + ['fit=1', 'truth=1']
exec(open(__file__.replace('replay_mtv_cfg.py', 'replay_cfg.py')).read())
_mtv = dict(a.split('=', 1) for a in _extra)
labels = _mtv.get('mtvLabels', 'hltInitialStepTracks::HLTX,hltInitialStepTracksMkFitFit').split(',')
dqmFile = _mtv.get('dqmFile', 'DQM_replay_%s.root' % opts.sample)

process.load('DQMServices.Core.DQMStore_cfi')
from Validation.RecoTrack.HLTmultiTrackValidator_cff import (hltTrackValidator, hltTPClusterProducer,
                                                            hltTrackAssociatorByHits,
                                                            trackingParticleNumberOfLayersProducer)
process.hltTPClusterProducer = hltTPClusterProducer.clone()
process.hltTrackAssociatorByHits = hltTrackAssociatorByHits.clone()
process.trackingParticleNumberOfLayersProducer = trackingParticleNumberOfLayersProducer.clone()
process.hltTrackValidator = hltTrackValidator.clone(label=[cms.InputTag(l) for l in labels])
process.mtvTask = cms.Task(process.hltTPClusterProducer, process.hltTrackAssociatorByHits,
                           process.trackingParticleNumberOfLayersProducer)
process.mtvPath = cms.Path(process.hltTrackValidator, process.mtvTask)
process.DQMoutput = cms.OutputModule('DQMRootOutputModule', fileName=cms.untracked.string('file:' + dqmFile))
process.dqmOutPath = cms.EndPath(process.DQMoutput)
process.schedule = cms.Schedule(process.replayPath, process.mtvPath, process.replayEndPath, process.dqmOutPath)
