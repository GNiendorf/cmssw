# Lane select: K2 vs stock on replay events.
# The stock chain runs with the PRIVATE instrumented MkFitCore of the lane area (env MKFA_SEL_DUMP_DIR=<dir> makes
# selectHitIndicesV2 write one record per candidate, test/select_dump_format.h); MkFitAlpakaSelectStockCheck then runs
# K2 on the same inputs and compares. ONE stream (the dump files are numbered by event order).
#   MKFA_SEL_DUMP_DIR=$PWD/dump_ttbar cmsRun select_stock_cfg.py sample=ttbar maxEvents=10 backend=serial_sync
import os
import FWCore.ParameterSet.Config as cms
from FWCore.ParameterSet.VarParsing import VarParsing
from RecoTracker.MkFitAlpaka.replay_cff import makeReplayProcess, replayFile

options = VarParsing('analysis')
options.register('sample', 'ttbar', VarParsing.multiplicity.singleton, VarParsing.varType.string, 'ttbar or qcd')
options.register('backend', 'serial_sync', VarParsing.multiplicity.singleton, VarParsing.varType.string,
                 'serial_sync or cuda_async (K2 only; the stock chain is CPU)')
options.register('skipEvents', 0, VarParsing.multiplicity.singleton, VarParsing.varType.int, 'events to skip')
options.register('threads', 8, VarParsing.multiplicity.singleton, VarParsing.varType.int, 'threads (1 stream)')
options.setDefault('maxEvents', 10)
options.parseArguments()

dumpDir = os.environ.get('MKFA_SEL_DUMP_DIR', '')
if not dumpDir:
    raise RuntimeError('set MKFA_SEL_DUMP_DIR (the instrumented stock MkFitCore writes there)')

acc = ['gpu-nvidia', 'cpu'] if options.backend.startswith('cuda') else ['cpu']
process = makeReplayProcess([replayFile(options.sample)], maxEvents=options.maxEvents, threads=options.threads,
                            streams=1, accelerators=acc, skipEvents=options.skipEvents, compare=True)
# every portable module of the stock chain stays on the CPU
for name, mod in process.producers_().items():
    if mod.type_().endswith('@alpaka'):
        mod.alpaka = cms.untracked.PSet(backend=cms.untracked.string('serial_sync'))

backendPSet = cms.untracked.PSet(backend=cms.untracked.string(options.backend))
process.mkFitAlpakaESProducer = cms.ESProducer('MkFitAlpakaESProducer@alpaka',
    ComponentName=cms.string(''),
    iterationConfig=cms.ESInputTag('', 'hltInitialStepTrackCandidatesMkFitConfig'),
    alpaka=backendPSet)
process.selectStockCheck = cms.EDProducer('MkFitAlpakaSelectStockCheck@alpaka',
    pixelHits=cms.InputTag('hltMkFitSiPixelHits'),
    stripHits=cms.InputTag('hltMkFitSiPhase2Hits'),
    stockCandidates=cms.InputTag('hltInitialStepTrackCandidatesMkFit'),
    esData=cms.ESInputTag('', ''),
    dumpDir=cms.string(dumpDir),
    maxPrint=cms.int32(20),
    alpaka=backendPSet)
process.replayPath += process.selectStockCheck
