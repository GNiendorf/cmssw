#include <alpaka/alpaka.hpp>

#include "HeterogeneousCore/AlpakaInterface/interface/config.h"
#include "HeterogeneousCore/AlpakaInterface/interface/workdivision.h"

#include "MkFitESDataTestKernels.h"

namespace ALPAKA_ACCELERATOR_NAMESPACE::mkfitdev_estest {

  using namespace cms::alpakatools;

  namespace {
    constexpr uint32_t kThreads = 256;
    uint32_t nBlocks(int n) { return divide_up_by(static_cast<uint32_t>(n > 0 ? n : 1), kThreads); }
  }  // namespace

  class EsDetIdLookupKernel {
  public:
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  mkfitdev::ESView view,
                                  int n,
                                  uint32_t const* detid,
                                  int const* layer,
                                  int* outModule,
                                  int* outShortId) const {
      for (int32_t i : uniform_elements(acc, n)) {
        outModule[i] = view.findModule(detid[i]);
        outShortId[i] = view.shortId(layer[i], detid[i]);
      }
    }
  };

  class EsMaterialLookupKernel {
  public:
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  mkfitdev::ESView view,
                                  int n,
                                  float const* z,
                                  float const* r,
                                  float* outBbxi,
                                  float* outRadl) const {
      for (int32_t i : uniform_elements(acc, n)) {
        view.material.materialChecked(z[i], r[i], outBbxi[i], outRadl[i]);
      }
    }
  };

  class EsLayerPredicatesKernel {
  public:
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  mkfitdev::ESView view,
                                  int n,
                                  int const* layer,
                                  float const* q,
                                  float const* dq,
                                  int* out) const {
      for (int32_t i : uniform_elements(acc, n)) {
        const int l = layer[i];
        const mkfitdev::WSRResult wz = view.isWithinZSensitiveRegion(l, q[i], dq[i]);
        const mkfitdev::WSRResult wr = view.isWithinRSensitiveRegion(l, q[i], dq[i]);
        out[4 * i + 0] = wz.wsr * 2 + (wz.in_gap ? 1 : 0);
        out[4 * i + 1] = wr.wsr * 2 + (wr.in_gap ? 1 : 0);
        out[4 * i + 2] = view.isWithinQLimits(l, q[i]) ? 1 : 0;
        out[4 * i + 3] = view.isInRHole(l, q[i]) ? 1 : 0;
      }
    }
  };

  class EsConfigReadKernel {
  public:
    ALPAKA_FN_ACC void operator()(Acc1D const& acc, mkfitdev::ESView view, unsigned char* out) const {
      const unsigned char* src = reinterpret_cast<const unsigned char*>(view.config);
      for (int32_t i : uniform_elements(acc, static_cast<int32_t>(sizeof(mkfitdev::ESConfig)))) {
        out[i] = src[i];
      }
    }
  };

  class EsConfigFieldsKernel {
  public:
    ALPAKA_FN_ACC void operator()(Acc1D const& acc, mkfitdev::ESView view, float* out) const {
      for (int32_t i : uniform_elements(acc, 1)) {
        if (i == 0)
          extractConfigFields(*view.config, out);
      }
    }
  };

  void launchConfigFields(Queue& queue, mkfitdev::ESView view, float* out) {
    auto workDiv = make_workdiv<Acc1D>(1, 1);
    alpaka::exec<Acc1D>(queue, workDiv, EsConfigFieldsKernel{}, view, out);
  }

  void launchDetIdLookups(Queue& queue,
                          mkfitdev::ESView view,
                          int n,
                          uint32_t const* detid,
                          int const* layer,
                          int* outModule,
                          int* outShortId) {
    auto workDiv = make_workdiv<Acc1D>(nBlocks(n), kThreads);
    alpaka::exec<Acc1D>(queue, workDiv, EsDetIdLookupKernel{}, view, n, detid, layer, outModule, outShortId);
  }

  void launchMaterialLookups(
      Queue& queue, mkfitdev::ESView view, int n, float const* z, float const* r, float* outBbxi, float* outRadl) {
    auto workDiv = make_workdiv<Acc1D>(nBlocks(n), kThreads);
    alpaka::exec<Acc1D>(queue, workDiv, EsMaterialLookupKernel{}, view, n, z, r, outBbxi, outRadl);
  }

  void launchLayerPredicates(
      Queue& queue, mkfitdev::ESView view, int n, int const* layer, float const* q, float const* dq, int* out) {
    auto workDiv = make_workdiv<Acc1D>(nBlocks(n), kThreads);
    alpaka::exec<Acc1D>(queue, workDiv, EsLayerPredicatesKernel{}, view, n, layer, q, dq, out);
  }

  void launchConfigRead(Queue& queue, mkfitdev::ESView view, unsigned char* out) {
    auto workDiv = make_workdiv<Acc1D>(nBlocks(sizeof(mkfitdev::ESConfig)), kThreads);
    alpaka::exec<Acc1D>(queue, workDiv, EsConfigReadKernel{}, view, out);
  }

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE::mkfitdev_estest
