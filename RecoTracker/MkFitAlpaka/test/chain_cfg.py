# Integration (round 3): the device mkFit chain of the LST step on replay events, compared with stock in the same job.
#   device EventOfHits (MkFitAlpakaEventOfHitsProducer, ES layer table) + ES product (MkFitAlpakaESProducer)
#   -> MkFitAlpakaChainProducer (seed import -> forward search -> backward fit -> backward search -> filters ->
#      export -> duplicate cleaner; TrackSoA products '' and 'export')
#   -> MkFitAlpakaOutputWrapperFromTrackSoA -> STOCK MkFitOutputConverter (hltInitialStepTrackCandidates clone).
# Comparisons (per seed, harness comparators, JSON summaries for test/replay_floor_check.py):
#   cmpChainExport  TrackSoA 'export' vs stockStages:export      (stage floor: r3_harness/stagefloor/floor_<s>_export.json)
#   cmpChainFinal   TrackSoA final    vs stockStages:''           (stage floor: floor_<s>_final.json)
#   cmpChainMkFit   MkFitOutputWrapper vs hltInitialStepTrackCandidatesMkFit
#   cmpChainCands   TrackCandidates   vs hltInitialStepTrackCandidates (floor: r3_harness/floor500/tgt_mkfit_<s>_cand.json)
#   fit=1:    cmpChainFitVsStockNoCPE (mkFit level) / cmpChainTracksVsStockNoCPE (reco::Track; floor500/tgt_mkfit_<s>_fit.json)
#   stages=1: cmpChainFwd / cmpChainBkFit  front candidate after the forward search / backward fit vs stockStages:fwd|bkfit
# cmsRun RecoTracker/MkFitAlpaka/test/chain_cfg.py sample=ttbar500 accelerators=cpu|gpu-nvidia maxEvents=N summaryDir=D
import FWCore.ParameterSet.Config as cms
from FWCore.ParameterSet.VarParsing import VarParsing
from RecoTracker.MkFitAlpaka.replay_cff import (makeReplayProcess, replayFile, addCandidateCompare,
                                                addMkFitTrackCompare, addStockStages, addTrackSoACompare, addTiming,
                                                addTrackCompare, RAW_FILES)

opts = VarParsing('analysis')
opts.register('sample', 'ttbar500', VarParsing.multiplicity.singleton, VarParsing.varType.string, 'replay sample')
opts.register('threads', 8, VarParsing.multiplicity.singleton, VarParsing.varType.int, 'threads')
opts.register('accelerators', 'cpu', VarParsing.multiplicity.singleton, VarParsing.varType.string, 'cpu, gpu-nvidia')
opts.register('summaryDir', '', VarParsing.multiplicity.singleton, VarParsing.varType.string, 'comparator JSON dir')
opts.register('prefix', 'chain', VarParsing.multiplicity.singleton, VarParsing.varType.string, 'JSON file prefix')
opts.register('backwardFit', True, VarParsing.multiplicity.singleton, VarParsing.varType.bool, 'device backward fit')
opts.register('stages', False, VarParsing.multiplicity.singleton, VarParsing.varType.bool,
              'also compare the front candidate after the forward search and after the backward fit')
opts.register('fit', False, VarParsing.multiplicity.singleton, VarParsing.varType.bool,
              'also run the device final fit (lane fit, no CPE) on the chain output; needs lane fit\'s private stock '
              'RecoTracker/MkFit (disableCPE switch) first in LD_LIBRARY_PATH for the stock no-CPE reference')
opts.register('truth', False, VarParsing.multiplicity.singleton, VarParsing.varType.bool, 'attach RAW parents (MTV)')
opts.register('verbose', False, VarParsing.multiplicity.singleton, VarParsing.varType.bool, 'CHAIN lines')
opts.register('timingJson', '', VarParsing.multiplicity.singleton, VarParsing.varType.string, 'FastTimerService JSON')
opts.setDefault('maxEvents', 10)
opts.parseArguments()

process = makeReplayProcess([replayFile(opts.sample)], maxEvents=opts.maxEvents, threads=opts.threads,
                            accelerators=opts.accelerators.split(','), compare=False, fit=opts.fit,
                            rawFiles=RAW_FILES.get(opts.sample) if opts.truth else None)
process.MessageLogger.cerr.FwkReport.reportEvery = 50

def _summary(name):
    return '%s/%s_%s.json' % (opts.summaryDir, opts.prefix, name) if opts.summaryDir else ''

process.chainESData = cms.ESProducer('MkFitAlpakaESProducer@alpaka', ComponentName=cms.string('chainES'),
                                     iterationConfig=cms.ESInputTag('', 'hltInitialStepTrackCandidatesMkFitConfig'))
