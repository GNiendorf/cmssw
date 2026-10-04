# HLT menu customisation (D4-menu): run the mkFit chain of the LST initial step on the device (Alpaka: serial_sync on
# CPU, cuda_async on GPU), applied with --customise on top of the reference HLT command line (doc/chain.txt).
#
#   customizeHLTforMkFitAlpaka            device EventOfHits + device building chain (the production swap)
#   customizeHLTforMkFitAlpakaEventOfHits device EventOfHits ALONGSIDE the stock one, checked against it in the job
#                                         (stock physics untouched; first step of the swap)
#   customizeHLTforMkFitAlpakaValidation  production swap + the stock chain in the same job (after the menu's initial
#                                         step) + per-seed comparators and paired truth of port vs stock
#   customizeHLTforMkFitAlpakaFit         production swap + the device mkFit final fit; needs procModifier
#                                         trackingMkFitFit; REFUSES a fit without the CPE (D4-H1)
#   customizeHLTforMkFitAlpakaFitNoCPE    the same, explicitly accepting the no-CPE device fit (validation only)
#
# Module swaps (labels of the menu are kept, so every downstream InputTag resolves unchanged):
#   hltMkFitEventOfHits                -> MkFitAlpakaEventOfHitsProducer@alpaka (device product)
#   hltMkFitEventOfHitsHost            host MkFitEventOfHits for the host consumers (stock MkFitOutputConverter,
#                                      MkFitOutputTrackConverter): the light hit-less one when lane io's module exists
#                                      (LIGHT_EVENT_OF_HITS_PLUGIN), else a clone of the stock producer
#   hltInitialStepTrackCandidatesMkFitDevice  device building chain (TrackSoA)
#   hltInitialStepTrackCandidatesMkFit        MkFitAlpakaOutputWrapperFromTrackSoA (host MkFitOutputWrapper, consumed by
#                                             the stock MkFitOutputConverter hltInitialStepTrackCandidates)
#   [fit] hltInitialStepTrackCandidatesMkFitFit -> MkFitAlpakaFitProducer@alpaka
#   ES: hltMkFitAlpakaESProducer (MkFitAlpakaESProducer@alpaka, ComponentName hltMkFitAlpakaES)
#   [fit] hltMkFitAlpakaFitCpeESProducer (MkFitAlpakaFitCpeESProducer@alpaka, ComponentName hltMkFitAlpakaFitCpe)
import FWCore.ParameterSet.Config as cms
from FWCore.ParameterSet.MassReplace import MassSearchReplaceAnyInputTagVisitor

# Device building module: lane build's production module (the round-3 integration module MkFitAlpakaChainProducer has
# the same parameters and can be swapped back here for a comparison).
BUILD_PLUGIN = 'MkFitAlpakaBuildProducer@alpaka'
# Light (hit-less) host MkFitEventOfHits for the stock converters (D4-H6, lane io). None: clone the stock producer.
LIGHT_EVENT_OF_HITS_PLUGIN = 'MkFitAlpakaLightEventOfHits'
# MkFitAlpakaFitProducer applies the PixelCPEGeneric track-angle CPE (D-H1, D4-H1; round 4), tables from an ES product
# checked per IOV against the menu's CPE object (round 5, R4-H1/H2).
DEVICE_FIT_HAS_CPE = True
# Round 6 (lane gpu): the fit runs as the asynchronous MkFitAlpakaFitDeviceProducer@alpaka on the device building output
# (device candCutSel, no host round trip); the menu fit label becomes its host converter. False = the round-5
# MkFitAlpakaFitProducer (host MkFitOutputWrapper input, SynchronizingEDProducer).
DEVICE_FIT_FROM_BUILD = True
FIT_DEVICE = 'hltInitialStepTrackCandidatesMkFitFitDevice'
# O6-1 option (b) (lane seeds, round 6): the state of the LST T5/T4/pT3/pT5 seeds from the device mkFit Kalman fit of
# their hits instead of the host seed creator (pLS keep the copied pixel state). A DEVIATION candidate: OFF by default;
# turned on per job by the *LstSeeds customise functions below (or by setting this flag before customising).
DEVICE_LST_SEED_FIT = False

ES_LABEL = 'hltMkFitAlpakaES'
EOH = 'hltMkFitEventOfHits'
EOH_HOST = 'hltMkFitEventOfHitsHost'
BUILD = 'hltInitialStepTrackCandidatesMkFit'
BUILD_DEVICE = 'hltInitialStepTrackCandidatesMkFitDevice'
FIT = 'hltInitialStepTrackCandidatesMkFitFit'


def _checkStockBuildConfig(mkf):
    """Validated envelope of the device chain at the module level (the IterationConfig is checked in C++, D4-H3)."""
    want = {'buildingRoutine': 'cloneEngine', 'backwardFitInCMSSW': False, 'seedCleaning': True,
            'removeDuplicates': True, 'config': cms.ESInputTag('', 'hltInitialStepTrackCandidatesMkFitConfig')}
    bad = []
    for k, v in want.items():
        have = getattr(mkf, k).value() if hasattr(mkf, k) else None
        if have != (v.value() if hasattr(v, 'value') else v):
            bad.append('%s=%s (validated: %s)' % (k, have, v))
    if mkf.clustersToSkip.getModuleLabel() != '':
        bad.append('clustersToSkip=%s (validated: empty)' % mkf.clustersToSkip.value())
    if len(mkf.minGoodStripCharge.parameterNames_()) != 0:
        bad.append('minGoodStripCharge set (validated: empty PSet)')
    if bad:
        raise RuntimeError('customizeHLTforMkFitAlpaka: %s is outside the validated configuration: %s'
                           % (mkf.label_(), '; '.join(bad)))


def _modules(process):
    for d in (process.producers_(), process.filters_(), process.analyzers_()):
        for label, m in d.items():
            yield label, m


def _consumers(process, label, skip=()):
    """Modules with an InputTag parameter pointing at module label 'label'."""
    found = []

    def visit(pset, path):
        for n in pset.parameterNames_():
            p = getattr(pset, n)
            if isinstance(p, cms.InputTag):
                if p.getModuleLabel() == label:
                    found.append(path + n)
            elif isinstance(p, cms.VInputTag):
                for t in p:
                    tl = t.getModuleLabel() if isinstance(t, cms.InputTag) else str(t).split(':')[0]
                    if tl == label:
                        found.append(path + n)
            elif isinstance(p, cms.PSet):
                visit(p, path + n + '.')
            elif isinstance(p, cms.VPSet):
                for i, q in enumerate(p):
                    visit(q, '%s%s[%d].' % (path, n, i))

    for l, m in _modules(process):
        if l not in skip:
            visit(m, l + '.')
    return found


def _scheduled(process, label):
    for d in (process.paths_(), process.endpaths_()):
        for p in d.values():
            if label in p.moduleNames():
                return True
    return False


