#ifndef RecoTracker_MkFitAlpaka_interface_seeds_alpaka_LstSeedFitRunner_h
#define RecoTracker_MkFitAlpaka_interface_seeds_alpaka_LstSeedFitRunner_h

// HOST-ONLY (plugins; never from a .dev.cc): calls the O6-1 (b) device seed fit (LstSeedFit.h) for the build module
// and, in validation mode, writes per seed the host (CMSSW seed creator + MkFitSeedConverter) and the device state
// to a text file (one sync per dumped event) and prints the fit counters at the end of the job.
// Dump line: evt row cls nHits qHost qDev parHost[6] parDev[6] errDiagHost[6] errDiagDev[6] st nHitsDev
//   (cls 0 pixel-only/kept, 1 OT-only = T5/T4, 2 pixel+OT = pT3/pT5, 3 too few hits; params x y z 1/pT phi theta;
//    st = LstSeedFitStatus (-1 not fitted, 0 ok, 1 dphi, 2 backward, 3 invalid, 4 pixel fallback, 5 fallback failed,
//    6 OT-only dropped); nHitsDev = hit count after the fit (< nHits: truncated to the pixel hits)).

#include <atomic>
#include <cstdio>
#include <mutex>
#include <string>

#include "FWCore/MessageLogger/interface/MessageLogger.h"
#include "HeterogeneousCore/AlpakaInterface/interface/config.h"
#include "HeterogeneousCore/AlpakaInterface/interface/memory.h"
#include "RecoTracker/MkFitAlpaka/interface/seeds/SeedsHostCollection.h"
#include "RecoTracker/MkFitAlpaka/interface/seeds/alpaka/LstSeedFit.h"
#include "RecoTracker/MkFitAlpaka/interface/seeds/alpaka/SeedsDeviceCollection.h"
#include "RecoTracker/MkFitAlpaka/interface/tracks/TrackSoA.h"

namespace ALPAKA_ACCELERATOR_NAMESPACE::mkfitdev::lstseeds {

  class LstSeedFitRunner {
  public:
    LstSeedFitRunner(std::string dumpFile, int dumpEvents) : file_(std::move(dumpFile)), dumpEvents_(dumpEvents) {}
    ~LstSeedFitRunner() {
      if (fp_)
        std::fclose(fp_);
      if (nEv_ > 0)
        edm::LogPrint("MkFitAlpakaLstSeedFit")
            << "LST_SEED_FIT events " << nEv_ << " pixelOnly " << tot_[0] << " fitted " << tot_[1] << " tooFew "
            << tot_[2] << " failed " << tot_[3] << " chargeFlip " << tot_[4] << " rejDPhi " << tot_[5]
            << " rejBackward " << tot_[6] << " safeRetry " << tot_[7] << " fallback " << tot_[8] << " fallbackFailed "
            << tot_[9];
    }

    void run(Queue& queue,
             unsigned long long evt,
             ::mkfitdev::ESView const& es,
             ::mkfitdev::HitSoAConstView hits,
             uint32_t nPixel,
             SeedsDeviceCollection& seedsD,
             int n,
             LstSeedFitConfig const& cfg) {
      using namespace cms::alpakatools;
      auto cnt = make_device_buffer<LstSeedFitCounters>(queue);
      alpaka::memset(queue, cnt, 0);
      const bool dump = !file_.empty() && nDumpClaim_.fetch_add(1) < dumpEvents_;
      if (!dump) {
        fitLstSeeds(queue, es, hits, nPixel, seedsD.view(), n, cfg, cnt.data());
        return;
      }
      ::mkfitdev::SeedsHostCollection h0(queue, n), h1(queue, n);
      alpaka::memcpy(queue, h0.buffer(), seedsD.buffer());
      auto clsD = make_device_buffer<int8_t[]>(queue, n);
      alpaka::memset(queue, clsD, 0xff);
      auto stD = make_device_buffer<int8_t[]>(queue, n);
      alpaka::memset(queue, stD, 0xff);
      fitLstSeeds(queue, es, hits, nPixel, seedsD.view(), n, cfg, cnt.data(), clsD.data(), stD.data());
      alpaka::memcpy(queue, h1.buffer(), seedsD.buffer());
      auto clsH = make_host_buffer<int8_t[]>(queue, n);
      alpaka::memcpy(queue, clsH, clsD);
      auto stH = make_host_buffer<int8_t[]>(queue, n);
      alpaka::memcpy(queue, stH, stD);
      auto cntH = make_host_buffer<LstSeedFitCounters>(queue);
      alpaka::memcpy(queue, cntH, cnt);
      alpaka::wait(queue);
      std::lock_guard<std::mutex> lk(mutex_);
      if (!fp_)
        fp_ = std::fopen(file_.c_str(), "w");
      ++nEv_;
      const auto& c = *cntH.data();
      tot_[0] += c.nPixelOnly;
      tot_[1] += c.nFitted;
      tot_[2] += c.nTooFewHits;
      tot_[3] += c.nFailed;
      tot_[4] += c.nChargeFlip;
      tot_[5] += c.nRejDPhi;
      tot_[6] += c.nRejBackward;
      tot_[7] += c.nSafeRetry;
      tot_[8] += c.nFallback;
      tot_[9] += c.nFallbackFailed;
      if (!fp_)
        return;
      const auto v0 = h0.const_view();
      const auto v1 = h1.const_view();
      for (int s = 0; s < n; ++s) {
        std::fprintf(fp_,
                     "%llu %d %d %d %d %d",
                     evt,
                     s,
                     int(clsH[s]),
                     int(v0[s].nHits()),
                     int(v0[s].charge()),
                     int(v1[s].charge()));
        for (int k = 0; k < 6; ++k)
          std::fprintf(fp_, " %.7g", v0[s].params().v[k]);
        for (int k = 0; k < 6; ++k)
          std::fprintf(fp_, " %.7g", v1[s].params().v[k]);
        for (int k = 0; k < 6; ++k)
          std::fprintf(fp_, " %.7g", v0[s].errors().v[::mkfitdev::symIdx6(k, k)]);
        for (int k = 0; k < 6; ++k)
          std::fprintf(fp_, " %.7g", v1[s].errors().v[::mkfitdev::symIdx6(k, k)]);
        std::fprintf(fp_, " %d %d\n", int(stH[s]), int(v1[s].nHits()));
      }
    }

  private:
    const std::string file_;
    const int dumpEvents_;
    std::atomic<int> nDumpClaim_{0};
    std::mutex mutex_;
    std::FILE* fp_ = nullptr;
    long long nEv_ = 0;
    long long tot_[10] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  };

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE::mkfitdev::lstseeds

#endif
