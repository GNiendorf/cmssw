# customizeOutConv.py (round 7, lane outconv; moved into python/ by the verify7 integrator): stage C, the single host output
# conversion. Apply AFTER customizeHLTforMkFitAlpakaTarget (or ...TargetValidation), e.g.
#   --customise RecoTracker/MkFitAlpaka/customizeOutConv.customizeOutConv
# customizeOutConv(process, validate=False, timing=False, fieldStudy=False)
#   hltInitialStepTracks (stock MkFitOutputTrackConverter on the MkFitOutputWrapper of the device fit) is replaced by
#   MkFitAlpakaOutputTrackConverter reading the device fit's TrackSoA directly; the wrapper module
#   (hltInitialStepTrackCandidatesMkFitFit = MkFitAlpakaOutputWrapperFromTrackSoA) leaves the sequence.
#   validate=True keeps the stock converter as hltInitialStepTracksStockConv (on the wrapper) + MkFitAlpakaOutputTrackCompare
#   (field-by-field, requireIdentical) in the initial-step sequence.
import FWCore.ParameterSet.Config as cms

_OC_FIT = 'hltInitialStepTrackCandidatesMkFitFit'
_OC_FIT_DEVICE = 'hltInitialStepTrackCandidatesMkFitFitDevice'


def customizeOutConv(process, validate=False, timing=False, fieldStudy=False, stockOrder=False, orderStudy=False):
    stock = process.hltInitialStepTracks
    if stock.type_() == 'MkFitAlpakaOutputTrackConverter':
        # already applied (the target customise does it by default since round 8, OUTCONV): only the untracked study
        # switches can still be set; the in-job stock comparison needs the stock converter, i.e. OUTCONV = False
        if validate:
            raise RuntimeError('customizeOutConv(validate=True): hltInitialStepTracks is already '
                               'MkFitAlpakaOutputTrackConverter; set customizeHLTforMkFitAlpaka.OUTCONV = False first')
        stock.timing = cms.untracked.bool(timing)
        stock.fieldStudy = cms.untracked.bool(fieldStudy)
        stock.validateStockOrder = cms.untracked.bool(stockOrder)
        stock.orderStudy = cms.untracked.bool(orderStudy)
        return process
    if stock.type_() != 'MkFitOutputTrackConverter':
        raise RuntimeError('customizeOutConv: hltInitialStepTracks is %s, expected MkFitOutputTrackConverter '
                           '(procModifier trackingMkFitFit + the target customise)' % stock.type_())
    wrap = getattr(process, _OC_FIT)
    if wrap.type_() != 'MkFitAlpakaOutputWrapperFromTrackSoA' or wrap.tracks.getModuleLabel() != _OC_FIT_DEVICE:
        raise RuntimeError('customizeOutConv: %s must be the device-fit TrackSoA wrapper (DEVICE_FIT_FROM_BUILD)' % _OC_FIT)
    if stock.TrajectoryInEvent.value():
        raise RuntimeError('customizeOutConv: TrajectoryInEvent (mtd_at_hlt) needs per-hit states; not supported')
    if stock.src.getModuleLabel() != _OC_FIT:
        raise RuntimeError('customizeOutConv: hltInitialStepTracks.src is %s' % stock.src.value())
    # every other parameter must be at the values the new module implements
    for k, want in (('ttrhBuilder', ('', 'WithTrackAngle')),):
        v = getattr(stock, k, None)
        if v is not None and (v.getModuleLabel(), v.getDataLabel()) != want:
            raise RuntimeError('customizeOutConv: hltInitialStepTracks.%s = %s is not supported' % (k, v.value()))
    new = cms.EDProducer('MkFitAlpakaOutputTrackConverter',
                         tracks=cms.InputTag(_OC_FIT_DEVICE), status=cms.InputTag(_OC_FIT_DEVICE),
                         mkFitPixelHits=stock.mkFitPixelHits, mkFitStripHits=stock.mkFitStripHits, seeds=stock.seeds,
                         propagatorAlong=stock.propagatorAlong, propagatorOpposite=stock.propagatorOpposite,
                         qualityMaxInvPt=stock.qualityMaxInvPt, qualityMinTheta=stock.qualityMinTheta,
                         qualityMaxR=stock.qualityMaxR, qualityMaxZ=stock.qualityMaxZ,
                         qualityMaxPosErr=stock.qualityMaxPosErr, qualitySignPt=stock.qualitySignPt,
                         NavigationSchool=stock.NavigationSchool, measurementTrackerEvent=stock.measurementTrackerEvent,
                         beamSpot=cms.InputTag('offlineBeamSpot'),  # hard-coded in the stock converter (R7-L6)
                         TrajectoryInEvent=cms.bool(False),
                         timing=cms.untracked.bool(timing), fieldStudy=cms.untracked.bool(fieldStudy),
                         validateStockOrder=cms.untracked.bool(stockOrder),
                         orderStudy=cms.untracked.bool(orderStudy))
    if validate:
        process.hltInitialStepTracksStockConv = stock.clone()
    process.hltInitialStepTracks = new
    if not validate:
        # the wrapper has no other reader in the target menu: take it out of the sequences that hold it DIRECTLY.
        # Sequence.remove on an outer sequence copies the nested sequences on its way (the e/gamma L1Seeded sequences
        # then hold private copies of HLTInitialStepSequence, and later edits of it, e.g. customizeLstIn, miss them).
        for s in process.sequences_().values():
            c = s._seq
            kids = list(c._collection) if hasattr(c, '_collection') else ([c] if c is not None else [])
            if any(k is wrap for k in kids):
                s.remove(wrap)
    else:
        process.outconvCompare = cms.EDAnalyzer('MkFitAlpakaOutputTrackCompare',
                                                reference=cms.InputTag('hltInitialStepTracksStockConv'),
                                                target=cms.InputTag('hltInitialStepTracks'),
                                                label=cms.string('outconv'), maxPrint=cms.int32(5),
                                                requireIdentical=cms.bool(False))
        process.HLTInitialStepSequence += cms.Sequence(process.hltInitialStepTracksStockConv + process.outconvCompare)
    return process


