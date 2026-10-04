# Lane lstin (rounds 6-9): stage-B prototype switch, OFF by default (nothing changes unless this customise is applied).
# Apply after the production customise, e.g. --customise RecoTracker/MkFitAlpaka/customizeLstIn.customizeLstInDeviceOn
#   mode 'compare' : hltInputLSTDevice (device-built LST input) next to hltInputLST, compared per event (LSTIN_CMP);
#                    hltLSTDevIn = LST on the device input; LSTIN_TC = TC counts host input vs device input.
#                    The production chain (hltLST -> converter -> mkFit) is untouched.
#   mode 'timing'  : hltInputLSTDevice only (no comparison, no second LST), for the producer's own cost.
#   mode 'on'      : the switch ON form: hltInputLST is REMOVED; hltLST runs on the device-built input (pLS option (i),
#                    Patatrack SoA order, only tracks with a host seed); LSTOutputConverter reads the OT rechit pointers
#                    from hltInputLSTDevice:otRecHitPtrs and the pixel seeds directly from hltInitialStepSeeds.
#   mode 'lazy'    : (round 7) the switch ON form WITHOUT host pixel seeds: hltInputLST AND hltInitialStepSeeds are
#                    REMOVED; hltLST runs on the device-built input (pLS option (i), seedIdx = hltPhase2PixelTracks index);
#                    LSTOutputConverter (lazyPixelSeeds) makes the pixel seed of a pLS/pT3/pT5 TC itself, with
#                    hltInitialStepSeeds' creator and region (bitwise the same seed); the OT module table is the shared
#                    ES product (MkFitAlpakaEventOfHitsModuleTableESProducer), also used by the device EventOfHits.
#   mode 'lazycheck': 'lazy' + hltInitialStepSeeds kept as the reference: LSTOUT_LAZY lines count lazy seeds that are
#                    not bitwise equal to hltInitialStepSeeds' seed of the same pixel track (validation only).
#   mode 'replace' : physics-test arm (switch ON for the pLS parameters): hltLST runs on hltInputLST's collection with
#                    the pLS parameters/pseudo-hits of option (i) (device, Patatrack fit) for every pLS matched by hit
#                    keys; same OT hits (bitwise), pLS set, order and seedIdx, so the converter/mkFit chain is unchanged.
import FWCore.ParameterSet.Config as cms

# round 9: with stage D (customizeOTDev / customizeOTDevFull or the target's OT_DEVICE) the producer reads its OT hits
# from the device OT rechit SoA (otSoA) and the CA OT hitModuleStart from whichever producer makes the CA OT layers.
# Apply this customise AFTER the stage-D one (customizeOTDevFull would otherwise find hltInputLSTDevice reading
# hltSiPhase2RecHits and refuse).
OTDEV = 'hltMkFitAlpakaOTRecHits'
CA_OT = 'hltPhase2OtRecHitsSoA'
FAILMASK = 'hltPixelSeedFailMask'


