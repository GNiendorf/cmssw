#ifndef RecoTracker_MkFitAlpaka_src_alpaka_prop_PropMath_h
#define RecoTracker_MkFitAlpaka_src_alpaka_prop_PropMath_h

// Small helper the stock propagation code takes from interface/cms_common_macros.h that lane mplex does not
// provide. Constants (Const::, Config::), vdt fast math, hipo, sincos4, getPhi and getTheta come from lane
// mplex (interface/math, exported by interface/matriplex/MatriplexBackend.h).

#include <cstdint>
#include <cstring>

#include "HeterogeneousCore/AlpakaInterface/interface/config.h"
#include "RecoTracker/MkFitAlpaka/interface/math/MathUtils.h"

namespace mkfitdev::prop {

  // mkfit::isFinite (cms_common_macros.h): one copy in interface/math/MathUtils.h.
  using ::mkfitdev::isFinite;

}  // namespace mkfitdev::prop

#endif
