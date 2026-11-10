# ptt_common.py: helpers shared by the pixel-track Torch training configs (cache_cfg.py, dump_cfg.py).
# Exec'd after a full-menu config (L1P2GT,HLT:75e33_timing,VALIDATION, procModifiers trackingMkFitFit,trackingMkFitAlpaka)
# has defined `process`.
import os
import FWCore.ParameterSet.Config as cms

# Products the cache holds; their producers are deleted in the jobs that read the cache.
PTT_CACHED_PRODUCERS = ['hltOnlineBeamSpot', 'hltSiPhase2Clusters']
PTT_CACHE_KEEP = ['keep *_hltOnlineBeamSpot_*_PTTCACHE', 'keep *_hltSiPhase2Clusters_*_PTTCACHE',
                  'keep *_pttTruth_*_PTTCACHE',
                  'keep PixelDigiedmDetSetVector_simSiPixelDigis_Pixel_*', 'keep recoBeamSpot_offlineBeamSpot_*_*',
                  'keep *_randomEngineStateProducer_*_*']


def _ptt_tags(pset, acc):
    for name in pset.parameterNames_():
        p = getattr(pset, name)
        if isinstance(p, cms.InputTag):
            acc.append(p.getModuleLabel())
        elif isinstance(p, cms.VInputTag):
            for t in p:
                acc.append(t.getModuleLabel() if hasattr(t, 'getModuleLabel') else str(t).split(':')[0])
        elif isinstance(p, cms.PSet):
            _ptt_tags(p, acc)
        elif isinstance(p, cms.VPSet):
            for q in p:
                _ptt_tags(q, acc)
    return acc


def ptt_closure(process, roots):
    """producers and filters of this process the roots need, following InputTags (stops at input products)"""
    mods = dict(process.producers_())
    mods.update(process.filters_())
    seen, todo = set(), list(roots)
    while todo:
        lab = todo.pop()
        if lab in seen or lab not in mods:
            continue
        seen.add(lab)
        todo += [x for x in _ptt_tags(mods[lab], []) if x and x not in seen]
    return sorted(seen)


def ptt_run_only(process, roots, analyzers=(), extra_paths=()):
    """Schedule = one Path with the analyzers (or the roots) plus a Task holding the closure of the roots."""
    need = ptt_closure(process, list(roots) + [l for a in analyzers for l in _ptt_tags(getattr(process, a), [])])
    process.pttTask = cms.Task(*[getattr(process, l) for l in need])
    if analyzers:
        seq = getattr(process, analyzers[0])
        for a in analyzers[1:]:
            seq = seq + getattr(process, a)
        process.pttPath = cms.Path(seq, process.pttTask)
    else:
        process.pttPath = cms.Path(process.pttTask)
    process.schedule = cms.Schedule(process.pttPath, *extra_paths)
    return need