def _addESProducer(process):
    if not hasattr(process, 'hltMkFitAlpakaESProducer'):
        process.hltMkFitAlpakaESProducer = cms.ESProducer(
            'MkFitAlpakaESProducer@alpaka', ComponentName=cms.string(ES_LABEL),
            iterationConfig=cms.ESInputTag('', 'hltInitialStepTrackCandidatesMkFitConfig'))


def _deviceEventOfHits(stockEOH, compareTo=''):
    return cms.EDProducer('MkFitAlpakaEventOfHitsProducer@alpaka',
                          pixelHits=cms.InputTag(stockEOH.pixelHits.value()),
                          stripHits=cms.InputTag(stockEOH.stripHits.value()),
                          usePixelQualityDB=cms.bool(stockEOH.usePixelQualityDB.value()),
                          useESLayers=cms.bool(True), esData=cms.ESInputTag('', ES_LABEL),
                          compareTo=cms.InputTag(compareTo))


def _hostEventOfHits(stockEOH, light=True):
    if light and LIGHT_EVENT_OF_HITS_PLUGIN:
        return cms.EDProducer(LIGHT_EVENT_OF_HITS_PLUGIN)
    return stockEOH.clone()


def customizeHLTforMkFitAlpakaEventOfHits(process):
    """First step: the device EventOfHits runs next to the stock one and is checked against it (EOH_COMPARE lines in
    the log); nothing downstream reads it, so the menu physics is the stock one by construction."""
    if not hasattr(process, EOH):
        return process
    _addESProducer(process)
    process.hltMkFitEventOfHitsDevice = _deviceEventOfHits(getattr(process, EOH), compareTo=EOH)
    process.HLTMkFitInputSequence += process.hltMkFitEventOfHitsDevice
    return process


def _swap(process, fullHostEventOfHits=False, replaceFit=False):
    """The production swap. Returns the original (stock) EventOfHits and MkFitProducer modules."""
    # the stock MkFitFitProducer (trackingMkFitFit, not replaced) reads the hits: it needs the full host EventOfHits.
    # (The module is defined in every menu; it matters only when it is scheduled.)
    if hasattr(process, FIT) and getattr(process, FIT).type_() == 'MkFitFitProducer' and not replaceFit \
            and _scheduled(process, FIT):
        fullHostEventOfHits = True
    stockEOH = getattr(process, EOH)
    stockBuild = getattr(process, BUILD)
    if stockEOH.type_() != 'MkFitEventOfHitsProducer' or stockBuild.type_() != 'MkFitProducer':
        raise RuntimeError('customizeHLTforMkFitAlpaka: expected the stock %s/%s, found %s/%s'
                           % (EOH, BUILD, stockEOH.type_(), stockBuild.type_()))
    _checkStockBuildConfig(stockBuild)
    _addESProducer(process)

    # 1. every host consumer of the stock EventOfHits (converters, the stock fit) reads the host copy instead
    process.__setattr__(EOH_HOST, _hostEventOfHits(stockEOH, light=not fullHostEventOfHits))
    visitor = MassSearchReplaceAnyInputTagVisitor(EOH, EOH_HOST, verbose=False)
    for label, m in list(_modules(process)):
        if label not in (EOH, EOH_HOST, BUILD):
            visitor.doIt(m, label)

    # 2. device EventOfHits under the menu label (replaces the stock module in every sequence)
    setattr(process, EOH, _deviceEventOfHits(stockEOH))

    # 3. device building + host wrapper under the menu label (the stock converter consumes it unchanged)
    setattr(process, BUILD_DEVICE, cms.EDProducer(BUILD_PLUGIN, seeds=cms.InputTag(stockBuild.seeds.value()),
                                                  pixelHits=cms.InputTag(stockBuild.pixelHits.value()),
                                                  eventOfHits=cms.InputTag(EOH),
                                                  esData=cms.ESInputTag('', ES_LABEL),
                                                  removeDuplicates=cms.bool(stockBuild.removeDuplicates.value()),
                                                  # the C++ envelope check (review H3) sees the stock values too
                                                  clustersToSkip=cms.InputTag(stockBuild.clustersToSkip.getModuleLabel(),
                                                                              stockBuild.clustersToSkip.getProductInstanceLabel(),
                                                                              stockBuild.clustersToSkip.getProcessName()),
                                                  buildingRoutine=cms.string(stockBuild.buildingRoutine.value()),
                                                  seedCleaning=cms.bool(stockBuild.seedCleaning.value()),
                                                  backwardFitInCMSSW=cms.bool(stockBuild.backwardFitInCMSSW.value()),
                                                  # cms-sw#52015 backward-search gate: the stock EventOfHits beam spot
                                                  beamSpot=cms.InputTag(stockEOH.beamSpot.value()),
                                                  lstSeedFit=cms.bool(DEVICE_LST_SEED_FIT)))
    setattr(process, BUILD, cms.EDProducer('MkFitAlpakaOutputWrapperFromTrackSoA', tracks=cms.InputTag(BUILD_DEVICE),
                                           status=cms.InputTag(BUILD_DEVICE)))  # review H4: LogWarning if not clean

    # on-demand modules (run when the host wrapper / converters ask for them, in the events where the menu tracks)
    process.hltMkFitAlpakaTask = cms.Task(getattr(process, EOH_HOST), getattr(process, BUILD_DEVICE))
    process.HLTInitialStepSequence.associate(process.hltMkFitAlpakaTask)

    # nothing but the device modules may read the device EventOfHits
    left = [c for c in _consumers(process, EOH) if not c.startswith(BUILD_DEVICE + '.')]
    if left:
        raise RuntimeError('customizeHLTforMkFitAlpaka: host consumers of the device %s left: %s' % (EOH, left))
    return stockEOH, stockBuild


def customizeHLTforMkFitAlpaka(process):
    """Production swap: device EventOfHits + device building chain + host converter."""
    if not hasattr(process, BUILD):
        return process
    _swap(process)
    return process


