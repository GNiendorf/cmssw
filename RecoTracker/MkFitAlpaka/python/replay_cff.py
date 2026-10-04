"""Replay of the stock mkFit chain of the Phase-2 HLT LST initial step from persisted inputs (harness lane).

The replay files (<lanes>/mkfit_alpaka/data/replay_{ttbar,qcd}.root) hold, from the HLT run (process HLTX):
  hltInitialStepTrajectorySeedsLST (LST TrajectorySeeds), hltSiPixelClusters, hltPhase2SiPixelRecHitsSoA (pixel rechit
  SoA), hltSiPhase2Clusters, hltOnlineBeamSpot, and the fidelity references hltInitialStepTrackCandidates,
  hltInitialStepTracks (KF fit), hltInitialStepTrackSelectionHighPurity.
The sim truth (TrackingParticles, simlinks, SimTracks) stays in the RAW parent: pass rawFiles to read it as
secondaryFileNames.

All module and ES definitions come from the release HLT menu (HLTrigger.Configuration.HLT_75e33_timing_cff), with the
same era/geometry/conditions as the HLT job, so the replayed modules are the menu's own.

Typical use (see test/replay_cfg.py and doc/harness.txt):
    from RecoTracker.MkFitAlpaka.replay_cff import makeReplayProcess, addCandidateCompare
    process = makeReplayProcess(['file:replay_ttbar.root'], maxEvents=-1, threads=8)
    process.myProducer = cms.EDProducer('...')          # a lane's own producer
    process.replayPath += process.myProducer
    addCandidateCompare(process, 'cmpMine', reference='hltInitialStepTrackCandidates::REPLAY', target='myProducer')
"""
import FWCore.ParameterSet.Config as cms

DATA_DIR = '/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/mem_ref/lanes/mkfit_alpaka/data'

# RAW parents of the replay files (for sim truth via secondaryFileNames)
RAW_FILES = {
    'ttbar': ['file:/mnt/data1/gsn27/here/ttbar_raw/0033230b-a131-453a-95c0-fe14d5027d1f.root'],
    'qcd': ['file:/mnt/data1/gsn27/here/qcd_raw/03b794c3-dede-4201-b0a2-279c27f5a723.root',
            'file:/mnt/data1/gsn27/here/qcd_raw/057d0c1d-fc1f-4afe-a126-ac3b4524add1.root'],
}
# Round 3: 500-event files (all events of the local RAW: 5 ttbar files x 100, 10 QCD files x 50), made with
# test/replay_make.sh into the harness lane's area (the coordinator may move them to DATA_DIR; then drop the entry).
DATA_DIR_R3 = '/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/mem_ref/lanes/mkfit_alpaka/r3_harness/data'
REPLAY_FILES = {'ttbar500': 'file:%s/replay_ttbar500.root' % DATA_DIR_R3,
                'qcd500': 'file:%s/replay_qcd500.root' % DATA_DIR_R3}
RAW_FILES['ttbar500'] = ['file:/mnt/data1/gsn27/here/ttbar_raw/%s.root' % f for f in (
    '0033230b-a131-453a-95c0-fe14d5027d1f', '795569f1-d53a-4649-8998-88766b5fdcea',
    '8652bc91-6927-4f2f-86a1-8de93b4cfbaa', 'de923b86-a678-47e3-9baa-2b1de1bdf330',
    'f5dd80d8-5828-4091-b895-d426638253d2')]
RAW_FILES['qcd500'] = ['file:/mnt/data1/gsn27/here/qcd_raw/%s.root' % f for f in (
    '03b794c3-dede-4201-b0a2-279c27f5a723', '057d0c1d-fc1f-4afe-a126-ac3b4524add1',
    '058d0934-b40a-462b-893c-6edf5049a8c8', '05933539-4050-46d1-a510-76ef18ac37a9',
    '06e0fec9-22ac-4975-8ee4-5f32d1a07bb1', '0730363c-fa12-46ba-a49a-df7cc275cf49',
    '08be16af-fd11-46a8-aacd-e00573886713', '096ff017-c8e1-4b93-ad36-83c0affee60d',
    '0a2ca3ba-e102-4bec-8393-8d6f4a757167', '0c167b1c-a995-4370-89e9-2abb44ea4a1b')]

# The LST-step mkFit chain of the menu, in order (stock modules, menu labels)
STOCK_CHAIN = ['hltMkFitSiPixelHits', 'hltMkFitSiPhase2Hits', 'hltMkFitEventOfHits',
               'hltInitialStepMkFitSeeds', 'hltInitialStepTrackCandidatesMkFit', 'hltInitialStepTrackCandidates']


