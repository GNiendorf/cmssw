# cache_cfg.py: stream one GEN-SIM-DIGI-RAW file and write a compact local-reco cache with truth for training dumps.
# Kept: pixel digis (simSiPixelDigis:Pixel, copied), the outer-tracker clusters and the online beamspot (made here), the
# offline beamspot and the random-engine states (copied), and the compact cluster -> TrackingParticle truth (pttTruth,
# CompactClusterTruthProducer). The pixel chain digis -> clusters -> rechits is re-run by the jobs that read the cache
# (persisted legacy pixel clusters and rechits lose transient fields), so the truth is keyed by cluster position.
# All events of the file are kept (no HLT path selection).
# env: PTT_MENU   full-menu config to start from (process HLTX, VALIDATION included)
#      PTT_RAW    input file (xrootd URL or local path)
#      PTT_OUT    output EDM file
#      PTT_NEV    events (default all), PTT_THREADS (default 8), PTT_DIR this directory's parent (analysis/DNN/pixel_track_torch)
import os
exec(open(os.environ['PTT_MENU']).read())
import FWCore.ParameterSet.Config as cms
exec(open(os.path.join(os.environ['PTT_DIR'], 'cfg', 'ptt_common.py')).read())
process.setName_('PTTCACHE')

raw = os.environ['PTT_RAW']
process.source.fileNames = cms.untracked.vstring(raw if '://' in raw else 'file:' + raw)
process.maxEvents.input = int(os.environ.get('PTT_NEV', '-1'))
process.pttTruth = cms.EDProducer('CompactClusterTruthProducer',
                                  pixelClusters=cms.InputTag('hltSiPixelClusters'),
                                  phase2OTClusters=cms.InputTag('hltSiPhase2Clusters'),
                                  clusterTP=cms.InputTag('hltTPClusterProducer'),
                                  trackingParticles=cms.InputTag('mix', 'MergedTrackTruth'))
process.pttOut = cms.OutputModule('PoolOutputModule',
                                  fileName=cms.untracked.string(os.environ['PTT_OUT']),
                                  compressionAlgorithm=cms.untracked.string('ZSTD'),
                                  compressionLevel=cms.untracked.int32(4),
                                  outputCommands=cms.untracked.vstring('drop *', *PTT_CACHE_KEEP))
process.pttEnd = cms.EndPath(process.pttOut)
need = ptt_run_only(process, ['pttTruth', 'hltOnlineBeamSpot', 'hltSiPhase2Clusters'], extra_paths=(process.pttEnd,))
process.options.numberOfThreads = int(os.environ.get('PTT_THREADS', '8'))
process.options.numberOfStreams = 0
process.options.wantSummary = True
if hasattr(process, 'FastTimerService'):
    del process.FastTimerService
process.MessageLogger.cerr.FwkReport.reportEvery = 10
print('PTTCACHE', raw, '->', os.environ['PTT_OUT'], 'modules', len(need))
