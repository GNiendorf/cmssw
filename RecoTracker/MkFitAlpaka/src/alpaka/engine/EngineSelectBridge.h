#ifndef RecoTracker_MkFitAlpaka_src_alpaka_engine_EngineSelectBridge_h
#define RecoTracker_MkFitAlpaka_src_alpaka_engine_EngineSelectBridge_h

// Bridge between the engine (K1 / K3) and lane select's K2 (src/alpaka/select): entry points defined in
// src/alpaka/Cands.dev.cc. A src/ header because select's SoAs are src/ headers (the public CandsEngine.h must not
// include them). Producers of this package include it directly.
//   engineBuildSelectList  K1 -> K2: dense list of the listed candidates (stock seed_cand_idx order: seed-major, ic
//                          ascending): an exclusive prefix scan of seeds.nActive in one block + a fill kernel.
//   engineScatterSelect    K2 -> K3: select's dense outputs into the engine's per-(seed, ic) CandSelHitsSoA rows
//                          (RAW WSR = SelHits.wsrRaw: the engine applies find_tracks_handle_missed_layers itself).
//   EngineSelectK2         an EngineSelectFn: list -> select::runSelectHits writing the engine rows directly
//                          (round 4; engineScatterSelect stays for the tests).

#include <functional>

#include "HeterogeneousCore/AlpakaInterface/interface/config.h"
#include "RecoTracker/MkFitAlpaka/interface/cands/alpaka/CandsEngine.h"
#include "RecoTracker/MkFitAlpaka/src/alpaka/select/SelectEntry.h"
#include "RecoTracker/MkFitAlpaka/src/alpaka/select/SelectSoA.h"

namespace ALPAKA_ACCELERATOR_NAMESPACE::mkfitdev {

  void engineBuildSelectList(Queue& queue, EngineBuffers& b, ::mkfitdev::SelListSoA::View list, int nSeeds);

  void engineScatterSelect(Queue& queue,
                           EngineBuffers& b,
                           ::mkfitdev::SelListSoA::ConstView list,
                           ::mkfitdev::PropStateSoA::ConstView props,
                           ::mkfitdev::SelHitsSoA::ConstView sels,
                           int nListMax);

  // K2 of one step for engineSearch. The caller owns the list / output collections (capacity nSeeds * 5 rows) and
  // the event inputs; nSeeds = capacity in seeds, the current seed count comes from engineSearch (nSeedsNow).
  struct EngineSelectK2 {
    ::mkfitdev::SelListSoA::View list;
    ::mkfitdev::PropStateSoA::View props;
    ::mkfitdev::SelHitsSoA::View sels;
    ::mkfitdev::ESView es;
    ::mkfitdev::LayerSoA::ConstView eohLayers;
    ::mkfitdev::BinnedHitSoA::ConstView eohBinned;
    ::mkfitdev::BinSoA::ConstView eohBins;
    ::mkfitdev::HitSoA::ConstView hits;
    int nSeeds;
    uint32_t* selTies = nullptr;  // validation: K2 tie counters (select::runSelectHits ties), GPU group scan only
    bool groupScan = true;        // GPU: K2 hit scan with kSelLanes lanes per candidate (round 5); false = A/B

    void operator()(Queue& queue, EngineBuffers& b, int /*t*/, int nSeedsNow) const {
      engineBuildSelectList(queue, b, list, nSeedsNow);
      select::runSelectHits(queue,
                            b.slots.const_view(),
                            list,
                            nSeedsNow * ::mkfitdev::kMaxCandsPerSeed,
                            es,
                            eohLayers,
                            eohBinned,
                            eohBins,
                            hits,
                            props,
                            sels,
                            nullptr,
                            b.sel.view().metadata().addressOf_prop(),
                            b.sel.view().metadata().addressOf_sel(),  // K2 writes the engine rows (no scatter)
                            selTies,
                            groupScan);
    }
  };

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE::mkfitdev

#endif
