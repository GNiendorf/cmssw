# Stock chain up to hltMkFitEventOfHits + MkFitAlpakaEventOfHitsProducer validating against it (one EOH_COMPARE line
# per event). cmsRun hitsEventOfHits_cfg.py <serial_sync|cuda_async> [nEvents] [inputFile]   (cwd has hlt.py)
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(sys.argv[0])))  # test/ (for hitsCommon)
import FWCore.ParameterSet.Config as cms
from hitsCommon import stockChainUpToEventOfHits
args = [a for a in sys.argv[1:] if not a.endswith('.py')]
backend = args[0] if args else 'serial_sync'
process, path = stockChainUpToEventOfHits()
process.eohPath = path
# every other portable module of the stock chain stays on the CPU; only the new producer runs on the requested backend
for name, mod in process.producers_().items():
    if mod.type_().endswith('@alpaka'):
        mod.alpaka = cms.untracked.PSet(backend = cms.untracked.string('serial_sync'))
process.eohAlpaka = cms.EDProducer("MkFitAlpakaEventOfHitsProducer@alpaka",
    pixelHits = cms.InputTag("hltMkFitSiPixelHits"), stripHits = cms.InputTag("hltMkFitSiPhase2Hits"),
    usePixelQualityDB = cms.bool(True), compareTo = cms.InputTag("hltMkFitEventOfHits"),
    alpaka = cms.untracked.PSet(backend = cms.untracked.string(backend)))
process.eohPath += process.eohAlpaka
process.schedule = cms.Schedule(process.eohPath)
process.options.accelerators = ['gpu-nvidia', 'cpu'] if backend.startswith('cuda') else ['cpu']
process.maxEvents.input = int(args[1]) if len(args) > 1 else 10
if len(args) > 2 and args[2]:
    process.source.fileNames = ['file:' + args[2]]
process.options.numberOfThreads = 8
