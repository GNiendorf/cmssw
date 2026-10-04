// Stage D (round 8, lane otdev): see MkFitAlpakaOTRecHitsKernels.h and doc/otdev.txt.
#include <alpaka/alpaka.hpp>

#include "HeterogeneousCore/AlpakaInterface/interface/config.h"
#include "HeterogeneousCore/AlpakaInterface/interface/workdivision.h"
#include "RecoTracker/MkFitAlpaka/interface/othits/OTCpe.h"

#include "MkFitAlpakaOTRecHitsKernels.h"

namespace ALPAKA_ACCELERATOR_NAMESPACE::mkfitdev::otdev {

  using namespace cms::alpakatools;
  using ::mkfitdev::Contract;

  namespace {
    template <Contract ML, Contract MG>
    struct KernelOTCpe {
      ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                    ::mkfitdev::OTRecHitSoA::View v,
                                    ::mkfitdev::OTCpeModule const* table,
                                    int32_t firstIndex,
                                    uint32_t n) const {
        for (uint32_t i : uniform_elements(acc, n)) {
          ::mkfitdev::OTCpeModule const& m = table[v[i].module() - firstIndex];
          const uint32_t s = v[i].strip();
          float lx, ly;
          ::mkfitdev::otcpe::localPosition<ML>(m, s & 0xffffu, s >> 16, v[i].clustSize(), lx, ly);
          float g[3];
          ::mkfitdev::otcpe::toGlobal<MG>(m, lx, ly, g);
          v[i].lx() = lx;
          v[i].ly() = ly;
          v[i].exx() = m.exx;
          v[i].eyy() = m.eyy;
          v[i].gx() = g[0];
          v[i].gy() = g[1];
          v[i].gz() = g[2];
        }
      }
    };

    template <Contract ML>
    void launchG(Queue& queue,
                 WorkDiv1D const& wd,
                 ::mkfitdev::OTRecHitSoA::View view,
                 ::mkfitdev::OTCpeModule const* table,
                 int32_t firstIndex,
                 uint32_t n,
                 int contractGlobal) {
      if (contractGlobal == 0)
        alpaka::exec<Acc1D>(queue, wd, KernelOTCpe<ML, Contract::kFuseSecond>{}, view, table, firstIndex, n);
      else if (contractGlobal == 2)
        alpaka::exec<Acc1D>(queue, wd, KernelOTCpe<ML, Contract::kNoFuse>{}, view, table, firstIndex, n);
      else
        alpaka::exec<Acc1D>(queue, wd, KernelOTCpe<ML, Contract::kFuseFirst>{}, view, table, firstIndex, n);
    }
  }  // namespace

  void runOTCpe(Queue& queue,
                ::mkfitdev::OTRecHitSoA::View view,
                ::mkfitdev::OTCpeModule const* table,
                int32_t firstIndex,
                uint32_t n,
                int contractLocal,
                int contractGlobal) {
    if (n == 0)
      return;
    constexpr uint32_t kBlock = 128;
    const auto wd = make_workdiv<Acc1D>(divide_up_by(n, kBlock), kBlock);
    if (contractLocal == 0)
      launchG<Contract::kFuseSecond>(queue, wd, view, table, firstIndex, n, contractGlobal);
    else if (contractLocal == 2)
      launchG<Contract::kNoFuse>(queue, wd, view, table, firstIndex, n, contractGlobal);
    else
      launchG<Contract::kFuseFirst>(queue, wd, view, table, firstIndex, n, contractGlobal);
  }

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE::mkfitdev::otdev
