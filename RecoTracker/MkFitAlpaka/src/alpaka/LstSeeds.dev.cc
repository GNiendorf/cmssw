// Top-level translation unit of the LST seed fit (O6-1 option (b), D-layout): kernel in src/alpaka/seeds/LstSeedKernels.h.
#include "HeterogeneousCore/AlpakaInterface/interface/workdivision.h"
#include "RecoTracker/MkFitAlpaka/interface/seeds/alpaka/LstSeedFit.h"
#include "RecoTracker/MkFitAlpaka/src/alpaka/seeds/LstSeedKernels.h"

namespace ALPAKA_ACCELERATOR_NAMESPACE::mkfitdev::lstseeds {

  void fitLstSeeds(Queue& queue,
                   ::mkfitdev::ESView const& es,
                   ::mkfitdev::HitSoAConstView hits,
                   uint32_t nPixel,
                   ::mkfitdev::SeedSoAView seeds,
                   int32_t n,
                   LstSeedFitConfig const& cfg,
                   LstSeedFitCounters* counters,
                   int8_t* cls,
                   int8_t* st) {
    if (n <= 0)
      return;
    // heavy kernel (Matriplex N = 1 Kalman chain): block <= 128 (D5-e); 64 as the final fit
    constexpr uint32_t kThreads = 64;
    const uint32_t blocks = cms::alpakatools::divide_up_by(static_cast<uint32_t>(n), kThreads);
    alpaka::exec<Acc1D>(queue,
                        cms::alpakatools::make_workdiv<Acc1D>(blocks, kThreads),
                        KernelLstSeedFit{},
                        es,
                        hits,
                        nPixel,
                        seeds,
                        n,
                        cfg,
                        counters,
                        cls,
                        st);
  }

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE::mkfitdev::lstseeds
