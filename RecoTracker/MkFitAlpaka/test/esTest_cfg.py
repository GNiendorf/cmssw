# MkFitAlpaka ES lane test: build the device ES data with the HLT menu's ES setup (geometry ExtendedRun4D121,
# era Phase2C22I13M9, GT auto:phase2_realistic_T35, the menu's MkFit ES producers from
# HLTrigger/Configuration/python/HLT_75e33/eventsetup/hltESPMkFit_cfi.py), let the framework copy it to the
# device, copy it back, run device kernels on it and compare every value with the stock host objects.
#
#   cmsRun esTest_cfg.py backend=serial_sync
#   cmsRun esTest_cfg.py backend=cuda_async
#   cmsRun esTest_cfg.py backend=cuda_async streams=4 maxEvents=8   (both GPUs, per-device copies)
import FWCore.ParameterSet.Config as cms
from FWCore.ParameterSet.VarParsing import VarParsing
from Configuration.Eras.Era_Phase2C22I13M9_cff import Phase2C22I13M9

options = VarParsing('analysis')
options.register('backend', 'serial_sync', VarParsing.multiplicity.singleton, VarParsing.varType.string,
                 'alpaka backend: serial_sync or cuda_async')
options.register('globalTag', 'auto:phase2_realistic_T35', VarParsing.multiplicity.singleton,
                 VarParsing.varType.string, 'global tag')
options.register('streams', 1, VarParsing.multiplicity.singleton, VarParsing.varType.int,
                 'number of threads and streams (more streams spread events over the GPUs)')
options.setDefault('maxEvents', 1)
options.parseArguments()

process = cms.Process('MKFITESTEST', Phase2C22I13M9)
process.load('FWCore.MessageService.MessageLogger_cfi')
process.load('Configuration.Geometry.GeometryExtendedRun4D121Reco_cff')
process.load('Configuration.StandardSequences.MagneticField_cff')
process.load('Configuration.StandardSequences.FrontierConditions_GlobalTag_cff')
process.load('Configuration.StandardSequences.Accelerators_cff')
from Configuration.AlCa.GlobalTag import GlobalTag
process.GlobalTag = GlobalTag(process.GlobalTag, options.globalTag, '')

# the HLT menu's mkFit ES producers (MkFitGeometryESProducer, hltInitialStepTrackCandidatesMkFitConfig)
process.load('HLTrigger.Configuration.HLT_75e33.eventsetup.hltESPMkFit_cfi')

# SiPixelQualityRcd as in the dumped HLT job (source of MkFitEventOfHitsProducer's dead regions; only counted here)
process.siPixelQualityESProducer = cms.ESProducer('SiPixelQualityESProducer',
    ListOfRecordToMerge = cms.VPSet(
        cms.PSet(record = cms.string('SiPixelQualityFromDbRcd'), tag = cms.string('')),
        cms.PSet(record = cms.string('SiPixelDetVOffRcd'), tag = cms.string(''))
    ),
    siPixelQualityFromDbLabel = cms.string('')
)

process.source = cms.Source('EmptySource')
process.maxEvents.input = options.maxEvents
process.options.numberOfThreads = options.streams
process.options.numberOfStreams = options.streams
process.options.accelerators = ['cpu'] if options.backend == 'serial_sync' else ['gpu-nvidia']

backendPSet = cms.untracked.PSet(backend = cms.untracked.string(options.backend))

process.mkFitAlpakaESProducer = cms.ESProducer('MkFitAlpakaESProducer@alpaka',
    ComponentName = cms.string(''),
    iterationConfig = cms.ESInputTag('', 'hltInitialStepTrackCandidatesMkFitConfig'),
    alpaka = backendPSet
)

process.mkFitESDataTester = cms.EDProducer('MkFitESDataTester@alpaka',
    esData = cms.ESInputTag('', ''),
    iterationConfig = cms.ESInputTag('', 'hltInitialStepTrackCandidatesMkFitConfig'),
    alpaka = backendPSet
)

process.p = cms.Path(process.mkFitESDataTester)
