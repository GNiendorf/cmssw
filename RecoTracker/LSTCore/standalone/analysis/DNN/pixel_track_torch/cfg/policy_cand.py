# policy_cand.py: the integrated candidate menu of branch itaca-290-extoff-v2 (the extension-off chain), the policy the v2
# classifier and the v2 otpull pair network were trained on. Exec'd at the end of a 75e33(_timing) config built with
# --procModifiers trackingMkFitFit,trackingMkFitAlpaka (PTT_DIR = this directory's parent), then call
#   customizeCandidate(process, pttWP=<classifier working-point json>, otpullWP=[<18 otpull working points>])
# On top of customizeOP (policy_op.py):
#   CA   : forward skip pairs d3-d5, d5-d7, d6-d8, d7-d9 kept (51 pixel pairs); 3-layer tuples with disk outer layers
#          (tripletTuplesOuterDisks); 3-hit storage sized from a count (countTripletTuples; tuple cap 40960 + the count)
#   Torch: the classifier, its per-(pT, |eta|) tables and the high-purity subset for the pixel vertices (pttDeploy; a
#          json from wp_rows.py adds the subset's per-pT rows and three-hit row, e.g. shipped/v2/..._pv.json)
#   LST  : 3-hit pLS fill-holes candidates (tripletpLSFillHoles); the otpull pair network (customizeOtpullScore); the
#          T5 x pLS cross cleaning off (t5CleanspLS False, as customizeOP): its duplicates are removed after the fit
#          instead (sameParticleChi2); True = the extension menu's rule: less efficiency, fewer duplicates
#   mkFit: duplicate removal after the final fit (removeDuplicates False in the building, duplicateRemoval in the fit);
#          overlap hits on the first two outer-tracker layers of pT3 / pT5 seeds (seedOverlapHits); with
#          sameParticleChi2 > 0 the fit's same-particle pass for pixel-seeded / pixel-less tracks without a shared hit
#          (duplicateRemovalSameParticleChi2). CAND_SAME_PARTICLE_CHI2 is not a chi2 quantile: the pixel-less tracks'
#          fitted covariance is far below their scatter, so the cut comes from a scan (lane itaca_extoff/w3/h1): the
#          value that brings the duplicate rate below the extension menu's at a small efficiency cost against the
#          smallest value within the allowance (lane itaca_extoff/w3/integrate, dev + holdout)
#   Memory (memory=True, candidateMemory): the CA buffers sized from the doublet and 3-hit tuple counts
#          (useExactAllocations); the CA tracks deleted after the Torch selector (which then waits for its device work)
#          and the otpull cells after LST, through process.options.canDeleteEarly. No physics change.
# On a menu without the Alpaka mkFit (plain master) the mkFit settings are skipped: they need the matching mkFit code.
import os
import FWCore.ParameterSet.Config as cms

exec(open(os.path.join(os.environ['PTT_DIR'], 'cfg', 'policy_op.py')).read())
exec(open(os.path.join(os.environ['PTT_DIR'], 'cfg', 'deploy_block.py')).read())
exec(open(os.path.join(os.environ['PTT_DIR'], '..', 'otpull_score', 'cfg', 'otpull_score_block.py')).read())

CAND_FORWARD_SKIP_KEEP = ('d3-d5', 'd5-d7', 'd6-d8', 'd7-d9')
CAND_PTT_MODEL = 'RecoTracker/FinalTrackSelectors/data/PixelTrackTorchHighPuritySelector/pixel_track_classifier_noOT.pt'
CAND_MAX_TUPLES = 40960
CAND_TORCH_MAX_TRACKS = 262144
CAND_OVERLAP_OT_HITS = 2
CAND_SAME_PARTICLE_CHI2 = 400.
# product type names (friendly class names) of the CA tracks and of the otpull cells: CUDA, ROCm, host
_CAND_TRACKS_TYPES = ('alpakaDevCudaRt128falserecoTrackBlocksLayoutvoidPortableDeviceCollectionedmDeviceProduct',
                      'alpakaDevHipRt128falserecoTrackBlocksLayoutvoidPortableDeviceCollectionedmDeviceProduct',
                      '128falserecoTrackBlocksLayoutPortableHostCollection')
_CAND_OTPULL_TYPES = ('alpakaDevCudaRt128falselstOTPullCellsSoALayoutvoidPortableDeviceCollectionedmDeviceProduct',
                      'alpakaDevHipRt128falselstOTPullCellsSoALayoutvoidPortableDeviceCollectionedmDeviceProduct',
                      '128falselstOTPullCellsSoALayoutPortableHostCollection')


def _cand_input_tags(v):
    # every InputTag inside a parameter (InputTag, VInputTag, PSet, VPSet)
    if isinstance(v, cms.InputTag):
        yield v
    elif isinstance(v, cms.VInputTag):
        for x in v:
            yield x if isinstance(x, cms.InputTag) else cms.InputTag(x)
    elif isinstance(v, cms.PSet):
        for n in v.parameterNames_():
            yield from _cand_input_tags(getattr(v, n))
    elif isinstance(v, cms.VPSet):
        for p in v:
            yield from _cand_input_tags(p)