def customizeLstInDevice(process, mode='compare', dumpFile='', contract=1, tracksFromHost=False, shareModuleTable=True,
                         ptFieldCorrection=False, failMask=None, failMaskValidate=False, pseudoLastHit=0, pcaAnchor=0):
    """failMask (lazy modes, round 9): None = round-7 lazy (pixel tracks whose host creator fails still get a pLS);
    a number = LSTPixelSeedFailMask's maxDr pre-filter in cm (< 0: the creator on every pixel track).
    pseudoLastHit (round 10, lane stageb): the pLS last-hit pseudo-hit, 0 = the outermost hit (rounds 7-9), 1 = the
    Patatrack helix point at the outermost hit's transverse radius (KF-state-like, as hltInputLST's host seed state).
    pcaAnchor (round 10, lane stageb): the pLS PCA quantities, 0 = Patatrack's PCA (rounds 7-9), 1 = the PCA of the
    Patatrack helix re-anchored on the outermost hit (as the host: TSCBL of the KF seed state on the last hit)."""
    if mode in ('otdev', 'otdevcheck'):
        return _otDev(process, mode == 'otdevcheck')
    fullD = process.hltInputLST.phase2OTRecHits.getModuleLabel() == ''  # full stage D: legacy OT rechits out
    soa = hasattr(process, OTDEV)
    if fullD and not soa:
        raise RuntimeError('customizeLstInDevice: hltInputLST has no legacy OT rechits but %s is missing' % OTDEV)
    if soa and mode in ('lazy', 'lazycheck', 'on'):
        conv = process.hltInitialStepTrajectorySeedsLST
        if not hasattr(conv, 'otClustersOnDemand') or conv.otClustersOnDemand.getModuleLabel() == '':
            raise RuntimeError('customizeLstInDevice %s with the OT rechit SoA: LSTOutputConverter needs otClustersOnDemand '
                               '(full stage D: customizeOTDevFull before this customise)' % mode)
    process.hltInputLSTDevice = cms.EDProducer('MkFitAlpakaLstInputProducer@alpaka',
        ptCut = process.hltInputLST.ptCut,
        contract = cms.int32(contract),
        compare = cms.bool(mode in ('compare', 'replace')),
        replace = cms.bool(mode == 'replace'),
        tracksFromHost = cms.bool(tracksFromHost),
        ptFieldCorrection = cms.bool(ptFieldCorrection),
        pseudoLastHit = cms.int32(pseudoLastHit),
        pcaAnchor = cms.int32(pcaAnchor),
        dumpFile = cms.string(dumpFile),
        phase2OTRecHits = cms.InputTag('') if soa else process.hltInputLST.phase2OTRecHits,
        otSoA = cms.InputTag(OTDEV if soa else ''),
        pixelRecHits = cms.InputTag('hltSiPixelRecHits'),
        otRecHitsSoA = cms.InputTag(CA_OT if hasattr(process, CA_OT) else OTDEV),
        pixelTracksSoA = process.hltPhase2PixelTracks.trackSrc,
        hitsSoA = cms.InputTag('hltPhase2PixelRecHitsExtendedSoA'),
        beamSpot = process.hltInputLST.beamSpot,
        reference = cms.InputTag('hltInputLST'),
        alpaka = cms.untracked.PSet(backend = cms.untracked.string('')))
    if soa:  # a device reader of the OT SoA after the target's early deletion of it (OT_EARLY_DELETE)
        from RecoTracker.MkFitAlpaka.customizeMemory import revokeOTSoAEarlyDelete
        revokeOTSoAEarlyDelete(process, 'hltInputLSTDevice', OTDEV)
    if mode in ('lazy', 'lazycheck'):
        seeds = process.hltInitialStepSeeds
        # the converter emulates SeedGeneratorFromProtoTracksEDProducer for this configuration only
        if (seeds.type_() != 'SeedGeneratorFromProtoTracksEDProducer' or seeds.useProtoTrackKinematics.value()
                or seeds.removeOTRechits.value() or seeds.produceComplement.value() or seeds.usePV.value()
                or seeds.InputVertexCollection.value() not in ('', '""') or not seeds.useEventsWithNoVertex.value()
                or seeds.InputCollection.value() != 'hltPhase2PixelTracks'):
            raise RuntimeError('customizeLstIn lazy: hltInitialStepSeeds configuration not supported by lazyPixelSeeds')
        dev = process.hltInputLSTDevice
        dev.seedIdxFromTracks = cms.bool(True)
        if shareModuleTable:
            if not hasattr(process, 'mkFitAlpakaHitModuleTable'):
                process.mkFitAlpakaHitModuleTable = cms.ESProducer('MkFitAlpakaEventOfHitsModuleTableESProducer@alpaka',
                    ComponentName = cms.string('MkFitAlpakaHitModuleTable'))
            dev.moduleTable = cms.string('MkFitAlpakaHitModuleTable')
            for m in process.producers_().values():  # the device hit input of mkFit reads the same table
                if m.type_() == 'MkFitAlpakaEventOfHitsProducer@alpaka' and hasattr(m, 'deviceHits') and m.deviceHits.value():
                    m.moduleTable = cms.string('MkFitAlpakaHitModuleTable')
        process.HLTInitialStepSequence.insert(process.HLTInitialStepSequence.index(process.hltInputLST), dev)
        if failMask is not None:
            # round 9: the pixel tracks whose hltInitialStepSeeds seed the host creator rejects give no pLS (as in stock)
            setattr(process, FAILMASK, cms.EDProducer('LSTPixelSeedFailMask',
                pixelTracks = cms.InputTag('hltPhase2PixelTracks'),
                includeFourthHit = cms.bool(seeds.includeFourthHit.value()),
                maxDr = cms.double(failMask),
                validate = cms.bool(failMaskValidate),
                SeedCreatorPSet = seeds.SeedCreatorPSet.clone()))
            process.HLTInitialStepSequence.insert(process.HLTInitialStepSequence.index(dev), getattr(process, FAILMASK))
            dev.seedFailMask = cms.InputTag(FAILMASK)
            if failMaskValidate:
                process.MessageLogger.cerr.LSTPixelSeedFailMask = cms.untracked.PSet(limit = cms.untracked.int32(-1))
        process.HLTInitialStepSequence.remove(process.hltInputLST)
        process.hltLST.lstInput = 'hltInputLSTDevice'
        conv = process.hltInitialStepTrajectorySeedsLST
        conv.lstInput = 'hltInputLSTDevice:otRecHitPtrs'
        conv.lazyPixelSeeds = cms.bool(True)
        conv.pixelTracks = cms.InputTag('hltPhase2PixelTracks')
        conv.includeFourthHit = cms.bool(seeds.includeFourthHit.value())
        conv.pixelSeedCreatorPSet = seeds.SeedCreatorPSet.clone()
        if mode == 'lazycheck':
            conv.lazyCheckSeeds = cms.InputTag('hltInitialStepSeeds')
            process.MessageLogger.cerr.LSTOutputConverter = cms.untracked.PSet(limit = cms.untracked.int32(-1))
        else:
            process.HLTInitialStepSequence.remove(process.hltInitialStepSeeds)
        return process
    if mode == 'on':
        process.hltInputLSTDevice.seedsFromHost = cms.bool(True)
        process.HLTInitialStepSequence.insert(process.HLTInitialStepSequence.index(process.hltInputLST), process.hltInputLSTDevice)
        process.HLTInitialStepSequence.remove(process.hltInputLST)
        process.hltLST.lstInput = 'hltInputLSTDevice'
        process.hltInitialStepTrajectorySeedsLST.lstInput = 'hltInputLSTDevice:otRecHitPtrs'
        process.hltInitialStepTrajectorySeedsLST.lstPixelSeeds = ['hltInitialStepSeeds']
        return process
    if mode == 'replace':
        process.HLTInitialStepSequence.insert(process.HLTInitialStepSequence.index(process.hltLST), process.hltInputLSTDevice)
        process.hltLST.lstInput = 'hltInputLSTDevice'
        process.MessageLogger.cerr.MkFitAlpakaLstInput = cms.untracked.PSet(limit = cms.untracked.int32(-1))
        return process
    if mode == 'timers':
        process.hltInputLSTDevice.timers = cms.bool(True)
        process.HLTInitialStepSequence.insert(process.HLTInitialStepSequence.index(process.hltLST) + 1, process.hltInputLSTDevice)
        process.MessageLogger.cerr.MkFitAlpakaLstInput = cms.untracked.PSet(limit = cms.untracked.int32(-1))
        return process
    if mode == 'timing':
        process.HLTInitialStepSequence.insert(process.HLTInitialStepSequence.index(process.hltLST) + 1, process.hltInputLSTDevice)
        return process
    process.hltLSTDevIn = process.hltLST.clone(lstInput = 'hltInputLSTDevice')
    process.HLTInitialStepSequence.insert(process.HLTInitialStepSequence.index(process.hltLST) + 1, process.hltInputLSTDevice)
    process.HLTInitialStepSequence.insert(process.HLTInitialStepSequence.index(process.hltInputLSTDevice) + 1, process.hltLSTDevIn)
    if mode == 'compare':
        process.hltLstInTCCompare = cms.EDAnalyzer('MkFitAlpakaLstInputTCCompare',
            reference = cms.InputTag('hltLST'), test = cms.InputTag('hltLSTDevIn'))
        process.HLTInitialStepSequence.insert(process.HLTInitialStepSequence.index(process.hltLSTDevIn) + 1, process.hltLstInTCCompare)
    process.MessageLogger.cerr.MkFitAlpakaLstInput = cms.untracked.PSet(limit = cms.untracked.int32(-1))
    process.MessageLogger.cerr.MkFitAlpakaLstInputTCCompare = cms.untracked.PSet(limit = cms.untracked.int32(-1))
    return process


