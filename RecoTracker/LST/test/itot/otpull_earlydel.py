# Lane itot_r3/otpullmem: delete the CA's otpull cells product right after its only reader, the LST producer.
# LST reads the product only before its track-candidate stage waits on the queue (LSTEvent::binOTCells), so when the
# product returns to the caching allocator no kernel reads it any more. exec'd with `process` in scope.
import FWCore.ParameterSet.Config as _ed_cms

_OTPULL_TYPES = ('alpakaDevCudaRt128falselstOTPullCellsSoALayoutvoidPortableDeviceCollectionedmDeviceProduct',
                 'alpakaDevHipRt128falselstOTPullCellsSoALayoutvoidPortableDeviceCollectionedmDeviceProduct',
                 '128falselstOTPullCellsSoALayoutPortableHostCollection')


def otpull_early_delete(process):
    labels = [n for n, m in process.producers_().items()
              if m.type_().startswith('CAHitNtupletAlpaka') and hasattr(m, 'otpullExport') and m.otpullExport.value()]
    readers = {}
    for n, m in process.producers_().items():
        for p in m.parameterNames_():
            v = getattr(m, p)
            if isinstance(v, _ed_cms.InputTag) and v.getModuleLabel() in labels and v.getProductInstanceLabel() == 'otpullCells':
                readers.setdefault(v.getModuleLabel(), []).append((n, m.type_()))
    added = []
    for lab in labels:
        rs = readers.get(lab, [])
        if any(not t.startswith('LSTProducer') for _, t in rs):
            raise RuntimeError('otpull_early_delete: %s:otpullCells has readers %s' % (lab, rs))
        have = set(process.options.canDeleteEarly)
        new = ['%s_%s_otpullCells_%s' % (t, lab, process.name_()) for t in _OTPULL_TYPES]
        process.options.canDeleteEarly.extend([b for b in new if b not in have])
        added += new
    print('[otpull] early delete of the cells product:', labels, 'readers', readers)
    return added
