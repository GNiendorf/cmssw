# customizeFitDev.py (round 8, lane fitdev): the switch-gated DEVIATIONS of the device mkFit final fit (doc/DEVIATIONS.txt
# D3, D4). All OFF in the target menu; apply AFTER customizeHLTforMkFitAlpakaTarget (and after customizeOutConv), e.g.
#   --customise RecoTracker/MkFitAlpaka/customizeFitDev.customizeFitDevOutliers
# customizeFitDevOutliers(process, edge=True, rounds=3, dropBadChi2=True) = our mkfitfit patches 0001 + 0005 (fitgap arm b):
#   D3 edgeOutliers / outlierRounds on the device fit module (MkFitAlpakaFitDeviceProducer or MkFitAlpakaFitProducer),
#   D4 dropNegativeChi2 on MkFitAlpakaOutputTrackConverter (needs customizeOutConv; the stock converter has no switch).
import FWCore.ParameterSet.Config as cms

_FD_FIT = 'hltInitialStepTrackCandidatesMkFitFit'
_FD_FIT_DEVICE = 'hltInitialStepTrackCandidatesMkFitFitDevice'
_FD_FIT_TYPES = ('MkFitAlpakaFitDeviceProducer@alpaka', 'MkFitAlpakaFitProducer@alpaka')


def customizeFitDevOutliers(process, edge=True, rounds=3, dropBadChi2=True):
    # every device-fit module, incl. the serial clones of the validation customise (type alpaka_serial_sync::...)
    fits = [m for m in process.producers_().values()
            if m.type_().split('::')[-1].split('@')[0] in ('MkFitAlpakaFitDeviceProducer', 'MkFitAlpakaFitProducer')]
    if not fits:
        raise RuntimeError('customizeFitDevOutliers: no device fit module (%s / %s); apply after the target customise'
                           % (_FD_FIT_DEVICE, _FD_FIT))
    for f in fits:
        f.edgeOutliers = cms.bool(edge)
        f.outlierRounds = cms.int32(rounds)
    if dropBadChi2:
        conv = process.hltInitialStepTracks
        if conv.type_() != 'MkFitAlpakaOutputTrackConverter':
            raise RuntimeError('customizeFitDevOutliers: dropBadChi2 needs MkFitAlpakaOutputTrackConverter as '
                               'hltInitialStepTracks (apply customizeOutConv first), found %s' % conv.type_())
        conv.dropNegativeChi2 = cms.bool(True)
    return process


def customizeFitDevOutliersFitOnly(process):
    """D3 alone (0001 without 0005): edge outliers + 3 rounds, the converter untouched."""
    return customizeFitDevOutliers(process, dropBadChi2=False)


def customizeFitDevStates(process):
    """DEVIATION D6 (R7-H3): the device fit also puts the state at the outermost fitted hit of every track, and the
    output converter fills the TrackExtra inner/outer states (as TrackProducer does in the KF menu) instead of the
    stock mkFit-fit default TrackExtra without states. Needs the device-handoff fit and customizeOutConv."""
    if not hasattr(process, _FD_FIT_DEVICE) or getattr(process, _FD_FIT_DEVICE).type_() != _FD_FIT_TYPES[0]:
        raise RuntimeError('customizeFitDevStates: needs %s (MkFitAlpakaFitDeviceProducer, DEVICE_FIT_FROM_BUILD)'
                           % _FD_FIT_DEVICE)
    conv = process.hltInitialStepTracks
    if conv.type_() != 'MkFitAlpakaOutputTrackConverter':
        raise RuntimeError('customizeFitDevStates: needs MkFitAlpakaOutputTrackConverter as hltInitialStepTracks '
                           '(apply customizeOutConv first), found %s' % conv.type_())
    getattr(process, _FD_FIT_DEVICE).storeOuterState = cms.bool(True)
    conv.outerStates = cms.InputTag(_FD_FIT_DEVICE)
    return process


def customizeFitDevFirstHitProp(process, mode=1):
    """DEVIATION D7 candidate (round 9, R7-M3): propagate to the first hit of a fit pass instead of the trackreco#186
    update without propagation; mode 1 = in the refits after outlier removal (where the innermost hit may have been
    removed), 2 = every pass. Every device-fit module, incl. the serial clones of the validation customise."""
    fits = [m for m in process.producers_().values()
            if m.type_().split('::')[-1].split('@')[0] in ('MkFitAlpakaFitDeviceProducer', 'MkFitAlpakaFitProducer')]
    if not fits:
        raise RuntimeError('customizeFitDevFirstHitProp: no device fit module; apply after the target customise')
    for f in fits:
        f.firstHitProp = cms.int32(mode)
    return process


def customizeFitDevOutliersD7(process):
    """D3 + D4 + D7 (mode 1)."""
    return customizeFitDevFirstHitProp(customizeFitDevOutliers(process), 1)


def customizeFitPhysTrackAlgo(process, algo='initialStep'):
    """DEVIATION D8 candidate (round 10, lane fitphys, R9-H1): the converted tracks get an explicit algorithm (the KF
    menu's TrackProducer AlgorithmName) instead of the stock rule (seeds label without 'Seeds'), which turns the HLT
    label hltInitialStepTrajectorySeedsLST into undefAlgorithm. PFAlgo gives undefAlgorithm a 1e9 pT-error scale
    (PFTrackAlgoTools::errorScale), so rejectTracks_Bad drops ~11 good tracks per ttbar event. Our converter
    (MkFitAlpakaOutputTrackConverter.algorithm); the stock converter is handled by customizeFitPhysStockTrackAlgo."""
    convs = [m for m in process.producers_().values() if m.type_() == 'MkFitAlpakaOutputTrackConverter']
    if not convs:
        raise RuntimeError('customizeFitPhysTrackAlgo: no MkFitAlpakaOutputTrackConverter; apply after the target customise')
    for c in convs:
        c.algorithm = cms.string(algo)
    return process


def customizeFitPhysStockTrackAlgo(process, algo='initialStep'):
    """The same for the STOCK MkFitOutputTrackConverter (trackingMkFitFit menu), without a code change: its seeds input
    is read through an EDAlias named <algo>Seeds, so the stock label rule yields <algo>. Validation / demonstration
    only (the proper stock fix is an explicit parameter, see doc/fitphys.txt)."""
    convs = [(n, m) for n, m in process.producers_().items() if m.type_() == 'MkFitOutputTrackConverter']
    if not convs:
        raise RuntimeError('customizeFitPhysStockTrackAlgo: no MkFitOutputTrackConverter (trackingMkFitFit menu?)')
    for n, c in convs:
        src = c.seeds.getModuleLabel()
        setattr(process, algo + 'Seeds', cms.EDAlias(**{src: cms.EDAlias.allProducts()}))
        c.seeds = cms.InputTag(algo + 'Seeds')
    return process