def customizeLstInDeviceOn(process):
    """Switch ON form (stage B, O6-1 option (i) pLS): hltInputLST removed, LST on the device-built input."""
    return customizeLstInDevice(process, mode='on')


def customizeLstInDeviceCompare(process):
    """Validation form: device-built LST input next to hltInputLST, compared per event; production chain untouched."""
    return customizeLstInDevice(process, mode='compare')


def customizeLstInDeviceLazy(process):
    """Switch ON form without host pixel seeds (round 7): hltInputLST and hltInitialStepSeeds removed, LST on the
    device-built input, pixel seeds made lazily in LSTOutputConverter."""
    return customizeLstInDevice(process, mode='lazy')


def customizeLstInDeviceLazyCheck(process):
    """Validation of 'lazy': hltInitialStepSeeds kept as the bitwise reference of the lazy pixel seeds (LSTOUT_LAZY)."""
    return customizeLstInDevice(process, mode='lazycheck')


def customizeLstInDeviceReplaceFieldCorr(process):
    """Round 8 physics-test arm: 'replace' with the field-corrected pLS momentum (ptFieldCorrection)."""
    return customizeLstInDevice(process, mode='replace', ptFieldCorrection=True)


def customizeLstInDeviceReplace(process):
    """Round 8: 'replace' (R7-H2 decomposition arm: the pLS parameters of option (i) alone, host pLS set)."""
    return customizeLstInDevice(process, mode='replace')


