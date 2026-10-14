# Lane itot_r3/combine: Cfinal2 for timing / memory = S2's cpu timing menu (itot_firm/stack3/cfg/full_S2_cpu.py) + Cfinal2
# (cfg/cfinal2_block.py: otpull + T5 pixel stub + SO + early delete of the otpull cells; hp5/sc/A5 via env). Run ONLY in the
# environment of itot_r3/combine/area/CMSSW_20_1_0_pre3. The DRIVER must source cfg/env_Cfinal2.sh (the config refuses to load
# without the four LST env knobs, or with an otpull diagnostic set).
import sys, os
_ITOT_DIR = os.environ.get('ITOT_DIR') or os.path.dirname(os.path.abspath(__file__))
sys.argv = [sys.argv[0]]
exec(open(os.path.join(_ITOT_DIR, 'full_S2_cpu.py')).read())
exec(open(os.path.join(_ITOT_DIR, 'cfinal2_block.py')).read())
cfinal2(process, allow_diag=globals().get('_CFINAL2_ALLOW_DIAG', False))
_lst = [n for n, m in process.producers_().items() if m.type_().startswith('LSTProducer')]
print('CFINAL2 TIMING ARM cpu: LST', [(n, getattr(process, n).otpullCells.value(), getattr(process, n).otpullAttach.value()) for n in _lst],
      'SO', [(n, m.duplicateRemovalBySurvivorsOnly.value()) for n, m in process.producers_().items() if m.type_().startswith('MkFitAlpakaBuildProducer')],
      'canDeleteEarly', [b for b in process.options.canDeleteEarly if 'otpullCells' in b],
      'stats', process.hltPhase2PixelTracksSoA.fillStatistics.value(), 'torch log', process.hltPhase2PixelTrackTorchHighPuritySelector.logPreselection.value())
