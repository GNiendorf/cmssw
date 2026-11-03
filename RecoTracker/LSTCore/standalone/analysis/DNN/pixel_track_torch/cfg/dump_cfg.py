# dump_cfg.py: on-policy training dump of the Torch pixel-track classifier from the cache (cfg/cache_cfg.py).
# The menu config PTT_MENU (CPU) defines the policy: the CA and its settings as deployed. Here the classifier's cut is
# switched off (every preselected tuple passes, no preselection cap), and PixelTupleTorchDump writes one row per tuple
# with its inputs, the deployed decision and truth from the cache.
# env: PTT_MENU    full-menu CPU config with the policy applied
#      PTT_CACHE   comma list of cache files
#      PTT_OUT     output ROOT file (TFileService)
#      PTT_DIR     analysis/DNN/pixel_track_torch
#      PTT_DEP1    optional python file defining ptt_dep1(module): settings of a second deployed clone (parity check)
#      PTT_NEV (-1), PTT_THREADS (8)
import os
exec(open(os.environ['PTT_MENU']).read())
import FWCore.ParameterSet.Config as cms
exec(open(os.path.join(os.environ['PTT_DIR'], 'cfg', 'ptt_common.py')).read())

files = [f for f in os.environ['PTT_CACHE'].split(',') if f]
process.source = cms.Source('PoolSource',
                            fileNames=cms.untracked.vstring(*['file:' + f if '://' not in f else f for f in files]),
                            inputCommands=cms.untracked.vstring('keep *'))
process.maxEvents.input = int(os.environ.get('PTT_NEV', '-1'))
for lab in PTT_CACHED_PRODUCERS:
    if hasattr(process, lab):
        delattr(process, lab)

hp = process.hltPhase2PixelTrackTorchHighPuritySelector
# deployed clone: the policy's own classifier settings (in-menu decision)
process.pttDep0 = hp.clone()
dep = [cms.InputTag('pttDep0')]
if os.environ.get('PTT_DEP1'):
    process.pttDep1 = hp.clone()
    _ns = {'cms': cms}
    exec(open(os.environ['PTT_DEP1']).read(), _ns)
    _ns['ptt_dep1'](process.pttDep1)
    dep.append(cms.InputTag('pttDep1'))
# the classifier's cut off: every preselected tuple passes, nothing is truncated before scoring
hp.scoreThreshold = 0.0
hp.passThreeHitTracks = True
hp.maxPreselectedTracks = hp.maxNumberOfTracks.value()
for p in ('wpPtEdges', 'wpAbsEtaEdges', 'wpThresholds', 'wpThresholdsThreeHit'):
    if hasattr(hp, p):
        setattr(hp, p, cms.vdouble())
if hasattr(hp, 'highPurityThreshold'):
    hp.highPurityThreshold = -1.0

process.pttDump = cms.EDAnalyzer('PixelTupleTorchDump',
                                 allTuples=cms.InputTag('hltPhase2PixelTrackTorchHighPuritySelector'),
                                 deployedTuples=cms.VInputTag(*dep),
                                 indToEdm=cms.InputTag('hltPhase2PixelTracks'),
                                 tracks=cms.InputTag('hltPhase2PixelTracks'),
                                 truthLabel=cms.string('pttTruth'),
                                 associationCut=cms.double(0.75))
process.TFileService = cms.Service('TFileService', fileName=cms.string(os.environ['PTT_OUT']))
need = ptt_run_only(process, [], analyzers=['pttDump'])
process.options.numberOfThreads = int(os.environ.get('PTT_THREADS', '8'))
process.options.numberOfStreams = 0
process.options.wantSummary = True
if hasattr(process, 'FastTimerService'):
    del process.FastTimerService
process.MessageLogger.cerr.FwkReport.reportEvery = 50
print('PTTDUMP', len(files), 'cache files ->', os.environ['PTT_OUT'], 'modules', len(need), 'deployed', [d.value() for d in dep],
      'presel cap', hp.maxPreselectedTracks.value(), 'deployed cap', process.pttDep0.maxPreselectedTracks.value())