# device CPE tables of the fit (ES product, round 5 R4-H2; default ComponentName MkFitAlpakaFitCpe)
process.chainFitCpeES = cms.ESProducer('MkFitAlpakaFitCpeESProducer@alpaka')
process.chainEventOfHits = cms.EDProducer('MkFitAlpakaEventOfHitsProducer@alpaka', compareTo=cms.InputTag(''),
                                          useESLayers=cms.bool(True), esData=cms.ESInputTag('', 'chainES'))
process.chain = cms.EDProducer('MkFitAlpakaChainProducer@alpaka', eventOfHits=cms.InputTag('chainEventOfHits'),
                               esData=cms.ESInputTag('', 'chainES'), backwardFit=cms.bool(opts.backwardFit),
                               verbose=cms.bool(opts.verbose), stages=cms.bool(opts.stages))
process.chainOutputWrapper = cms.EDProducer('MkFitAlpakaOutputWrapperFromTrackSoA', tracks=cms.InputTag('chain'))
process.chainTrackCandidates = process.hltInitialStepTrackCandidates.clone(tracks='chainOutputWrapper')
process.replayPath += process.chainEventOfHits
process.replayPath += process.chain
process.replayPath += process.chainOutputWrapper
process.replayPath += process.chainTrackCandidates

addStockStages(process)
addTrackSoACompare(process, 'cmpChainExport', cms.InputTag('stockStages', 'export'), cms.InputTag('chain', 'export'),
                   paramTolerance=1e-3, summaryFile=_summary('export'))
addTrackSoACompare(process, 'cmpChainFinal', cms.InputTag('stockStages', ''), cms.InputTag('chain'),
                   paramTolerance=1e-3, summaryFile=_summary('final'))
addMkFitTrackCompare(process, 'cmpChainMkFit', 'hltInitialStepTrackCandidatesMkFit::REPLAY', 'chainOutputWrapper',
                     paramTolerance=1e-3, summaryFile=_summary('mkfit'))
addCandidateCompare(process, 'cmpChainCands', 'hltInitialStepTrackCandidates::REPLAY', 'chainTrackCandidates',
                    paramTolerance=1e-3, summaryFile=_summary('cand'))

if opts.stages:
    # front candidate per seed in exportTrack(true) form vs stock's stage snapshot (all candidates, exportTrack(false)):
    # valid hits only; for 'fwd' the stock non-front candidates of a seed have no partner (unpaired by construction)
    addTrackSoACompare(process, 'cmpChainFwd', cms.InputTag('stockStages', 'fwd'), cms.InputTag('chain', 'fwd'),
                       paramTolerance=1e-3, validHitsOnly=True, summaryFile=_summary('fwd'))
    addTrackSoACompare(process, 'cmpChainBkFit', cms.InputTag('stockStages', 'bkfit'), cms.InputTag('chain', 'bkfit'),
                       paramTolerance=1e-3, validHitsOnly=True, summaryFile=_summary('bkfit'))

if opts.fit:
    # device final fit (no CPE, D-H1 step 1) on the device-built tracks vs stock building + stock fit with the CPE off
    menu = process.hltInitialStepTrackCandidatesMkFitFit
    process.stockFitNoCPE = menu.clone(disableCPE=cms.untracked.bool(True))
    process.chainFit = cms.EDProducer('MkFitAlpakaFitProducer@alpaka', tracks=cms.InputTag('chainOutputWrapper'),
                                      pixelHits=cms.InputTag('hltMkFitSiPixelHits'),
                                      stripHits=cms.InputTag('hltMkFitSiPhase2Hits'),
                                      esData=cms.ESInputTag('', 'chainES'), candCutSel=menu.candCutSel,
                                      candMinPtCut=menu.candMinPtCut, candMinNHitsCut=menu.candMinNHitsCut,
                                      candMinPtRelaxedCut=menu.candMinPtRelaxedCut,
                                      candMinAbsEtaForRelaxedCut=menu.candMinAbsEtaForRelaxedCut)
    process.stockTracksNoCPE = process.hltInitialStepTracksMkFitFit.clone(src='stockFitNoCPE')
    process.chainTracks = process.hltInitialStepTracksMkFitFit.clone(src='chainFit')
    for m in ('stockFitNoCPE', 'chainFit', 'stockTracksNoCPE', 'chainTracks'):
        process.replayPath += getattr(process, m)
    addMkFitTrackCompare(process, 'cmpChainFitVsStockNoCPE', 'stockFitNoCPE', 'chainFit', paramTolerance=1e-3,
                         summaryFile=_summary('fit'))
    addTrackCompare(process, 'cmpChainTracksVsStockNoCPE', 'stockTracksNoCPE', 'chainTracks', paramTolerance=1e-3,
                    summaryFile=_summary('fittracks'))

if opts.timingJson:
    addTiming(process, opts.timingJson)
