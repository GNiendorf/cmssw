# Per-stage dumps of the stock clone engine (harness lane): stockStages snapshots -> flat binary files
# <prefix><stage>.bin (plugins/ReplayStageDump.cc). Run once with the production libraries and once with the
# x86-64-v2 mkFit libraries (test/replay_stage_floor.sh does both) and compare with test/replay_stage_compare.py:
# that is the per-stage D-M4 floor. A port dumps its stage products with MkFitAlpakaReplayStageDumpSoA and is
# compared with the v3 dumps the same way.
#   cmsRun test/replay_stage_floor_cfg.py sample=ttbar500 maxEvents=100 prefix=/path/v3_
import FWCore.ParameterSet.Config as cms
from FWCore.ParameterSet.VarParsing import VarParsing
from RecoTracker.MkFitAlpaka.replay_cff import (makeReplayProcess, replayFile, addStockStages, addMkFitTrackCompare,
                                                STOCK_STAGES)

opts = VarParsing('analysis')
opts.register('sample', 'ttbar', VarParsing.multiplicity.singleton, VarParsing.varType.string, '')
opts.register('threads', 8, VarParsing.multiplicity.singleton, VarParsing.varType.int, '<= 8')
opts.register('prefix', 'stage_', VarParsing.multiplicity.singleton, VarParsing.varType.string, 'output prefix')
opts.setDefault('maxEvents', 100)
opts.parseArguments()

inputs = opts.inputFiles if opts.inputFiles else [replayFile(opts.sample)]
process = makeReplayProcess(inputs, maxEvents=opts.maxEvents, threads=opts.threads, compare=False)
addStockStages(process)
addMkFitTrackCompare(process, 'stagesSelfCheck', reference='hltInitialStepTrackCandidatesMkFit', target='stockStages',
                     maxPrint=1)
process.stageDump = cms.EDAnalyzer('MkFitAlpakaReplayStageDump',
                                   src=cms.VInputTag(*[cms.InputTag('stockStages', s) for s in STOCK_STAGES]),
                                   files=cms.vstring(*[opts.prefix + (s if s else 'final') + '.bin' for s in STOCK_STAGES]))
process.replayEndPath += process.stageDump
