# Lane itot_r2/integrate: Cfinal on top of S2 (python part). exec'd with `process` in scope.
# Cfinal = S2 + otpull (GPU-capable device path, lane itot_r2/otpull_gpu: chi2 < 1.0, phi 0.15 rad, no pLS-hit filter)
#        + arb sc (pT5 rank score) + A5 (pT5 inward rPhi cut at all T5 radii, x5 above 5 GeV; kWpHigh_pT5 0.7).
# The otpull settings are module parameters set here; sc and A5 are LST env knobs read once on the host by getenv, so the
# DRIVER must export them (a config cannot: the benchmark re-dumps the config in a subprocess):
#   LST_PT5_SCORE=1 LST_PT5_RPHIIN_HISCALE=5 LST_PT5_WPHIGH=0.7     (optional dup lever: LST_T5CC_MODE=1 LST_T5CC_PTMAX=1.5)
import os as _cf_os
import FWCore.ParameterSet.Config as _cf_cms

# q68 resolution table of the OT-seeded lookup (round-1 itot_r1/otpull, QCD dev): 5 variables x 5 T3 pT bins
_CF_OTPULL_SIGMA = [0.1408, 0.0652, 0.0432, 0.0505, 0.0510,
                    0.7877, 0.5830, 0.5203, 0.5950, 0.6413,
                    0.0201, 0.0071, 0.0043, 0.0051, 0.0050,
                    0.0472, 0.0284, 0.0246, 0.0278, 0.0312,
                    0.0176, 0.0078, 0.0050, 0.0069, 0.0071]
CFINAL_ENV = {'LST_PT5_SCORE': '1', 'LST_PT5_RPHIIN_HISCALE': '5', 'LST_PT5_WPHIGH': '0.7'}


def cfinal_otpull(process, chi2=1.0, phi=0.15, plsMatch=0):
    ca = [n for n, m in process.producers_().items() if m.type_().startswith('CAHitNtupletAlpaka')]
    lst = [n for n, m in process.producers_().items() if m.type_().startswith('LSTProducer')]
    for n in ca:
        getattr(process, n).otpullExport = _cf_cms.bool(True)
    main_ca = [n for n in ca if 'SerialSync' not in n]
    for n in lst:
        m = getattr(process, n)
        src = [c for c in ca if ('SerialSync' in c) == ('SerialSync' in n)] or main_ca
        m.otpullCells = _cf_cms.InputTag(src[0], 'otpullCells')
        m.otpullChi2Cut = _cf_cms.double(chi2)
        m.otpullSigma = _cf_cms.vdouble(*_CF_OTPULL_SIGMA)
        m.otpullPtEdges = _cf_cms.vdouble(1.5, 3., 6., 15.)
        m.otpullPhiWindow = _cf_cms.double(phi)
        m.otpullNPhiBins = _cf_cms.int32(512)
        m.otpullNShells = _cf_cms.int32(15)
        m.otpullShellWidth = _cf_cms.double(2.0)
        m.otpullPLSMatch = _cf_cms.int32(plsMatch)
        m.otpullCapacity = _cf_cms.uint32(1024)
    print('[cfinal] otpull CA', ca, 'LST', lst, 'chi2', chi2, 'phi', phi, 'plsMatch', plsMatch)


def cfinal_check_env(strict=True, allow_diag=False):
    bad = {k: (_cf_os.environ.get(k), v) for k, v in CFINAL_ENV.items() if _cf_os.environ.get(k) != v}
    for k in ('LST_OTPULL_PRINT', 'LST_OTPULL_TIME', 'CA_OTPULL_TIME'):
        if _cf_os.environ.get(k) and not allow_diag:
            bad[k] = (_cf_os.environ.get(k), 'unset (diagnostic, adds host syncs)')
    print('[cfinal] LST env knobs', {k: _cf_os.environ.get(k) for k in list(CFINAL_ENV) + ['LST_T5CC_MODE', 'LST_T5CC_PTMAX']})
    if bad and strict:
        raise RuntimeError('[cfinal] export these in the driver (got, want): %s' % bad)
    return not bad