def replayFile(sample):
    return REPLAY_FILES.get(sample, 'file:%s/replay_%s.root' % (DATA_DIR, sample))


def makeReplayProcess(inputFiles, maxEvents=-1, threads=8, streams=0, accelerators=('cpu',), fit=False,
                      rawFiles=None, processName='REPLAY', skipEvents=0, compare=True, wantSummary=False):
    """Build a process that re-runs the stock mkFit chain of the LST step on a replay file.

    fit=True adds the menu's mkFit final-fit path (procModifier trackingMkFitFit): hltInitialStepTrackCandidatesMkFitFit
    (MkFitFitProducer) + hltInitialStepTracksMkFitFit (MkFitOutputTrackConverter, needs hltMeasurementTrackerEvent).
    compare=True adds the fidelity comparator: replayed hltInitialStepTrackCandidates vs the HLT's (process HLTX).
    """
    from Configuration.Eras.Era_Phase2C22I13M9_cff import Phase2C22I13M9
    process = cms.Process(processName, Phase2C22I13M9)
    process.load('Configuration.StandardSequences.Services_cff')
    process.load('FWCore.MessageService.MessageLogger_cfi')
    process.load('Configuration.Geometry.GeometryExtendedRun4D121Reco_cff')
    process.load('Configuration.StandardSequences.MagneticField_cff')
    process.load('HLTrigger.Configuration.HLT_75e33_timing_cff')
    process.load('Configuration.StandardSequences.FrontierConditions_GlobalTag_cff')
    from Configuration.AlCa.GlobalTag import GlobalTag
    process.GlobalTag = GlobalTag(process.GlobalTag, 'auto:phase2_realistic_T35', '')
    process.load('Configuration.StandardSequences.Accelerators_cff')

    process.source = cms.Source('PoolSource', fileNames=cms.untracked.vstring(*inputFiles),
                                secondaryFileNames=cms.untracked.vstring(*(rawFiles or [])),
                                skipEvents=cms.untracked.uint32(skipEvents))
    process.maxEvents = cms.untracked.PSet(input=cms.untracked.int32(maxEvents))
    process.options.numberOfThreads = threads
    process.options.numberOfStreams = streams
    process.options.accelerators = list(accelerators)
    process.options.wantSummary = wantSummary
    process.MessageLogger.cerr.FwkReport.reportEvery = 10

    # Legacy rechits cannot be persisted usefully (BaseTrackerRecHit pos_/err_ are transient), so the replay re-runs the
    # rechits on the persisted inputs: hltSiPixelRecHits (from the kept pixel rechit SoA hltPhase2SiPixelRecHitsSoA +
    # hltSiPixelClusters, see below) and the menu's own hltSiPhase2RecHits (Phase2TrackerRecHits, from hltSiPhase2Clusters). Persisted seeds have no GeomDet pointer (transient) and MkFitSeedConverter dereferences
    # it: they are copied unchanged with the det re-attached, under the SAME label in this process, so the menu modules'
    # InputTags resolve to the copy. Only cluster indices, detIds and the starting state of seed hits are used.
    # The menu's hltSiPixelRecHits needs SiPixelCluster::originalId() (transient): MkFitAlpakaReplayPixelRecHits does
    # the same conversion and recovers the per-module SoA index (exact charge + nearest position; counters at endJob).
    _maxHits = process.hltSiPixelRecHits.maxHitsInModules.value() if hasattr(process.hltSiPixelRecHits, 'maxHitsInModules') else 1024
    process.hltSiPixelRecHits = cms.EDProducer('MkFitAlpakaReplayPixelRecHits',
                                               pixelRecHitSrc=cms.InputTag('hltPhase2SiPixelRecHitsSoA', '', 'HLTX'),
                                               src=cms.InputTag('hltSiPixelClusters', '', 'HLTX'),
                                               maxHitsInModules=cms.uint32(_maxHits), checkOriginalId=cms.bool(True))
    process.hltInitialStepTrajectorySeedsLST = cms.EDProducer('MkFitAlpakaReplaySeeds',
                                                              src=cms.InputTag('hltInitialStepTrajectorySeedsLST', '', 'HLTX'))

    # the menu's schedule is dropped; only the LST-step mkFit chain runs
    chain = process.hltSiPixelRecHits + process.hltSiPhase2RecHits + process.hltInitialStepTrajectorySeedsLST
    for name in STOCK_CHAIN:
        m = getattr(process, name)
        chain = chain + m
    process.replayPath = cms.Path(chain)
    process.replayEndPath = cms.EndPath()
    process.schedule = cms.Schedule(process.replayPath, process.replayEndPath)

    if fit:
        addMkFitFit(process)
    if compare:
        addCandidateCompare(process, 'compareCandidatesHLTvsReplay',
                            reference=cms.InputTag('hltInitialStepTrackCandidates', '', 'HLTX'),
                            target=cms.InputTag('hltInitialStepTrackCandidates', '', processName))
    return process


