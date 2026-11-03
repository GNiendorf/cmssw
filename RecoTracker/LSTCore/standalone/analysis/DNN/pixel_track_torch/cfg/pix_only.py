# pix_only.py (copied from lane itaca_extoff/base): PIX arm = the menu with the Patatrack CA's OT extension switched off.
# Exec'd at the end of a 75e33(_timing) config (after the trackingMkFitAlpaka process modifier), then call
#   customizePixelTracksPixelOnly(process)
# The switch is the one the menu itself provides: hltPhase2PixelTracksSoA_cfi.py builds its layer pairs from
# 'layerPairs' minus the pairs touching 'layersToExclude' ("[28, 29, 30] excludes the OT"). The same CA module
# (CAHitNtupletAlpakaPhase2OT, same per-layer cuts, same caps) keeps only its pixel-pixel pairs; startingPairs are
# recomputed exactly as the cfi does. The pixel-track converter no longer reads OT hits (useOTExtension False).
# LST and mkFit are unchanged (they still read the device OT rechits).
import FWCore.ParameterSet.Config as cms

PIX_OT_LAYERS = (28, 29, 30)


def customizePixelTracksPixelOnly(process, excluded=PIX_OT_LAYERS):
    from HLTrigger.Configuration.HLT_75e33.modules import hltPhase2PixelTracksSoA_cfi as _ca
    full = _ca.layerPairs
    pairs = [lp for lp in full if lp[0] not in excluded and lp[1] not in excluded]
    done = []
    for lab in ('hltPhase2PixelTracksSoA', 'hltPhase2PixelTracksSoASerialSync'):
        if not hasattr(process, lab):
            continue
        m = getattr(process, lab)
        if m.type_().split('@')[0].split('::')[-1] != 'CAHitNtupletAlpakaPhase2OT':
            raise RuntimeError('pix_only: %s is %s' % (lab, m.type_()))
        g = m.geometry
        if list(g.pairGraph) != sum([[lp[0], lp[1]] for lp in full], []):
            raise RuntimeError('pix_only: %s pairGraph is not the menu cfi one' % lab)
        g.pairGraph = cms.vuint32(sum([[lp[0], lp[1]] for lp in pairs], []))
        g.startingPairs = cms.vuint32([i for i, lp in enumerate(pairs) if lp[2]])
        g.skipsLayers = cms.vuint32([int(lp[3]) for lp in pairs])
        g.phiCuts = cms.vint32([lp[4] for lp in pairs])
        for name, k in (('minInner', 5), ('maxInner', 6), ('minOuter', 7), ('maxOuter', 8), ('maxDR', 9),
                        ('minDZ', 10), ('maxDZ', 11), ('ptCuts', 12)):
            setattr(g, name, cms.vdouble([lp[k] for lp in pairs]))
        done.append('%s pairs %d -> %d, starting %d' % (lab, len(full), len(pairs), len(g.startingPairs)))
    for lab in ('hltPhase2PixelTracks', 'hltPhase2PixelTracksSerialSync'):
        if hasattr(process, lab) and getattr(process, lab).type_() == 'PixelTrackProducerFromSoAAlpaka':
            getattr(process, lab).useOTExtension = False
            done.append('%s useOTExtension False' % lab)
    if not any(d.startswith('hltPhase2PixelTracksSoA ') for d in done):
        raise RuntimeError('pix_only: no hltPhase2PixelTracksSoA in the process')
    print('PIX_ONLY: ' + '; '.join(done))
    return process
