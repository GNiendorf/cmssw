#ifndef RecoTracker_LSTCore_interface_PixelMap_h
#define RecoTracker_LSTCore_interface_PixelMap_h

#include <vector>
#include <cstdint>

#include "RecoTracker/LSTCore/interface/Common.h"

namespace lst {
  // Post-deletion: the pT5 / pT3 superbin -> outer-tracker connection maps are gone with their
  // builders. What remains is the one datum the pLS machinery needs -- the index of the virtual
  // pixel module row at the end of the module list.
  struct PixelMap {
    uint16_t pixelModuleIndex = 0;
  };
}  // namespace lst

#endif
