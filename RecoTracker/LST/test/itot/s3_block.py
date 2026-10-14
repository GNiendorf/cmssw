
# --- lane itot_firm/stack3 (F1): A-star as one arm, on top of itot_w3/arms/scripts/arms_block.py (apply_w3_arm, _arms_caps, _ARMS_*) ---
# Area: itot_firm/stack3/CMSSW_20_1_0_pre3 = #290 + noT5cc + c2 (code) + cacpu S3 window code + three barrel-3 path + Torch
# pass3hit/log code; every new knob defaults to off.
#   S1   : A-star = apply_w3_arm('A0nf') (noOT, 14 forward skip pairs dropped -> 43 pairs, q5, caps 4,194,303 / 40,960)
#          + S3 window (mode 3: inner + outer hit of barrel-barrel cells, A = -4, B = 12 on all 4 barrel layers)
#          + Torch-rt: torch_noot's per-bin fixed-efficiency model tn_scratch_perbin0.993_FP16.pt at threshold 0.5
#          + Torch preselection log on.  (noT5cc and c2 are in the area's code.)
#   S2   : S1 + Ptc at budget 32,768 (barrel-terminal 3-hit path, triplet cleaner on, Torch 3-hit bypass, tcpLSTriplets False;
#          maxNumberOfTuples and Torch maxNumberOfTracks raised by the budget so the main search keeps 40,960)
#   S2p  : S1 + Ptc at budget 62,592 (total tuples 103,552)
# fillStatistics on in every arm (apply_w3_arm); Torch maxPreselectedTracks stays at the menu's 9984.
_S3K_MODEL = 'tn_scratch_perbin0.993_FP16.pt'

def _s3k_window(process, mode=3, a=-4.0, b=12.0):
    import FWCore.ParameterSet.Config as cms
    for lab in _ARMS_CA:
        if hasattr(process, lab):
            m = getattr(process, lab)
            m.sizeWinMode = cms.int32(mode)
            m.sizeWinA = cms.vdouble(*([a] * 4))
            m.sizeWinB = cms.vdouble(*([b] * 4))

def _s3k_torch(process, model, thr):
    import FWCore.ParameterSet.Config as cms
    for lab in _ARMS_HP:
        if hasattr(process, lab):
            m = getattr(process, lab)
            m.model = cms.FileInPath('RecoTracker/FinalTrackSelectors/data/PixelTrackTorchHighPuritySelector/' + model)
            m.scoreThreshold = thr
            m.logPreselection = cms.bool(True)

def _s3k_ptc(process, budget):
    import FWCore.ParameterSet.Config as cms
    for lab in _ARMS_CA:
        if hasattr(process, lab):
            m = getattr(process, lab)
            m.maxBarrelTripletTuples = cms.uint32(budget)
            m.maxNumberOfTuples = str(40960 + budget)
            m.doTripletCleaner = True
    for lab in _ARMS_HP:
        if hasattr(process, lab):
            m = getattr(process, lab)
            m.maxNumberOfTracks = 40960 + budget
            m.passThreeHitTracks = cms.bool(True)
    n_lst = 0
    for lab in [l for l in process.producers_() if l.startswith('hltLST')]:
        m = getattr(process, lab)
        if hasattr(m, 'tcpLSTriplets'):
            m.tcpLSTriplets = False
            n_lst += 1
    assert n_lst >= 1

def apply_s3_arm(process, arm):
    assert arm in ('S1', 'S2', 'S2p'), arm
    apply_w3_arm(process, 'A0nf')
    _s3k_window(process)
    _s3k_torch(process, _S3K_MODEL, 0.5)
    if arm == 'S2':
        _s3k_ptc(process, 32768)
    elif arm == 'S2p':
        _s3k_ptc(process, 62592)
    ca = getattr(process, _ARMS_CA[0]); hp = getattr(process, _ARMS_HP[0])
    print('S3K ARM', arm, 'pairs', len(ca.geometry.pairGraph) // 2, 'maxD', ca.maxNumberOfDoublets.value(), 'maxT', ca.maxNumberOfTuples.value(),
          'chi2', ca.trackQualityCuts.maxChi2TripletsOrQuadruplets.value(), ca.trackQualityCuts.maxChi2Quintuplets.value(),
          'win', ca.sizeWinMode.value(), list(ca.sizeWinA), list(ca.sizeWinB),
          'budget', ca.maxBarrelTripletTuples.value() if hasattr(ca, 'maxBarrelTripletTuples') else 0,
          'tripletCleaner', ca.doTripletCleaner.value(), 'stats', ca.fillStatistics.value(),
          'torch', hp.model.value(), hp.scoreThreshold.value(), 'presel', hp.maxPreselectedTracks.value(),
          'maxTracks', hp.maxNumberOfTracks.value(), 'pass3', hp.passThreeHitTracks.value() if hasattr(hp, 'passThreeHitTracks') else False,
          'log', hp.logPreselection.value())
