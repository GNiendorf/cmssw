#ifndef RecoTracker_LSTCore_interface_ChainIncidenceDeviceCollection_h
#define RecoTracker_LSTCore_interface_ChainIncidenceDeviceCollection_h

#include "DataFormats/Portable/interface/alpaka/PortableCollection.h"

#include "RecoTracker/LSTCore/interface/alpaka/Common.h"
#include "RecoTracker/LSTCore/interface/ChainIncidenceSoA.h"

namespace ALPAKA_ACCELERATOR_NAMESPACE::lst {
  // Device-side CSR incidence bookkeeping of the chain-tracking graph (ChainIncidenceSoA.h). Two
  // instances are allocated per event, one keyed by dense MiniDoublet index and one by dense
  // Segment index, each with nKeys + 1 rows.
  using ChainIncidenceDeviceCollection = PortableCollection<ChainIncidenceSoA>;
}  // namespace ALPAKA_ACCELERATOR_NAMESPACE::lst
#endif
