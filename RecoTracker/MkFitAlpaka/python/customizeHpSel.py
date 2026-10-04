# customizeHpSel.py (round 7, lane hpsel; moved into python/ by the verify7 integrator)
# Stage C, device high-purity selection, step 1: the initial step's HP Torch classifier on the device.
#   release menu: hltInitialStepTrackFeatureExtractor (alpaka_serial_sync, host loop over reco::Track, HOST SoA)
#                 -> hltInitialStepTrackTorchClassifier (alpaka_serial_sync::TrackTorchClassifierAlpaka, libtorch CPU)
#                 -> hltInitialStepTrackTorchClassifierOutput (host, threshold) -> CutClassifier -> HighPurity cloner
#   device:       same extractor -> hltInitialStepTrackFeaturesDevice (MkFitAlpakaHpFeaturesToDevice@alpaka, async
#                 copy of the 15 feature columns) -> hltInitialStepTrackTorchClassifier = TrackTorchClassifierAlpaka@alpaka
#                 (same module, same model.pt, PyTorchAlpaka on the GPU) -> unchanged host consumers (the scores come back
#                 through the framework's automatic device->host copy).
# On CPU-only jobs @alpaka resolves to serial_sync: identical to the release menu (plus a 120 kB copy).
import FWCore.ParameterSet.Config as cms


def customizeHpSelDevice(process):
    if not hasattr(process, 'hltInitialStepTrackTorchClassifier'):
        return process
    stock = process.hltInitialStepTrackTorchClassifier
    process.hltInitialStepTrackFeaturesDevice = cms.EDProducer('MkFitAlpakaHpFeaturesToDevice@alpaka',
        src = cms.InputTag('hltInitialStepTrackFeatureExtractor'))
    process.hltInitialStepTrackTorchClassifier = cms.EDProducer('TrackTorchClassifierAlpaka@alpaka',
        modelPath = stock.modelPath,
        features = cms.InputTag('hltInitialStepTrackFeaturesDevice'))
    # schedule the copy right before the classifier, ONLY in the sequence(s) holding the classifier directly
    # (HLTInitialStepHPSelectionSequence); a replace() in the enclosing sequences would insert it again (once per level)
    for s in process.sequences_().values():
        try:
            i = s.index(process.hltInitialStepTrackTorchClassifier)
        except Exception:
            continue
        s.insert(i, process.hltInitialStepTrackFeaturesDevice)
    for t in process.tasks_().values():
        if 'hltInitialStepTrackTorchClassifier' in t.moduleNames():
            t.add(process.hltInitialStepTrackFeaturesDevice)
    return process


def customizeHpSelDeviceValidation(process, dumpFile='', featuresFromTrackSoA=False):
    """device HP selection in the main chain + the release serial classifier on the same features in the same job +
    MkFitAlpakaHpCompare (per-track scores, HP decisions, HP collection sizes).
    featuresFromTrackSoA: also the step-2 prototype MkFitAlpakaHpFeaturesFromTrackSoA@alpaka (features from the device
    fit output), compared column by column with the host extractor."""
    stockCls = process.hltInitialStepTrackTorchClassifier.clone()
    stockOut = process.hltInitialStepTrackTorchClassifierOutput
    customizeHpSelDevice(process)
    process.hltInitialStepTrackTorchClassifierRef = stockCls
    process.hltInitialStepTrackTorchClassifierOutputRef = stockOut.clone(scores = 'hltInitialStepTrackTorchClassifierRef')
    process.hltHpCompare = cms.EDAnalyzer('MkFitAlpakaHpCompare',
        scoresRef = cms.InputTag('hltInitialStepTrackTorchClassifierOutputRef', 'MVAScores'),
        scoresTest = cms.InputTag('hltInitialStepTrackTorchClassifierOutput', 'MVAScores'),
        features = cms.InputTag('hltInitialStepTrackFeatureExtractor'),
        hpRef = cms.InputTag('hltInitialStepTrackTorchClassifierOutputRef'),
        hpTest = cms.InputTag('hltInitialStepTrackTorchClassifierOutput'),
        featuresRef = cms.InputTag(''),
        featuresTest = cms.InputTag(''),
        minScore = stockOut.minScore,
        dxyThreshold = stockOut.dxyThreshold,
        highDxyMinScore = stockOut.highDxyMinScore,
        verbose = cms.bool(True),
        dumpFile = cms.string(dumpFile))
    process.hltHpCompareTask = cms.Task(process.hltInitialStepTrackTorchClassifierRef,
                                        process.hltInitialStepTrackTorchClassifierOutputRef)
    if featuresFromTrackSoA:
        process.hltHpFeaturesFromTrackSoA = cms.EDProducer('MkFitAlpakaHpFeaturesFromTrackSoA@alpaka',
            tracks = cms.InputTag('hltInitialStepTrackCandidatesMkFitFitDevice'),
            beamSpot = cms.InputTag('hltOnlineBeamSpot'),
            esData = cms.ESInputTag('', 'hltMkFitAlpakaES'))
        process.hltHpCompare.featuresTest = 'hltHpFeaturesFromTrackSoA'
        process.hltHpCompareTask.add(process.hltHpFeaturesFromTrackSoA)
    # the comparator runs right after the classifier output, in the sequence(s) holding it directly: an EndPath would
    # also run on events whose paths stop before the initial step (no features -> ProductNotFound; verify7)
    for s in process.sequences_().values():
        try:
            i = s.index(process.hltInitialStepTrackTorchClassifierOutput)
        except Exception:
            continue
        s.insert(i + 1, process.hltHpCompare)
        s.insert(i + 1, process.hltInitialStepTrackTorchClassifierOutputRef)
        s.insert(i + 1, process.hltInitialStepTrackTorchClassifierRef)
    if hasattr(process, 'hltHpFeaturesFromTrackSoA'):
        for s in process.sequences_().values():
            try:
                i = s.index(process.hltHpCompare)
            except Exception:
                continue
            s.insert(i, process.hltHpFeaturesFromTrackSoA)
    del process.hltHpCompareTask
    return process