def customizeHLTforMkFitAlpakaValidation(process):
    """Production swap + the stock chain of the same events (appended to the menu's initial-step sequence, so it runs
    exactly in the events where the menu tracks) + per-seed comparators (MkFitOutputWrapper, TrackCandidates,
    hltInitialStepTracks) and paired truth (the MTV TP selection of hltTrackValidator) in an EndPath.
    Log lines: [compare menuCmp*], [paired menu]."""
    if not hasattr(process, BUILD):
        return process
    stockEOH, stockBuild = _swap(process)
    # stock reference chain on its own full stock EventOfHits (the port keeps exactly the production configuration,
    # incl. the light host EventOfHits): MkFitProducer, stock converter, menu KF fit
    process.hltMkFitEventOfHitsStock = stockEOH.clone()
    process.hltInitialStepTrackCandidatesMkFitStock = stockBuild.clone(eventOfHits='hltMkFitEventOfHitsStock')
    process.hltInitialStepTrackCandidatesStock = process.hltInitialStepTrackCandidates.clone(
        tracks='hltInitialStepTrackCandidatesMkFitStock', mkFitEventOfHits='hltMkFitEventOfHitsStock')
    process.hltInitialStepTracksStock = process.hltInitialStepTracks.clone(src='hltInitialStepTrackCandidatesStock')
    cmp = []

    def add(name, plugin, ref, tgt, **kw):
        setattr(process, name, cms.EDAnalyzer(plugin, reference=cms.InputTag(ref), target=cms.InputTag(tgt),
                                              label=cms.string(name), paramTolerance=cms.double(1e-3),
                                              maxPrint=cms.int32(3), **kw))
        cmp.append(getattr(process, name))

    add('menuCmpMkFit', 'MkFitAlpakaMkFitTrackCompare', 'hltInitialStepTrackCandidatesMkFitStock', BUILD)
    add('menuCmpCands', 'MkFitAlpakaCandidateCompare', 'hltInitialStepTrackCandidatesStock',
        'hltInitialStepTrackCandidates')
    add('menuCmpTracks', 'MkFitAlpakaTrackCompare', 'hltInitialStepTracksStock', 'hltInitialStepTracks',
        validHitsOnly=cms.bool(True))
    if hasattr(process, 'hltTrackValidator') and hasattr(process, 'hltTrackAssociatorByHits'):
        v = process.hltTrackValidator
        sel = cms.PSet(**{k: getattr(v, k) for k in [
            'ptMinTP', 'ptMaxTP', 'minRapidityTP', 'maxRapidityTP', 'tipTP', 'lipTP', 'minHitTP', 'signalOnlyTP',
            'intimeOnlyTP', 'chargedOnlyTP', 'stableOnlyTP', 'pdgIdTP', 'invertRapidityCutTP', 'minPhi', 'maxPhi']})
        process.menuPaired = cms.EDAnalyzer('MkFitAlpakaPairedTruthCompare',
                                            reference=cms.InputTag('hltInitialStepTracksStock'),
                                            target=cms.InputTag('hltInitialStepTracks'),
                                            trackingParticles=v.label_tp_effic,
                                            associator=cms.InputTag(v.associators[0]), label=cms.string('menu'),
                                            tpSelection=sel)
        cmp.append(process.menuPaired)
    # Scheduled right after the menu's initial step (same events, before the menu's early deletion of the mkFit
    # inputs).
    # The paired-truth module sits in an EndPath (the TP associator comes from the VALIDATION step) and skips events
    # without tracks (the menu tracks only in some events).
    seq = (process.hltMkFitEventOfHitsStock + process.hltInitialStepTrackCandidatesMkFitStock +
           process.hltInitialStepTrackCandidatesStock +
           process.hltInitialStepTracksStock)
    for c in cmp:
        if c.label_() != 'menuPaired':
            seq = seq + c
    process.hltMkFitAlpakaValidationSequence = cms.Sequence(seq)
    process.HLTInitialStepSequence += process.hltMkFitAlpakaValidationSequence
    if hasattr(process, 'menuPaired'):
        process.hltMkFitAlpakaValidationEndPath = cms.EndPath(process.menuPaired)
        if process.schedule_() is not None:
            process.schedule.append(process.hltMkFitAlpakaValidationEndPath)
    return process


CPE_ES_LABEL = 'hltMkFitAlpakaFitCpe'

# PixelCPEGeneric settings the device CPE implements (interface/fit/CpeGeneric.h; the phase2 values of
# PixelCPEGeneric_cfi.py). name: (validated value, C++ default when the PSet does not set it).
_CPE_GENERIC_VALIDATED = {
    'UseErrorsFromTemplates': (True, True), 'TruncatePixelCharge': (False, True),
    'IrradiationBiasCorrection': (False, False), 'DoCosmics': (False, False), 'inflate_errors': (False, False),
    'eff_charge_cut_lowX': (0., 0.), 'eff_charge_cut_lowY': (0., 0.),
    'eff_charge_cut_highX': (1., 1.), 'eff_charge_cut_highY': (1., 1.),
    'size_cutX': (3., 3.), 'size_cutY': (3., 3.),
    'EdgeClusterErrorX': (50., 50.), 'EdgeClusterErrorY': (85., 85.),
    'useLAWidthFromDB': (True, True), 'LoadTemplatesFromDB': (True, True),
}
# module constants (Lorentz shift/width, B field) come from the PixelCPEFastParams product: these must agree between
# the two ES producers. name: C++ default (PixelCPEBase::fillPSetDescription, both producers)
_CPE_LORENTZ_DEFAULTS = {'useLAFromDB': True, 'lAOffset': 0., 'lAWidthBPix': 0., 'lAWidthFPix': 0.,
                         'doLorentzFromAlignment': False, 'useLAWidthFromDB': True}


def _esByComponentName(process, name, typeSubstr):
    found = [e for e in process.es_producers_().values()
             if typeSubstr in e.type_() and hasattr(e, 'ComponentName') and e.ComponentName.value() == name]
    if len(found) != 1:
        raise RuntimeError('customizeHLTforMkFitAlpakaFit: expected one %s ES producer with ComponentName %s, found %d'
                           % (typeSubstr, name, len(found)))
    return found[0]


def _checkCpeConfig(process, fitModule):
    """R4-H1, python half: the menu fit's CPE object is the PixelCPEGeneric the device code implements, and the
    PixelCPEFastParams producer (module constants of the device tables) has the same Lorentz settings. The C++ half is
    the per-IOV numeric cross-check of MkFitAlpakaFitProducer against the CPE object itself (cpeCheck = 'throw')."""
    name = fitModule.pixelCPE.value()
    if name != 'PixelCPEGeneric':
        raise RuntimeError("customizeHLTforMkFitAlpakaFit: the menu fit uses pixelCPE '%s'; the device fit implements "
                           "'PixelCPEGeneric' only" % name)
    gen = _esByComponentName(process, name, 'PixelCPEGeneric')
    fast = _esByComponentName(process, 'PixelCPEFastParamsPhase2', 'PixelCPEFastParams')

    def val(es, k, default):
        return getattr(es, k).value() if hasattr(es, k) else default

    bad = ['%s=%s (validated %s)' % (k, val(gen, k, d), v) for k, (v, d) in _CPE_GENERIC_VALIDATED.items()
           if val(gen, k, d) != v]
    bad += ['Lorentz %s: PixelCPEGeneric %s vs PixelCPEFastParamsPhase2 %s' % (k, val(gen, k, d), val(fast, k, d))
            for k, d in _CPE_LORENTZ_DEFAULTS.items() if val(gen, k, d) != val(fast, k, d)]
    if bad:
        raise RuntimeError('customizeHLTforMkFitAlpakaFit: CPE configuration outside the validated envelope: %s'
                           % '; '.join(bad))
    return fast


# Parameters of the stock MkFitFitProducer that the device fit honours (copied, or equal to what the device fit does).
# Anything else with a non-default value refuses the swap (review R6-M1: storeHitStates was dropped silently).
_FIT_HONOURED = {'tracks', 'eventOfHits', 'mkFitPixelHits', 'config', 'pixelCPE', 'candCutSel', 'candMinPtCut',
                 'candMinNHitsCut', 'candMinPtRelaxedCut', 'candMinAbsEtaForRelaxedCut', 'mkFitSilent',
                 'limitConcurrency', 'mightGet'}
