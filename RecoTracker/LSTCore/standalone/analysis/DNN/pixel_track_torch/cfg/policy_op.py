# policy_op.py (copy of lane itaca_extoff/port op_block.py, plus keepForwardSkipPairs: all, none or a subset): OP arm = PIX (base lane cfg/pix_only.py) + the earlier prototype's CA, Torch,
# LST and mkFit settings (Cfinal2 = d73c2adaf82 + e620e6ec7a2 on old #290), ported to branch itaca-290-extoff.
# Exec'd at the end of a 75e33(_timing) config built with --procModifiers trackingMkFitFit,trackingMkFitAlpaka, then call
#   customizeOP(process)
# Needs the itaca-290-extoff code (new parameters: sizeWin*, maxBarrelTripletTuples, otpullExport, passThreeHitTracks,
# LSTProducer otpull*/pT5*/t5CleanspLS, MkFitAlpakaBuildProducer duplicateRemovalBySurvivorsOnly).
# On a menu without the Alpaka mkFit (plain master: LSTOutputConverter seeds, CPU mkFit) the pixel stubs take their
# cluster key -> CA row list from LSTPixelClusterRowsProducer, the LSTOutputConverters add the stub hits, and the
# mkFit-side setting is skipped (it needs the matching mkFit code).
#
# What the prototype's S2 + Cfinal + Cfinal2 set, all applied here:
#   CA   : no OT pairs (PIX), the forward skip pairs (skipsLayers with a disk as inner layer) dropped, q5 (chi2 cut 5 for
#          every tuple length), caps 4194303 doublets / 40960 + 32768 tuples, cluster-size window (mode 3, A -4, B 12),
#          barrel-terminal 3-hit path (budget 32768, triplet cleaner on), export of the terminal cells for LST (otpull)
#   Torch: retrained OT-free model tn_scratch_perbin0.993_cudafix_FP16.pt at 0.5, 3-hit tracks pass, cap 73728
#   LST  : tcpLSTriplets False, otpull (chi2 < 1, phi 0.15, sigma table, pLS filter off, 1024 slots), T5 pixel stub
#          (attach 5, chi2 1 / 0.25 above 10 GeV, pLS veto), pT5 score 1, inward rPhi x25 above 5 GeV, wpHigh 0.7,
#          outward rPhi off above 5 GeV, pT5 chi2 cuts at all pT (c2), no T5 x pLS cross cleaning (noT5cc)
#   mkFit: duplicate removal by survivors only
import os
import FWCore.ParameterSet.Config as cms

exec(open(os.path.join(os.environ['PTT_DIR'], 'cfg', 'pix_only.py')).read())

_OP_CA = ('hltPhase2PixelTracksSoA', 'hltPhase2PixelTracksSoASerialSync')
_OP_HP = ('hltPhase2PixelTrackTorchHighPuritySelector', 'hltPhase2PixelTrackTorchHighPuritySelectorSerialSync')
_OP_MODEL = 'RecoTracker/FinalTrackSelectors/data/PixelTrackTorchHighPuritySelector/tn_scratch_perbin0.993_cudafix_FP16.pt'
_OP_BUDGET = 32768
_OP_MAXD = '4194303'
_OP_MAXT = 40960
# q68 resolution table of the OT-seeded lookup (itot_r1/otpull, QCD dev): 5 variables x 5 T3 pT bins
_OP_OTPULL_SIGMA = [0.1408, 0.0652, 0.0432, 0.0505, 0.0510,
                    0.7877, 0.5830, 0.5203, 0.5950, 0.6413,
                    0.0201, 0.0071, 0.0043, 0.0051, 0.0050,
                    0.0472, 0.0284, 0.0246, 0.0278, 0.0312,
                    0.0176, 0.0078, 0.0050, 0.0069, 0.0071]
_OP_PER_PAIR = ('skipsLayers', 'phiCuts', 'minInner', 'maxInner', 'minOuter', 'maxOuter', 'maxDR', 'minDZ', 'maxDZ', 'ptCuts')


def _op_forward_skip_label(p):
    # TFPX disk d -> disk d + 2: CA layers (3 + d, 5 + d) on the positive side, (15 + d, 17 + d) on the negative side
    return 'd%d-d%d' % ((p[0] - 3, p[1] - 3) if p[0] < 16 else (p[0] - 15, p[1] - 15))


