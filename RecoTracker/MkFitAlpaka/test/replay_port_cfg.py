# Port-vs-stock stage comparison in one replay job (harness lane): the stock chain + its per-stage references
# (MkFitAlpakaReplayStockStages) + a lane's port modules + one comparator per compared stage.
#   cmsRun test/replay_port_cfg.py sample=ttbar500 maxEvents=100 accelerators=cpu|gpu-nvidia \
#          portCff=RecoTracker.MkFitAlpaka.<lane>_cff portSeq=<sequence> \
#          compare=bkfit:myBkFit,final:myTracks[:instance] [fit=1 compareFit=myFitTracks] [summaryPrefix=out_]
#   compare: comma list of <stage>:<port module>[:<instance>]; the port product is an mkfitdev::TrackSoA (device or
#            host collection; the framework copies device products to the host) of that stage, in the DUMP
#            CONVENTION of doc/harness.txt R3.3. <stage> is one of replay_cff.STOCK_STAGES ('final' = '').
#            Use <stage>:<module>:<instance>:wrapper for an MkFitOutputWrapper port product instead of TrackSoA.
#   compareFit: port final-fit TrackSoA vs the stock mkFit final fit (hltInitialStepTrackCandidatesMkFitFit).
# Comparator lines: "[compare port_<stage>]"; with summaryPrefix, JSON summaries <prefix><stage>.json for
#   python3 test/replay_floor_check.py <floor log of the same sample> <JSON> --label floorCand
import FWCore.ParameterSet.Config as cms
from FWCore.ParameterSet.VarParsing import VarParsing
from RecoTracker.MkFitAlpaka.replay_cff import (makeReplayProcess, replayFile, addStockStages, addTrackSoACompare,
                                                addMkFitTrackCompare, STOCK_STAGES)

opts = VarParsing('analysis')
opts.register('sample', 'ttbar', VarParsing.multiplicity.singleton, VarParsing.varType.string, 'ttbar, qcd, ttbar500, qcd500')
opts.register('threads', 8, VarParsing.multiplicity.singleton, VarParsing.varType.int, '<= 8')
opts.register('accelerators', 'cpu', VarParsing.multiplicity.singleton, VarParsing.varType.string, 'cpu | gpu-nvidia')
opts.register('portCff', '', VarParsing.multiplicity.singleton, VarParsing.varType.string, 'python module to load')
opts.register('portSeq', '', VarParsing.multiplicity.singleton, VarParsing.varType.string, 'sequence/task to run')
opts.register('compare', '', VarParsing.multiplicity.singleton, VarParsing.varType.string, 'stage:module[:instance]')
opts.register('fit', False, VarParsing.multiplicity.singleton, VarParsing.varType.bool, 'stock mkFit final fit too')
opts.register('compareFit', '', VarParsing.multiplicity.singleton, VarParsing.varType.string, 'module[:instance]')
opts.register('summaryPrefix', '', VarParsing.multiplicity.singleton, VarParsing.varType.string, '')
opts.register('requireIdentical', False, VarParsing.multiplicity.singleton, VarParsing.varType.bool,
              'comparators throw unless bit-identical (unit tests)')
opts.register('maxPrint', 3, VarParsing.multiplicity.singleton, VarParsing.varType.int, '')
opts.setDefault('maxEvents', -1)
opts.parseArguments()

inputs = opts.inputFiles if opts.inputFiles else [replayFile(opts.sample)]
process = makeReplayProcess(inputs, maxEvents=opts.maxEvents, threads=opts.threads, compare=False,
                            accelerators=opts.accelerators.split(','), fit=opts.fit)
addStockStages(process)
if opts.portCff:
    process.load(opts.portCff)
    if opts.portSeq:
        seq = getattr(process, opts.portSeq)
        if isinstance(seq, cms.Task):
            process.replayPath.associate(seq)
        else:
            process.replayPath += seq


def _cmpKw(stage):
    return dict(maxPrint=opts.maxPrint, requireIdentical=opts.requireIdentical, summaryFile=(opts.summaryPrefix + stage + '.json') if opts.summaryPrefix else '')


for item in [c for c in opts.compare.split(',') if c]:
    f = item.split(':')
    stage = '' if f[0] == 'final' else f[0]
    if stage not in STOCK_STAGES:
        raise RuntimeError('unknown stage %s (one of %s, final)' % (f[0], STOCK_STAGES))
    tgt = cms.InputTag(f[1], f[2] if len(f) > 2 else '')
    ref = cms.InputTag('stockStages', stage)
    name = 'port_' + (f[0])
    if len(f) > 3 and f[3] == 'wrapper':
        addMkFitTrackCompare(process, name, reference=ref, target=tgt, **_cmpKw(f[0]))
    else:
        addTrackSoACompare(process, name, reference=ref, target=tgt, **_cmpKw(f[0]))
if opts.compareFit:
    if not opts.fit:
        raise RuntimeError('compareFit needs fit=1')
    f = opts.compareFit.split(':')
    addTrackSoACompare(process, 'port_fit', reference='hltInitialStepTrackCandidatesMkFitFit',
                       target=cms.InputTag(f[0], f[1] if len(f) > 1 else ''), **_cmpKw('fit'))