_FIT_DEFAULTS = {'storeHitStates': False}


def _checkStockFitConfig(menu):
    """Refuse to replace a stock MkFitFitProducer whose configuration the device fit does not reproduce. D7-g: the
    mtd_at_hlt procModifier sets storeHitStates = True (per-hit smoothed states for the MTD extension); the device fit
    of the target menu (asynchronous, DEVICE_FIT_FROM_BUILD) has no per-hit states, so that combination is refused."""
    for name in menu.parameterNames_():
        if name in _FIT_HONOURED:
            continue
        val = getattr(menu, name).value()
        if name in _FIT_DEFAULTS and val == _FIT_DEFAULTS[name]:
            continue
        if name == 'storeHitStates':
            raise RuntimeError('customizeHLTforMkFitAlpakaFit: %s has storeHitStates = True (procModifier mtd_at_hlt): '
                               'the device mkFit fit does not provide the per-hit states. mtd_at_hlt with the device fit '
                               'is not supported yet (D7-g); run the menu without mtd_at_hlt or without the device fit.'
                               % FIT)
        raise RuntimeError('customizeHLTforMkFitAlpakaFit: %s parameter %s = %r is not honoured by the device fit; '
                           'refusing the swap (review R6-M1)' % (FIT, name, val))


def _customizeFit(process, allowNoCPE):
    if not hasattr(process, FIT):
        raise RuntimeError('customizeHLTforMkFitAlpakaFit: no %s in the menu (procModifier trackingMkFitFit needed)'
                           % FIT)
    if not DEVICE_FIT_HAS_CPE and not allowNoCPE:
        raise RuntimeError('customizeHLTforMkFitAlpakaFit: the device fit has no CPE yet (D-H1); refusing to replace '
                           'the stock MkFitFitProducer silently. Use customizeHLTforMkFitAlpakaFitNoCPE to accept it '
                           'explicitly (validation only).')
    menu = getattr(process, FIT)
    if menu.type_() != 'MkFitFitProducer':
        raise RuntimeError('customizeHLTforMkFitAlpakaFit: expected the stock MkFitFitProducer as %s, found %s'
                           % (FIT, menu.type_()))
    _checkStockFitConfig(menu)
    useCpe = not allowNoCPE
    fast = _checkCpeConfig(process, menu) if useCpe else None
    _swap(process, replaceFit=True)
    stockEOH = getattr(process, EOH)  # device EventOfHits now; its hit inputs are the stock hit wrappers
    fit = cms.EDProducer('MkFitAlpakaFitProducer@alpaka', tracks=menu.tracks,
                         pixelHits=menu.mkFitPixelHits, stripHits=cms.InputTag(stockEOH.stripHits.value()),
                         esData=cms.ESInputTag('', ES_LABEL), candCutSel=menu.candCutSel,
                         candMinPtCut=menu.candMinPtCut, candMinNHitsCut=menu.candMinNHitsCut,
                         candMinPtRelaxedCut=menu.candMinPtRelaxedCut,
                         candMinAbsEtaForRelaxedCut=menu.candMinAbsEtaForRelaxedCut,
                         cpe=cms.bool(useCpe),  # explicit in both variants (R4-M2)
                         # hits from the device EventOfHits (same rows as the hit wrappers): no host hit packing
                         eventOfHits=cms.InputTag(EOH))
    if useCpe:
        if not hasattr(process, 'hltMkFitAlpakaFitCpeESProducer'):
            process.hltMkFitAlpakaFitCpeESProducer = cms.ESProducer(
                'MkFitAlpakaFitCpeESProducer@alpaka', ComponentName=cms.string(CPE_ES_LABEL),
                cpeFastParams=cms.string(fast.ComponentName.value()))
        fit.cpeTables = cms.string(CPE_ES_LABEL)
        fit.pixelCPE = cms.string(menu.pixelCPE.value())
        fit.cpeCheck = cms.string('throw')
    if DEVICE_FIT_FROM_BUILD:
        dev = cms.EDProducer('MkFitAlpakaFitDeviceProducer@alpaka', tracks=cms.InputTag(BUILD_DEVICE),
                             eventOfHits=cms.InputTag(EOH), pixelHits=cms.InputTag(fit.pixelHits.getModuleLabel()),
                             esData=fit.esData, candCutSel=fit.candCutSel, candMinPtCut=fit.candMinPtCut,
                             candMinNHitsCut=fit.candMinNHitsCut, candMinPtRelaxedCut=fit.candMinPtRelaxedCut,
                             candMinAbsEtaForRelaxedCut=fit.candMinAbsEtaForRelaxedCut, cpe=fit.cpe)
        if useCpe:
            dev.cpeTables = fit.cpeTables
            dev.pixelCPE = fit.pixelCPE
            dev.cpeCheck = fit.cpeCheck
        setattr(process, FIT_DEVICE, dev)
        process.hltMkFitAlpakaTask.add(dev)
        fit = cms.EDProducer('MkFitAlpakaOutputWrapperFromTrackSoA', tracks=cms.InputTag(FIT_DEVICE),
                             propagatedToFirstLayer=cms.bool(True),
                             status=cms.InputTag(FIT_DEVICE))  # review H4: LogWarning if not clean
    setattr(process, FIT, fit)
    return process


def customizeHLTforMkFitAlpakaFit(process):
    return _customizeFit(process, allowNoCPE=False)


def customizeHLTforMkFitAlpakaFitNoCPE(process):
    return _customizeFit(process, allowNoCPE=True)


