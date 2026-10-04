# D-M4 noise floor: stock mkFit (x86-64-v3, production) vs the same stock mkFit built for x86-64-v2 (harness lane).
# Two jobs on the same events; run them with test/replay_floor.sh (it switches the libraries of the second job).
#   role=ref : replay + mkFit final fit with the production libraries, writes out= (inputs + REPLAY candidates/tracks)
#   role=tgt : reads the ref out= file, replays again as process REPLAY2 (other libraries) and compares per seed:
#              candidates  hltInitialStepTrackCandidates  REPLAY vs REPLAY2  (hit lists + local state pulls)
#              fit tracks  hltInitialStepTracksMkFitFit   REPLAY vs REPLAY2  (valid hits + 5 helix parameters)
#   mtv=1    : MultiTrackValidator on this job's hltInitialStepTracksMkFitFit (truth from the RAW parent) -> dqmFile
#   cmsRun test/replay_floor_cfg.py role=ref sample=ttbar out=file:v3.root mtv=1 dqmFile=DQM_v3.root
#   cmsRun test/replay_floor_cfg.py role=tgt sample=ttbar inputFiles=file:v3.root mtv=1 dqmFile=DQM_v2.root summaryPrefix=fl_
import FWCore.ParameterSet.Config as cms
from FWCore.ParameterSet.VarParsing import VarParsing
from RecoTracker.MkFitAlpaka.replay_cff import (makeReplayProcess, addCandidateCompare, addTrackCompare,
                                                replayFile, RAW_FILES)

opts = VarParsing('analysis')
opts.register('role', 'ref', VarParsing.multiplicity.singleton, VarParsing.varType.string, 'ref or tgt')
opts.register('sample', 'ttbar', VarParsing.multiplicity.singleton, VarParsing.varType.string, 'ttbar or qcd')
opts.register('threads', 8, VarParsing.multiplicity.singleton, VarParsing.varType.int, 'threads (<= 8)')
opts.register('streams', 0, VarParsing.multiplicity.singleton, VarParsing.varType.int,
              '0 = threads; 1 avoids the secondary-file switching crash with many RAW parents (mtv=1)')
opts.register('out', '', VarParsing.multiplicity.singleton, VarParsing.varType.string, 'ref: output file')
opts.register('mtv', False, VarParsing.multiplicity.singleton, VarParsing.varType.bool, 'MTV on this job\'s fit tracks')
opts.register('dqmFile', 'DQM_floor.root', VarParsing.multiplicity.singleton, VarParsing.varType.string, '')
opts.register('rawFiles', '', VarParsing.multiplicity.singleton, VarParsing.varType.string,
              'comma-separated RAW parents (default: RAW_FILES[sample])')
opts.register('summaryPrefix', '', VarParsing.multiplicity.singleton, VarParsing.varType.string,
              'comparator JSON files <prefix>cand.json, <prefix>fit.json (tgt)')
opts.register('paired', False, VarParsing.multiplicity.singleton, VarParsing.varType.bool,
              'tgt + mtv: paired truth comparison (MkFitAlpakaPairedTruthCompare) of the REPLAY and REPLAY2 fit tracks')
opts.register('maxPrint', 3, VarParsing.multiplicity.singleton, VarParsing.varType.int, '')
opts.register('skipEvents', 0, VarParsing.multiplicity.singleton, VarParsing.varType.int, 'ref: skip N events (chunked floors)')
opts.setDefault('maxEvents', -1)
opts.parseArguments()

ref = opts.role == 'ref'
pn = 'REPLAY' if ref else 'REPLAY2'
inputs = opts.inputFiles if opts.inputFiles else [replayFile(opts.sample)]
raw = opts.rawFiles.split(',') if opts.rawFiles else RAW_FILES.get(opts.sample)
process = makeReplayProcess(inputs, maxEvents=opts.maxEvents, threads=opts.threads, streams=opts.streams, fit=True,
                            rawFiles=raw if opts.mtv else None, processName=pn, compare=True,
                            skipEvents=opts.skipEvents)
