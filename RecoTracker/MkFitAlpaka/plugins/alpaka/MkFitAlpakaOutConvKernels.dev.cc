// Stage C (lane stagec): device precomputation for the host output conversion (see MkFitAlpakaOutConvKernels.h).
#include <alpaka/alpaka.hpp>

#include "HeterogeneousCore/AlpakaInterface/interface/config.h"
#include "HeterogeneousCore/AlpakaInterface/interface/workdivision.h"

#include "MkFitAlpakaOutConvKernels.h"

namespace ALPAKA_ACCELERATOR_NAMESPACE::mkfitdev::outconv {
  using namespace cms::alpakatools;
  namespace pca = ::mkfitdev::pca;

  namespace {
    struct KernelOutConvPca {
      ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                    ::mkfitdev::TrackSoAConstView trk,
                                    int capacity,
                                    pca::BeamIn bl,
                                    ::mkfitdev::OutConvSoAView out) const {
        const int n = trk.nTracks();
        for (auto t : uniform_elements(acc, capacity)) {
          if (int(t) >= n)
            continue;
          pca::TrackIn in;
          pca::ccsToCurvilinear(trk[t].params().v, trk[t].errors().v, trk[t].charge(), in);
          pca::PcaOut o{};
          pca::pcaFromFirstHitState(in, bl, o);
          auto row = out[t];
          row.pcaStatus() = int8_t(o.status);
          if (o.status != pca::kPcaOk)
            continue;
          auto& s = row.pcaState();
          s.v[0] = o.x.x;
          s.v[1] = o.x.y;
          s.v[2] = o.x.z;
          s.v[3] = o.p.x;
          s.v[4] = o.p.y;
          s.v[5] = o.p.z;
          auto& c = row.pcaCov();
          for (int i = 0, k = 0; i < 5; ++i)
            for (int j = 0; j <= i; ++j)
              c.v[k++] = float(o.C[i][j]);
        }
      }
    };
  }  // namespace

  void launchOutConvStates(Queue& queue,
                           ::mkfitdev::TrackSoAConstView trk,
                           int capacity,
                           pca::BeamIn const& beamLine,
                           ::mkfitdev::OutConvSoAView out) {
    if (capacity <= 0)
      return;
    // heavy double-precision kernel (TTMD + Jacobian): 64-thread blocks
    alpaka::exec<Acc1D>(
        queue, make_workdiv<Acc1D>(divide_up_by(capacity, 64), 64), KernelOutConvPca{}, trk, capacity, beamLine, out);
  }
}  // namespace ALPAKA_ACCELERATOR_NAMESPACE::mkfitdev::outconv
