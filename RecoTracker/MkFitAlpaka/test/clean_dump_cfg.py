# Lane clean: run the stock LST-step mkFit three ways on HLT events (CPU menu of CMSSW_20_1_0_pre2, = pre2_4way v1cur dump)
#   production (stock JSON), no duplicate cleaning (test/clean_lstStep_noDC.json), no cleaning and no quality filters
#   (test/clean_lstStep_noDCnoFilt.json), and dump tracks + STOCK cleaner/filter decisions with MkFitCleanDump.
# Usage: cmsRun clean_dump_cfg.py [maxEvents=N] [skip=K] [out=FILE] [input=FILE]
import sys
import FWCore.ParameterSet.Config as cms
_args = dict(a.split('=', 1) for a in sys.argv[1:] if '=' in a)
exec(open('/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/mem_ref/lanes/pre2_4way/cfg/dump_v1cur.py').read())

process.maxEvents.input = int(_args.get('maxEvents', 10))
process.source.skipEvents = cms.untracked.uint32(int(_args.get('skip', 0)))
if 'input' in _args:
    process.source.fileNames = cms.untracked.vstring('file:' + _args['input'])
process.options.numberOfThreads = 8
process.options.numberOfStreams = 0
process.options.wantSummary = False
process.MessageLogger.cerr.FwkReport.reportEvery = 1

process.cleanNoDCConfig = process.hltInitialStepTrackCandidatesMkFitConfig.clone(
    ComponentName = 'cleanNoDCConfig',
    config = cms.FileInPath('RecoTracker/MkFitAlpaka/test/clean_lstStep_noDC.json'))
process.cleanNoFiltConfig = process.hltInitialStepTrackCandidatesMkFitConfig.clone(
    ComponentName = 'cleanNoFiltConfig',
    config = cms.FileInPath('RecoTracker/MkFitAlpaka/test/clean_lstStep_noDCnoFilt.json'))
process.cleanMkFitNoDC = process.hltInitialStepTrackCandidatesMkFit.clone(config = cms.ESInputTag('', 'cleanNoDCConfig'))
process.cleanMkFitNoFilt = process.hltInitialStepTrackCandidatesMkFit.clone(config = cms.ESInputTag('', 'cleanNoFiltConfig'))
process.cleanDump = cms.EDAnalyzer('MkFitCleanDump',
    stockTracks = cms.InputTag('hltInitialStepTrackCandidatesMkFit'),
    noDCTracks = cms.InputTag('cleanMkFitNoDC'),
    noFiltTracks = cms.InputTag('cleanMkFitNoFilt'),
    eventOfHits = cms.InputTag('hltMkFitEventOfHits'),
    config = cms.ESInputTag('', 'hltInitialStepTrackCandidatesMkFitConfig'),
    fileName = cms.string(_args.get('out', 'clean_dump.bin')))

process.cleanPath = cms.Path(process.HLTBeginSequence + process.HLTItLocalRecoSequence + process.HLTOtLocalRecoSequence +
                             process.hltTrackerClusterCheck + process.HLTPhase2PixelTracksAndVerticesSequence +
                             process.hltInitialStepSeeds + process.hltInputLST + process.hltLST +
                             process.hltInitialStepTrajectorySeedsLST + process.HLTMkFitInputSequence +
                             process.hltInitialStepMkFitSeeds + process.hltInitialStepTrackCandidatesMkFit +
                             process.cleanMkFitNoDC + process.cleanMkFitNoFilt + process.cleanDump)
process.schedule = cms.Schedule(process.cleanPath)
