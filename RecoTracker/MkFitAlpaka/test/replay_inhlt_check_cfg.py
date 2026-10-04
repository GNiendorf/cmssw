# In-memory check of the replay helper modules inside a full HLT job (harness lane): runs the HLT from RAW and a second
# mkFit chain fed by MkFitAlpakaReplayPixelRecHits (pixel rechits from the SoA with the recovered cluster index;
# compared with SiPixelCluster::originalId(), which is valid in memory), the menu's Phase2TrackerRecHits and
# MkFitAlpakaReplaySeeds; then compares its candidates with the HLT's (expect identical).
#   HLTCFG=<hlt.py made by lane_harness/scripts/make_replay.sh (run/<sample>/hlt.py)> NEV=20 cmsRun test/replay_inhlt_check_cfg.py
# Result 2026-10-03 (ttbar, 20 ev): originalId checked 3610066 wrong 0; 35351/35351 candidates bit-identical.
import os
import FWCore.ParameterSet.Config as cms
exec(open(os.environ['HLTCFG']).read())
process.maxEvents.input = int(os.environ.get('NEV', '20'))
for _p in ['DQMoutput_step', 'replayOutPath']:
    if hasattr(process, _p):
        process.schedule.remove(getattr(process, _p))
for _n in ['DQMoutput', 'replayOut']:
    if hasattr(process, _n):
        delattr(process, _n)
process.cpPix = cms.EDProducer('MkFitAlpakaReplayPixelRecHits', pixelRecHitSrc=cms.InputTag('hltPhase2SiPixelRecHitsSoA'),
                               src=cms.InputTag('hltSiPixelClusters'), checkOriginalId=cms.bool(True))
process.cpOT = process.hltSiPhase2RecHits.clone()
process.cpSeeds = cms.EDProducer('MkFitAlpakaReplaySeeds', src=cms.InputTag('hltInitialStepTrajectorySeedsLST'))
process.cpMkPix = process.hltMkFitSiPixelHits.clone(hits='cpPix')
process.cpMkOT = process.hltMkFitSiPhase2Hits.clone(hits='cpOT')
process.cpEoH = process.hltMkFitEventOfHits.clone(pixelHits='cpMkPix', stripHits='cpMkOT')
process.cpMkSeeds = process.hltInitialStepMkFitSeeds.clone(seeds='cpSeeds')
process.cpMkCands = process.hltInitialStepTrackCandidatesMkFit.clone(pixelHits='cpMkPix', stripHits='cpMkOT',
                                                                     eventOfHits='cpEoH', seeds='cpMkSeeds')
process.cpCands = process.hltInitialStepTrackCandidates.clone(mkFitEventOfHits='cpEoH', mkFitPixelHits='cpMkPix',
                                                              mkFitStripHits='cpMkOT', mkFitSeeds='cpMkSeeds',
                                                              seeds='cpSeeds', tracks='cpMkCands')
process.cpTask = cms.Task(process.cpPix, process.cpOT, process.cpSeeds, process.cpMkPix, process.cpMkOT, process.cpEoH,
                          process.cpMkSeeds, process.cpMkCands, process.cpCands)
process.cmp = cms.EDAnalyzer('MkFitAlpakaCandidateCompare', reference=cms.InputTag('hltInitialStepTrackCandidates'),
                             target=cms.InputTag('cpCands'), label=cms.string('inHLT'), paramTolerance=cms.double(1e-3),
                             validHitsOnly=cms.bool(False), seedKeyOnly=cms.bool(True), maxPrint=cms.int32(3),
                             printEvents=cms.bool(False), summaryFile=cms.string(''), requireIdentical=cms.bool(False))
process.cmpPath = cms.EndPath(process.cmp, process.cpTask)
process.schedule.append(process.cmpPath)