process.MessageLogger.cerr.FwkReport.reportEvery = 50
if ref and opts.out:
    # inputs of the replay file + this job's candidates/fit tracks; NOT the RAW secondaries (mtv=1 attaches them)
    process.floorOut = cms.OutputModule('PoolOutputModule', fileName=cms.untracked.string(opts.out),
        outputCommands=cms.untracked.vstring('drop *', 'keep *_*_*_HLTX', 'keep *_addPileupInfo_*_*',
                                             'keep *_genParticles_*_*', 'keep *_generator_*_*',
                                             'keep *_hltInitialStepTrackCandidates_*_%s' % pn,
                                             'keep *_hltInitialStepTracksMkFitFit_*_%s' % pn))
    process.replayEndPath += process.floorOut
if not ref:
    sp = opts.summaryPrefix
    addCandidateCompare(process, 'floorCand', reference=cms.InputTag('hltInitialStepTrackCandidates', '', 'REPLAY'),
                        target=cms.InputTag('hltInitialStepTrackCandidates', '', pn), maxPrint=opts.maxPrint,
                        summaryFile=(sp + 'cand.json') if sp else '')
    addTrackCompare(process, 'floorFit', reference=cms.InputTag('hltInitialStepTracksMkFitFit', '', 'REPLAY'),
                    target=cms.InputTag('hltInitialStepTracksMkFitFit', '', pn), maxPrint=opts.maxPrint,
                    summaryFile=(sp + 'fit.json') if sp else '')

if opts.mtv:
    process.load('DQMServices.Core.DQMStore_cfi')
    from Validation.RecoTrack.HLTmultiTrackValidator_cff import (hltTrackValidator, hltTPClusterProducer,
                                                                hltTrackAssociatorByHits,
                                                                trackingParticleNumberOfLayersProducer)
    process.hltTPClusterProducer = hltTPClusterProducer.clone()
    process.hltTrackAssociatorByHits = hltTrackAssociatorByHits.clone()
    process.trackingParticleNumberOfLayersProducer = trackingParticleNumberOfLayersProducer.clone()
    process.hltTrackValidator = hltTrackValidator.clone(label=[cms.InputTag('hltInitialStepTracksMkFitFit', '', pn)])
    process.mtvTask = cms.Task(process.hltTPClusterProducer, process.hltTrackAssociatorByHits,
                               process.trackingParticleNumberOfLayersProducer)
    process.mtvPath = cms.Path(process.hltTrackValidator, process.mtvTask)
    process.DQMoutput = cms.OutputModule('DQMRootOutputModule', fileName=cms.untracked.string('file:' + opts.dqmFile))
    process.dqmOutPath = cms.EndPath(process.DQMoutput)
    if opts.paired and not ref:
        v = process.hltTrackValidator
        sel = cms.PSet(**{k: getattr(v, k) for k in [
            'ptMinTP', 'ptMaxTP', 'minRapidityTP', 'maxRapidityTP', 'tipTP', 'lipTP', 'minHitTP', 'signalOnlyTP',
            'intimeOnlyTP', 'chargedOnlyTP', 'stableOnlyTP', 'pdgIdTP', 'invertRapidityCutTP', 'minPhi', 'maxPhi']})
        process.floorPaired = cms.EDAnalyzer('MkFitAlpakaPairedTruthCompare',
                                             reference=cms.InputTag('hltInitialStepTracksMkFitFit', '', 'REPLAY'),
                                             target=cms.InputTag('hltInitialStepTracksMkFitFit', '', pn),
                                             trackingParticles=v.label_tp_effic,
                                             associator=cms.InputTag(v.associators[0]), label=cms.string('floor'),
                                             tpSelection=sel)
        process.mtvPath += process.floorPaired
    process.schedule = cms.Schedule(process.replayPath, process.mtvPath, process.replayEndPath, process.dqmOutPath)