def customizeLstInDeviceLazyFieldCorr(process):
    """Round 8: 'lazy' with the field-corrected pLS momentum."""
    return customizeLstInDevice(process, mode='lazy', ptFieldCorrection=True)


def customizeLstInDeviceLazyMask(process):
    """Round 9: 'lazy' + the creator-failure emulation (LSTPixelSeedFailMask, maxDr 0.05 cm pre-filter)."""
    return customizeLstInDevice(process, mode='lazy', failMask=0.05)


def customizeLstInDeviceLazyMaskAll(process):
    """Round 9: 'lazy' + the creator-failure emulation with the creator on every pixel track (exact by construction)."""
    return customizeLstInDevice(process, mode='lazy', failMask=-1.)


def customizeLstInDeviceLazyMaskCheck(process):
    """Round 9 validation: 'lazycheck' + the mask with validate = True (LSTMASK_MISS = rejections the pre-filter misses;
    LSTOUT_LAZYFAIL = TCs whose pixel seed still fails in the converter)."""
    return customizeLstInDevice(process, mode='lazycheck', failMask=0.05, failMaskValidate=True)


def customizeLstInDeviceLazyMaskHelix(process):
    """Round 10 (stageb): 'lazy' + the creator-failure mask + the helix last-hit pseudo-hit (pseudoLastHit 1)."""
    return customizeLstInDevice(process, mode='lazy', failMask=0.05, pseudoLastHit=1)