def _op_drop_forward_skip_pairs(m, keep=()):
    g = m.geometry
    pg = list(g.pairGraph)
    pairs = [(pg[2 * i], pg[2 * i + 1]) for i in range(len(pg) // 2)]
    sk = list(g.skipsLayers)
    fwd = [i for i, p in enumerate(pairs) if sk[i] and 4 <= p[0] < 28]
    assert len(fwd) == 14, [pairs[i] for i in fwd]
    drop = [i for i in fwd if _op_forward_skip_label(pairs[i]) not in keep]
    starting = list(g.startingPairs)
    assert not any(i in starting for i in drop), 'a forward skip pair is a starting pair'
    kept = [i for i in range(len(pairs)) if i not in drop]
    newidx = {o: n for n, o in enumerate(kept)}
    for name in _OP_PER_PAIR:
        v = list(getattr(g, name))
        assert len(v) == len(pairs), name
        getattr(g, name).setValue([v[i] for i in kept])
    g.pairGraph = cms.vuint32(*[x for i in kept for x in pairs[i]])
    g.startingPairs = cms.vuint32(*[newidx[i] for i in starting])
    return [pairs[i] for i in drop], len(kept)


def _op_pixel_cluster_rows(process, label='hltLSTPixelClusterRows'):
    """menu without the Alpaka mkFit: the cluster key -> CA row list of the pixel stubs from the legacy pixel rechits
    the pixel tracks are made of, scheduled before every LSTProducer; the LSTOutputConverters add the stub hits"""
    src = process.hltPhase2PixelTracks.pixelRecHitLegacySrc.value()
    setattr(process, label, cms.EDProducer('LSTPixelClusterRowsProducer', pixelRecHits=cms.InputTag(src)))
    rows = getattr(process, label)
    lsts = [m for m in process.producers_().values() if 'LSTProducer' in m.type_()]
    for s in process.sequences_().values():
        for m in lsts:
            try:
                i = s.index(m)
            except Exception:
                continue
            s.insert(i, rows)
            break
    for t in process.tasks_().values():
        if any(m.label_() in t.moduleNames() for m in lsts):
            t.add(rows)
    convs = [n for n, m in process.producers_().items() if m.type_() == 'LSTOutputConverter']
    for n in convs:
        getattr(process, n).pixelStubRecHits = cms.InputTag(src)
    return cms.InputTag(label), convs


def customizeOP(process, budget=_OP_BUDGET, keepForwardSkipPairs=False):
    """keepForwardSkipPairs: True keeps the menu's 14 forward skip pairs (all pixel pairs of PIX), False drops them, a
    tuple of labels 'dA-dB' (TFPX disk A -> disk B, both sides) keeps that subset"""
    customizePixelTracksPixelOnly(process)
    done = []
    ca_labels = [l for l in _OP_CA if hasattr(process, l)]
    for lab in ca_labels:
        m = getattr(process, lab)
        if keepForwardSkipPairs is True:
            dropped, npairs = [], len(m.geometry.pairGraph) // 2
        else:
            keep = tuple(keepForwardSkipPairs) if keepForwardSkipPairs else ()
            dropped, npairs = _op_drop_forward_skip_pairs(m, keep)
            assert len(dropped) == 14 - 2 * len(keep), dropped
        m.trackQualityCuts.maxChi2TripletsOrQuadruplets = 5.0
        m.trackQualityCuts.maxChi2Quintuplets = 5.0
        m.maxNumberOfDoublets = _OP_MAXD
        m.maxNumberOfTuples = str(_OP_MAXT + budget)
        m.sizeWinMode = cms.int32(3)
        m.sizeWinA = cms.vdouble(*([-4.0] * 4))
        m.sizeWinB = cms.vdouble(*([12.0] * 4))
        m.maxBarrelTripletTuples = cms.uint32(budget)
        m.doTripletCleaner = True
        m.otpullExport = cms.bool(True)
        done.append('%s pairs %d (forward skip dropped %d) maxD %s maxT %s' % (lab, npairs, len(dropped),
                                                                               m.maxNumberOfDoublets.value(),
                                                                               m.maxNumberOfTuples.value()))
    for lab in _OP_HP:
        if hasattr(process, lab):
            m = getattr(process, lab)
            assert 'PixelTrackTorchHighPuritySelector' in m.type_(), m.type_()
            m.model = cms.FileInPath(_OP_MODEL)
            m.scoreThreshold = 0.5
            m.maxNumberOfTracks = _OP_MAXT + budget
            m.passThreeHitTracks = cms.bool(True)
            done.append('%s torch-rt pass3hit maxTracks %d' % (lab, m.maxNumberOfTracks.value()))
    rows = [m.pixelSrcRows for m in process.producers_().values()
            if 'MkFitAlpakaLstInputProducer' in m.type_() and m.pixelSrcRows.getModuleLabel()]
    if not rows:
        tag, convs = _op_pixel_cluster_rows(process)
        rows = [tag]
        done.append('stub rows %s, stub hits in %s' % (tag.getModuleLabel(), convs))
    main_ca = [c for c in ca_labels if 'SerialSync' not in c]
    for n, m in process.producers_().items():
        if 'LSTProducer' not in m.type_():
            continue
        src = [c for c in ca_labels if ('SerialSync' in c) == ('SerialSync' in n)] or main_ca
        m.tcpLSTriplets = False
        m.otpullCells = cms.InputTag(src[0], 'otpullCells')
        m.otpullChi2Cut = cms.double(1.0)
        m.otpullSigma = cms.vdouble(*_OP_OTPULL_SIGMA)
        m.otpullPtEdges = cms.vdouble(1.5, 3., 6., 15.)
        m.otpullPhiWindow = cms.double(0.15)
        m.otpullNPhiBins = cms.int32(512)
        m.otpullNShells = cms.int32(15)
        m.otpullShellWidth = cms.double(2.0)
        m.otpullPLSMatch = cms.int32(0)
        m.otpullCapacity = cms.uint32(1024)
        m.otpullAttach = cms.int32(5)
        m.otpullAttachChi2Cut = cms.double(1.0)
        m.otpullAttachHiPt = cms.double(10.0)
        m.otpullAttachChi2CutHi = cms.double(0.25)
        m.otpullAttachPLSVeto = cms.int32(1)
        m.otpullPixelSrcRows = rows[0]
        m.pT5ScoreMode = cms.int32(1)
        m.pT5RPhiInHiScale = cms.double(25.)
        m.pT5WpHigh = cms.double(0.7)
        m.pT5RPhiOutHiPow = cms.double(-1.)
        m.pT5Chi2CutsAllPt = cms.bool(True)
        done.append('%s otpull %s attach 5 pT5 opts, noT5cc, c2, tcpLSTriplets False' % (n, m.otpullCells.value()))
    so = [n for n, m in process.producers_().items() if 'MkFitAlpakaBuildProducer' in m.type_()]
    for n in so:
        getattr(process, n).duplicateRemovalBySurvivorsOnly = cms.bool(True)
    done.append('mkFit survivors-only %s' % so if so else 'mkFit survivors-only skipped (no MkFitAlpakaBuildProducer)')
    print('OP: ' + '; '.join(done))
    return process
