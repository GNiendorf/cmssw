import FWCore.ParameterSet.Config as cms

def no52015(process):
    """cms-sw#52015's five LST-step changes switched back to the pre-PR behaviour by configuration (S3 code stays):
    JSON (cleaner phase1 pixelseed, dc_drth restored, backward-fit outliers off), backward-search gate off,
    candMinPtCut 0.9, the default final fitter."""
    process.hltInitialStepTrackCandidatesMkFitConfig.config = cms.FileInPath('RecoTracker/MkFitAlpaka/test/mkfit-phase2-lstStep-no52015.json')
    process.hltInitialStepTrackCandidatesMkFitConfig.backwardSearchMinPixelLayers = 0
    process.hltInitialStepTrackCandidatesMkFitConfig.backwardSearchPromptMaxD0 = 0.0
    for n in ('hltInitialStepTrackCandidates', 'hltInitialStepTrackCandidatesMkFitFit'):
        m = getattr(process, n, None)
        if m is not None and hasattr(m, 'candMinPtCut'):
            m.candMinPtCut = 0.9
    t = getattr(process, 'hltInitialStepTracks', None)
    if t is not None and t.type_() == 'TrackProducer':
        t.Fitter = 'FlexibleKFFittingSmoother'
    print('[PRS] no52015: JSON %s, bkw gate 0, candMinPtCut 0.9, fitter %s' % (
        process.hltInitialStepTrackCandidatesMkFitConfig.config.value(), t.Fitter.value() if t is not None and t.type_() == 'TrackProducer' else '-'))
    return process

# leave-one-out arms: ONE of cms-sw#52015's LST-step changes switched back, the others kept
def _json(process, name):
    process.hltInitialStepTrackCandidatesMkFitConfig.config = cms.FileInPath('RecoTracker/MkFitAlpaka/test/' + name)
    print('[PRS] json %s' % name)
    return process

def loo_cleaner(process):   # pixel-priority cleaner + dc_drth = 0 (the JSON jet fix) off
    return _json(process, 'mkfit-phase2-lstStep-noCleaner.json')

def loo_outlier(process):   # backward-fit outlier rejection off
    return _json(process, 'mkfit-phase2-lstStep-noOutlier.json')

def loo_gate(process):      # backward-search pixel-layer gate off
    process.hltInitialStepTrackCandidatesMkFitConfig.backwardSearchMinPixelLayers = 0
    print('[PRS] gate off'); return process

def loo_candpt(process):    # candMinPtCut back to 0.9
    process.hltInitialStepTrackCandidates.candMinPtCut = 0.9
    print('[PRS] candMinPtCut 0.9'); return process

def loo_fitter(process):    # default final fitter for hltInitialStepTracks
    process.hltInitialStepTracks.Fitter = 'FlexibleKFFittingSmoother'
    print('[PRS] fitter default'); return process
