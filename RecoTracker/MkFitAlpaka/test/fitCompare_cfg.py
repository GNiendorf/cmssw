# Device mkFit final fit (lane fit) vs stock MkFitFitProducer on replay events.
#   cmsRun test/fitCompare_cfg.py sample=ttbar backend=serial_sync     # or backend=cuda_async (needs a GPU)
# References (both stock, same job, same building output):
#   stockFitNoCPE                          private stock copy (RecoTracker/MkFit in this area) with disableCPE = True:
#                                          the like-for-like reference of the no-CPE device fit (DESIGN D-H1 step 1)
#   hltInitialStepTrackCandidatesMkFitFit  the menu's stock fit (PixelCPEGeneric hook on every pixel hit), for scale
# Comparisons at mkFit level (MkFitOutputWrapper, paired by seed label): hit lists incl. removed outliers (index -1),
# 6 parameters in units of the reference errors, chi2. Also: the stock MkFitOutputTrackConverter on the device
# output (devTracks) vs on the stock no-CPE output (stockTracksNoCPE).
import FWCore.ParameterSet.Config as cms
from FWCore.ParameterSet.VarParsing import VarParsing
from RecoTracker.MkFitAlpaka.replay_cff import (makeReplayProcess, replayFile, addMkFitTrackCompare, addTrackCompare,
                                                setAlpakaBackend, addTiming)

opts = VarParsing('analysis')
opts.register('sample', 'ttbar', VarParsing.multiplicity.singleton, VarParsing.varType.string, 'ttbar or qcd')
opts.register('threads', 8, VarParsing.multiplicity.singleton, VarParsing.varType.int, 'threads (<= 8)')
opts.register('backend', 'serial_sync', VarParsing.multiplicity.singleton, VarParsing.varType.string,
              'serial_sync | cuda_async | both (device fit on cuda_async + a serial_sync clone, compared)')
opts.register('summaryPrefix', '', VarParsing.multiplicity.singleton, VarParsing.varType.string, 'comparator JSONs')
opts.register('converter', True, VarParsing.multiplicity.singleton, VarParsing.varType.bool,
              'also run MkFitOutputTrackConverter on device and stock-no-CPE outputs and compare reco::Tracks')
opts.register('timing', False, VarParsing.multiplicity.singleton, VarParsing.varType.bool, 'FastTimerService')
opts.register('timingJson', 'fit_timing.json', VarParsing.multiplicity.singleton, VarParsing.varType.string, '')
opts.register('determinism', False, VarParsing.multiplicity.singleton, VarParsing.varType.bool,
              'run the device fit twice on the same backend and compare (expect bit-identical)')
opts.register('cpe', True, VarParsing.multiplicity.singleton, VarParsing.varType.bool,
              'device fit with the device PixelCPEGeneric (stock menu); False = the no-CPE variant')
opts.register('deviceHits', False, VarParsing.multiplicity.singleton, VarParsing.varType.bool,
              'fit hits from the device EventOfHits product (MkFitAlpakaEventOfHitsProducer) instead of host packing')
opts.register('maxPrint', 3, VarParsing.multiplicity.singleton, VarParsing.varType.int, '')
opts.setDefault('maxEvents', -1)
opts.parseArguments()

acc = ['cpu'] if opts.backend == 'serial_sync' else ['gpu-nvidia', 'cpu']
process = makeReplayProcess([replayFile(opts.sample)], maxEvents=opts.maxEvents, threads=opts.threads,
                            accelerators=acc, fit=True, compare=False)

# stock no-CPE reference (private copy of RecoTracker/MkFit with the disableCPE switch)
process.stockFitNoCPE = process.hltInitialStepTrackCandidatesMkFitFit.clone(disableCPE=cms.untracked.bool(True))
process.replayPath += process.stockFitNoCPE

# device ES data and the device fit
backend = 'cuda_async' if opts.backend in ('cuda_async', 'both') else 'serial_sync'
process.mkFitAlpakaESProducer = cms.ESProducer('MkFitAlpakaESProducer@alpaka',
    ComponentName=cms.string(''),
    iterationConfig=cms.ESInputTag('', 'hltInitialStepTrackCandidatesMkFitConfig'))