def customizeHLTforMkFitAlpakaFitValidation(process):
    """customizeHLTforMkFitAlpakaFit + the stock MkFitFitProducer on the SAME input tracks (the device building output)
    in the same job (on its own full stock EventOfHits), its MkFitOutputTrackConverter, and in-job comparators:
    [compare menuCmpFit] (MkFitOutputWrapper, device fit vs stock fit) and [compare menuCmpFitTracks] (reco::Track)."""
    if not hasattr(process, FIT):
        return process
    stockFit = getattr(process, FIT).clone()
    stockEOH = getattr(process, EOH).clone()
    _customizeFit(process, allowNoCPE=False)
    process.hltMkFitEventOfHitsStockFit = stockEOH
    process.hltInitialStepTrackCandidatesMkFitFitStock = stockFit.clone(eventOfHits='hltMkFitEventOfHitsStockFit')
    process.hltInitialStepTracksStockFit = process.hltInitialStepTracks.clone(
        src='hltInitialStepTrackCandidatesMkFitFitStock', mkFitEventOfHits='hltMkFitEventOfHitsStockFit')
    process.menuCmpFit = cms.EDAnalyzer('MkFitAlpakaMkFitTrackCompare',
                                        reference=cms.InputTag('hltInitialStepTrackCandidatesMkFitFitStock'),
                                        target=cms.InputTag(FIT), label=cms.string('menuCmpFit'),
                                        paramTolerance=cms.double(1e-3), maxPrint=cms.int32(3))
    process.menuCmpFitTracks = cms.EDAnalyzer('MkFitAlpakaTrackCompare',
                                              reference=cms.InputTag('hltInitialStepTracksStockFit'),
                                              target=cms.InputTag('hltInitialStepTracks'),
                                              label=cms.string('menuCmpFitTracks'), paramTolerance=cms.double(1e-3),
                                              maxPrint=cms.int32(3), validHitsOnly=cms.bool(True))
    # same-job rounding reference: a serial_sync clone of the device fit (and of its device EventOfHits) on the same
    # input; [compare menuCmpFitSerial] (stock vs serial) and [compare menuCmpFitDevSerial] (serial vs device) show
    # the rounding-level spread of this sample (on a CPU job both are serial: a determinism check)
    from HeterogeneousCore.AlpakaCore.functions import makeSerialClone
    process.hltMkFitEventOfHitsSerialFit = makeSerialClone(getattr(process, EOH))
    if DEVICE_FIT_FROM_BUILD:
        # serial clone of the device fit on the host copy of the device building output, + its host converter
        process.hltInitialStepTrackCandidatesMkFitFitSerialDevice = makeSerialClone(
            getattr(process, FIT_DEVICE), eventOfHits='hltMkFitEventOfHitsSerialFit', cpeCheck='off')
        process.hltInitialStepTrackCandidatesMkFitFitSerial = cms.EDProducer(
            'MkFitAlpakaOutputWrapperFromTrackSoA', tracks=cms.InputTag('hltInitialStepTrackCandidatesMkFitFitSerialDevice'),
            propagatedToFirstLayer=cms.bool(True), status=cms.InputTag('hltInitialStepTrackCandidatesMkFitFitSerialDevice'))
        process.hltMkFitAlpakaTask.add(process.hltInitialStepTrackCandidatesMkFitFitSerialDevice)
    else:
        process.hltInitialStepTrackCandidatesMkFitFitSerial = makeSerialClone(getattr(process, FIT),
                                                                              eventOfHits='hltMkFitEventOfHitsSerialFit',
                                                                              cpeCheck='off')
    process.menuCmpFitSerial = process.menuCmpFit.clone(target='hltInitialStepTrackCandidatesMkFitFitSerial',
                                                        label='menuCmpFitSerial', maxPrint=0)
    process.menuCmpFitDevSerial = process.menuCmpFit.clone(reference='hltInitialStepTrackCandidatesMkFitFitSerial',
                                                           label='menuCmpFitDevSerial', maxPrint=0)
    process.hltMkFitAlpakaFitValidationSequence = cms.Sequence(
        process.hltMkFitEventOfHitsStockFit + process.hltInitialStepTrackCandidatesMkFitFitStock +
        process.hltInitialStepTracksStockFit + process.menuCmpFit + process.menuCmpFitTracks +
        process.hltMkFitEventOfHitsSerialFit + process.hltInitialStepTrackCandidatesMkFitFitSerial +
        process.menuCmpFitSerial + process.menuCmpFitDevSerial)
    process.HLTInitialStepSequence += process.hltMkFitAlpakaFitValidationSequence
    return process


# ---------------------------------------------------------------------------------------------------------------------
# Round 6: device hit input + the TARGET MENU
#
#   customizeHLTforMkFitAlpakaDeviceHits        device hit input on top of an already applied production swap
#   customizeHLTforMkFitAlpakaTarget            THE TARGET MENU (procModifier trackingMkFitFit): device hit input +
#                                               index-only hit maps + device EventOfHits + device building + device mkFit
#                                               final fit with the CPE (replaces the stock MkFitFitProducer)
#   customizeHLTforMkFitAlpakaTargetValidation  the target menu + the complete STOCK chain of the same events in the
#                                               same job (stock hit converters, EventOfHits, MkFitProducer,
#                                               MkFitFitProducer, MkFitOutputTrackConverter) + in-job checks:
#                                               DEVICE_HITS_EOH (device HitSoA vs the stock converters, bitwise),
#                                               [compare menuCmpMkFit] (building, chained), [compare menuCmpFit*]
#                                               (device fit vs stock fit on the SAME input = isolated; + serial clone),
#                                               [compare menuCmpTracks] (reco::Track, chained), [paired menu] (truth)
# Device hit input (lane inputs, round 5): hltMkFitEventOfHits builds its HitSoA on the device from the pixel rechit
# SoA (+ legacy cluster order/sizes) and the OT rechits, bitwise equal to the stock hit converters; the stock
# converters hltMkFitSiPixelHits / hltMkFitSiPhase2Hits are replaced by index-only MkFitClusterIndexToHit producers
# under the same labels (the stock MkFitOutputConverter / MkFitOutputTrackConverter and our modules read only that).
DEVICE_HITS = True
# GPU only: read the pixel rechit columns straight from the device SoA. Lane inputs could not separate it from the
# host-SoA read (4.7 vs 4.5 ms host time); off until measured at the CI shape.
PIXEL_SOA_ON_DEVICE = False
# Device seeds, option (b) of O6-1 (lane seeds, round 6): the building module fits the T5/T4/pT3/pT5 seed states on the
# device with the ported mkFit fit code (BuildProducer parameter lstSeedFit); pLS seeds keep the copied pixel state.
# A DEVIATION candidate: OFF by default, kept only if the full O6-1 regression list shows nothing worse. Needs lane
# seeds' BuildProducer (an unknown parameter is a configuration error, never a silent fallback).
DEVICE_SEEDS = False
# D7-f (round 8, lane mem): the device EventOfHits is deleted right after the device fit (canDeleteEarly), not at the
# end of the event (python/customizeMemory.py, doc/mem.txt). Output identical; GPU memory per stream in flight lower.
EOH_EARLY_DELETE = True
# D8-c (coordinator, round 8; verify7_int gate passed): the single host output conversion of lane outconv
# (python/customizeOutConv.py, MkFitAlpakaOutputTrackConverter on the device fit's TrackSoA) is part of the target.
OUTCONV = True
# Stage D (round 8, lane otdev; python/customizeOTDev.py customizeOTDevFull): device OT rechits, legacy hltSiPhase2RecHits
# out of the menu. Bitwise identical outputs: lane otdev's gates, and verify8_int (CPU ttbar 50 + QCD 50: hltGeneralTracks,
# hltInitialStepTracks, hltPhase2PixelTracks, LST seeds identical in every event; GPU ttbar 100: 17.6M OT rechits, 3.2M CA
# OT hits, EventOfHits and LST input OT hits bitwise). The bitwise gates (customizeOTDevGate / ...FullGate) need it OFF.
# Round 8 kept it OFF for GPU memory (+1.1 GB peak at the CI shape: the OT rechit SoA lived to the end of the event).
# ON since verify9_int (D9-e / D9-f): with OT_EARLY_DELETE the CI-shape peak (4 jobs x 16 streams, L40) is target + stage
# D 13.42 GB vs stock mkFit-fit 12.44 GB (+7.9%, budget +10%; target without stage D 13.49 GB), outputs identical on CPU
# (ttbar 50 + QCD 50, tracks and LST seeds) and the GPU input gates bitwise (verify9_int/REPORT.txt sections 5, 6).
OT_DEVICE = True
# D9-f (round 9, lane mem; python/customizeMemory.py, doc/mem.txt): with stage D, the device OT rechit SoA is deleted
# right after hltMkFitEventOfHits (canDeleteEarly + a device-side queue order), not at the end of the event. Output
# identical; GPU memory per event in flight lower.
OT_EARLY_DELETE = True
# R8-M4 lever 1 (round 9, lane mem): the unread build host wrapper hltInitialStepTrackCandidatesMkFit leaves the menu
# (its status warnings move to MkFitAlpakaStatusCheck); the build TrackSoA is then deleted after the device fit.
DROP_BUILD_WRAPPER = True
# Seeds option (b') LIGHT mode in the target (DEVIATION D2, r8/stageb): LSTOutputConverter skips the host seed creator
# (seedStates False) and the build module fits the T5/T4/pT3/pT5 seed states on the device with the (b') settings
# (_lstSeedsLight: one forward pass, origin prior 3, the host-fallback emulation). Known regression: paired eff -0.00013
# (ttbar) / -0.00014 (QCD) +- 0.00001. ON by the owner's decision D9-a (2026-10-04 ~8:55 am: accept the known regression
# for now, fix it later with (b) methods). TargetValidation never applies it (its in-job stock chain must keep the host
# seed states).
SEEDS_LIGHT = True
# DEVIATIONS D3 + D4 + D7 (customizeFitDev.customizeFitDevOutliersD7): edge outliers + 3 outlier rounds in the device
# fit, negative / non-finite chi2 tracks dropped in the converter, propagation to the first hit of a refit (the #186
# fix). ON by the owner's decision O10-3 (2026-10-04): the target then matches the KF menu's efficiency (ttbar 500,
# QCD 500). TargetValidation keeps the stock fit (it never applies these).
FIT_OUTLIERS_D7 = True
# DEVIATION D8 (customizeFitDev.customizeFitPhysTrackAlgo): the converted tracks get the algorithm 'initialStep', as in
# the KF menu, instead of undefAlgorithm from the HLT seeds label (PF drops ~12 good tracks per ttbar event with it).
# ON by the owner's decision O10-2 (2026-10-04); tracks identical (verify10_int 5c).
TRACK_ALGO = 'initialStep'
PIX_HITS = 'hltMkFitSiPixelHits'
OT_HITS = 'hltMkFitSiPhase2Hits'
# plugins that read the hit labels only as MkFitClusterIndexToHit (safe with the index-only producers)
_INDEX_ONLY_READERS = ('MkFitOutputConverter', 'MkFitOutputTrackConverter', 'MkFitAlpakaBuildProducer@alpaka',
                       'MkFitAlpakaFitDeviceProducer@alpaka',
                       'MkFitAlpakaPixelClusterIndexToHit', 'MkFitAlpakaPhase2ClusterIndexToHit')


