# Mock "port" for testing test/replay_port_cfg.py (harness lane): stock stage snapshots re-packed as TrackSoA.
#   cmsRun test/replay_port_cfg.py portCff=RecoTracker.MkFitAlpaka.replayMockPort_cff portSeq=mockPort \
#          compare=bkfit:mockPortBkFit,final:mockPortFinal      -> must be bit-identical
import FWCore.ParameterSet.Config as cms

mockPortBkFit = cms.EDProducer('MkFitAlpakaReplayStockToTrackSoA', src=cms.InputTag('stockStages', 'bkfit'))
mockPortFinal = cms.EDProducer('MkFitAlpakaReplayStockToTrackSoA', src=cms.InputTag('hltInitialStepTrackCandidatesMkFit'))
mockPort = cms.Sequence(mockPortBkFit + mockPortFinal)
