# otpull_score_block.py: switch the otpull T3 lookup of every LSTProducer in a menu to the pair network (otpullScoreMode 1)
# with a working-point table from wp_otpull.py. Exec'd after a menu that already enables otpull (e.g. customizeOP), then
#   customizeOtpullScore(process, wp=[...18 values, row-major pT x |eta|...])
import FWCore.ParameterSet.Config as cms


def customizeOtpullScore(process, wp, window=3.0, jrmsCut=5.0, ptEdges=(2., 5., 10., 25., 50.), etaEdges=(0.9, 1.5)):
    assert len(wp) == (len(ptEdges) + 1) * (len(etaEdges) + 1)
    n = 0
    for lab, m in process.producers_().items():
        if 'LSTProducer' not in m.type_():
            continue
        assert m.otpullCells.getModuleLabel(), 'otpull is not enabled on %s' % lab
        m.otpullScoreMode = cms.int32(1)
        m.otpullScoreWindow = cms.double(window)
        m.otpullScoreJrmsCut = cms.double(jrmsCut)
        m.otpullScoreWPPtEdges = cms.vdouble(*ptEdges)
        m.otpullScoreWPEtaEdges = cms.vdouble(*etaEdges)
        m.otpullScoreWP = cms.vdouble(*wp)
        n += 1
    assert n, 'no LSTProducer'
    print('otpull score: %d LSTProducer(s), window %g, jrms %g, WP %s' % (n, window, jrmsCut, list(wp)))
    return process