def _checkPixelRecHitsFromSoA(process, eoh):
    """R5-M2, python half: the device hit transform takes row k of a module from SoA row moduleStart + originalId(k);
    that is right only if the legacy pixel rechits are the legacy copy of the same rechit SoA."""
    rh = getattr(process, eoh.pixelRecHits.getModuleLabel(), None)
    soa = eoh.pixelSoA.getModuleLabel()
    if rh is None or not rh.type_().startswith('SiPixelRecHitFromSoAAlpaka') or \
            rh.pixelRecHitSrc.getModuleLabel().split('@')[0] != soa:
        raise RuntimeError('customizeHLTforMkFitAlpakaDeviceHits: %s must be the legacy copy of %s (found %s)'
                           % (eoh.pixelRecHits.getModuleLabel(), soa, rh.type_() if rh is not None else None))


def customizeHLTforMkFitAlpakaDeviceHits(process, compare=False, soaOnDevice=None):
    """Device hit input (after the production swap). compare=True keeps the stock converters as <label>Stock and
    checks the device HitSoA against them in every event (DEVICE_HITS_EOH ... IDENTICAL lines)."""
    if not hasattr(process, EOH) or getattr(process, EOH).type_() != 'MkFitAlpakaEventOfHitsProducer@alpaka':
        raise RuntimeError('customizeHLTforMkFitAlpakaDeviceHits: apply the production swap first')
    eoh = getattr(process, EOH)
    pix, ot = getattr(process, PIX_HITS), getattr(process, OT_HITS)
    if pix.type_() != 'MkFitSiPixelHitConverter' or ot.type_() != 'MkFitPhase2HitConverter':
        raise RuntimeError('customizeHLTforMkFitAlpakaDeviceHits: expected the stock hit converters, found %s/%s'
                           % (pix.type_(), ot.type_()))
    # stock converters stay available (unscheduled: they run only if a validation module asks for them)
    process.hltMkFitSiPixelHitsStock = pix.clone()
    process.hltMkFitSiPhase2HitsStock = ot.clone()
    process.hltMkFitAlpakaTask.add(process.hltMkFitSiPixelHitsStock, process.hltMkFitSiPhase2HitsStock)
    eoh.pixelHits = 'hltMkFitSiPixelHitsStock'
    eoh.stripHits = 'hltMkFitSiPhase2HitsStock'
    eoh.compareHostHits = cms.bool(compare)
    eoh.deviceHits = cms.bool(True)
    eoh.pixelSoAOnDevice = cms.bool(PIXEL_SOA_ON_DEVICE if soaOnDevice is None else soaOnDevice)
    eoh.pixelSoA = cms.InputTag('hltPhase2SiPixelRecHitsSoA')
    eoh.pixelRecHits = cms.InputTag(pix.hits.getModuleLabel())
    eoh.otRecHits = cms.InputTag(ot.hits.getModuleLabel())
    eoh.pixelClusters = cms.InputTag(pix.clusters.getModuleLabel())
    eoh.otClusters = cms.InputTag(ot.clusters.getModuleLabel())
    _checkPixelRecHitsFromSoA(process, eoh)
    setattr(process, PIX_HITS, cms.EDProducer('MkFitAlpakaPixelClusterIndexToHit', hits=pix.hits))
    setattr(process, OT_HITS, cms.EDProducer('MkFitAlpakaPhase2ClusterIndexToHit', hits=ot.hits))
    # every remaining reader of the hit labels must read only MkFitClusterIndexToHit (the device fit does so when its
    # hits come from the device EventOfHits; the stock MkFitFitProducer / MkFitProducer / EventOfHits producer read
    # MkFitHitWrapper and must not be left on these labels)
    bad = []
    for lab in (PIX_HITS, OT_HITS):
        for c in _consumers(process, lab):
            m = getattr(process, c.split('.')[0])
            if m.type_() in _INDEX_ONLY_READERS:
                continue
            if m.type_() == 'MkFitAlpakaFitProducer@alpaka' and m.eventOfHits.getModuleLabel() != '':
                continue
            bad.append('%s (%s)' % (c, m.type_()))
    if bad:
        raise RuntimeError('customizeHLTforMkFitAlpakaDeviceHits: modules that need the stock MkFitHitWrapper read %s/%s: '
                           '%s' % (PIX_HITS, OT_HITS, bad))
    return process


