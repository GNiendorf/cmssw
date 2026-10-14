# Lane itot_r3/combine: Cfinal2 = Cfinal + otpullT5 A4 (pixel stub for pixel-less T5 TCs, carry, pLS veto, chi2 < 1 / < 0.25
# above 10 GeV) + hp5 (pT5: outward rPhi cut off for pLS pT >= 5 GeV, inward x25) + SO (mkFit duplicate removal by survivors
# only) + otpullmem (early deletion of the CA otpull cells after hltLST; LST/CA memory trims) + atomics (global atomics scope).
# exec'd with `process` in scope. hp5 and the Cfinal pT5 knobs are LST env knobs read on the host: the DRIVER must export
#   LST_PT5_SCORE=1 LST_PT5_RPHIIN_HISCALE=25 LST_PT5_WPHIGH=0.7 LST_PT5_RPHIOUT_HIPOW=-1     (source cfg/env_Cfinal2.sh)
import os as _c2_os
import FWCore.ParameterSet.Config as _c2_cms
exec(open(os.path.join(_ITOT_DIR, 'cfinal_block.py')).read())
exec(open(os.path.join(_ITOT_DIR, 'otpull_earlydel.py')).read())
CFINAL2_ENV = {'LST_PT5_SCORE': '1', 'LST_PT5_RPHIIN_HISCALE': '25', 'LST_PT5_WPHIGH': '0.7', 'LST_PT5_RPHIOUT_HIPOW': '-1'}
CFINAL2_ENV_NOHP5 = {'LST_PT5_SCORE': '1', 'LST_PT5_RPHIIN_HISCALE': '5', 'LST_PT5_WPHIGH': '0.7', 'LST_PT5_RPHIOUT_HIPOW': '0'}


def cfinal2_t5attach(process, attach=5, chi2=1.0, hipt=10.0, chi2hi=0.25, veto=1):
    src = [m.pixelSrcRows for m in process.producers_().values()
           if m.type_().startswith('MkFitAlpakaLstInputProducer') and m.pixelSrcRows.getModuleLabel()]
    assert src, 'cfinal2: no MkFitAlpakaLstInputProducer.pixelSrcRows in the menu'
    out = []
    for n, m in process.producers_().items():
        if m.type_().startswith('LSTProducer'):
            m.otpullAttach = _c2_cms.int32(attach)
            m.otpullAttachChi2Cut = _c2_cms.double(chi2)
            m.otpullAttachHiPt = _c2_cms.double(hipt)
            m.otpullAttachChi2CutHi = _c2_cms.double(chi2hi)
            m.otpullAttachPLSVeto = _c2_cms.int32(veto)
            m.otpullPixelSrcRows = src[0]
            out.append(n)
    print('[cfinal2] T5 attach', attach, 'chi2', chi2, 'hipt', hipt, 'chi2hi', chi2hi, 'veto', veto, 'rows', src[0].value(), 'LST', out)


def cfinal2_so(process, on=True):
    labs = [n for n, m in process.producers_().items() if m.type_().startswith('MkFitAlpakaBuildProducer')]
    assert labs, 'cfinal2: no MkFitAlpakaBuildProducer'
    for n in labs:
        getattr(process, n).duplicateRemovalBySurvivorsOnly = _c2_cms.bool(on)
    print('[cfinal2] mkFit duplicateRemovalBySurvivorsOnly', on, labs)


def cfinal2_check_env(strict=True, allow_diag=False, hp5=True):
    want = CFINAL2_ENV if hp5 else CFINAL2_ENV_NOHP5
    bad = {k: (_c2_os.environ.get(k), v) for k, v in want.items() if _c2_os.environ.get(k) != v}
    for k in ('LST_OTPULL_PRINT', 'LST_OTPULL_TIME', 'CA_OTPULL_TIME', 'LST_OTPULL_T5DUMP', 'LST_OTPULL_T5ROT', 'OTPULL_OFF'):
        if _c2_os.environ.get(k) and not allow_diag:
            bad[k] = (_c2_os.environ.get(k), 'unset (diagnostic)')
    print('[cfinal2] LST env knobs', {k: _c2_os.environ.get(k) for k in list(CFINAL2_ENV) + ['LST_T5CC_MODE', 'LST_T5CC_PTMAX']})
    if bad and strict:
        raise RuntimeError('[cfinal2] export these in the driver (got, want): %s' % bad)
    return not bad


def cfinal2(process, t5=True, so=True, early=True, hp5=True, strict=True, allow_diag=False):
    cfinal_otpull(process, 1.0, 0.15, 0)
    if t5:
        cfinal2_t5attach(process)
    if so:
        cfinal2_so(process, True)
    if early:
        otpull_early_delete(process)
    cfinal2_check_env(strict=strict, allow_diag=allow_diag, hp5=hp5)
    print('[cfinal2] arm t5', t5, 'so', so, 'early delete', early, 'hp5 (env)', hp5)
