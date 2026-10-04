# ISOLATED noise floor of the mkFit final fit (review H2, DESIGN D4-floor): stock MkFitFitProducer, no-CPE path,
# production x86-64-v3 mkFit libraries vs the release's x86-64-v2 mkFit libraries on IDENTICAL input tracks.
# Needs the area's private stock RecoTracker/MkFit (doc/fit.txt: disableCPE + fitDump/fitDumpMode).
#   job A (v3):  cmsRun test/fitFloor_cfg.py role=write dump=<file> sample=ttbar maxEvents=100
#                stock building + stock no-CPE fit; the fit's input tracks and output tracks are written to <file>.
#   job B (v2):  LD_LIBRARY_PATH=<dir with the v2 libRecoTrackerMkFit{Core,CMS,}.so>:$LD_LIBRARY_PATH \
#                cmsRun test/fitFloor_cfg.py role=read dump=<file> ...
#                the stock no-CPE fit runs on job A's input tracks (fitDumpMode readInput) and is compared with job A's
#                output (readOutput): mkFit level [compare floorFitIso] and reco::Track level [compare floorFitIsoTrk]
#                (both through the same converter of job B).
# cpe=1 runs the same with the CPE ON on both sides (isolated floor of the CPE-on fit).
import FWCore.ParameterSet.Config as cms
from FWCore.ParameterSet.VarParsing import VarParsing
from RecoTracker.MkFitAlpaka.replay_cff import (makeReplayProcess, replayFile, addMkFitTrackCompare, addTrackCompare)

opts = VarParsing('analysis')
opts.register('sample', 'ttbar', VarParsing.multiplicity.singleton, VarParsing.varType.string, 'ttbar or qcd')
opts.register('threads', 8, VarParsing.multiplicity.singleton, VarParsing.varType.int, 'threads (<= 8)')
opts.register('role', 'write', VarParsing.multiplicity.singleton, VarParsing.varType.string, 'write | read')
opts.register('dump', 'fitfloor.bin', VarParsing.multiplicity.singleton, VarParsing.varType.string, 'track dump')
opts.register('cpe', False, VarParsing.multiplicity.singleton, VarParsing.varType.bool, 'CPE on (both sides)')
opts.register('summaryPrefix', '', VarParsing.multiplicity.singleton, VarParsing.varType.string, 'comparator JSONs')
opts.setDefault('maxEvents', 100)
opts.parseArguments()

process = makeReplayProcess([replayFile(opts.sample)], maxEvents=opts.maxEvents, threads=opts.threads,
                            accelerators=['cpu'], fit=True, compare=False)
menu = process.hltInitialStepTrackCandidatesMkFitFit
noCPE = cms.untracked.bool(not opts.cpe)
if opts.role == 'write':
    process.floorFit = menu.clone(disableCPE=noCPE, fitDump=cms.untracked.string(opts.dump),
                                  fitDumpMode=cms.untracked.string('write'))
    process.replayPath += process.floorFit
else:
    process.floorFit = menu.clone(disableCPE=noCPE, fitDump=cms.untracked.string(opts.dump),
                                  fitDumpMode=cms.untracked.string('readInput'))
    process.floorFitRef = menu.clone(fitDump=cms.untracked.string(opts.dump),
                                     fitDumpMode=cms.untracked.string('readOutput'))
    process.replayPath += process.floorFitRef
    process.replayPath += process.floorFit
    sp = opts.summaryPrefix
    addMkFitTrackCompare(process, 'floorFitIso', reference='floorFitRef', target='floorFit', paramTolerance=1e-3,
                         maxPrint=0, summaryFile=(sp + 'fitiso.json') if sp else '')
    process.floorTracksRef = process.hltInitialStepTracksMkFitFit.clone(src='floorFitRef')
    process.floorTracks = process.hltInitialStepTracksMkFitFit.clone(src='floorFit')
    process.replayPath += process.floorTracksRef
    process.replayPath += process.floorTracks
    addTrackCompare(process, 'floorFitIsoTrk', reference='floorTracksRef', target='floorTracks', paramTolerance=1e-3,
                    maxPrint=0, summaryFile=(sp + 'fitisotrk.json') if sp else '')
