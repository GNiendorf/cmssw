// Stage C (round 8, lane stagec): device navigation prototype kernel (steps (a) + (b), MkFitAlpakaNavDevice.h)
#include <alpaka/alpaka.hpp>

#include "HeterogeneousCore/AlpakaInterface/interface/config.h"
#include "HeterogeneousCore/AlpakaInterface/interface/workdivision.h"

#include "MkFitAlpakaNavKernels.h"

namespace ALPAKA_ACCELERATOR_NAMESPACE::mkfitdev::navdev {
  using namespace cms::alpakatools;
  namespace nav = ::mkfitdev::nav;

  namespace {
    struct KernelNav {
      ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                    nav::Table const* __restrict__ table,
                                    nav::TrackIn const* __restrict__ in,
                                    uint64_t* __restrict__ out,
                                    int n) const {
        for (auto k : uniform_elements(acc, 2 * n)) {
          const int i = k / 2, d = k % 2;
          const int start = in[i].start[d];
          out[k] = start < 0 ? 0 : nav::compatibleLayers(*table, start, in[i], d == 1);
        }
      }
    };
  }  // namespace

  void launchNav(Queue& queue, nav::Table const* table, nav::TrackIn const* in, uint64_t* out, int n) {
    if (n <= 0)
      return;
    alpaka::exec<Acc1D>(queue, make_workdiv<Acc1D>(divide_up_by(2 * n, 64), 64), KernelNav{}, table, in, out, n);
  }
}  // namespace ALPAKA_ACCELERATOR_NAMESPACE::mkfitdev::navdev
