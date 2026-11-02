#ifndef RecoTracker_LSTCore_interface_OTPullCellsSoA_h
#define RecoTracker_LSTCore_interface_OTPullCellsSoA_h

#include "DataFormats/SoATemplate/interface/SoALayout.h"
#include "DataFormats/Portable/interface/PortableHostCollection.h"

namespace lst {
  // CA terminal cells offered to LST's OT-seeded lookup: hit positions and the CA hit indices (pixel cluster keys).
  GENERATE_SOA_LAYOUT(OTPullCellsSoALayout,
                      SOA_COLUMN(float, xi),
                      SOA_COLUMN(float, yi),
                      SOA_COLUMN(float, zi),
                      SOA_COLUMN(float, xo),
                      SOA_COLUMN(float, yo),
                      SOA_COLUMN(float, zo),
                      SOA_COLUMN(uint32_t, hi),
                      SOA_COLUMN(uint32_t, ho))

  using OTPullCellsSoA = OTPullCellsSoALayout<>;
  using OTPullCellsHostCollection = PortableHostCollection<OTPullCellsSoA>;
}  // namespace lst

#endif
