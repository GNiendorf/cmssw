# dump_cfg.py: on-policy dump of the otpull pair-network rows (dump.patch build) on a training cache made by
# analysis/DNN/pixel_track_torch/cfg/cache_cfg.py (pixel digis, OT clusters, beamspots, random-engine states and the
# compact cluster truth pttTruth). Runs the closure of hltLST only (pixel local reco, CA, Torch, LST input, LST); every
# pair that reaches the network is written whatever the decision (the decision does not change the dumped set).
# env: OTP_ARM    arm config = the deployed chain (full menu + customizeOP + customizeOtpullScore)
#      OTP_CACHE  cache file(s), comma separated      OTP_OUT  output ROOT file (TFileService)
#      OTP_NEV    events (-1)   OTP_THREADS (16)      OTP_FALSE_KEEP  false-pair keep fraction (1.0)
import os
import sys
sys.argv = [sys.argv[0]]
exec(open(os.environ['OTP_ARM']).read())
import FWCore.ParameterSet.Config as cms

process.source = cms.Source('PoolSource',
                            fileNames=cms.untracked.vstring(*['file:' + f if '://' not in f else f
                                                              for f in os.environ['OTP_CACHE'].split(',')]),
                            inputCommands=cms.untracked.vstring('keep *'))
process.maxEvents.input = int(os.environ.get('OTP_NEV', '-1'))
for _l in ('hltOnlineBeamSpot', 'hltSiPhase2Clusters'):  # read from the cache
    if hasattr(process, _l):
        delattr(process, _l)
_lst = [l for l, m in process.producers_().items() if 'LSTProducer' in m.type_()]
for _l in _lst:
    getattr(process, _l).diagDump = cms.bool(True)
_main = 'hltLST' if 'hltLST' in _lst else _lst[0]
process.otpullPairDump = cms.EDAnalyzer('OTPullPairDump',
                                        pairs=cms.InputTag(_main, 'diagPairs'), t3i=cms.InputTag(_main, 'diagT3i'),
                                        cellKeys=cms.InputTag(_main, 'diagCellKeys'),
                                        pixelClusters=cms.InputTag('hltSiPixelClusters'),
                                        phase2OTClusters=cms.InputTag('hltSiPhase2Clusters'),
                                        truthLabel=cms.string('pttTruth'),
                                        falsePairKeep=cms.double(float(os.environ.get('OTP_FALSE_KEEP', '1.0'))))
process.TFileService = cms.Service('TFileService', fileName=cms.string(os.environ['OTP_OUT']))


def _tags(pset, acc):
    for name in pset.parameterNames_():
        p = getattr(pset, name)
        if isinstance(p, cms.InputTag):
            acc.append(p.getModuleLabel())
        elif isinstance(p, cms.VInputTag):
            for t in p:
                acc.append(t.getModuleLabel() if hasattr(t, 'getModuleLabel') else str(t).split(':')[0])
        elif isinstance(p, cms.PSet):
            _tags(p, acc)
        elif isinstance(p, cms.VPSet):
            for q in p:
                _tags(q, acc)
    return acc


_mods = dict(process.producers_())
_mods.update(process.filters_())
# hltPhase2PixelTracks is read by the LST input producer through a parameter that is not an InputTag
_seen, _todo = set(), _tags(process.otpullPairDump, []) + ['hltPhase2PixelTracks']
while _todo:
    _x = _todo.pop()
    if _x in _seen or _x not in _mods:
        continue
    _seen.add(_x)
    _todo += [y for y in _tags(_mods[_x], []) if y and y not in _seen]
process.otpDumpTask = cms.Task(*[getattr(process, l) for l in sorted(_seen)])
process.otpDumpPath = cms.Path(process.otpullPairDump, process.otpDumpTask)
process.schedule = cms.Schedule(process.otpDumpPath)
process.options.numberOfThreads = int(os.environ.get('OTP_THREADS', '16'))
process.options.numberOfStreams = 0
if hasattr(process, 'FastTimerService'):
    del process.FastTimerService
process.MessageLogger.cerr.FwkReport.reportEvery = 20
print('OTPDUMP', os.environ['OTP_CACHE'], '->', os.environ['OTP_OUT'], 'modules', len(_seen), 'LST', _main)
