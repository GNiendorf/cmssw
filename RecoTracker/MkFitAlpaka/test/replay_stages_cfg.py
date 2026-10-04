# Per-stage stock references (harness lane): MkFitAlpakaReplayStockStages snapshots the stock clone engine after each
# stage (seeds, loaded, fwd, fwdFiltered, bkfit, bkwsearch, postFilter, export, final). Self-check: its final output
# must equal the menu's MkFitProducer output (hltInitialStepTrackCandidatesMkFit) bit for bit.
#   cmsRun test/replay_stages_cfg.py sample=ttbar maxEvents=20 [requireIdentical=1]
# A port stage X is compared with:
#   addMkFitTrackCompare(process, 'cmpX', reference=cms.InputTag('stockStages', 'X'), target='<port MkFitOutputWrapper>')
import FWCore.ParameterSet.Config as cms
from FWCore.ParameterSet.VarParsing import VarParsing
from RecoTracker.MkFitAlpaka.replay_cff import makeReplayProcess, replayFile, addStockStages, addMkFitTrackCompare

opts = VarParsing('analysis')
opts.register('sample', 'ttbar', VarParsing.multiplicity.singleton, VarParsing.varType.string, 'ttbar or qcd')
opts.register('threads', 8, VarParsing.multiplicity.singleton, VarParsing.varType.int, 'threads (<= 8)')
opts.register('accelerators', 'cpu', VarParsing.multiplicity.singleton, VarParsing.varType.string, 'cpu | gpu-nvidia')
opts.register('requireIdentical', False, VarParsing.multiplicity.singleton, VarParsing.varType.bool, '')
opts.setDefault('maxEvents', -1)
opts.parseArguments()

inputs = opts.inputFiles if opts.inputFiles else [replayFile(opts.sample)]
process = makeReplayProcess(inputs, maxEvents=opts.maxEvents, threads=opts.threads, compare=False,
                            accelerators=opts.accelerators.split(','))
addStockStages(process)
addMkFitTrackCompare(process, 'stagesSelfCheck', reference='hltInitialStepTrackCandidatesMkFit', target='stockStages',
                     requireIdentical=opts.requireIdentical, maxPrint=3)

# TrackSoA path check: every stage snapshot as mkfitdev::TrackSoAHostCollection (MkFitAlpakaReplayStockToTrackSoA),
# compared back with MkFitAlpakaTrackSoACompare: validates the comparator a port's TrackSoA product goes through.
import FWCore.ParameterSet.Config as _cms
from RecoTracker.MkFitAlpaka.replay_cff import STOCK_STAGES, addTrackSoACompare
for _st in STOCK_STAGES:
    _n = 'stockSoA' + (_st[0].upper() + _st[1:] if _st else 'Final')
    setattr(process, _n, _cms.EDProducer('MkFitAlpakaReplayStockToTrackSoA', src=_cms.InputTag('stockStages', _st)))
    process.replayPath += getattr(process, _n)
    addTrackSoACompare(process, 'soaCheck' + _n[8:], reference=_cms.InputTag('stockStages', _st), target=_n,
                       requireIdentical=opts.requireIdentical, maxPrint=1)