# ---- round 8, lane stagec: HP option (b), the model.pt-folded MLP kernel (MkFitAlpakaHpClassifier@alpaka) ----
def _gpuBackendPossible(process):
    acc = list(process.options.accelerators) if hasattr(process.options, 'accelerators') else ['*']
    return any(a == '*' or a.startswith('gpu') for a in acc)


def customizeHpSelMlp(process, produceHpMask=False, cpuToo=False):
    """Stage C, HP option (b): customizeHpSelDevice with the classifier replaced by MkFitAlpakaHpClassifier@alpaka (the
    same model.pt, BatchNorm folded at construction, a hand-written MLP kernel; no libtorch on the device path, no
    per-stream libtorch pools). The scores product type is unchanged, so TrackTorchClassifierFromSoA and everything
    downstream are untouched. produceHpMask: also the device HP decision (instance 'hpMask'; nothing reads it yet).
    GPU menus only: on CPU-only jobs @alpaka resolves to the serial kernel, 10.1-10.3 ms vs libtorch's 4.5-4.7 ms, so a
    job whose accelerators allow no GPU keeps the release classifier (R8-M5) unless cpuToo=True (validation)."""
    if not hasattr(process, 'hltInitialStepTrackTorchClassifier'):
        return process
    if not cpuToo and not _gpuBackendPossible(process):
        print('customizeHpSelMlp: accelerators %s allow no GPU backend: the release classifier is kept'
              % list(process.options.accelerators))
        return process
    stock = process.hltInitialStepTrackTorchClassifier
    if stock.type_() not in ('TrackTorchClassifierAlpaka@alpaka', 'alpaka_serial_sync::TrackTorchClassifierAlpaka',
                             'TrackTorchClassifierAlpaka'):
        raise RuntimeError('customizeHpSelMlp: unexpected classifier type %s' % stock.type_())
    modelPath = stock.modelPath
    customizeHpSelDevice(process)
    out = process.hltInitialStepTrackTorchClassifierOutput
    process.hltInitialStepTrackTorchClassifier = cms.EDProducer('MkFitAlpakaHpClassifier@alpaka',
        features = cms.InputTag('hltInitialStepTrackFeaturesDevice'),
        modelPath = modelPath,
        produceHpMask = cms.bool(produceHpMask),
        minScore = out.minScore,
        dxyThreshold = out.dxyThreshold,
        highDxyMinScore = out.highDxyMinScore)
    return process


def customizeHpSelMlpValidation(process, dumpFile='', checkProgramFile=''):
    """customizeHpSelDeviceValidation with the MLP classifier: the release serial libtorch classifier on the same
    features in the same job is the reference (HPCMP lines), and the device HP mask is checked against the host
    decision (MkFitAlpakaHpMaskCheck, added by the caller). checkProgramFile: the r7_hpsel exported .bin, compared byte by byte with the fold."""
    stockCls = process.hltInitialStepTrackTorchClassifier.clone()
    customizeHpSelDeviceValidation(process, dumpFile)
    # customizeHpSelDeviceValidation installed TrackTorchClassifierAlpaka@alpaka as the test classifier: swap in the MLP
    out = process.hltInitialStepTrackTorchClassifierOutput
    process.hltInitialStepTrackTorchClassifier = cms.EDProducer('MkFitAlpakaHpClassifier@alpaka',
        features = cms.InputTag('hltInitialStepTrackFeaturesDevice'),
        modelPath = stockCls.modelPath,
        produceHpMask = cms.bool(True),
        minScore = out.minScore,
        dxyThreshold = out.dxyThreshold,
        highDxyMinScore = out.highDxyMinScore,
        checkProgramFile = cms.untracked.string(checkProgramFile))
    return process