def _deviceSeeds(process, on):
    if on:
        getattr(process, BUILD_DEVICE).lstSeedFit = cms.bool(True)


def customizeHLTforMkFitAlpakaTarget(process):
    """The round-6 target menu (needs --procModifiers trackingMkFitFit): device hits (DEVICE_HITS) + device EventOfHits
    + device building (+ device seed fit if DEVICE_SEEDS) + device mkFit final fit with the CPE."""
    if not hasattr(process, BUILD):
        return process
    _customizeFit(process, allowNoCPE=False)
    if DEVICE_HITS:
        customizeHLTforMkFitAlpakaDeviceHits(process)
    _deviceSeeds(process, DEVICE_SEEDS)
    if EOH_EARLY_DELETE:
        from RecoTracker.MkFitAlpaka.customizeMemory import customizeMkFitAlpakaEarlyDelete
        customizeMkFitAlpakaEarlyDelete(process, eoh=EOH, fit=FIT_DEVICE, build=BUILD_DEVICE)
    if OUTCONV:
        from RecoTracker.MkFitAlpaka.customizeOutConv import customizeOutConv
        customizeOutConv(process)
    if OT_DEVICE:
        from RecoTracker.MkFitAlpaka.customizeOTDev import customizeOTDevFull
        customizeOTDevFull(process)
        if OT_EARLY_DELETE:
            from RecoTracker.MkFitAlpaka.customizeMemory import customizeOTSoAEarlyDelete
            customizeOTSoAEarlyDelete(process)
    if DROP_BUILD_WRAPPER:
        from RecoTracker.MkFitAlpaka.customizeMemory import customizeDropBuildWrapper
        customizeDropBuildWrapper(process)
    if SEEDS_LIGHT:
        _lstSeedsLight(process)
    if FIT_OUTLIERS_D7 or TRACK_ALGO:
        from RecoTracker.MkFitAlpaka.customizeFitDev import customizeFitDevOutliersD7, customizeFitPhysTrackAlgo
        if FIT_OUTLIERS_D7:
            customizeFitDevOutliersD7(process)
        if TRACK_ALGO:
            customizeFitPhysTrackAlgo(process, TRACK_ALGO)
    return process


def customizeHLTforMkFitAlpakaTargetDeviceSeeds(process):
    """Target menu with the device seed fit (O6-1 (b)) switched on: for the regression checks of lane seeds' switch."""
    customizeHLTforMkFitAlpakaTarget(process)
    _deviceSeeds(process, True)
    return process


def customizeHLTforMkFitAlpakaTargetValidation(process, compareHits=True):
    """Target menu + the complete stock chain of the same events in the same job + in-job comparators (see above).
    The stock chain is appended to the menu's initial-step sequence, so it runs exactly in the events where the menu
    tracks."""
    if not hasattr(process, BUILD):
        return process
    if not hasattr(process, FIT) or getattr(process, FIT).type_() != 'MkFitFitProducer':
        raise RuntimeError('customizeHLTforMkFitAlpakaTargetValidation: needs procModifier trackingMkFitFit')
    stockEOH = getattr(process, EOH).clone()
    stockBuild = getattr(process, BUILD).clone()
    stockFit = getattr(process, FIT).clone()
    stockTracks = process.hltInitialStepTracks.clone()
    _customizeFit(process, allowNoCPE=False)
    _deviceSeeds(process, DEVICE_SEEDS)
    if DEVICE_HITS:
        customizeHLTforMkFitAlpakaDeviceHits(process, compare=compareHits)
        pixStock, otStock = 'hltMkFitSiPixelHitsStock', 'hltMkFitSiPhase2HitsStock'
    else:
        pixStock, otStock = PIX_HITS, OT_HITS
    # stock chain on the stock hit converters and its own full EventOfHits
    process.hltMkFitEventOfHitsStock = stockEOH.clone(pixelHits=pixStock, stripHits=otStock)
    process.hltInitialStepTrackCandidatesMkFitStock = stockBuild.clone(eventOfHits='hltMkFitEventOfHitsStock',
                                                                       pixelHits=pixStock, stripHits=otStock)
    process.hltInitialStepTrackCandidatesMkFitFitStock = stockFit.clone(
        tracks='hltInitialStepTrackCandidatesMkFitStock', eventOfHits='hltMkFitEventOfHitsStock',
        mkFitPixelHits=pixStock)
    process.hltInitialStepTracksStock = stockTracks.clone(src='hltInitialStepTrackCandidatesMkFitFitStock',
                                                          mkFitEventOfHits='hltMkFitEventOfHitsStock',
                                                          mkFitPixelHits=pixStock, mkFitStripHits=otStock)
    # isolated fit reference: stock MkFitFitProducer on the DEVICE building output (same input tracks)
    process.hltInitialStepTrackCandidatesMkFitFitStockIso = stockFit.clone(eventOfHits='hltMkFitEventOfHitsStock',
                                                                           mkFitPixelHits=pixStock)
    process.hltInitialStepTracksStockIso = stockTracks.clone(src='hltInitialStepTrackCandidatesMkFitFitStockIso',
                                                             mkFitEventOfHits='hltMkFitEventOfHitsStock',
                                                             mkFitPixelHits=pixStock, mkFitStripHits=otStock)
    cmp = []

    def add(name, plugin, ref, tgt, **kw):
        setattr(process, name, cms.EDAnalyzer(plugin, reference=cms.InputTag(ref), target=cms.InputTag(tgt),
                                              label=cms.string(name), paramTolerance=cms.double(1e-3),
                                              maxPrint=cms.int32(kw.pop('maxPrint', 3)), **kw))
        cmp.append(getattr(process, name))

    add('menuCmpMkFit', 'MkFitAlpakaMkFitTrackCompare', 'hltInitialStepTrackCandidatesMkFitStock', BUILD)
    add('menuCmpFit', 'MkFitAlpakaMkFitTrackCompare', 'hltInitialStepTrackCandidatesMkFitFitStockIso', FIT)
    add('menuCmpFitTracks', 'MkFitAlpakaTrackCompare', 'hltInitialStepTracksStockIso', 'hltInitialStepTracks',
        validHitsOnly=cms.bool(True))
    add('menuCmpFitChain', 'MkFitAlpakaMkFitTrackCompare', 'hltInitialStepTrackCandidatesMkFitFitStock', FIT)
    add('menuCmpTracks', 'MkFitAlpakaTrackCompare', 'hltInitialStepTracksStock', 'hltInitialStepTracks',
        validHitsOnly=cms.bool(True))
    # same-job rounding reference of the fit: serial clones of the device EventOfHits and fit on the same input
    from HeterogeneousCore.AlpakaCore.functions import makeSerialClone
    process.hltMkFitEventOfHitsSerialFit = makeSerialClone(getattr(process, EOH), compareHostHits=False)
    fitStatus = []
    if DEVICE_FIT_FROM_BUILD:
        # the fit label is the host converter of the async device fit: clone the device module, then convert
        process.hltInitialStepTrackCandidatesMkFitFitSerialDevice = makeSerialClone(
            getattr(process, FIT_DEVICE), eventOfHits='hltMkFitEventOfHitsSerialFit', cpeCheck='off')
        process.hltInitialStepTrackCandidatesMkFitFitSerial = cms.EDProducer(
            'MkFitAlpakaOutputWrapperFromTrackSoA', tracks=cms.InputTag('hltInitialStepTrackCandidatesMkFitFitSerialDevice'),
            propagatedToFirstLayer=cms.bool(True), status=cms.InputTag('hltInitialStepTrackCandidatesMkFitFitSerialDevice'))
        process.hltMkFitAlpakaTask.add(process.hltInitialStepTrackCandidatesMkFitFitSerialDevice)
        process.menuStatusFit = cms.EDAnalyzer('MkFitAlpakaStatusCheck', src=cms.InputTag(FIT_DEVICE),
                                               requireClean=cms.bool(True), summaryFile=cms.string('status_fit.json'))
        fitStatus.append(process.menuStatusFit)
    else:
        process.hltInitialStepTrackCandidatesMkFitFitSerial = makeSerialClone(getattr(process, FIT),
                                                                              eventOfHits='hltMkFitEventOfHitsSerialFit',
                                                                              cpeCheck='off')
    add('menuCmpFitSerial', 'MkFitAlpakaMkFitTrackCompare', 'hltInitialStepTrackCandidatesMkFitFitStockIso',
        'hltInitialStepTrackCandidatesMkFitFitSerial', maxPrint=0)
    add('menuCmpFitDevSerial', 'MkFitAlpakaMkFitTrackCompare', 'hltInitialStepTrackCandidatesMkFitFitSerial', FIT,
        maxPrint=0)
    process.menuStatus = cms.EDAnalyzer('MkFitAlpakaStatusCheck', src=cms.InputTag(BUILD_DEVICE),
                                        requireClean=cms.bool(True), summaryFile=cms.string('status.json'))
    seq = (process.hltMkFitEventOfHitsStock + process.hltInitialStepTrackCandidatesMkFitStock +
           process.hltInitialStepTrackCandidatesMkFitFitStock + process.hltInitialStepTracksStock +
           process.hltInitialStepTrackCandidatesMkFitFitStockIso + process.hltInitialStepTracksStockIso +
           process.hltMkFitEventOfHitsSerialFit + process.hltInitialStepTrackCandidatesMkFitFitSerial)
    for c in cmp:
        seq = seq + c
    seq = seq + process.menuStatus
    for c in fitStatus:
        seq = seq + c
    process.hltMkFitAlpakaValidationSequence = cms.Sequence(seq)
    process.HLTInitialStepSequence += process.hltMkFitAlpakaValidationSequence
    if hasattr(process, 'hltTrackValidator') and hasattr(process, 'hltTrackAssociatorByHits'):
        v = process.hltTrackValidator
        sel = cms.PSet(**{k: getattr(v, k) for k in [
            'ptMinTP', 'ptMaxTP', 'minRapidityTP', 'maxRapidityTP', 'tipTP', 'lipTP', 'minHitTP', 'signalOnlyTP',
            'intimeOnlyTP', 'chargedOnlyTP', 'stableOnlyTP', 'pdgIdTP', 'invertRapidityCutTP', 'minPhi', 'maxPhi']})
        process.menuPaired = cms.EDAnalyzer('MkFitAlpakaPairedTruthCompare',
                                            reference=cms.InputTag('hltInitialStepTracksStock'),
                                            target=cms.InputTag('hltInitialStepTracks'),
                                            trackingParticles=v.label_tp_effic,
                                            associator=cms.InputTag(v.associators[0]), label=cms.string('menu'),
                                            tpSelection=sel)
        process.hltMkFitAlpakaValidationEndPath = cms.EndPath(process.menuPaired)
        if process.schedule_() is not None:
            process.schedule.append(process.hltMkFitAlpakaValidationEndPath)
    return process