def customizeLstInDeviceReplaceHelix(process):
    """Round 10 (stageb) physics-test arm: 'replace' with the helix last-hit pseudo-hit."""
    return customizeLstInDevice(process, mode='replace', pseudoLastHit=1)


def customizeLstInDeviceLazyMaskAnchor(process):
    """Round 10 (stageb): 'lazy' + the creator-failure mask + the PCA re-anchored on the outermost hit (pcaAnchor 1)."""
    return customizeLstInDevice(process, mode='lazy', failMask=0.05, pcaAnchor=1)


def customizeLstInDeviceReplaceAnchor(process):
    """Round 10 (stageb) physics-test arm: 'replace' with the PCA re-anchored on the outermost hit."""
    return customizeLstInDevice(process, mode='replace', pcaAnchor=1)


def _otDev(process, check):
    """Round 9, NO physics change: hltInputLST (host, pLS from the host seeds as stock) skips the OT x/y/z rows
    (otKeysOnly: no OT SoA host copy), hltInputLSTDevice copies its collection to the device and fills the OT rows from
    the device OT rechit SoA; hltLST reads hltInputLSTDevice. Needs full stage D (the OT SoA + on-demand OT hits in the
    LST output converter). check: an unpatched hltInputLST clone is compared column by column (LSTIN_FULLCMP)."""
    inp = process.hltInputLST
    if not hasattr(process, OTDEV) or not hasattr(inp, 'otSoA') or inp.otSoA.getModuleLabel() != OTDEV:
        raise RuntimeError('customizeLstInDeviceOTDev: needs full stage D (customizeOTDevFull or OT_DEVICE) before it')
    if check:
        process.hltInputLSTRef = inp.clone()
        process.HLTInitialStepSequence.insert(process.HLTInitialStepSequence.index(inp), process.hltInputLSTRef)
        process.MessageLogger.cerr.MkFitAlpakaLstInput = cms.untracked.PSet(limit = cms.untracked.int32(-1))
    inp.otKeysOnly = cms.InputTag('hltSiPhase2Clusters')
    inp.otSoA = cms.InputTag('')
    if hasattr(inp, 'compareOTTo'):
        inp.compareOTTo = cms.InputTag('')
    process.hltInputLSTDevice = cms.EDProducer('MkFitAlpakaLstInputProducer@alpaka',
        ptCut = inp.ptCut,
        fromHostInput = cms.bool(True),
        reference = cms.InputTag('hltInputLST'),
        compareTo = cms.InputTag('hltInputLSTRef' if check else ''),
        otSoA = cms.InputTag(OTDEV),
        phase2OTRecHits = cms.InputTag(''),
        otRecHitsSoA = cms.InputTag(CA_OT if hasattr(process, CA_OT) else OTDEV),
        pixelTracksSoA = process.hltPhase2PixelTracks.trackSrc,
        beamSpot = inp.beamSpot,
        alpaka = cms.untracked.PSet(backend = cms.untracked.string('')))
    process.HLTInitialStepSequence.insert(process.HLTInitialStepSequence.index(inp) + 1, process.hltInputLSTDevice)
    process.hltLST.lstInput = 'hltInputLSTDevice'
    from RecoTracker.MkFitAlpaka.customizeMemory import revokeOTSoAEarlyDelete
    revokeOTSoAEarlyDelete(process, 'hltInputLSTDevice', OTDEV)
    return process


def customizeLstInDeviceOTDev(process):
    """Round 9: OT rows of the LST input from the device OT rechit SoA, pLS from hltInputLST as stock (no physics change;
    needs full stage D)."""
    return customizeLstInDevice(process, mode='otdev')


def customizeLstInDeviceOTDevCheck(process):
    """Round 9 validation: 'otdev' + an unpatched hltInputLST clone compared column by column (LSTIN_FULLCMP)."""
    return customizeLstInDevice(process, mode='otdevcheck')
