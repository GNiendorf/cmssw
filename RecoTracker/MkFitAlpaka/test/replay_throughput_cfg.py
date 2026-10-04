# Throughput benchmark of the LST-step mkFit chain on replay events (harness lane). Use test/replay_throughput.sh.
#   mode=inputs : only what every mode needs (replay rechits, seed copy, beam spot, [fit] measurement tracker event)
#   mode=stock  : inputs + the stock chain (hit converters, EventOfHits, seed converter, MkFitProducer, output
#                 converter, [fit=1] MkFitFitProducer + MkFitOutputTrackConverter)
#   mode=port   : inputs + the port chain: the sequence <portSeq> of the cff <portCff> (a lane's python fragment that
#                 defines its modules; e.g. portCff=RecoTracker.MkFitAlpaka.mkFitAlpakaChain_cff portSeq=mkFitAlpakaChain)
# Events: RepeatingCachedRootSource keeps <cache> events in memory and repeats them (no I/O in the measurement).
#   cmsRun test/replay_throughput_cfg.py mode=stock threads=8 streams=8 maxEvents=2000 cache=50 accelerators=cpu
# ThroughputService prints the event rate; FastTimerService writes per-module times to timingJson.
import importlib
import FWCore.ParameterSet.Config as cms
from FWCore.ParameterSet.VarParsing import VarParsing
from RecoTracker.MkFitAlpaka.replay_cff import makeReplayProcess, addMkFitFit, replayFile, STOCK_CHAIN

opts = VarParsing('analysis')
opts.register('mode', 'stock', VarParsing.multiplicity.singleton, VarParsing.varType.string, 'inputs | stock | port')
opts.register('sample', 'ttbar', VarParsing.multiplicity.singleton, VarParsing.varType.string, '')
opts.register('threads', 8, VarParsing.multiplicity.singleton, VarParsing.varType.int, '<= 8')
opts.register('streams', 0, VarParsing.multiplicity.singleton, VarParsing.varType.int, '0 = threads')
opts.register('cache', 50, VarParsing.multiplicity.singleton, VarParsing.varType.int, 'events kept in memory')
opts.register('skip', 0, VarParsing.multiplicity.singleton, VarParsing.varType.int, 'first event of the cache')
opts.register('fit', False, VarParsing.multiplicity.singleton, VarParsing.varType.bool, 'include the final fit')
opts.register('accelerators', 'cpu', VarParsing.multiplicity.singleton, VarParsing.varType.string, 'cpu | gpu-nvidia')
opts.register('portCff', '', VarParsing.multiplicity.singleton, VarParsing.varType.string, 'python module (port)')
opts.register('portSeq', 'mkFitAlpakaChain', VarParsing.multiplicity.singleton, VarParsing.varType.string, '')
opts.register('timingJson', 'throughput_timing.json', VarParsing.multiplicity.singleton, VarParsing.varType.string, '')
opts.setDefault('maxEvents', 1000)
opts.parseArguments()

inputs = opts.inputFiles if opts.inputFiles else [replayFile(opts.sample)]
process = makeReplayProcess(inputs, maxEvents=opts.maxEvents, threads=opts.threads, streams=opts.streams,
                            accelerators=opts.accelerators.split(','), fit=False, compare=False)
process.source = cms.Source('RepeatingCachedRootSource', fileName=cms.untracked.string(inputs[0]),
                            repeatNEvents=cms.untracked.uint32(opts.cache),
                            skipEvents=cms.untracked.uint32(opts.skip))
process.MessageLogger.cerr.FwkReport.reportEvery = 1000

from RecoVertex.BeamSpotProducer.BeamSpot_cfi import offlineBeamSpot
process.offlineBeamSpot = offlineBeamSpot.clone()
seq = process.hltSiPixelRecHits + process.hltSiPhase2RecHits + process.hltInitialStepTrajectorySeedsLST + \
    process.offlineBeamSpot
if opts.fit:
    seq += process.hltMeasurementTrackerEvent
if opts.mode == 'stock':
    for name in STOCK_CHAIN:
        seq += getattr(process, name)
    if opts.fit:
        from HLTrigger.Configuration.HLT_75e33.modules.hltInitialStepTrackCandidatesMkFitFit_cfi import \
            hltInitialStepTrackCandidatesMkFitFit
        from HLTrigger.Configuration.HLT_75e33.modules.hltInitialStepTracks_cfi import _hltInitialStepTracksMkFitFit
        process.hltInitialStepTrackCandidatesMkFitFit = hltInitialStepTrackCandidatesMkFitFit.clone()
        process.hltInitialStepTracksMkFitFit = _hltInitialStepTracksMkFitFit.clone()
        seq += process.hltInitialStepTrackCandidatesMkFitFit + process.hltInitialStepTracksMkFitFit
elif opts.mode == 'port':
    if not opts.portCff:
        raise RuntimeError('mode=port needs portCff=<python module> (and portSeq)')
    process.load(opts.portCff)
    seq += getattr(process, opts.portSeq)
elif opts.mode != 'inputs':
    raise RuntimeError('mode: inputs | stock | port')
process.replayPath = cms.Path(seq)
process.replayEndPath = cms.EndPath()
process.schedule = cms.Schedule(process.replayPath)

process.FastTimerService = cms.Service('FastTimerService', printEventSummary=cms.untracked.bool(False),
                                       printRunSummary=cms.untracked.bool(False),
                                       printJobSummary=cms.untracked.bool(True), enableDQM=cms.untracked.bool(False),
                                       writeJSONSummary=cms.untracked.bool(True),
                                       jsonFileName=cms.untracked.string(opts.timingJson))
process.ThroughputService = cms.Service('ThroughputService', eventRange=cms.untracked.uint32(100000),
                                        eventResolution=cms.untracked.uint32(100),
                                        printEventSummary=cms.untracked.bool(True), enableDQM=cms.untracked.bool(False))
# ThroughputService reports through LogInfo: let that category through (and nothing else at INFO)
process.MessageLogger.cerr.threshold = 'INFO'
process.MessageLogger.cerr.INFO = cms.untracked.PSet(limit=cms.untracked.int32(0))
process.MessageLogger.cerr.ThroughputService = cms.untracked.PSet(limit=cms.untracked.int32(10000000))