def candidateMemory(process):
    """CA buffers sized from counts, and early deletion of the CA tracks (after the Torch selector) and of the otpull
    cells (after LST). Raises if a CA product has a reader other than these."""
    cas = [lab for lab in _OP_CA if hasattr(process, lab)]
    for lab in cas:
        getattr(process, lab).useExactAllocations = cms.bool(True)
    readers = {}
    for n, m in list(process.producers_().items()) + list(process.filters_().items()) + list(process.analyzers_().items()):
        for p in m.parameterNames_():
            for tag in _cand_input_tags(getattr(m, p)):
                if tag.getModuleLabel() in cas:
                    readers.setdefault((tag.getModuleLabel(), tag.getProductInstanceLabel()), set()).add((n, m.type_()))
    deletes = []
    for (lab, inst), rs in sorted(readers.items()):
        if inst == '' and all('PixelTrackTorchHighPuritySelector' in t for _, t in rs):
            for n, _ in rs:
                getattr(process, n).waitBeforeReturn = cms.bool(True)
            types = _CAND_TRACKS_TYPES
        elif inst == 'otpullCells' and all('LSTProducer' in t for _, t in rs):
            types = _CAND_OTPULL_TYPES
        else:
            raise RuntimeError('candidateMemory: %s:%s has readers %s' % (lab, inst, sorted(rs)))
        deletes += ['%s_%s_%s_%s' % (t, lab, inst, process.name_()) for t in types]
    have = set(process.options.canDeleteEarly)
    process.options.canDeleteEarly.extend([b for b in deletes if b not in have])
    return ['%s:%s after %s' % (lab, inst or 'tracks', sorted(n for n, _ in rs)) for (lab, inst), rs in sorted(readers.items())]


def customizeCandidate(process,
                       pttWP,
                       otpullWP,
                       pttModel=CAND_PTT_MODEL,
                       keep=CAND_FORWARD_SKIP_KEEP,
                       t5CleanspLS=False,
                       sameParticleChi2=CAND_SAME_PARTICLE_CHI2,
                       memory=True):
    customizeOP(process, keepForwardSkipPairs=keep)
    done = []
    for lab in _OP_CA:
        if hasattr(process, lab):
            m = getattr(process, lab)
            m.tripletTuplesOuterDisks = cms.bool(True)
            m.countTripletTuples = cms.bool(True)
            m.maxNumberOfTuples = str(CAND_MAX_TUPLES)
            done.append('%s disk triplets, counted 3-hit storage, maxT %s' % (lab, m.maxNumberOfTuples.value()))
    for lab in _OP_HP:
        if hasattr(process, lab):
            getattr(process, lab).maxNumberOfTracks = CAND_TORCH_MAX_TRACKS
    for n, m in process.producers_().items():
        if 'LSTProducer' in m.type_():
            m.tcpLSTriplets = False
            m.tripletpLSFillHoles = cms.bool(True)
            if t5CleanspLS:
                raise RuntimeError('candidate: the T5 x pLS cross cleaning is removed on this branch')
            done.append('%s fill-holes, T5 x pLS cleaning removed' % n)
    pttDeploy(process, pttModel, pttWP)
    customizeOtpullScore(process, wp=otpullWP)
    nb = nf = 0
    for n, m in process.producers_().items():
        if 'MkFitAlpakaBuildProducer' in m.type_():
            m.removeDuplicates = cms.bool(False)
            m.seedOverlapHits = cms.bool(True)
            m.seedOverlapHitsMaxOTHits = cms.int32(CAND_OVERLAP_OT_HITS)
            nb += 1
        if 'MkFitAlpakaFitDeviceProducer' in m.type_():
            m.duplicateRemoval = cms.bool(True)
            m.duplicateRemovalBySurvivorsOnly = cms.bool(True)
            m.duplicateRemovalSameParticleChi2 = cms.double(sameParticleChi2)
            nf += 1
    if nb == 0 and nf == 0:
        done.append('mkFit-side settings skipped (no MkFitAlpaka build / fit module)')
    else:
        assert nb > 0 and nf > 0, 'candidate: no mkFit build / fit module (%d / %d)' % (nb, nf)
        done.append('mkFit: post-fit duplicate removal (fits %d), same-particle chi2 cut %g, overlap hits K=%d (builds %d)'
                    % (nf, sameParticleChi2, CAND_OVERLAP_OT_HITS, nb))
    if memory:
        done.append('memory: exact CA allocations, early delete ' + ', '.join(candidateMemory(process)))
    print('CANDIDATE: forward skip pairs kept %s; ' % list(keep) + '; '.join(done))
    return process
