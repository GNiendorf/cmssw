# Stage D (round 8, lane otdev; doc/otdev.txt): device OT rechits (MkFitAlpakaOTRecHitsProducer).
#   customizeOTDevGate     validation: the producer runs next to hltSiPhase2RecHits (HLTOtLocalRecoSequence) and compares
#                          bitwise with the legacy rechits (OTDEV_CMP) and with the stock CA OT hit SoA
#                          (hltPhase2OtRecHitsSoA, OTDEV_CA); hltMkFitEventOfHits takes its OT rows from the SoA and
#                          compares its HitSoA with the stock converters (compareHostHits, DEVICE_HITS_EOH). Waits.
#   customizeOTDev         production arm: the SoA feeds the CA OT layers (hltPhase2OtRecHitsSoA leaves the menu) and
#                          hltMkFitEventOfHits (no host OT pass). hltSiPhase2RecHits stays (LST input, LST output
#                          converter, pixel tracks' hit clones and the output converter's index map still read it).
import FWCore.ParameterSet.Config as cms

OTDEV = 'hltMkFitAlpakaOTRecHits'
CA_OT = 'hltPhase2OtRecHitsSoA'
EOH = 'hltMkFitEventOfHits'


def _producer(process, compare):
    return cms.EDProducer(
        "MkFitAlpakaOTRecHitsProducer@alpaka",
        src=cms.InputTag("hltSiPhase2Clusters"),
        Phase2StripCPE=process.hltSiPhase2RecHits.Phase2StripCPE,
        contractLocal=cms.int32(1),
        contractGlobal=cms.int32(1),
        compareTo=cms.InputTag("hltSiPhase2RecHits" if compare else ""),
        produceCAHits=cms.bool(True),
        beamSpot=getattr(process, CA_OT).beamSpot,
        pixelRecHitSoASource=getattr(process, CA_OT).pixelRecHitSoASource,
        contractCA=cms.int32(1),
        compareCATo=cms.InputTag(CA_OT if compare else ""),
    )


def _eoh(process, compare):
    eoh = getattr(process, EOH, None)
    if eoh is None or eoh.type_() != 'MkFitAlpakaEventOfHitsProducer@alpaka' or not eoh.deviceHits.value():
        raise RuntimeError('customizeOTDev: needs the target menu (device hit input of %s)' % EOH)
    eoh.otSoA = cms.InputTag(OTDEV)
    if compare:
        eoh.compareHostHits = cms.bool(True)


def _refuseIfFull(process, func):
    eoh = getattr(process, EOH, None)
    if eoh is not None and hasattr(eoh, 'otRecHits') and eoh.otRecHits.getModuleLabel() == '':
        raise RuntimeError('%s: full stage D is already applied (customizeHLTforMkFitAlpaka.OT_DEVICE = True in the target); '
                           'the bitwise gate needs the legacy hltSiPhase2RecHits as its reference: set OT_DEVICE = False '
                           'before the target customise' % func)


def customizeOTDevGate(process):
    _refuseIfFull(process, 'customizeOTDevGate')
    setattr(process, OTDEV, _producer(process, True))
    # right after the stock CA OT converter (it compares with its products; both follow hltSiPhase2RecHits)
    ca, ot = getattr(process, CA_OT), getattr(process, OTDEV)
    for s in process.sequences_().values():
        s.replace(ca, cms.Sequence(ca + ot))
    _eoh(process, True)
    return process


def customizeOTDev(process):
    setattr(process, OTDEV, _producer(process, False))
    process.hltMkFitAlpakaTask.add(getattr(process, OTDEV))
    # CA OT layers: every reader of the stock converter's products reads ours (same types, same label semantics)
    for name, m in process.producers_().items():
        for p in ('trackerRecHitsSoA', 'outerTrackerRecHitSoAConverterSrc'):
            if hasattr(m, p) and getattr(m, p).getModuleLabel() == CA_OT:
                setattr(m, p, cms.InputTag(OTDEV))
    for s in process.sequences_().values():
        s.remove(getattr(process, CA_OT))
    for t in process.tasks_().values():
        t.remove(getattr(process, CA_OT))
    delattr(process, CA_OT)
    _eoh(process, False)
    return process


# ---------------------------------------------------------------------------------------------------------------------
# Full stage D (local patches in RecoLocalTracker/Phase2TrackerRecHits, RecoTracker/PixelTrackFitting, RecoTracker/LST,
# RecoTracker/MkFit): every reader of hltSiPhase2RecHits switched to the SoA or to on-demand hits, the legacy OT rechit
# producer leaves the menu.
#   hltInputLST                  OT detId / size / global position from the SoA (host copy), no hit pointers
#   LSTOutputConverter           OT hits of the TCs on demand (Phase2TrackerRecHitOnDemand)
#   PixelTrackProducerFromSoA..  OT hits of the pixel tracks on demand
#   hltMkFitSiPhase2Hits         size-only index map (build/fit read its size)
#   MkFitOutputTrackConverter    OT hits of the output tracks on demand
OT_CLUSTERS = 'hltSiPhase2Clusters'
OT_RECHITS = 'hltSiPhase2RecHits'
# Surface::toGlobal as LSTInputProducer.cc compiles it: fuse first (LSTIN_OT: 0 differences on 5.66M ttbar hits; fuse
# second differs on 275k of them)
CONTRACT_LST = 1


