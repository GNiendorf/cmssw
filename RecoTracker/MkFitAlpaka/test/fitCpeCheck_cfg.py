# Per-hit check of the portable track-angle PixelCPEGeneric position against a stock CPE-call dump (lane fit):
#   cmsRun test/fitCpeCheck_cfg.py dumpFile=<MKFIT_FIT_CPE_DUMP output of a fitCompare_cfg.py job>
import FWCore.ParameterSet.Config as cms
from FWCore.ParameterSet.VarParsing import VarParsing
from RecoTracker.MkFitAlpaka.replay_cff import makeReplayProcess, replayFile

opts = VarParsing('analysis')
opts.register('dumpFile', '', VarParsing.multiplicity.singleton, VarParsing.varType.string, 'CPE call dump')
opts.register('maxPrint', 5, VarParsing.multiplicity.singleton, VarParsing.varType.int, '')
opts.parseArguments()

process = makeReplayProcess([replayFile('ttbar')], maxEvents=1, threads=1, compare=False)
process.cpeCheck = cms.EDProducer('MkFitAlpakaFitCpeCheck@alpaka', dumpFile=cms.string(opts.dumpFile),
                                  cpeFastParams=cms.string('PixelCPEFastParamsPhase2'), maxPrint=cms.int32(opts.maxPrint))
process.replayPath = cms.Path(process.cpeCheck)
process.schedule = cms.Schedule(process.replayPath)
