# Shared by hitsDump_cfg.py / hitsEventOfHits_cfg.py: load ./hlt.py (test/hits_make_hlt.sh) and keep only the stock
# chain up to hltMkFitEventOfHits (modules of HLT_AK4PFPuppiJet520 in path order, filters dropped).
import sys
import importlib.util
import FWCore.ParameterSet.Config as cms

def stockChainUpToEventOfHits(hltFile='hlt.py'):
    spec = importlib.util.spec_from_file_location("hlt", hltFile)
    m = importlib.util.module_from_spec(spec)
    argv = sys.argv
    sys.argv = [hltFile]
    spec.loader.exec_module(m)
    sys.argv = argv
    process = m.process

    class V:
        def __init__(self):
            self.l = []
        def enter(self, v):
            if isinstance(v, (cms.EDProducer, cms.EDFilter, cms.EDAnalyzer)) and v not in self.l:
                self.l.append(v)
        def leave(self, v):
            pass

    v = V()
    process.HLT_AK4PFPuppiJet520.visit(v)
    mods = []
    for x in v.l:
        if isinstance(x, cms.EDFilter):
            continue
        mods.append(x)
        if x.label_() == 'hltMkFitEventOfHits':
            break
    assert mods[-1].label_() == 'hltMkFitEventOfHits'
    path = cms.Path(mods[0])
    for x in mods[1:]:
        path += x
    for o in list(process.outputModules_()):
        delattr(process, o)
    process.options.numberOfStreams = 0
    process.options.wantSummary = False
    return process, path