def addMkFitFit(process):
    """Add the menu's mkFit final fit (trackingMkFitFit flavour) after the candidates."""
    from HLTrigger.Configuration.HLT_75e33.modules.hltInitialStepTrackCandidatesMkFitFit_cfi import \
        hltInitialStepTrackCandidatesMkFitFit
    from HLTrigger.Configuration.HLT_75e33.modules.hltInitialStepTracks_cfi import _hltInitialStepTracksMkFitFit
    process.hltInitialStepTrackCandidatesMkFitFit = hltInitialStepTrackCandidatesMkFitFit.clone()
    process.hltInitialStepTracksMkFitFit = _hltInitialStepTracksMkFitFit.clone()
    # MkFitOutputTrackConverter consumes a hard-coded 'offlineBeamSpot' (BeamSpotProducer from conditions)
    if not hasattr(process, 'offlineBeamSpot'):
        from RecoVertex.BeamSpotProducer.BeamSpot_cfi import offlineBeamSpot
        process.offlineBeamSpot = offlineBeamSpot.clone()
    process.replayPath += process.offlineBeamSpot
    process.replayPath += process.hltMeasurementTrackerEvent
    process.replayPath += process.hltInitialStepTrackCandidatesMkFitFit
    process.replayPath += process.hltInitialStepTracksMkFitFit
    return process


def _tag(t):
    if isinstance(t, cms.InputTag):
        return t
    return cms.InputTag(t)


def addCandidateCompare(process, name, reference, target, paramTolerance=1e-3, maxPrint=5, printEvents=False,
                        summaryFile='', validHitsOnly=False, seedKeyOnly=True, requireIdentical=False):
    """Per-seed comparison of two TrackCandidateCollections (hits + local state in units of the reference errors)."""
    m = cms.EDAnalyzer('MkFitAlpakaCandidateCompare', reference=_tag(reference), target=_tag(target),
                       label=cms.string(name), paramTolerance=cms.double(paramTolerance),
                       validHitsOnly=cms.bool(validHitsOnly), seedKeyOnly=cms.bool(seedKeyOnly), maxPrint=cms.int32(maxPrint),
                       printEvents=cms.bool(printEvents), summaryFile=cms.string(summaryFile),
                       requireIdentical=cms.bool(requireIdentical))
    setattr(process, name, m)
    process.replayEndPath += m
    return m


def addTrackCompare(process, name, reference, target, paramTolerance=1e-3, maxPrint=5, printEvents=False,
                    summaryFile='', validHitsOnly=True, seedKeyOnly=True, requireIdentical=False):
    """Per-seed comparison of two reco::Track collections (hits + 5 helix parameters in units of reference errors)."""
    m = cms.EDAnalyzer('MkFitAlpakaTrackCompare', reference=_tag(reference), target=_tag(target),
                       label=cms.string(name), paramTolerance=cms.double(paramTolerance),
                       validHitsOnly=cms.bool(validHitsOnly), seedKeyOnly=cms.bool(seedKeyOnly), maxPrint=cms.int32(maxPrint),
                       printEvents=cms.bool(printEvents), summaryFile=cms.string(summaryFile),
                       requireIdentical=cms.bool(requireIdentical))
    setattr(process, name, m)
    process.replayEndPath += m
    return m


def addTiming(process, jsonFile='replay_timing.json'):
    """FastTimerService with per-module times: job summary in the log + JSON (per-module mean real/cpu time)."""
    process.FastTimerService = cms.Service('FastTimerService',
        printEventSummary=cms.untracked.bool(False),
        printRunSummary=cms.untracked.bool(False),
        printJobSummary=cms.untracked.bool(True),
        enableDQM=cms.untracked.bool(False),
        writeJSONSummary=cms.untracked.bool(True),
        jsonFileName=cms.untracked.string(jsonFile))
    process.ThroughputService = cms.Service('ThroughputService', eventRange=cms.untracked.uint32(10000),
                                            eventResolution=cms.untracked.uint32(10), printEventSummary=cms.untracked.bool(True),
                                            enableDQM=cms.untracked.bool(False))
    return process


