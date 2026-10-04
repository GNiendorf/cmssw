# GPU memory (D7-f, round 8, lane mem; doc/mem.txt): the device EventOfHits product (~31 MB per ttbar PU200 event) is
# read only by the device building and the device fit, but as an event product it lived until the end of the event,
# i.e. one copy per EDM stream in flight. customizeMkFitAlpakaEarlyDelete lists it in process.options.canDeleteEarly,
# so the framework deletes it right after its last consumer (the device fit) and its block goes back to the caching
# allocator. The fit checks that it ran on the EventOfHits' queue (eventOfHitsQueue) and waits otherwise, so the
# allocator cannot hand the block to another queue while it is still read.
import FWCore.ParameterSet.Config as cms

# friendly class names of the device EventOfHits product per backend (edm::friendlyname of
# edm::DeviceProduct<PortableDeviceCollection<Dev, EventOfHitsBlocksLayout<128,false>>> on GPU backends, of
# mkfitdev::EventOfHitsPooledHostCollection on CPU backends). Names not in the job are ignored by the framework.
_EOH_TYPES = (
    'alpakaDevCudaRt128falsemkfitdevEventOfHitsBlocksLayoutvoidPortableDeviceCollectionedmDeviceProduct',  # cuda
    'alpakaDevHipRt128falsemkfitdevEventOfHitsBlocksLayoutvoidPortableDeviceCollectionedmDeviceProduct',   # rocm
    'mkfitdevEventOfHitsPooledHostCollection',                                                             # serial
)


def eventOfHitsBranches(process, label):
    return ['%s_%s__%s' % (t, label, process.name_()) for t in _EOH_TYPES]


def customizeMkFitAlpakaEarlyDelete(process, eoh='hltMkFitEventOfHits',
                                    fit='hltInitialStepTrackCandidatesMkFitFitDevice',
                                    build='hltInitialStepTrackCandidatesMkFitDevice'):
    """Delete the device EventOfHits after the device fit. Only when the device building and the device fit are its
    only consumers (the fit's queue guard covers exactly that chain); otherwise nothing is changed."""
    from RecoTracker.MkFitAlpaka.customizeHLTforMkFitAlpaka import _consumers
    if not (hasattr(process, eoh) and hasattr(process, fit) and hasattr(process, build)):
        return process
    if getattr(process, fit).type_() != 'MkFitAlpakaFitDeviceProducer@alpaka':
        return process
    others = [c for c in _consumers(process, eoh) if not (c.startswith(fit + '.') or c.startswith(build + '.'))]
    if others:
        print('customizeMkFitAlpakaEarlyDelete: %s has other consumers %s; not deleted early' % (eoh, others))
        return process
    getattr(process, fit).eventOfHitsQueue = cms.InputTag(eoh, 'queue')
    branches = eventOfHitsBranches(process, eoh)
    have = set(process.options.canDeleteEarly)
    process.options.canDeleteEarly.extend([b for b in branches if b not in have])
    return process


# ---------------------------------------------------------------------------------------------------------------------
# Round 9 (lane mem, D9-f; doc/mem.txt section 6). Full stage D's device OT rechit SoA (MkFitAlpakaOTRecHitsProducer,
# ~46 B per OT cluster, ~11 MB per ttbar PU200 event, a 16 MiB caching block) was an event product held by every event
# in flight (+1.1 GB at the CI shape). Its readers: hltMkFitEventOfHits (device, the hit fill kernel) and hltInputLST
# (the framework's host copy, made by the producer's transform). customizeOTSoAEarlyDelete deletes it right after
# hltMkFitEventOfHits:
#   - otSoAHost: the EventOfHits consumes the host copy, so the transform (which reads the device SoA and is not a
#     consumer the early deletion knows about) runs before it, i.e. before the deletion;
#   - otSoAQueue: the producer's "queue" product; on another queue the EventOfHits makes that queue wait (device side)
#     for its fill kernel, so the caching allocator cannot hand the block to a third queue while it is read.
# Applied only when every reader of the producer's label is known (below); otherwise nothing is changed (printed).
_OT_TYPES = (
    'alpakaDevCudaRt128falsemkfitdevOTRecHitSoALayoutvoidPortableDeviceCollectionedmDeviceProduct',  # cuda
    'alpakaDevHipRt128falsemkfitdevOTRecHitSoALayoutvoidPortableDeviceCollectionedmDeviceProduct',   # rocm
    '128falsemkfitdevOTRecHitSoALayoutPortableHostCollection',  # serial product / host copy on GPU backends
)
# parameters of the CA OT layer modules: they read the producer's OTHER products (CA hit SoA, hitModuleStart)
_OT_CA_PARAMS = ('trackerRecHitsSoA', 'outerTrackerRecHitSoAConverterSrc')


def customizeOTSoAEarlyDelete(process, ot='hltMkFitAlpakaOTRecHits', eoh='hltMkFitEventOfHits'):
    """Delete the device OT rechit SoA right after the EventOfHits (full stage D). Output identical."""
    from RecoTracker.MkFitAlpaka.customizeHLTforMkFitAlpaka import _consumers
    if not (hasattr(process, ot) and hasattr(process, eoh)):
        return process
    e = getattr(process, eoh)
    if e.type_() != 'MkFitAlpakaEventOfHitsProducer@alpaka' or e.otSoA.getModuleLabel() != ot:
        return process
    others = []
    for c in _consumers(process, ot):
        lab, par = c.split('.', 1)
        t = getattr(process, lab).type_()
        if lab == eoh and par in ('otSoA', 'otSoAQueue', 'otSoAHost'):
            continue
        if 'LSTInputProducer' in t and par == 'otSoA':  # host copy (RecoTracker/LST local patch, stage D)
            continue
        if par in _OT_CA_PARAMS:
            continue
        others.append('%s (%s)' % (c, t))
    if others:
        print('customizeOTSoAEarlyDelete: %s has other readers %s; not deleted early' % (ot, others))
        return process
    e.otSoAQueue = cms.InputTag(ot, 'queue')
    e.otSoAHost = cms.InputTag(ot)
    have = set(process.options.canDeleteEarly)
    process.options.canDeleteEarly.extend(
        [b for b in ('%s_%s__%s' % (t, ot, process.name_()) for t in _OT_TYPES) if b not in have])
    return process


