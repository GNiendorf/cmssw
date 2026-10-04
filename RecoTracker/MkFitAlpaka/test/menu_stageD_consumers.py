#!/usr/bin/env python3
"""consumers.py <dump.py> <label>...: every module of an edmConfigDump that references one of the labels (InputTag / VInputTag /
string / VString parameters, recursively through PSets/VPSets), with its C++ type, the parameter path(s), and where it runs
(HLT paths / EndPaths / Tasks; 'VAL' = only in the VALIDATION/DQM step sequences). Read-only."""
import sys, importlib.util, collections
import FWCore.ParameterSet.Config as cms

def load(p):
    spec = importlib.util.spec_from_file_location("dumpmod", p); m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m); return m.process

def scan(pset, labels, prefix, out):
    for name in pset.parameterNames_():
        v = getattr(pset, name); path = f"{prefix}{name}"
        t = type(v).__name__
        if isinstance(v, cms.InputTag):
            if v.getModuleLabel() in labels: out.append((v.getModuleLabel(), path, v.configValue()))
        elif isinstance(v, cms.VInputTag):
            for x in v:
                lab = x.getModuleLabel() if isinstance(x, cms.InputTag) else str(x).split(':')[0]
                if lab in labels: out.append((lab, path, str(x)))
        elif isinstance(v, cms.string):
            s = v.value().split(':')[0]
            if s in labels: out.append((s, path, v.value()))
        elif isinstance(v, cms.vstring):
            for x in v:
                if x.split(':')[0] in labels: out.append((x.split(':')[0], path, x))
        elif isinstance(v, cms.PSet):
            scan(v, labels, path + ".", out)
        elif isinstance(v, cms.VPSet):
            for i, ps in enumerate(v): scan(ps, labels, f"{path}[{i}].", out)

def where(process):
    loc = collections.defaultdict(set)
    for kind, d in (("P", process.paths_()), ("E", process.endpaths_())):
        for pn, p in d.items():
            for m in p.moduleNames(): loc[m].add(f"{kind}:{pn}")
    for tn, t in process.tasks_().items():
        for m in t.moduleNames(): loc[m].add(f"T:{tn}")
    return loc

if __name__ == "__main__":
    proc = load(sys.argv[1]); labels = set(sys.argv[2:])
    loc = where(proc)
    mods = {}
    for d in (proc.producers_(), proc.filters_(), proc.analyzers_(), proc.switchProducers_() if hasattr(proc, "switchProducers_") else {}):
        mods.update(d)
    res = collections.defaultdict(list)
    for name, m in sorted(mods.items()):
        out = []
        if hasattr(m, "parameterNames_"): scan(m, labels, "", out)
        if type(m).__name__ == "SwitchProducer":
            for cn in m.parameterNames_():
                c = getattr(m, cn); o2 = []
                if hasattr(c, "parameterNames_"): scan(c, labels, cn + ".", o2)
                out += o2
        for lab, path, val in out: res[lab].append((name, m.type_() if hasattr(m, "type_") else type(m).__name__, path, val))
    for lab in sys.argv[2:]:
        prod = mods.get(lab)
        ptype = (prod.type_() if hasattr(prod, "type_") else type(prod).__name__) if prod is not None else "NOT IN MENU"
        print(f"=== {lab}  [producer: {ptype}; runs in {len(loc.get(lab, []))} paths/tasks]")
        byname = collections.OrderedDict()
        for name, typ, path, val in res[lab]: byname.setdefault((name, typ), []).append(f"{path}={val}")
        for (name, typ), ps in byname.items():
            L = loc.get(name, set())
            hlt = [x for x in L if not any(k in x for k in ("alidation", "DQM", "dqm", "Validator", "MultiTrack", "PVValidation", "Associat"))]
            tag = "HLT" if hlt else ("VAL" if L else "UNSCHEDULED")
            paths = sorted(L); ps_short = "; ".join(ps)[:160]
            print(f"  {tag:11s} {name:55s} {typ:45s} {ps_short}   [{len(L)} loc: {', '.join(paths[:3])}{' ...' if len(paths) > 3 else ''}]")