def setAlpakaBackend(module, backend):
    """Pin one @alpaka module to a backend: 'serial_sync' (CPU) or 'cuda_async' (NVIDIA GPU)."""
    module.alpaka = cms.untracked.PSet(backend=cms.untracked.string(backend))
    return module


def addOutput(process, fileName, extraKeep=()):
    """Write the replay inputs (process HLTX/HLT products of the input file) + the replayed TrackCandidates/tracks of this
    process: the file is a valid replay input for a later job with another processName, which can then compare its own
    products with this job's (cross-run / cross-version comparisons)."""
    pn = process.name_()
    process.replayOut = cms.OutputModule('PoolOutputModule', fileName=cms.untracked.string(fileName),
        outputCommands=cms.untracked.vstring('keep *', 'drop *_*_*_%s' % pn,
                                             'keep *_hltInitialStepTrackCandidates_*_%s' % pn,
                                             'keep *_hltInitialStepTracksMkFitFit_*_%s' % pn,
                                             *extraKeep))
    process.replayEndPath += process.replayOut
    return process


def _addGenericCompare(process, plugin, name, reference, target, paramTolerance=1e-3, maxPrint=5, printEvents=False,
                       summaryFile='', validHitsOnly=False, seedKeyOnly=True, requireIdentical=False):
    m = cms.EDAnalyzer(plugin, reference=_tag(reference), target=_tag(target), label=cms.string(name),
                       paramTolerance=cms.double(paramTolerance), validHitsOnly=cms.bool(validHitsOnly),
                       seedKeyOnly=cms.bool(seedKeyOnly), maxPrint=cms.int32(maxPrint),
                       printEvents=cms.bool(printEvents), summaryFile=cms.string(summaryFile),
                       requireIdentical=cms.bool(requireIdentical))
    setattr(process, name, m)
    process.replayEndPath += m
    return m


def addMkFitTrackCompare(process, name, reference, target, **kw):
    """Per-seed comparison of two MkFitOutputWrapper products (mkFit candidates or mkFit-fit output): (layer, index)
    hit lists incl. negative indices, mkFit's 6 parameters (x, y, z, 1/pT, phi, theta) in units of reference errors."""
    return _addGenericCompare(process, 'MkFitAlpakaMkFitTrackCompare', name, reference, target, **kw)


def addMkFitSeedCompare(process, name, reference, target, **kw):
    """Per-seed comparison of two MkFitSeedWrapper products (mkFit seeds)."""
    return _addGenericCompare(process, 'MkFitAlpakaMkFitSeedCompare', name, reference, target, **kw)


# Per-stage STOCK references (plugins/ReplayStockStages.cc): MkFitOutputWrapper snapshots of the stock clone engine
# after each stage; product instance names below ('' = final output, equal to hltInitialStepTrackCandidatesMkFit).
STOCK_STAGES = ['seeds', 'loaded', 'fwd', 'fwdFiltered', 'bkfit', 'bkwsearch', 'postFilter', 'export', '']


def addStockStages(process, name='stockStages'):
    """Add MkFitAlpakaReplayStockStages with the parameters of the menu's MkFitProducer (hltInitialStepTrackCandidatesMkFit).
    Reference for a port stage X: cms.InputTag(name, X); compare with addMkFitTrackCompare."""
    mk = process.hltInitialStepTrackCandidatesMkFit
    if mk.backwardFitInCMSSW.value() or (hasattr(mk, 'clustersToSkip') and mk.clustersToSkip.getModuleLabel()):
        raise RuntimeError('addStockStages supports the LST-step setup only (mkFit backward fit, no clustersToSkip)')
    m = cms.EDProducer('MkFitAlpakaReplayStockStages', pixelHits=mk.pixelHits, stripHits=mk.stripHits,
                       eventOfHits=mk.eventOfHits, seeds=mk.seeds, config=mk.config,
                       seedCleaning=mk.seedCleaning, removeDuplicates=mk.removeDuplicates)
    setattr(process, name, m)
    process.replayPath += m
    return m


def addTrackSoACompare(process, name, reference, target, **kw):
    """Per-seed comparison of a port's mkfitdev::TrackSoA host collection (target) with a stock MkFitOutputWrapper
    (reference, e.g. cms.InputTag('stockStages', 'bkfit') or 'hltInitialStepTrackCandidatesMkFit'), at mkFit level."""
    return _addGenericCompare(process, 'MkFitAlpakaTrackSoACompare', name, reference, target, **kw)