def customizeOutConvValidate(process):
    # in-job identity check (default hit order) + per-section timers + field and missing-hit studies
    return customizeOutConv(process, validate=True, timing=True, fieldStudy=True)


def customizeOutConvValidateExact(process):
    # in-job identity check with the stock std::sort hit order (must be IDENTICAL in every field) + timers
    return customizeOutConv(process, validate=True, timing=True, stockOrder=True)


def customizeOutConvValidateTiming(process):
    # in-job identity check (default hit order) + per-section timers, no studies: same-job time of both converters
    return customizeOutConv(process, validate=True, timing=True)


# ---- round 9, lane stagec: the device PCA (MkFitAlpakaOutConvStateProducer@alpaka -> pcaStates) ----
_OC_STATES = 'hltInitialStepOutConvStates'


def customizeOutConvDevicePca(process, check=False):
    """Stage C: the converter takes the state at the beam-line PCA from the device (MkFitAlpakaOutConvStateProducer on
    the device fit's TrackSoA, interface/math/PcaToBeamLine.h) instead of running TSCBLBuilderNoMaterial per track.
    Rows the device cannot do (first-hit state outside the closed-form field volume) or where its TTMD fails fall back
    to the host TSCBL. check=True also runs the host TSCBL for every track and prints [outconv PCACHECK] at endJob
    (output = the device PCA). Apply after the target customise (OUTCONV)."""
    conv = process.hltInitialStepTracks
    if conv.type_() != 'MkFitAlpakaOutputTrackConverter':
        raise RuntimeError('customizeOutConvDevicePca: hltInitialStepTracks is %s, expected '
                           'MkFitAlpakaOutputTrackConverter (target + OUTCONV)' % conv.type_())
    if conv.tracks.getModuleLabel() != _OC_FIT_DEVICE:
        raise RuntimeError('customizeOutConvDevicePca: hltInitialStepTracks.tracks is %s' % conv.tracks.value())
    if not hasattr(conv, 'beamSpot'):
        conv.beamSpot = cms.InputTag('offlineBeamSpot')
    setattr(process, _OC_STATES, cms.EDProducer('MkFitAlpakaOutConvStateProducer@alpaka',
                                                tracks=cms.InputTag(_OC_FIT_DEVICE),
                                                beamSpot=conv.beamSpot))  # = the converter's (R7-L6)
    process.hltMkFitAlpakaTask.add(getattr(process, _OC_STATES))
    conv.pcaStates = cms.InputTag(_OC_STATES)
    conv.pcaCheck = cms.untracked.bool(check)
    return process


def customizeOutConvDevicePcaCheck(process):
    return customizeOutConvDevicePca(process, check=True)


def customizeOutConvNavEmulated(process, study=False):
    """Stage C (c), round 10: the converter's missing inner/outer hit entries from the TkDetLayers search transliterated
    in plugins/OutConvNavEmulation.h (analytic propagator, no material) instead of DetLayer::compatibleDets. Identical
    entries on every call measured (doc/stagec.txt section 8); no host time gain: the reference for the device kernel.
    study=True instead keeps the stock navigation and compares every call in-job ([outconv NAVEMU3]). Apply after the
    target customise (OUTCONV)."""
    conv = process.hltInitialStepTracks
    if conv.type_() != 'MkFitAlpakaOutputTrackConverter':
        raise RuntimeError('customizeOutConvNavEmulated: hltInitialStepTracks is %s, expected '
                           'MkFitAlpakaOutputTrackConverter (target + OUTCONV)' % conv.type_())
    if study:
        conv.navEmuStudy = cms.untracked.bool(True)
    else:
        conv.navEmulated = cms.bool(True)
    return process
