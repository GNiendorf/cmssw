# MTV of the mkFit final-fit variants on replay events (lane fit), sim truth from the RAW parent:
#   cmsRun test/fitMtv_cfg.py sample=ttbar dqmFile=DQM_fit_ttbar.root [backend=serial_sync|cuda_async]
#   test/replay_mtv_harvest.sh harv.root DQM_fit_ttbar.root; python3 test/replay_mtv_numbers.py harv.root
# Collections: hltInitialStepTracksMkFitFit (stock fit, PixelCPEGeneric), stockTracksNoCPE (stock fit, CPE off,
# private copy), devTracks (device fit WITH the device CPE, round 4), devTracksNoCPE (device no-CPE fit).
# All through the stock MkFitOutputTrackConverter.
import FWCore.ParameterSet.Config as cms
from FWCore.ParameterSet.VarParsing import VarParsing
from RecoTracker.MkFitAlpaka.replay_cff import makeReplayProcess, replayFile, RAW_FILES, setAlpakaBackend

opts = VarParsing('analysis')
opts.register('sample', 'ttbar', VarParsing.multiplicity.singleton, VarParsing.varType.string, 'ttbar or qcd')
opts.register('threads', 8, VarParsing.multiplicity.singleton, VarParsing.varType.int, 'threads (<= 8)')
opts.register('backend', 'serial_sync', VarParsing.multiplicity.singleton, VarParsing.varType.string, '')
opts.register('dqmFile', 'DQM_fit.root', VarParsing.multiplicity.singleton, VarParsing.varType.string, '')
opts.setDefault('maxEvents', -1)
opts.parseArguments()

acc = ['cpu'] if opts.backend == 'serial_sync' else ['gpu-nvidia', 'cpu']
process = makeReplayProcess([replayFile(opts.sample)], maxEvents=opts.maxEvents, threads=opts.threads,
                            accelerators=acc, fit=True, compare=False, rawFiles=RAW_FILES.get(opts.sample))
menu = process.hltInitialStepTrackCandidatesMkFitFit
process.stockFitNoCPE = menu.clone(disableCPE=cms.untracked.bool(True))
process.mkFitAlpakaESProducer = cms.ESProducer('MkFitAlpakaESProducer@alpaka', ComponentName=cms.string(''),
    iterationConfig=cms.ESInputTag('', 'hltInitialStepTrackCandidatesMkFitConfig'))
# device CPE tables of the fit (ES product, round 5 R4-H2; default ComponentName MkFitAlpakaFitCpe)
process.mkFitAlpakaFitCpeESProducer = cms.ESProducer('MkFitAlpakaFitCpeESProducer@alpaka')
process.devFit = cms.EDProducer('MkFitAlpakaFitProducer@alpaka', tracks=menu.tracks,
    pixelHits=cms.InputTag('hltMkFitSiPixelHits'), stripHits=cms.InputTag('hltMkFitSiPhase2Hits'),
    esData=cms.ESInputTag('', ''), candCutSel=menu.candCutSel, candMinPtCut=menu.candMinPtCut,
    candMinNHitsCut=menu.candMinNHitsCut, candMinPtRelaxedCut=menu.candMinPtRelaxedCut,
    candMinAbsEtaForRelaxedCut=menu.candMinAbsEtaForRelaxedCut, cpe=cms.bool(True))
process.devFitNoCPE = process.devFit.clone(cpe=False)
setAlpakaBackend(process.devFit, opts.backend)
setAlpakaBackend(process.devFitNoCPE, opts.backend)
setAlpakaBackend(process.mkFitAlpakaESProducer, opts.backend)
process.stockTracksNoCPE = process.hltInitialStepTracksMkFitFit.clone(src='stockFitNoCPE')
process.devTracks = process.hltInitialStepTracksMkFitFit.clone(src='devFit')
process.devTracksNoCPE = process.hltInitialStepTracksMkFitFit.clone(src='devFitNoCPE')
for m in ('stockFitNoCPE', 'devFit', 'devFitNoCPE', 'stockTracksNoCPE', 'devTracks', 'devTracksNoCPE'):
    process.replayPath += getattr(process, m)

labels = ['hltInitialStepTracksMkFitFit', 'stockTracksNoCPE', 'devTracks', 'devTracksNoCPE']
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
process.DQMoutput = cms.OutputModule('DQMRootOutputModule', fileName=cms.untracked.string('file:' + opts.dqmFile))
process.dqmOutPath = cms.EndPath(process.DQMoutput)
process.schedule = cms.Schedule(process.replayPath, process.mtvPath, process.replayEndPath, process.dqmOutPath)
