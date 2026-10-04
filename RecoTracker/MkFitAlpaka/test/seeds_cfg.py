# Seeds lane (round 3) replay test: both ends of the device chain against STOCK, serial or cuda.
#  1. seed import: MkFitAlpakaSeedImportProducer validates the device seed table / import order / initial candidate
#     rows / best-cand export against stock MkBuilder import in the same job (SEED_COMPARE lines).
#  2. export path: stock TrackVec before the duplicate cleaner (stock MkFitProducer, removeDuplicates=False) ->
#     TrackSoA -> device post filter + duplicate cleaner (TrackSoA device product) -> framework copy to host ->
#     host MkFitAlpakaOutputWrapperFromTrackSoA -> MkFitOutputWrapper -> STOCK MkFitOutputConverter;
#     compared with the stock chain's hltInitialStepTrackCandidates (TrackCandidates) and with its mkFit tracks.
#  cmsRun RecoTracker/MkFitAlpaka/test/seeds_cfg.py sample=ttbar accelerators=cpu|gpu-nvidia maxEvents=N
import FWCore.ParameterSet.Config as cms
from FWCore.ParameterSet.VarParsing import VarParsing
from RecoTracker.MkFitAlpaka.replay_cff import (makeReplayProcess, replayFile, addCandidateCompare,
                                                addMkFitTrackCompare, addTiming)

opts = VarParsing('analysis')
opts.register('sample', 'ttbar', VarParsing.multiplicity.singleton, VarParsing.varType.string, 'ttbar or qcd')
opts.register('threads', 8, VarParsing.multiplicity.singleton, VarParsing.varType.int, 'threads (<= 8)')
opts.register('accelerators', 'cpu', VarParsing.multiplicity.singleton, VarParsing.varType.string, 'cpu, gpu-nvidia')
opts.register('validate', True, VarParsing.multiplicity.singleton, VarParsing.varType.bool, 'seed import check')
opts.register('injectSillyEvery', 0, VarParsing.multiplicity.singleton, VarParsing.varType.int, 'test: every k-th seed silly')
opts.register('hits', False, VarParsing.multiplicity.singleton, VarParsing.varType.bool, 'also run the device EventOfHits check')
opts.register('timingJson', '', VarParsing.multiplicity.singleton, VarParsing.varType.string, 'FastTimerService JSON')
opts.register('summaryFile', '', VarParsing.multiplicity.singleton, VarParsing.varType.string, 'comparator JSON')
opts.setDefault('maxEvents', -1)
opts.parseArguments()

process = makeReplayProcess([replayFile(opts.sample)], maxEvents=opts.maxEvents, threads=opts.threads,
                            accelerators=opts.accelerators.split(','), compare=False)

# 1. seed import
process.seedsImport = cms.EDProducer('MkFitAlpakaSeedImportProducer@alpaka', validate=cms.bool(opts.validate),
                                     injectSillyEvery=cms.int32(opts.injectSillyEvery),
                                     runTail=cms.bool(opts.validate))
process.replayPath += process.seedsImport

# 2. export path
process.seedsMkFitNoDC = process.hltInitialStepTrackCandidatesMkFit.clone(removeDuplicates=False)
process.seedsTail = cms.EDProducer('MkFitAlpakaSeedsTailProducer@alpaka', tracks=cms.InputTag('seedsMkFitNoDC'))
process.seedsOutputWrapper = cms.EDProducer('MkFitAlpakaOutputWrapperFromTrackSoA', tracks=cms.InputTag('seedsTail'))
process.seedsTrackCandidates = process.hltInitialStepTrackCandidates.clone(tracks='seedsOutputWrapper')
process.replayPath += process.seedsMkFitNoDC
process.replayPath += process.seedsTail
process.replayPath += process.seedsOutputWrapper
process.replayPath += process.seedsTrackCandidates
addMkFitTrackCompare(process, 'cmpSeedsMkFit', 'hltInitialStepTrackCandidatesMkFit::REPLAY', 'seedsOutputWrapper',
                     paramTolerance=0.0, summaryFile=opts.summaryFile.replace('.json', '_mkfit.json') if opts.summaryFile else '')
addCandidateCompare(process, 'cmpSeedsCands', 'hltInitialStepTrackCandidates::REPLAY', 'seedsTrackCandidates',
                    paramTolerance=0.0, summaryFile=opts.summaryFile)

# 3. (optional) device EventOfHits with the in-job stock check (EOH_COMPARE lines), e.g. after hits-build changes
if opts.hits:
    process.seedsEventOfHits = cms.EDProducer('MkFitAlpakaEventOfHitsProducer@alpaka',
                                              compareTo=cms.InputTag('hltMkFitEventOfHits' if opts.validate else ''))
    process.replayPath.associate(cms.Task(process.seedsEventOfHits))  # unscheduled: runs when seedsImport asks
    # static layer table from the es lane's ESData (host product of the portable ES producer)
    process.seedsESData = cms.ESProducer('MkFitAlpakaESProducer@alpaka', ComponentName=cms.string('seedsES'),
                                         iterationConfig=cms.ESInputTag('', 'hltInitialStepTrackCandidatesMkFitConfig'))
    process.seedsEventOfHits.useESLayers = cms.bool(True)
    process.seedsEventOfHits.esData = cms.ESInputTag('', 'seedsES')
    process.seedsImport.deviceEventOfHits = cms.InputTag('seedsEventOfHits')

if opts.timingJson:
    addTiming(process, opts.timingJson)
