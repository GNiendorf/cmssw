# Replay of the stock mkFit chain of the Phase-2 HLT LST initial step from a replay file (harness lane).
#   cmsRun test/replay_cfg.py sample=ttbar                    # fidelity: replayed candidates vs the HLT's
#   cmsRun test/replay_cfg.py sample=qcd fit=1                # + mkFit final fit (trackingMkFitFit flavour)
#   cmsRun test/replay_cfg.py sample=ttbar timing=1 compare=0 # per-module times (FastTimerService JSON)
#   cmsRun test/replay_cfg.py sample=ttbar accelerators=gpu-nvidia  # alpaka modules on cuda_async
#   cmsRun test/replay_cfg.py sample=ttbar truth=1            # also attach the RAW parent (sim truth, for MTV)
import FWCore.ParameterSet.Config as cms
from FWCore.ParameterSet.VarParsing import VarParsing
from RecoTracker.MkFitAlpaka.replay_cff import (makeReplayProcess, addTiming, addOutput, addTrackCompare,
                                                replayFile, RAW_FILES)

opts = VarParsing('analysis')
opts.register('sample', 'ttbar', VarParsing.multiplicity.singleton, VarParsing.varType.string, 'ttbar or qcd')
opts.register('threads', 8, VarParsing.multiplicity.singleton, VarParsing.varType.int, 'threads (<= 8)')
opts.register('streams', 0, VarParsing.multiplicity.singleton, VarParsing.varType.int, 'streams (0 = threads)')
opts.register('accelerators', 'cpu', VarParsing.multiplicity.singleton, VarParsing.varType.string,
              "process.options.accelerators: cpu (serial_sync), gpu-nvidia (cuda_async), '*'")
opts.register('fit', False, VarParsing.multiplicity.singleton, VarParsing.varType.bool, 'add the mkFit final fit')
opts.register('timing', False, VarParsing.multiplicity.singleton, VarParsing.varType.bool, 'FastTimerService')
opts.register('timingJson', 'replay_timing.json', VarParsing.multiplicity.singleton, VarParsing.varType.string, '')
opts.register('compare', True, VarParsing.multiplicity.singleton, VarParsing.varType.bool, 'HLT vs replay comparator')
opts.register('printEvents', False, VarParsing.multiplicity.singleton, VarParsing.varType.bool, 'per-event lines')
opts.register('requireIdentical', False, VarParsing.multiplicity.singleton, VarParsing.varType.bool,
              'comparator throws unless all events identical (unit test)')
opts.register('summaryFile', '', VarParsing.multiplicity.singleton, VarParsing.varType.string, 'comparator JSON')
opts.register('truth', False, VarParsing.multiplicity.singleton, VarParsing.varType.bool, 'attach RAW parent files')
opts.register('out', '', VarParsing.multiplicity.singleton, VarParsing.varType.string, 'write replayed products')
opts.register('processName', 'REPLAY', VarParsing.multiplicity.singleton, VarParsing.varType.string,
              'process name (use another one when the input is an out= file of an earlier replay)')
opts.register('skipEvents', 0, VarParsing.multiplicity.singleton, VarParsing.varType.int, '')
opts.setDefault('maxEvents', -1)
opts.parseArguments()

inputs = opts.inputFiles if opts.inputFiles else [replayFile(opts.sample)]
process = makeReplayProcess(inputs, maxEvents=opts.maxEvents, threads=opts.threads, streams=opts.streams,
                            accelerators=opts.accelerators.split(','), fit=opts.fit,
                            rawFiles=RAW_FILES.get(opts.sample) if opts.truth else None,
                            skipEvents=opts.skipEvents, compare=opts.compare, processName=opts.processName)
if opts.compare:
    process.compareCandidatesHLTvsReplay.printEvents = opts.printEvents
    process.compareCandidatesHLTvsReplay.summaryFile = opts.summaryFile
    process.compareCandidatesHLTvsReplay.requireIdentical = opts.requireIdentical
if opts.timing:
    addTiming(process, opts.timingJson)
if opts.out:
    addOutput(process, opts.out)