def revokeOTSoAEarlyDelete(process, reader, ot='hltMkFitAlpakaOTRecHits'):
    """For a customise applied AFTER the target that adds a DEVICE reader of the OT rechit SoA (lane lstin's
    hltInputLSTDevice): the queue guard covers only the EventOfHits' fill kernel, so the SoA is kept to the end of the
    event again (verify9_int)."""
    branches = set('%s_%s__%s' % (t, ot, process.name_()) for t in _OT_TYPES)
    keep = [b for b in process.options.canDeleteEarly if b not in branches]
    if len(keep) != len(process.options.canDeleteEarly):
        print('revokeOTSoAEarlyDelete: %s reads %s on the device; not deleted early' % (reader, ot))
        process.options.canDeleteEarly = cms.untracked.vstring(*keep)
    return process


# R8-M4 lever 1: the build's host wrapper (MkFitAlpakaOutputWrapperFromTrackSoA, hltInitialStepTrackCandidatesMkFit)
# is scheduled in the target menu, but its only reader (hltInitialStepTrackCandidates) is not. It costs a device->host
# copy of the build TrackSoA, ~0.2 ms of host time, and keeps the build TrackSoA alive to the end of the event.
# customizeDropBuildWrapper replaces it with MkFitAlpakaStatusCheck on the build's status (same per-event LogWarning on
# a non-zero counter, review H4, + a summary line at the end of the job) and, when the device fit is then the build
# TrackSoA's only reader and its EventOfHits queue guard is on (customizeMkFitAlpakaEarlyDelete), deletes the build
# TrackSoA after the fit: the guard (fit on the EventOfHits queue, or wait) covers the fit's reads of it.
_TRK_TYPES = (
    'alpakaDevCudaRt128falsemkfitdevTrackLayoutvoidPortableDeviceCollectionedmDeviceProduct',  # cuda
    'alpakaDevHipRt128falsemkfitdevTrackLayoutvoidPortableDeviceCollectionedmDeviceProduct',   # rocm
    '128falsemkfitdevTrackLayoutPortableHostCollection',                                       # serial
)


def _inSequenceOrTask(process, label):
    for s in list(process.sequences_().values()) + list(process.paths_().values()) + list(process.endpaths_().values()):
        if label in s.moduleNames():
            return True
    for t in process.tasks_().values():
        if label in t.moduleNames():
            return True
    return False


def customizeDropBuildWrapper(process, wrapper='hltInitialStepTrackCandidatesMkFit',
                              build='hltInitialStepTrackCandidatesMkFitDevice',
                              fit='hltInitialStepTrackCandidatesMkFitFitDevice'):
    from RecoTracker.MkFitAlpaka.customizeHLTforMkFitAlpaka import _consumers
    if not (hasattr(process, wrapper) and hasattr(process, build)):
        return process
    w = getattr(process, wrapper)
    if w.type_() != 'MkFitAlpakaOutputWrapperFromTrackSoA' or w.tracks.getModuleLabel() != build:
        return process
    readers = [c for c in _consumers(process, wrapper) if _inSequenceOrTask(process, c.split('.')[0])]
    if readers:
        print('customizeDropBuildWrapper: %s has readers %s; kept' % (wrapper, readers))
        return process
    chk = None
    if w.status.getModuleLabel() != '':
        chk = cms.EDAnalyzer('MkFitAlpakaStatusCheck', src=w.status, requireClean=cms.bool(False),
                             summaryFile=cms.string(''))
        setattr(process, wrapper + 'Status', chk)
    # only in the sequences that hold it DIRECTLY (see customizeOutConv: an edit through an outer sequence copies
    # the nested ones)
    for s in process.sequences_().values():
        c = s._seq
        kids = list(c._collection) if hasattr(c, '_collection') else ([c] if c is not None else [])
        if any(k is w for k in kids):
            if chk is not None:
                s.replace(w, chk)
            else:
                s.remove(w)
    for t in process.tasks_().values():
        t.remove(w)
    # the build TrackSoA after the fit
    f = getattr(process, fit, None)
    if f is None or f.type_() != 'MkFitAlpakaFitDeviceProducer@alpaka' or f.eventOfHitsQueue.getModuleLabel() == '':
        return process
    left = [c for c in _consumers(process, build)
            if _inSequenceOrTask(process, c.split('.')[0]) and not c.startswith(fit + '.')
            and not (chk is not None and c.startswith(wrapper + 'Status.'))]
    if left:
        print('customizeDropBuildWrapper: %s has other readers %s; TrackSoA not deleted early' % (build, left))
        return process
    have = set(process.options.canDeleteEarly)
    process.options.canDeleteEarly.extend(
        [b for b in ('%s_%s__%s' % (t, build, process.name_()) for t in _TRK_TYPES) if b not in have])
    return process
