#ifndef RecoTracker_MkFitAlpaka_src_alpaka_cands_CandSelectKernel_h
#define RecoTracker_MkFitAlpaka_src_alpaka_cands_CandSelectKernel_h

// Kernel of the per-seed candidate selection (one thread per seed) and its inline launcher.
// Used by the round-1 selection test only; the production K4 is KernelEngineSelect (src/alpaka/engine/EngineKernels.h).

#include <alpaka/alpaka.hpp>

#include "HeterogeneousCore/AlpakaInterface/interface/config.h"
#include "HeterogeneousCore/AlpakaInterface/interface/workdivision.h"
#include "RecoTracker/MkFitAlpaka/interface/cands/CandSelection.h"
#include "RecoTracker/MkFitAlpaka/interface/cands/CandsSoA.h"

namespace ALPAKA_ACCELERATOR_NAMESPACE::mkfitdev {

  using namespace ::mkfitdev;

  class KernelSelectCandidates {
  public:
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  SeedCandsSoA::View seeds,
                                  CandSlotsSoA::View slots,
                                  CandHotsSoA::View hots,
                                  CandOptionsSoA::ConstView opts,
                                  CandExtrasSoA::ConstView extras,
                                  CandUpdatesSoA::View upds,
                                  SeedSelParams params,
                                  int nSeeds) const {
      const int hps = seeds.hotsPerSeed();
      for (int32_t s : cms::alpakatools::uniform_elements(acc, nSeeds)) {
        const int cur = seeds.curBuf(s);
        const int nxt = 1 - cur;
        const int nIn = seeds.nCands(s);

        float pt[kMaxCandsPerSeed];
        for (int ic = 0; ic < nIn; ++ic) {
          const float v = 1.f / slots.state(candSlotRow(s, cur, ic)).par[3];  // TrackState::pT()
          pt[ic] = v < 0.f ? -v : v;
        }

        int32_t outSrc[kMaxCandsPerSeed];
        int32_t nOut = 0, nUpd = 0, nOvl = 0, bsSrc = -1;
        int32_t nHots = seeds.nHots(s);
        uint32_t ovf = seeds.overflowBits(s);
        const uint32_t ovf0 = ovf;
        int8_t bsValid = seeds.bestShortValid(s);

        SeedSelIO io;
        io.candsIn = &slots.book(candSlotRow(s, cur, 0));
        io.candsInPt = pt;
        io.nCandsIn = nIn;
        io.extras = &extras.extra(extraRow(s, 0));
        io.nExtras = seeds.nExtras(s);
        io.opts = &opts.opt(optRow(s, 0));
        io.nOptSlots = kMaxOptsPerSeed;
        io.state = seeds.state(s);
        io.bestShort = &seeds.bestShort(s);
        io.bestShortValid = &bsValid;
        io.bestShortSrc = &bsSrc;
        io.hots = &hots.node(hotRow(s, 0, hps));
        io.hotOffset = 0;
        io.hotCap = hps;
        io.nHots = &nHots;
        io.candsOut = &slots.book(candSlotRow(s, nxt, 0));
        io.candsOutSrc = outSrc;
        io.nCandsOut = &nOut;
        io.upd = &upds.upd(updRow(s, 0));
        io.nUpd = &nUpd;
        io.ovl = &upds.ovl(updRow(s, 0));
        io.nOvl = &nOvl;
        io.overflowBits = &ovf;

        SeedSelParams p = params;
        p.layer = seeds.layer(s);
        const bool changed = selectSeedCandidates(p, io);

        if (bsSrc >= 0)
          slots.state(bestShortRow(s)) = slots.state(candSlotRow(s, cur, bsSrc));
        if (changed) {
          for (int k = 0; k < nOut; ++k)
            slots.state(candSlotRow(s, nxt, k)) = slots.state(candSlotRow(s, cur, outSrc[k]));
          seeds.nCands(s) = nOut;
          seeds.curBuf(s) = nxt;
        }
        seeds.bestShortValid(s) = bsValid;
        seeds.nHots(s) = nHots;
        seeds.nExtras(s) = 0;
        seeds.nUpdates(s) = nUpd;
        seeds.nOverlapUpdates(s) = nOvl;
        seeds.overflowBits(s) = ovf;
        if ((ovf & kOverflowHotsBit) && !(ovf0 & kOverflowHotsBit))
          alpaka::atomicAdd(acc, &seeds.nOverflowHots(), 1u, alpaka::hierarchy::Blocks{});
      }
    }
  };

  inline void selectCandidatesImpl(Queue& queue,
                        SeedCandsSoA::View seeds,
                        CandSlotsSoA::View slots,
                        CandHotsSoA::View hots,
                        CandOptionsSoA::ConstView opts,
                        CandExtrasSoA::ConstView extras,
                        CandUpdatesSoA::View upds,
                        SeedSelParams const& params,
                        int nSeeds) {
    if (nSeeds <= 0)
      return;
    constexpr uint32_t kThreads = 128;
    const uint32_t blocks = cms::alpakatools::divide_up_by(nSeeds, kThreads);
    const auto workDiv = cms::alpakatools::make_workdiv<Acc1D>(blocks, kThreads);
    alpaka::exec<Acc1D>(
        queue, workDiv, KernelSelectCandidates{}, seeds, slots, hots, opts, extras, upds, params, nSeeds);
  }

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE::mkfitdev

#endif
