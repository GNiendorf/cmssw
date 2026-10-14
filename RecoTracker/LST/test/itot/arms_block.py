
# --- lane itot_w2/otpairs: keep a subset of the CA OT-extension layer pairs (layers 28-30 = OT barrel L1-L3) ---
# Groups of the 16 pairs that touch layers 28-30 in the #290 menu (HLT_75e33 hltPhase2PixelTracksSoA_cfi):
#   B = pixel barrel -> OT1   : (2,28) x3 z-bands, (3,28)                                   4 pairs
#   D = pixel disk   -> OT1   : (4..8,28), (16..20,28)                                     10 pairs
#   O = OT1 -> OT2 -> OT3     : (28,29), (29,30)                                            2 pairs
# OT_KEEP is a string of group letters to keep ('BDO' = the menu, '' = no OT extension).
# Every per-pair vector is re-indexed together and startingPairs remapped (no OT pair is a starting pair).
# Optional: OT_MAXDOUBLETS / OT_MAXTUPLES (strings, may be formulas of x = nHits), OT_STATS (fillStatistics).
def _otpairs_group(p):
    if max(p) < 28:
        return None
    if p[0] >= 28:
        return 'O'
    return 'B' if p[0] <= 3 else 'D'

def _otpairs_variant(process, keep, maxd=None, maxt=None, stats=False):
    import FWCore.ParameterSet.Config as cms
    for lab in ('hltPhase2PixelTracksSoA', 'hltPhase2PixelTracksSoASerialSync'):
        if not hasattr(process, lab):
            continue
        mod = getattr(process, lab)
        g = mod.geometry
        pg = list(g.pairGraph)
        pairs = [(pg[2 * i], pg[2 * i + 1]) for i in range(len(pg) // 2)]
        kept = [i for i, p in enumerate(pairs) if _otpairs_group(p) is None or _otpairs_group(p) in keep]
        newidx = {o: n for n, o in enumerate(kept)}
        for name in ('maxDR', 'maxDZ', 'maxInner', 'maxOuter', 'minDZ', 'minInner', 'minOuter', 'phiCuts', 'ptCuts', 'skipsLayers'):
            v = list(getattr(g, name))
            assert len(v) == len(pairs), name
            getattr(g, name).setValue([v[i] for i in kept])
        assert all(_otpairs_group(pairs[i]) is None for i in g.startingPairs)
        g.pairGraph = cms.vuint32(*[x for i in kept for x in pairs[i]])
        g.startingPairs = cms.vuint32(*[newidx[i] for i in list(g.startingPairs) if i in newidx])
        if maxd is not None:
            mod.maxNumberOfDoublets = maxd
        if maxt is not None:
            mod.maxNumberOfTuples = maxt
        if stats:
            mod.fillStatistics = True
        cnt = {}
        for i in kept:
            k = _otpairs_group(pairs[i]) or 'IT'
            cnt[k] = cnt.get(k, 0) + 1
        print('OTPAIRS VARIANT keep=%r %s pairs %d (%s) starting %d maxDoublets %s maxTuples %s stats %s' % (
            keep, lab, len(kept), ' '.join('%s:%d' % kv for kv in sorted(cnt.items())), len(g.startingPairs),
            mod.maxNumberOfDoublets.value(), mod.maxNumberOfTuples.value(), mod.fillStatistics.value()))

# --- lane itot_w3/arms: workflow-3 config arms on top of _otpairs_variant (all with fillStatistics=True) ---
#   base   : #290 (OT groups BDO), base capacities 6.0M / 122880
#   baseq5 : base + q5 (one CA chi2 cut 5.0 for every tuple length; itot_w2/trace/cfg/variant_only.py '_q5')
#   A0     : noOT + caps 4194303 doublets / 40960 tuples (Torch HP maxNumberOfTracks 40960) + q5
#   Ap     : A' = OT group B only (pixel barrel -> TB1) + q5, base capacities
#   App    : A'' = OT groups B + O + q5, base capacities
#   A0nf   : A0 without the CA's forward skip pairs (skipsLayers=1 with a disk as inner layer: TFPX Fn->Fn+2, F7->E1; 14 pairs)
_ARMS_CA = ('hltPhase2PixelTracksSoA', 'hltPhase2PixelTracksSoASerialSync')
_ARMS_HP = ('hltPhase2PixelTrackTorchHighPuritySelector', 'hltPhase2PixelTrackTorchHighPuritySelectorSerialSync')

def _arms_q5(process):
    for lab in _ARMS_CA:
        if hasattr(process, lab):
            getattr(process, lab).trackQualityCuts.maxChi2TripletsOrQuadruplets = 5.0
            getattr(process, lab).trackQualityCuts.maxChi2Quintuplets = 5.0
    print('ARMS q5: chi2 cut 5.0 for all tuple lengths')

def _arms_drop_fskip(process):
    import FWCore.ParameterSet.Config as cms
    for lab in _ARMS_CA:
        if not hasattr(process, lab):
            continue
        g = getattr(process, lab).geometry
        pg = list(g.pairGraph)
        pairs = [(pg[2 * i], pg[2 * i + 1]) for i in range(len(pg) // 2)]
        sk = list(g.skipsLayers)
        drop = [i for i, p in enumerate(pairs) if sk[i] and 4 <= p[0] < 28]
        assert not any(i in list(g.startingPairs) for i in drop)
        kept = [i for i in range(len(pairs)) if i not in drop]
        newidx = {o: n for n, o in enumerate(kept)}
        for name in ('maxDR', 'maxDZ', 'maxInner', 'maxOuter', 'minDZ', 'minInner', 'minOuter', 'phiCuts', 'ptCuts', 'skipsLayers'):
            v = list(getattr(g, name))
            getattr(g, name).setValue([v[i] for i in kept])
        g.pairGraph = cms.vuint32(*[x for i in kept for x in pairs[i]])
        g.startingPairs = cms.vuint32(*[newidx[i] for i in list(g.startingPairs)])
        print('ARMS drop forward skip pairs', lab, [pairs[i] for i in drop], 'pairs left', len(kept))

def _arms_caps(process, maxd, maxt):
    for lab in _ARMS_CA:
        if hasattr(process, lab):
            getattr(process, lab).maxNumberOfDoublets = maxd
            getattr(process, lab).maxNumberOfTuples = maxt
    for lab in _ARMS_HP:
        if hasattr(process, lab):
            getattr(process, lab).maxNumberOfTracks = int(maxt)
    print('ARMS caps', maxd, maxt)

# Torch HP preselection cap (added 11:10 pm after baseq5): the selector keeps quality >= tight tuples in index order and scores only
# the first maxPreselectedTracks (9984 = 2 batches of 4992), silently dropping the rest. q5 on an OT-extended CA makes most of its
# ~19k post-cleaning tuples per event tight, so the tail (forward-starting tuples) is dropped. Arms suffixed P raise the cap to
# 59904 (12 batches, above every observed per-event count of quality >= strict tuples, an upper bound on tight): physics without
# truncation; Torch then scores 12 batches per event instead of 2 (a timing cost, not measured here).
#   ApP = Ap + cap 59904, AppP = App + cap 59904, baseq5P = baseq5 + cap 59904, basePS = base + cap 59904 (does #290 truncate?)
def _arms_presel(process, n):
    for lab in _ARMS_HP:
        if hasattr(process, lab):
            getattr(process, lab).maxPreselectedTracks = n
    print('ARMS Torch maxPreselectedTracks', n)

def apply_w3_arm(process, arm):
    keep = {'base': 'BDO', 'baseq5': 'BDO', 'A0': '', 'Ap': 'B', 'App': 'BO', 'A0nf': '',
            'ApP': 'B', 'AppP': 'BO', 'baseq5P': 'BDO', 'basePS': 'BDO'}[arm]
    _otpairs_variant(process, keep, stats=True)
    if arm not in ('base', 'basePS'):
        _arms_q5(process)
    if arm in ('ApP', 'AppP', 'baseq5P', 'basePS'):
        _arms_presel(process, 59904)
    if arm in ('A0', 'A0nf'):
        _arms_caps(process, '4194303', '40960')
    if arm == 'A0nf':
        _arms_drop_fskip(process)
    print('ARMS W3 ARM', arm, 'applied')
