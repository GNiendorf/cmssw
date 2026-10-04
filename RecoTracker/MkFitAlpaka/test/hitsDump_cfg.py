# Stock chain up to hltMkFitEventOfHits + MkFitHitsCompareDumper (input of testMkFitAlpakaHits*).
# cmsRun hitsDump_cfg.py <out.bin> [nEvents] [inputFile] [syntheticDeadsPerLayer] [timeStockRepeats] [keepOneHitIn]   (cwd has hlt.py)
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(sys.argv[0])))  # test/ (for hitsCommon)
import FWCore.ParameterSet.Config as cms
from hitsCommon import stockChainUpToEventOfHits
args = [a for a in sys.argv[1:] if not a.endswith('.py')]
process, path = stockChainUpToEventOfHits()
process.hitsDumpPath = path
process.hitsDumper = cms.EDAnalyzer("MkFitHitsCompareDumper",
    pixelHits = cms.InputTag("hltMkFitSiPixelHits"), stripHits = cms.InputTag("hltMkFitSiPhase2Hits"),
    eventOfHits = cms.InputTag("hltMkFitEventOfHits"), usePixelQualityDB = cms.bool(True),
    fileName = cms.string(args[0] if args else "hits_dump.bin"),
    syntheticDeadsPerLayer = cms.int32(int(args[3]) if len(args) > 3 else 0),
    timeStockRepeats = cms.int32(int(args[4]) if len(args) > 4 else 0),
    keepOneHitIn = cms.int32(int(args[5]) if len(args) > 5 else 0))
process.hitsDumpEnd = cms.EndPath(process.hitsDumper)
process.schedule = cms.Schedule(process.hitsDumpPath, process.hitsDumpEnd)
process.maxEvents.input = int(args[1]) if len(args) > 1 else 10
if len(args) > 2 and args[2]:
    process.source.fileNames = ['file:' + args[2]]
process.options.numberOfThreads = 1 if (len(args) > 4 and int(args[4]) > 0) else 8