def _paramTags(pset, prefix=''):
    """(name, InputTag-like value) of every InputTag / VInputTag / string parameter, recursively."""
    out = []
    for n in pset.parameterNames_():
        v = getattr(pset, n)
        t = type(v).__name__
        if t == 'InputTag':
            out.append((prefix + n, v.getModuleLabel()))
        elif t == 'VInputTag':
            out += [(prefix + n, x.getModuleLabel() if hasattr(x, 'getModuleLabel') else str(x).split(':')[0]) for x in v]
        elif t == 'PSet':
            out += _paramTags(v, prefix + n + '.')
        elif t == 'VPSet':
            for i, p in enumerate(v):
                out += _paramTags(p, '%s%s[%d].' % (prefix, n, i))
    return out


def _onDemand(process, compare):
    cpe = process.hltSiPhase2RecHits.Phase2StripCPE
    ot = getattr(process, OTDEV)
    ot.contractGlobal = cms.int32(CONTRACT_LST)
    # the legacy InputTags these modules no longer read are blanked (so the reader scan below sees only real readers)
    blank = cms.InputTag('')
    getattr(process, EOH).otRecHits = blank
    for name, m in process.producers_().items():
        t = m.type_()
        if t.endswith('LSTInputProducer') or t.startswith('LSTInputProducer'):
            m.otSoA = cms.InputTag(OTDEV)
            m.phase2OTRecHits = blank
            if compare:
                m.compareOTTo = cms.InputTag(OT_RECHITS)
        elif t == 'LSTOutputConverter':
            m.otClustersOnDemand = cms.InputTag(OT_CLUSTERS)
            m.Phase2StripCPE = cpe
        elif t == 'PixelTrackProducerFromSoAAlpaka' and m.useOTExtension.value():
            m.otClustersOnDemand = cms.InputTag(OT_CLUSTERS)
            m.Phase2StripCPE = cpe
            m.outerTrackerRecHitSrc = blank
        elif t == 'MkFitAlpakaPhase2ClusterIndexToHit' and name == 'hltMkFitSiPhase2Hits':
            m.sizeFromClusters = cms.InputTag(OT_CLUSTERS)
            m.hits = blank
        elif t in ('MkFitOutputTrackConverter', 'MkFitAlpakaOutputTrackConverter'):
            m.otClustersOnDemand = cms.InputTag(OT_CLUSTERS)
            m.Phase2StripCPE = cpe


def _isValidation(m):
    # DQM validation modules (DQMEDAnalyzers are EDProducers in the configuration, e.g. Phase2OTValidateRecHit)
    return 'Validat' in m.type_()


def _readersOf(process, label, skip=(), analyzers=False):
    """Configured readers of label: production modules, or (analyzers=True) only validation/analysis modules."""
    bad = []
    mods = list(process.producers_().items()) + list(process.filters_().items()) + list(process.analyzers_().items())
    for name, m in mods:
        isVal = name in process.analyzers_() or _isValidation(m)
        if name in skip or isVal != analyzers:
            continue
        for p, v in _paramTags(m):
            if v == label:
                bad.append('%s.%s (%s)' % (name, p, m.type_()))
    return bad


def customizeOTDevFull(process):
    """Production arm of full stage D: hltSiPhase2RecHits OFF."""
    if hasattr(process, OTDEV) and getattr(process, EOH).otRecHits.getModuleLabel() == '':
        return process  # already applied (the target customise does it when OT_DEVICE is set)
    customizeOTDev(process)
    _onDemand(process, False)
    # readers left: only validation-only modules that run on request (stock converter copies, compare modules)
    skip = ('hltMkFitSiPhase2HitsStock', OT_RECHITS)
    bad = _readersOf(process, OT_RECHITS, skip)
    if bad:
        raise RuntimeError('customizeOTDevFull: readers of %s left: %s' % (OT_RECHITS, bad))
    for s in process.sequences_().values():
        s.remove(getattr(process, OT_RECHITS))
    for t in process.tasks_().values():
        t.remove(getattr(process, OT_RECHITS))
    # validation (DQM) analyzers of the legacy rechits (e.g. hltRechitValidOT): the producer stays available
    # unscheduled, so it runs only in jobs that have such a reader (never in the timing menu)
    if _readersOf(process, OT_RECHITS, analyzers=True):
        process.hltMkFitAlpakaTask.add(getattr(process, OT_RECHITS))
    return process


def customizeOTDevFullGate(process):
    """Validation of full stage D: the gate (bitwise SoA / CA / EventOfHits checks) + all readers on the SoA or
    on-demand hits + hltInputLST's bitwise check of its OT hits (LSTIN_OT); hltSiPhase2RecHits still runs (it is the
    reference of the checks)."""
    _refuseIfFull(process, 'customizeOTDevFullGate')
    customizeOTDevGate(process)
    _onDemand(process, True)
    # the CA OT readers keep the stock converter in the gate (its products are compared); the EOH reads the SoA
    return process