# device CPE tables of the fit (ES product, round 5 R4-H2; default ComponentName MkFitAlpakaFitCpe)
process.mkFitAlpakaFitCpeESProducer = cms.ESProducer('MkFitAlpakaFitCpeESProducer@alpaka')
menu = process.hltInitialStepTrackCandidatesMkFitFit
process.devFit = cms.EDProducer('MkFitAlpakaFitProducer@alpaka',
    tracks=menu.tracks,
    pixelHits=cms.InputTag('hltMkFitSiPixelHits'),
    stripHits=cms.InputTag('hltMkFitSiPhase2Hits'),
    esData=cms.ESInputTag('', ''),
    candCutSel=menu.candCutSel,
    candMinPtCut=menu.candMinPtCut,
    candMinNHitsCut=menu.candMinNHitsCut,
    candMinPtRelaxedCut=menu.candMinPtRelaxedCut,
    candMinAbsEtaForRelaxedCut=menu.candMinAbsEtaForRelaxedCut,
    cpe=cms.bool(opts.cpe))
setAlpakaBackend(process.devFit, backend)
setAlpakaBackend(process.mkFitAlpakaESProducer, backend)
if opts.deviceHits:
    process.fitEventOfHits = cms.EDProducer('MkFitAlpakaEventOfHitsProducer@alpaka', compareTo=cms.InputTag(''),
                                            useESLayers=cms.bool(True), esData=cms.ESInputTag('', ''))
    setAlpakaBackend(process.fitEventOfHits, backend)
    process.devFit.eventOfHits = cms.InputTag('fitEventOfHits')
    process.replayPath += process.fitEventOfHits
process.replayPath += process.devFit

sp = opts.summaryPrefix
addMkFitTrackCompare(process, 'cmpDevVsStockNoCPE', reference='stockFitNoCPE', target='devFit',
                     paramTolerance=1e-3, maxPrint=0 if opts.cpe else opts.maxPrint,
                     summaryFile=(sp + 'nocpe.json') if sp else '')
addMkFitTrackCompare(process, 'cmpDevVsStockCPE', reference='hltInitialStepTrackCandidatesMkFitFit', target='devFit',
                     paramTolerance=1e-3, maxPrint=opts.maxPrint if opts.cpe else 0,
                     summaryFile=(sp + 'cpe.json') if sp else '')
addMkFitTrackCompare(process, 'cmpStockNoCPEVsStockCPE', reference='hltInitialStepTrackCandidatesMkFitFit',
                     target='stockFitNoCPE', paramTolerance=1e-3, maxPrint=0,
                     summaryFile=(sp + 'stockcpe.json') if sp else '')

if opts.backend == 'both':
    from HeterogeneousCore.AlpakaCore.functions import makeSerialClone
    process.devFitSerial = makeSerialClone(process.devFit)
    if opts.deviceHits:
        process.fitEventOfHitsSerial = makeSerialClone(process.fitEventOfHits)
        process.devFitSerial.eventOfHits = cms.InputTag('fitEventOfHitsSerial')
        process.replayPath += process.fitEventOfHitsSerial
    process.replayPath += process.devFitSerial
    addMkFitTrackCompare(process, 'cmpCudaVsSerial', reference='devFitSerial', target='devFit',
                         paramTolerance=1e-3, maxPrint=opts.maxPrint, summaryFile=(sp + 'cudaserial.json') if sp else '')
    addMkFitTrackCompare(process, 'cmpSerialVsStockNoCPE', reference='stockFitNoCPE', target='devFitSerial',
                         paramTolerance=1e-3, maxPrint=0, summaryFile=(sp + 'serialnocpe.json') if sp else '')

if opts.determinism:
    process.devFit2 = process.devFit.clone()
    process.replayPath += process.devFit2
    addMkFitTrackCompare(process, 'cmpDeterminism', reference='devFit', target='devFit2', paramTolerance=1e-3,
                         maxPrint=opts.maxPrint, summaryFile=(sp + 'determinism.json') if sp else '')

if opts.converter:
    process.stockTracksNoCPE = process.hltInitialStepTracksMkFitFit.clone(src='stockFitNoCPE')
    process.devTracks = process.hltInitialStepTracksMkFitFit.clone(src='devFit')
    process.replayPath += process.stockTracksNoCPE
    process.replayPath += process.devTracks
    addTrackCompare(process, 'cmpTracksDevVsStockNoCPE', reference='stockTracksNoCPE', target='devTracks',
                    paramTolerance=1e-3, maxPrint=0, summaryFile=(sp + 'tracks.json') if sp else '')
    # the menu's own reco::Tracks (stock fit with the CPE) vs the device fit
    addTrackCompare(process, 'cmpTracksDevVsStockCPE', reference='hltInitialStepTracksMkFitFit', target='devTracks',
                    paramTolerance=1e-3, maxPrint=0, summaryFile=(sp + 'trackscpe.json') if sp else '')

if opts.timing:
    addTiming(process, opts.timingJson)