# ---- O6-1 option (b): device LST seed fit (lane seeds, round 6). Switch-gated; default OFF (DEVICE_LST_SEED_FIT).
def _lstSeeds(process, passes=3, errScale=1.0):
    if hasattr(process, BUILD_DEVICE):
        b = getattr(process, BUILD_DEVICE)
        b.lstSeedFit = cms.bool(True)
        b.lstSeedFitPasses = cms.int32(passes)
        b.lstSeedFitErrScale = cms.double(errScale)
    return process


def customizeHLTforMkFitAlpakaLstSeeds(process):
    """Production swap + the device LST seed fit (O6-1 (b))."""
    return _lstSeeds(customizeHLTforMkFitAlpaka(process))


def customizeHLTforMkFitAlpakaFitLstSeeds(process):
    """customizeHLTforMkFitAlpakaFit (device final fit, trackingMkFitFit) + the device LST seed fit (O6-1 (b))."""
    return _lstSeeds(customizeHLTforMkFitAlpakaFit(process))


LST_SEEDS = 'hltInitialStepTrajectorySeedsLST'


def _lstSeedsLight(process, passes=1, originPrior=3):
    """The host LSTOutputConverter skips the seed creator's fit (seedStates = False: seed hits + a placeholder state,
    needs the lane-seeds LST patch); the device fit replaces every state with OT hits and removes failed fits.
    Defaults = the (b') settings of r8/stageb's D7-b runs (one forward pass, origin prior 3), not round 6's (R8-L4)."""
    _lstSeeds(process, passes=passes)
    if hasattr(process, BUILD_DEVICE):
        getattr(process, BUILD_DEVICE).lstSeedFitOriginPrior = cms.int32(originPrior)
    if hasattr(process, LST_SEEDS) and hasattr(process, BUILD_DEVICE):
        c = getattr(process, LST_SEEDS)
        if c.produceTrackCandidates.value():
            raise RuntimeError('customizeHLTforMkFitAlpakaLstSeedsLight: %s produces track candidates' % LST_SEEDS)
        c.seedStates = cms.bool(False)
        getattr(process, BUILD_DEVICE).lstSeedFitDropFailed = cms.bool(True)
    return process


def customizeHLTforMkFitAlpakaLstSeedsLight(process):
    return _lstSeedsLight(customizeHLTforMkFitAlpaka(process))


def customizeHLTforMkFitAlpakaFitLstSeedsLight(process):
    return _lstSeedsLight(customizeHLTforMkFitAlpakaFit(process))


def customizeHLTforMkFitAlpakaValidationLstSeeds(process):
    """Validation (stock chain in the same job, per-seed comparators, paired truth) with the device LST seed fit in the
    port: the in-job paired truth is then (b) vs stock."""
    return _lstSeeds(customizeHLTforMkFitAlpakaValidation(process))
