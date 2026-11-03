# policy_cand.py: the integrated candidate menu of branch itaca-290-extoff-v2 (the extension-off chain), the policy the v2
# classifier and the v2 otpull pair network were trained on. Exec'd at the end of a 75e33(_timing) config built with
# --procModifiers trackingMkFitFit,trackingMkFitAlpaka (PTT_DIR = this directory's parent), then call
#   customizeCandidate(process, pttWP=<classifier working-point json>, otpullWP=[<18 otpull working points>])
# On top of customizeOP (policy_op.py):
#   CA   : forward skip pairs d3-d5, d5-d7, d6-d8, d7-d9 kept (51 pixel pairs); 3-layer tuples with disk outer layers
#          (tripletTuplesOuterDisks); 3-hit storage sized from a count (countTripletTuples; tuple cap 40960 + the count)
#   Torch: the classifier, its per-(pT, |eta|) tables and the high-purity subset for the pixel vertices (pttDeploy)
#   LST  : 3-hit pLS fill-holes candidates (tripletpLSFillHoles); the otpull pair network (customizeOtpullScore); the
#          T5 x pLS cross cleaning of the extension menu kept (t5CleanspLS, which customizeOP turns off; False = the
#          prototype's setting: more efficiency, more duplicates)
#   mkFit: duplicate removal after the final fit (removeDuplicates False in the building, duplicateRemoval in the fit);
#          overlap hits on the first two outer-tracker layers of pT3 / pT5 seeds (seedOverlapHits)
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


def customizeCandidate(process, pttWP, otpullWP, pttModel=CAND_PTT_MODEL, keep=CAND_FORWARD_SKIP_KEEP, t5CleanspLS=True):
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
            m.t5CleanspLS = cms.bool(t5CleanspLS)
            done.append('%s fill-holes, T5 x pLS cleaning %s' % (n, t5CleanspLS))
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
            nf += 1
    assert nb > 0 and nf > 0, 'candidate: no mkFit build / fit module (%d / %d)' % (nb, nf)
    done.append('mkFit: post-fit duplicate removal (fits %d), overlap hits K=%d (builds %d)' % (nf, CAND_OVERLAP_OT_HITS, nb))
    print('CANDIDATE: forward skip pairs kept %s; ' % list(keep) + '; '.join(done))
    return process
