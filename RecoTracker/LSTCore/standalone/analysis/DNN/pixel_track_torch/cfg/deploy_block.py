# deploy_block.py: deploy a retrained pixel-track classifier on a menu config (after the CA policy), then call
#   pttDeploy(process, model, wp_json)
# - both Torch selectors (and their SerialSync copies) get the model (FP32 inference), the per-(pT, |eta|) working-point
#   tables for tuples with more than three hits and with three hits (no blanket three-hit pass), and the high-purity
#   marking of the published subset; the preselection cap stays the menu's (the model runs over the whole cap, so a
#   larger cap costs inference time; the dumps record the largest preselected count per event);
# - hltPhase2PixelTracksHighPurity (quality highPurity: at least four hits and the high-purity cut, or the per-(pT, |eta|)
#   high-purity rows of the json and its three-hit row when present, see wp_rows.py) is made from the
#   same SoA, and the pixel vertices read it; LST and the seed hand-off keep every passing tuple;
# - the validation gets the subset as one more track collection (hltTrackValidator only).
import json
import FWCore.ParameterSet.Config as cms


def pttDeploy(process, model, wp_json, highPurityThreshold=None, verbose=True):
    wp = json.load(open(wp_json))
    hpThr = wp['highPurityThreshold'] if highPurityThreshold is None else highPurityThreshold
    done = []
    for lab in ('hltPhase2PixelTrackTorchHighPuritySelector', 'hltPhase2PixelTrackTorchHighPuritySelectorSerialSync'):
        if not hasattr(process, lab):
            continue
        m = getattr(process, lab)
        m.model = cms.FileInPath(model)
        m.inferenceHalf = cms.bool(False)
        m.passThreeHitTracks = cms.bool(False)
        m.wpPtEdges = cms.vdouble(*wp['ptEdges'])
        m.wpAbsEtaEdges = cms.vdouble(*wp['absEtaEdges'])
        m.wpThresholds = cms.vdouble(*wp['thresholds'])
        m.wpThresholdsThreeHit = cms.vdouble(*wp['thresholdsThreeHit'])
        m.highPurityThreshold = cms.double(hpThr)
        m.highPurityMinNumberOfHits = cms.int32(wp.get('highPurityMinNumberOfHits', 4))
        # optional per-(pT, |eta|) high-purity rows (wp_rows.py): four or more hits, and three-hit tuples
        if 'highPurityThresholds' in wp:
            m.highPurityThresholds = cms.vdouble(*wp['highPurityThresholds'])
        if 'highPurityThresholdsThreeHit' in wp:
            m.highPurityThresholdsThreeHit = cms.vdouble(*wp['highPurityThresholdsThreeHit'])
        done.append('%s model %s presel cap %d hp %.3f' % (lab, model.split('/')[-1], m.maxPreselectedTracks.value(), hpThr))
    for lab in ('hltPhase2PixelTracks', 'hltPhase2PixelTracksSerialSync'):
        if not hasattr(process, lab):
            continue
        conv = getattr(process, lab)
        sub = lab.replace('hltPhase2PixelTracks', 'hltPhase2PixelTracksHighPurity')
        minHits = 3 if 'highPurityThresholdsThreeHit' in wp else wp.get('highPurityMinNumberOfHits', 4)
        setattr(process, sub, conv.clone(minQuality='highPurity', minNumberOfHits=minHits))
        vtx = 'hltPhase2PixelVertices' + ('SerialSync' if 'SerialSync' in lab else '')
        if hasattr(process, vtx):
            getattr(process, vtx).TrackCollection = sub
        for s in process.sequences_().values():
            try:
                i = s.index(conv)
            except Exception:
                continue
            s.insert(i + 1, getattr(process, sub))
        for t in process.tasks_().values():
            if lab in t.moduleNames():
                t.add(getattr(process, sub))
        done.append('%s -> %s (vertices %s)' % (lab, sub, vtx if hasattr(process, vtx) else '-'))
    if hasattr(process, 'hltTrackValidator') and 'hltPhase2PixelTracksHighPurity' not in [str(x) for x in process.hltTrackValidator.label]:
        process.hltTrackValidator.label.append('hltPhase2PixelTracksHighPurity')
    if verbose:
        print('PTT DEPLOY: ' + '; '.join(done))
    return process
