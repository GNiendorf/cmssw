import FWCore.ParameterSet.Config as cms

hltESPFlexibleKFFittingSmootherForLSTStep = cms.ESProducer("FlexibleKFFittingSmootherESProducer",
    ComponentName = cms.string('hltESPFlexibleKFFittingSmootherForLSTStep'),
    appendToDataLabel = cms.string(''),
    looperFitter = cms.string('LooperFittingSmoother'),
    standardFitter = cms.string('hltESPKFFittingSmootherForLSTStep')
)
