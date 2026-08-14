#include "HeterogeneousCore/AlpakaInterface/interface/AllocatorConfig.h"
#include "HeterogeneousCore/AlpakaInterface/interface/memory.h"
#include "HeterogeneousCore/AlpakaInterface/interface/workdivision.h"
#include "HeterogeneousCore/AlpakaInterface/interface/CopyToDevice.h"

#include "LSTEvent.h"

#include "ChainArbitrate.h"
#include "ChainAttach.h"
#include "ChainAttachT3.h"
#include "ChainCrossClean.h"
#include "ChainEdges.h"
#include "ChainGate.h"
#include "ChainGraph.h"
#include "ChainTrimLearn.h"
#include "ChainWeld.h"
#include "Hit.h"
#include "Kernels.h"
#include "MiniDoublet.h"
#include "Segment.h"
#include "TrackCandidate.h"
#include "Triplet.h"

#include <atomic>
#include <bit>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

using Device = ALPAKA_ACCELERATOR_NAMESPACE::Device;
using Queue = ALPAKA_ACCELERATOR_NAMESPACE::Queue;
using Acc1D = ALPAKA_ACCELERATOR_NAMESPACE::Acc1D;
using Acc3D = ALPAKA_ACCELERATOR_NAMESPACE::Acc3D;

using namespace ALPAKA_ACCELERATOR_NAMESPACE::lst;

namespace {
  // Optional per-kernel timing of the chain-tracking stages, enabled by LST_CHAIN_TIMING.
  // It inserts a queue drain around each kernel, so it perturbs an asynchronous backend and is
  // meant for stage attribution only, never for a headline number.
  bool chainTimingEnabled() {
    static bool const enabled = (std::getenv("LST_CHAIN_TIMING") != nullptr);
    return enabled;
  }

  // U3 attribution instrument for the single-block scan family, LST_CHAIN_TIMING only. It drains
  // the queue on BOTH sides of the launch so that the printed number is that launch and nothing
  // else; it is therefore attribution only and never a headline number, and with the flag unset it
  // compiles to exactly the launch it replaces. The label is the source line, which is a stable
  // identity across two binaries as long as this file itself is not edited between them.
  template <typename TQueue, typename TWorkDiv, typename TKernel, typename... TArgs>
  void chainScanTimed(
      bool timing, int line, TQueue& queue, TWorkDiv const& workDiv, TKernel const& kernel, TArgs&&... args) {
    if (!timing) {
      alpaka::exec<Acc1D>(queue, workDiv, kernel, std::forward<TArgs>(args)...);
      return;
    }
    alpaka::wait(queue);
    auto const t0 = std::chrono::steady_clock::now();
    alpaka::exec<Acc1D>(queue, workDiv, kernel, std::forward<TArgs>(args)...);
    alpaka::wait(queue);
    double const ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    lstWarning(std::format("[CHAIN SCAN] L{} {:.4f} ms", line, ms));
  }

  // P1 RE-BASELINE INSTRUMENT, default OFF (LST_DUP_SNAPSHOTS; the standalone driver sets it
  // for --allobj). BOOKKEEPING ONLY: it copies isDup columns to the host at the points where a
  // later kernel overwrites them, so the ntuple can record states the final collection has
  // lost. No kernel is added, removed or reordered and nothing reads the copies back.
  bool dupSnapshotsEnabled() {
    static bool const enabled = (std::getenv("LST_DUP_SNAPSHOTS") != nullptr);
    return enabled;
  }

  // Device slice count of the K8 attach scorer (ChainAttachScore). The shipped value is the
  // constant; LST_CHAIN_ATTACH_SLICES is a tuning override that exists so one measurement slot can
  // sweep it. It changes NOTHING about the result -- every slicing visits every candidate exactly
  // once and reduces through the same packed argmax.
  uint32_t chainAttachScoreSlices() {
    static uint32_t const n = []() {
      char const* s = std::getenv("LST_CHAIN_ATTACH_SLICES");
      if (s == nullptr)
        return kAttachScoreSlices;
      int const v = std::atoi(s);
      return (v > 0) ? static_cast<uint32_t>(v) : kAttachScoreSlices;
    }();
    return n;
  }

  // How many DEVICE threads share one pLS in the K8a grid count / scatter passes. The natural
  // element of both is a (pLS, r bin) PAIR -- see the launch-shape note on ChainAttachGridCount --
  // so kAttachRBins gives each pair its own thread and 1 restores the serial-over-r form the host
  // backends use. LST_U4_GRIDR is the measurement override; it changes no result.
  uint32_t chainAttachGridRSlices() {
    static uint32_t const n = []() {
      char const* s = std::getenv("LST_U4_GRIDR");
      if (s == nullptr)
        return static_cast<uint32_t>(kAttachRBins);
      int const v = std::atoi(s);
      return (v > 0) ? static_cast<uint32_t>(v) : 1u;
    }();
    return n;
  }

  // Prints the per-r-bin radial hull each attach stage measures from its own target set, so the
  // two stages' hulls can be compared before anything is merged. Off unless LST_U4_HULL is set.
  bool chainAttachHullDump() {
    static bool const enabled = (std::getenv("LST_U4_HULL") != nullptr);
    return enabled;
  }

  // Tile count of the multi-block K1b scan (ChainPrefixIncidenceTiled). The shipped value is the
  // constant; LST_CHAIN_SCAN_TILES is a tuning override that exists so that ONE measurement slot can
  // sweep it, including nTiles = 1 (one block, but with coalesced loads and no serial tail -- the
  // direct test of whether one block is enough). It changes NOTHING about the result: every tiling
  // partitions the same keys in the same order and sums the same addends.
  uint32_t chainScanTiles() {
    static uint32_t const n = []() {
      char const* s = std::getenv("LST_CHAIN_SCAN_TILES");
      if (s == nullptr)
        return kChainScanTilesDefault;
      int const v = std::atoi(s);
      return (v > 0 && static_cast<uint32_t>(v) <= kChainScanTilesMax) ? static_cast<uint32_t>(v)
                                                                      : kChainScanTilesDefault;
    }();
    return n;
  }

  // ------------------------------------------------------------------------------------------
  // JET ROUND (M1). The per-shared-key degree cap, and the allocation guard.
  // ------------------------------------------------------------------------------------------

  // Override of ChainConfig::degreeCap, so ONE binary can supply both arms of the A/B. 0 or a
  // negative value means OFF (kChainDegreeCapOff), which is today's behaviour bit for bit.
  // The C++ default lives in ChainConfig.h; this only ever overrides it.
  uint32_t chainDegreeCap(uint32_t configured) {
    static long const env = []() {
      char const* s = std::getenv("LST_CHAIN_DEG_CAP");
      return (s == nullptr || *s == '\0') ? -1L : std::atol(s);
    }();
    if (env < 0)
      return configured;
    return (env <= 0) ? kChainDegreeCapOff : static_cast<uint32_t>(env);
  }

  // ------------------------------------------------------------------------------------------
  // JET ROUND 3 (KEY). The two literals of the K9 order key, as env overrides, so ONE binary
  // supplies every arm of the A/B and no comparison can be a build artefact.
  //
  //   orderKey = score - orderAlpha * max(0, orderHinge - marginX)      (ChainArbitrate.h:141)
  //
  // marginX is the retrained 3-class gate's own margin, and it is now the best single core-purity
  // column the chain carries: on the population the claim actually arbitrates (candidates sharing
  // >= 3 claim hits with a core-true candidate, 200 jet tune events) it reaches AUC .9639 for
  // core-true-vs-fake, against .717 when orderAlpha = 10 was chosen, so the frozen weight
  // under-uses it. Both overrides are ABSENT-MEANS-CONFIGURED: an unset environment reproduces
  // the shipped key bit for bit. A negative value also means "use the configured literal".
  float chainOrderAlpha(float configured) {
    static double const env = []() {
      char const* s = std::getenv("LST_CHAIN_ORDER_ALPHA");
      return (s == nullptr || *s == '\0') ? -1.0 : std::atof(s);
    }();
    return (env < 0.0) ? configured : static_cast<float>(env);
  }

  float chainOrderHinge(float configured) {
    static double const env = []() {
      char const* s = std::getenv("LST_CHAIN_ORDER_HINGE");
      return (s == nullptr || *s == '\0') ? -1.0 : std::atof(s);
    }();
    return (env < 0.0) ? configured : static_cast<float>(env);
  }

  // The eta-conditioned central weight and its ramp (ChainConfig::orderAlphaCentral and friends).
  float chainOrderEnvF(char const* name, float configured) {
    char const* s = std::getenv(name);
    if (s == nullptr || *s == '\0')
      return configured;
    double const v = std::atof(s);
    return (v < 0.0) ? configured : static_cast<float>(v);
  }

  // An overflowing event is SKIPPED (loudly) rather than fatal, because one unallocatable event in
  // a thousand must not take the other 999 with it -- 9.0% of jet events are over the GPU ceiling
  // and the process currently dies on the first of them. This makes the failure a hard error again
  // for anyone who wants that instead.
  bool chainOverflowThrows() {
    static bool const enabled = (std::getenv("LST_CHAIN_OVERFLOW_THROW") != nullptr);
    return enabled;
  }

  // Every event-sized chain buffer is a PortableCollection, i.e. ONE
  // `make_device_buffer<std::byte[]>(queue, Layout::computeDataSize(rows))`, and there are three
  // ceilings on that single call. All three are read from the platform headers rather than
  // hardcoded, so a platform change moves the guard with it.
  //
  //  1. `Layout::computeDataSize` takes `cms::soa::size_type` = int32_t.
  //  2. THE DANGEROUS ONE. A buffer extent is `alpaka_common::Idx` = uint32_t
  //     (HeterogeneousCore/AlpakaInterface/interface/config.h), so the byte count is TRUNCATED
  //     MODULO 2^32 with no diagnostic: measured, a 4,647,903,488 B request became a 352,936,192 B
  //     buffer that the next kernel then wrote 4 GiB past the end of (SIGSEGV on the host backend,
  //     cudaErrorIllegalAddress on CUDA). `computeDataSize` itself returns std::size_t and is fine;
  //     the loss is entirely in the conversion to an extent.
  //  3. The CMS caching allocator refuses anything above binGrowth^maxBin = 1 GiB
  //     ("allocations larger than binGrowth^maxBin are set to fail", AllocatorConfig.h). This one
  //     THROWS, so it is not a corruption risk -- it is here so the event can be skipped with a
  //     census instead of aborting the job. It applies to DEVICE allocations only: the host backend
  //     was measured allocating 4,039 MB in this very path.
  constexpr uint64_t chainAllocatorMaxBinBytes() {
    cms::alpakatools::AllocatorConfig const cfg{};
    uint64_t bytes = 1;
    for (unsigned int i = 0; i < cfg.maxBin; ++i)
      bytes *= cfg.binGrowth;
    return bytes;
  }

  // Returns nullptr if a collection of `rows` rows can be allocated, or a string naming the ceiling
  // it breaks. `bytes` is always set to the 64-bit byte size the layout would ask for (0 if the row
  // count cannot even be expressed), so the caller can report the number that was refused.
  template <typename TLayout>
  char const* chainAllocationVeto(uint64_t rows, bool deviceIsHost, uint64_t& bytes) {
    bytes = 0;
    if (rows > static_cast<uint64_t>(std::numeric_limits<int32_t>::max()))
      return "SoA row count exceeds cms::soa::size_type (int32)";
    bytes = static_cast<uint64_t>(TLayout::computeDataSize(static_cast<int32_t>(rows)));
    if (bytes > static_cast<uint64_t>(std::numeric_limits<alpaka_common::Idx>::max()))
      return "alpaka Idx (uint32_t) buffer byte extent -- WOULD TRUNCATE SILENTLY AND CORRUPT";
    if (!deviceIsHost && bytes > chainAllocatorMaxBinBytes())
      return "CMS caching allocator max bin (binGrowth^maxBin) -- allocation would throw";
    return nullptr;
  }

  // Copies one single-byte device SoA column into a host vector. The wait is required because
  // the very next kernel in the queue is the one that overwrites the column.
  template <typename TQueue, typename TSpan>
  void snapshotByteColumn(TQueue& queue, TSpan column, unsigned int n, std::vector<char>& out) {
    using ElemT = std::remove_cv_t<typename TSpan::element_type>;
    static_assert(sizeof(ElemT) == 1, "snapshotByteColumn expects a one byte wide column");
    out.assign(n, 0);
    if (n == 0)
      return;
    auto dev = cms::alpakatools::make_device_view(queue, column, n);
    auto host = cms::alpakatools::make_host_view(reinterpret_cast<ElemT*>(out.data()), n);
    alpaka::memcpy(queue, host, dev);
    alpaka::wait(queue);
  }

}  // namespace

void LSTEvent::initSync() {
  alpaka::wait(queue_);  // other calls can be asynchronous

  //reset the arrays
  for (int i = 0; i < 6; i++) {
    n_minidoublets_by_layer_barrel_[i] = 0;
    n_segments_by_layer_barrel_[i] = 0;
    n_triplets_by_layer_barrel_[i] = 0;
    if (i < 5) {
      n_minidoublets_by_layer_endcap_[i] = 0;
      n_segments_by_layer_endcap_[i] = 0;
      n_triplets_by_layer_endcap_[i] = 0;
    }
  }
}

void LSTEvent::resetEventSync() {
  alpaka::wait(queue_);  // synchronize to reset consistently
  //reset the arrays
  for (int i = 0; i < 6; i++) {
    n_minidoublets_by_layer_barrel_[i] = 0;
    n_segments_by_layer_barrel_[i] = 0;
    n_triplets_by_layer_barrel_[i] = 0;
    if (i < 5) {
      n_minidoublets_by_layer_endcap_[i] = 0;
      n_segments_by_layer_endcap_[i] = 0;
      n_triplets_by_layer_endcap_[i] = 0;
    }
  }
  memoryAllocatedMB_ = 0;
  lstInputDC_ = nullptr;
  hitsDC_.reset();
  rangesDC_.reset();
  miniDoubletsDC_.reset();
  segmentsDC_.reset();
  pixelSegmentsDC_.reset();
  tripletsDC_.reset();
  trackCandidatesBaseDC_.reset();
  trackCandidatesExtendedDC_.reset();
  chainMdIncidenceDC_.reset();
  chainLsIncidenceDC_.reset();
  chainNodesDC_.reset();
  chainEdgesDC_.reset();
  chainsDC_.reset();
  chainItemsDC_.reset();
  chainsHC_.reset();
  nChainNodes_ = 0;
  nChainE1Edges_ = 0;
  nChainE2Edges_ = 0;
  nChainE1Edges64_ = 0;
  nChainE2Edges64_ = 0;
  nChainCount_ = 0;
  nChainWeldedNodes_ = 0;

  lstInputHC_.reset();
  hitsHC_.reset();
  rangesHC_.reset();
  miniDoubletsHC_.reset();
  segmentsHC_.reset();
  pixelSegmentsHC_.reset();
  tripletsHC_.reset();
  trackCandidatesBaseHC_.reset();
  trackCandidatesExtendedHC_.reset();
  modulesHC_.reset();
}

void LSTEvent::addInputToEvent(LSTInputDeviceCollection const* lstInputDC) {
  lstInputDC_ = lstInputDC;

  pixelSize_ = lstInputDC_->size()[1];
  pixelModuleIndex_ = pixelMapping_.pixelModuleIndex;
}

void LSTEvent::addHitToEvent() {
  if (!hitsDC_) {
    const int32_t nHits = lstInputDC_->size()[0];
    hitsDC_.emplace(queue_, nHits, nModules_);
    auto buf = hitsDC_->buffer();
    alpaka::memset(queue_, buf, 0xff);
    if (objectsStatistics_) {
      double mb = alpaka::getExtentProduct(hitsDC_->buffer()) / 1e6;
      memoryAllocatedMB_ += mb;
      lstWarning(std::format("[MEM] Hits: {} allocated ({:.1f} MB)", nHits, mb));
    }
  }

  if (!rangesDC_) {
    rangesDC_.emplace(queue_, nLowerModules_ + 1);
    auto buf = rangesDC_->buffer();
    alpaka::memset(queue_, buf, 0xff);
    if (objectsStatistics_) {
      double mb = alpaka::getExtentProduct(rangesDC_->buffer()) / 1e6;
      memoryAllocatedMB_ += mb;
      lstWarning(std::format("[MEM] Ranges: {} allocated ({:.1f} MB)", nLowerModules_ + 1, mb));
    }
  }

  auto const hit_loop_workdiv = cms::alpakatools::make_workdiv<Acc1D>(max_blocks, 256);

  alpaka::exec<Acc1D>(queue_,
                      hit_loop_workdiv,
                      HitLoopKernel{},
                      Endcap,
                      TwoS,
                      nModules_,
                      nEndCapMap_,
                      endcapGeometry_.const_view(),
                      modules_.const_view().modules(),
                      lstInputDC_->const_view().hits(),
                      hitsDC_->view().extended(),
                      hitsDC_->view().ranges());

  auto const module_ranges_workdiv = cms::alpakatools::make_workdiv<Acc1D>(max_blocks, 256);

  alpaka::exec<Acc1D>(queue_,
                      module_ranges_workdiv,
                      ModuleRangesKernel{},
                      modules_.const_view().modules(),
                      hitsDC_->view().ranges(),
                      nLowerModules_);
}

void LSTEvent::addPixelSegmentToEventStart() {
  if (pixelSize_ == n_max_pixel_segments_per_module) {
    lstWarning(
        "\
          *********************************************************\n\
          * Warning: Pixel line segments may be truncated.        *\n\
          * You need to increase n_max_pixel_segments_per_module. *\n\
          *********************************************************");
  }

  if (!pixelSegmentsDC_) {
    pixelSegmentsDC_.emplace(queue_, pixelSize_);
    if (objectsStatistics_) {
      double mb = alpaka::getExtentProduct(pixelSegmentsDC_->buffer()) / 1e6;
      memoryAllocatedMB_ += mb;
      lstWarning(std::format("[MEM] PixelSegments: {} allocated ({:.1f} MB)", pixelSize_, mb));
    }
  }
}

void LSTEvent::addPixelSegmentToEventFinalize() {
  auto const addPixelSegmentToEvent_workdiv = cms::alpakatools::make_workdiv<Acc1D>(max_blocks, 256);

  alpaka::exec<Acc1D>(queue_,
                      addPixelSegmentToEvent_workdiv,
                      AddPixelSegmentToEventKernel{},
                      modules_.const_view().modules(),
                      rangesDC_->const_view(),
                      lstInputDC_->const_view().hits(),
                      hitsDC_->view().extended(),
                      lstInputDC_->const_view().pixelSeeds(),
                      miniDoubletsDC_->view().miniDoublets(),
                      segmentsDC_->view().segments(),
                      pixelSegmentsDC_->view(),
                      pixelModuleIndex_,
                      pixelSize_);
}

void LSTEvent::createMiniDoublets() {
  if (!miniDoubletsDC_) {
    auto rangesOccupancy = rangesDC_->view();

    // Zero the occupancy array so CountMiniDoublets's atomicAdd starts from 0.
    auto miniDoubletModuleOccupancy_view =
        cms::alpakatools::make_device_view(queue_, rangesOccupancy.miniDoubletModuleOccupancy());
    alpaka::memset(queue_, miniDoubletModuleOccupancy_view, 0u);

    // Set the pixel slot to 2 * pixelSize_. pixelModuleIndex_ == nLowerModules_ by construction
    // (ModuleMethods.h sets the pixel detId's index to nLowerModules), so a single memcpy is enough.
    auto pixelMaxMDs_buf_h = cms::alpakatools::make_host_buffer<int>(queue_);
    *pixelMaxMDs_buf_h.data() = 2 * pixelSize_;
    auto dst_view_miniDoubletModuleOccupancyPix =
        cms::alpakatools::make_device_view(queue_, rangesOccupancy.miniDoubletModuleOccupancy()[pixelModuleIndex_]);
    alpaka::memcpy(queue_, dst_view_miniDoubletModuleOccupancyPix, pixelMaxMDs_buf_h);

    constexpr int threadsPerBlockY = 16;
    auto const countMiniDoublets_workDiv =
        cms::alpakatools::make_workdiv<Acc2D>({nLowerModules_ / threadsPerBlockY, 1}, {threadsPerBlockY, 32});

    alpaka::exec<Acc2D>(queue_,
                        countMiniDoublets_workDiv,
                        CountMiniDoublets{},
                        modules_.const_view().modules(),
                        lstInputDC_->const_view().hits(),
                        hitsDC_->const_view().extended(),
                        hitsDC_->const_view().ranges(),
                        rangesDC_->view(),
                        ptCut_,
                        clustSizeCut_);

    auto const createMDArrayRangesGPU_workDiv = cms::alpakatools::make_workdiv<Acc1D>(1, 1024);

    alpaka::exec<Acc1D>(queue_,
                        createMDArrayRangesGPU_workDiv,
                        CreateMDArrayRangesGPU{},
                        modules_.const_view().modules(),
                        rangesDC_->view());

    auto nTotalMDs_buf_h = cms::alpakatools::make_host_buffer<unsigned int>(queue_);
    auto nTotalMDs_buf_d = cms::alpakatools::make_device_view(queue_, rangesOccupancy.nTotalMDs());
    alpaka::memcpy(queue_, nTotalMDs_buf_h, nTotalMDs_buf_d);
    alpaka::wait(queue_);  // wait to get the data before manipulation

    *nTotalMDs_buf_h.data() += 2 * pixelSize_;
    unsigned int nTotalMDs = *nTotalMDs_buf_h.data();

    miniDoubletsDC_.emplace(queue_, nTotalMDs, nLowerModules_ + 1);
    if (objectsStatistics_) {
      double mb = alpaka::getExtentProduct(miniDoubletsDC_->buffer()) / 1e6;
      memoryAllocatedMB_ += mb;
      lstWarning(std::format("[MEM] MiniDoublets: {} allocated ({:.1f} MB)", nTotalMDs, mb));
    }

    auto mdsOccupancy = miniDoubletsDC_->view().miniDoubletsOccupancy();
    auto nMDs_view = cms::alpakatools::make_device_view(queue_, mdsOccupancy.nMDs());
    auto totOccupancyMDs_view = cms::alpakatools::make_device_view(queue_, mdsOccupancy.totOccupancyMDs());
    alpaka::memset(queue_, nMDs_view, 0u);
    alpaka::memset(queue_, totOccupancyMDs_view, 0u);
  }

  auto mdView = miniDoubletsDC_->view().miniDoublets();
  auto connView = cms::alpakatools::make_device_view(queue_, mdView.connectedMax());
  alpaka::memset(queue_, connView, 0u);

  unsigned int mdSize = pixelSize_ * 2;
  auto src_view_mdSize = cms::alpakatools::make_host_view(mdSize);

  auto mdsOccupancy = miniDoubletsDC_->view().miniDoubletsOccupancy();
  auto dst_view_nMDs = cms::alpakatools::make_device_view(queue_, mdsOccupancy.nMDs()[pixelModuleIndex_]);
  alpaka::memcpy(queue_, dst_view_nMDs, src_view_mdSize);

  auto dst_view_totOccupancyMDs =
      cms::alpakatools::make_device_view(queue_, mdsOccupancy.totOccupancyMDs()[pixelModuleIndex_]);
  alpaka::memcpy(queue_, dst_view_totOccupancyMDs, src_view_mdSize);

  alpaka::wait(queue_);  // FIXME: remove synch after inputs refactored to be in pinned memory

  constexpr int threadsPerBlockY = 16;
  auto const createMiniDoublets_workDiv =
      cms::alpakatools::make_workdiv<Acc2D>({nLowerModules_ / threadsPerBlockY, 1}, {threadsPerBlockY, 32});

  alpaka::exec<Acc2D>(queue_,
                      createMiniDoublets_workDiv,
                      CreateMiniDoublets{},
                      modules_.const_view().modules(),
                      lstInputDC_->const_view().hits(),
                      hitsDC_->const_view().extended(),
                      hitsDC_->const_view().ranges(),
                      miniDoubletsDC_->view().miniDoublets(),
                      miniDoubletsDC_->view().miniDoubletsOccupancy(),
                      rangesDC_->const_view(),
                      ptCut_,
                      clustSizeCut_);

  auto const addMiniDoubletRangesToEventExplicit_workDiv = cms::alpakatools::make_workdiv<Acc1D>(1, 1024);

  alpaka::exec<Acc1D>(queue_,
                      addMiniDoubletRangesToEventExplicit_workDiv,
                      AddMiniDoubletRangesToEventExplicit{},
                      modules_.const_view().modules(),
                      miniDoubletsDC_->view().miniDoubletsOccupancy(),
                      rangesDC_->view(),
                      hitsDC_->const_view().ranges());

  if (objectsStatistics_) {
    addMiniDoubletsToEventExplicit();
  }
}

void LSTEvent::createSegmentsWithModuleMap() {
  if (!segmentsDC_) {
    auto const countMDConn_wd = cms::alpakatools::make_workdiv<Acc3D>({nLowerModules_, 1, 1}, {1, 8, 32});

    auto execCountMDConn = [&](auto kernel) {
      alpaka::exec<Acc3D>(queue_,
                          countMDConn_wd,
                          kernel,
                          modules_.const_view().modules(),
                          miniDoubletsDC_->view().miniDoublets(),
                          miniDoubletsDC_->const_view().miniDoubletsOccupancy(),
                          rangesDC_->const_view(),
                          ptCut_);
    };
    if (reduceMemByFullPrecompute_)
      execCountMDConn(CountMiniDoubletConnectionsReduceMem{});
    else
      execCountMDConn(CountMiniDoubletConnections{});

    auto const createSegmentArrayRanges_workDiv = cms::alpakatools::make_workdiv<Acc1D>(1, 1024);

    alpaka::exec<Acc1D>(queue_,
                        createSegmentArrayRanges_workDiv,
                        CreateSegmentArrayRanges{},
                        modules_.const_view().modules(),
                        rangesDC_->view(),
                        miniDoubletsDC_->const_view().miniDoublets(),
                        miniDoubletsDC_->const_view().miniDoubletsOccupancy());

    auto rangesOccupancy = rangesDC_->view();
    auto nTotalSegments_view_h = cms::alpakatools::make_host_view(nTotalSegments_);
    auto nTotalSegments_view_d = cms::alpakatools::make_device_view(queue_, rangesOccupancy.nTotalSegs());
    alpaka::memcpy(queue_, nTotalSegments_view_h, nTotalSegments_view_d);
    alpaka::wait(queue_);  // wait to get the value before manipulation

    nTotalSegments_ += pixelSize_;

    segmentsDC_.emplace(queue_, nTotalSegments_, nLowerModules_ + 1);
    if (objectsStatistics_) {
      double mb = alpaka::getExtentProduct(segmentsDC_->buffer()) / 1e6;
      memoryAllocatedMB_ += mb;
      lstWarning(std::format("[MEM] Segments: {} allocated ({:.1f} MB)", nTotalSegments_, mb));
    }

    auto segmentsOccupancy = segmentsDC_->view().segmentsOccupancy();
    auto segments = segmentsDC_->view().segments();
    auto nSegments_view = cms::alpakatools::make_device_view(queue_, segmentsOccupancy.nSegments());
    auto totOccupancySegments_view =
        cms::alpakatools::make_device_view(queue_, segmentsOccupancy.totOccupancySegments());
    alpaka::memset(queue_, nSegments_view, 0u);
    alpaka::memset(queue_, totOccupancySegments_view, 0u);
    auto conn_view = cms::alpakatools::make_device_view(queue_, segments.connectedMax());
    alpaka::memset(queue_, conn_view, 0u);

    auto src_view_size = cms::alpakatools::make_host_view(pixelSize_);

    auto dst_view_segments =
        cms::alpakatools::make_device_view(queue_, segmentsOccupancy.nSegments()[pixelModuleIndex_]);
    alpaka::memcpy(queue_, dst_view_segments, src_view_size);

    auto dst_view_totOccupancySegments =
        cms::alpakatools::make_device_view(queue_, segmentsOccupancy.totOccupancySegments()[pixelModuleIndex_]);
    alpaka::memcpy(queue_, dst_view_totOccupancySegments, src_view_size);
    alpaka::wait(queue_);
  }

  auto const createSegments_workDiv = cms::alpakatools::make_workdiv<Acc3D>({nLowerModules_, 1, 1}, {1, 8, 32});

  alpaka::exec<Acc3D>(queue_,
                      createSegments_workDiv,
                      CreateSegments{},
                      modules_.const_view().modules(),
                      miniDoubletsDC_->const_view().miniDoublets(),
                      miniDoubletsDC_->const_view().miniDoubletsOccupancy(),
                      segmentsDC_->view().segments(),
                      segmentsDC_->view().segmentsOccupancy(),
                      rangesDC_->const_view(),
                      ptCut_);

  auto const addSegmentRangesToEventExplicit_workDiv = cms::alpakatools::make_workdiv<Acc1D>(1, 1024);

  alpaka::exec<Acc1D>(queue_,
                      addSegmentRangesToEventExplicit_workDiv,
                      AddSegmentRangesToEventExplicit{},
                      modules_.const_view().modules(),
                      segmentsDC_->view().segmentsOccupancy(),
                      rangesDC_->view());

  if (objectsStatistics_) {
    addSegmentsToEventExplicit();
  }
}

void LSTEvent::createTriplets() {
  // P2.6c. Per-module raw->dense key bias for the two chain incidence instances (filled by
  // ChainPrefixKeyModules in the allocation block below, consumed by the K1a tallies in the triplet
  // builder and by K1c in buildChainIncidence). Declared at function scope because those consumers
  // straddle the allocation block, matching the lifetime the incidence collections already have.
  unsigned int const chainKeyBiasSize = nLowerModules_;
  auto chainMdKeyBias_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, chainKeyBiasSize);
  auto chainLsKeyBias_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, chainKeyBiasSize);

  if (!tripletsDC_) {
    auto const countSegConn_wd = cms::alpakatools::make_workdiv<Acc3D>({nLowerModules_, 1, 1}, {1, 16, 16});

    auto execCountSegConn = [&](auto kernel) {
      alpaka::exec<Acc3D>(queue_,
                          countSegConn_wd,
                          kernel,
                          modules_.const_view().modules(),
                          miniDoubletsDC_->const_view().miniDoublets(),
                          segmentsDC_->view().segments(),
                          segmentsDC_->const_view().segmentsOccupancy(),
                          rangesDC_->const_view(),
                          ptCut_);
    };
    if (reduceMemByFullPrecompute_)
      execCountSegConn(CountSegmentConnectionsReduceMem{});
    else
      execCountSegConn(CountSegmentConnections{});

    auto const createTripletArrayRanges_workDiv = cms::alpakatools::make_workdiv<Acc1D>(1, 1024);

    alpaka::exec<Acc1D>(queue_,
                        createTripletArrayRanges_workDiv,
                        CreateTripletArrayRanges{},
                        modules_.const_view().modules(),
                        rangesDC_->view(),
                        segmentsDC_->const_view().segments(),
                        segmentsDC_->const_view().segmentsOccupancy());

    // TODO: Why are we pulling this back down only to put it back on the device in a new struct?
    auto rangesOccupancy = rangesDC_->view();
    auto maxTriplets_buf_h = cms::alpakatools::make_host_buffer<unsigned int>(queue_);
    auto maxTriplets_buf_d = cms::alpakatools::make_device_view(queue_, rangesOccupancy.nTotalTrips());
    alpaka::memcpy(queue_, maxTriplets_buf_h, maxTriplets_buf_d);

    // P2.6c. Build the raw->dense key bias here so that its two totals, which size the incidence
    // collections, ride down on the host sync the triplet count already pays for. No extra wait.
    auto chainKeyTotals_buf_h = cms::alpakatools::make_host_buffer<uint32_t[]>(queue_, 2u);
    auto chainKeyTotals_buf_d = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, 2u);
    {
      chainScanTimed(chainTimingEnabled(), __LINE__, queue_,
                          cms::alpakatools::make_workdiv<Acc1D>(1, kChainScanBlockThreads),
                          ChainPrefixKeyModules{},
                          modules_.const_view().modules(),
                          miniDoubletsDC_->const_view().miniDoubletsOccupancy(),
                          segmentsDC_->const_view().segmentsOccupancy(),
                          rangesDC_->const_view(),
                          chainMdKeyBias_buf.data(),
                          chainLsKeyBias_buf.data(),
                          chainKeyTotals_buf_d.data());
      alpaka::memcpy(queue_, chainKeyTotals_buf_h, chainKeyTotals_buf_d, 2u);
    }

    alpaka::wait(queue_);  // wait to get the value before using it

    unsigned int nTotalTriplets = *maxTriplets_buf_h.data();
    tripletsDC_.emplace(queue_, nTotalTriplets, nLowerModules_);
    if (objectsStatistics_) {
      double mb = alpaka::getExtentProduct(tripletsDC_->buffer()) / 1e6;
      memoryAllocatedMB_ += mb;
      lstWarning(std::format("[MEM] Triplets: {} allocated ({:.1f} MB)", nTotalTriplets, mb));
    }

    auto tripletsOccupancy = tripletsDC_->view().tripletsOccupancy();
    auto nTriplets_view = cms::alpakatools::make_device_view(queue_, tripletsOccupancy.nTriplets());
    alpaka::memset(queue_, nTriplets_view, 0u);
    auto totOccupancyTriplets_view =
        cms::alpakatools::make_device_view(queue_, tripletsOccupancy.totOccupancyTriplets());
    alpaka::memset(queue_, totOccupancyTriplets_view, 0u);

    {
      // Chain-tracking K1a target arrays, keyed by the DENSE MiniDoublet / Segment index that
      // ChainPrefixKeyModules just defined. Keying them by the RAW module-segmented index instead
      // forced them to span the full allocated extent of those two collections, which carries about
      // 2.0x (MD) and 4.7x (Segment) slack over the produced-object count and made this the single
      // largest chain allocation. Allocated and zeroed before the triplet builder runs, since the
      // builder tallies straight into them.
      unsigned int const nMDKeys = chainKeyTotals_buf_h.data()[0];
      unsigned int const nLSKeys = chainKeyTotals_buf_h.data()[1];
      chainMdIncidenceDC_.emplace(queue_, nMDKeys + 1);
      chainLsIncidenceDC_.emplace(queue_, nLSKeys + 1);
      // Only the tallies need clearing: K1b writes every entry of the offset and prefix columns
      // (including the terminating one) and K1c writes every entry of the item columns.
      resetChainIncidenceCounts();
      if (objectsStatistics_) {
        double mb = (alpaka::getExtentProduct(chainMdIncidenceDC_->buffer()) +
                     alpaka::getExtentProduct(chainLsIncidenceDC_->buffer())) /
                    1e6;
        memoryAllocatedMB_ += mb;
        lstWarning(std::format(
            "[MEM] ChainIncidence: {} dense MD keys + {} dense LS keys allocated ({:.1f} MB)", nMDKeys, nLSKeys, mb));
      }
    }
  }

  uint16_t nonZeroModules = 0;
  unsigned int max_InnerSeg = 0;

  // Allocate and copy nSegments from device to host (only nLowerModules in OT, not the +1 with pLSs)
  auto nSegments_buf_h = cms::alpakatools::make_host_buffer<unsigned int[]>(queue_, nLowerModules_);
  auto nSegments_buf_d = cms::alpakatools::make_device_view(
      queue_, segmentsDC_->const_view().segmentsOccupancy().nSegments(), nLowerModules_);
  alpaka::memcpy(queue_, nSegments_buf_h, nSegments_buf_d, nLowerModules_);

  // ... same for module_nConnectedModules
  // FIXME: replace by ES host data
  auto modules = modules_.const_view().modules();
  auto module_nConnectedModules_buf_h = cms::alpakatools::make_host_buffer<uint16_t[]>(queue_, nLowerModules_);
  auto module_nConnectedModules_buf_d =
      cms::alpakatools::make_device_view(queue_, modules.nConnectedModules(), nLowerModules_);  // only lower modules
  alpaka::memcpy(queue_, module_nConnectedModules_buf_h, module_nConnectedModules_buf_d, nLowerModules_);

  alpaka::wait(queue_);  // wait for nSegments and module_nConnectedModules before using

  auto const* nSegments = nSegments_buf_h.data();
  auto const* module_nConnectedModules = module_nConnectedModules_buf_h.data();

  // Allocate host index and fill it directly
  auto index_buf_h = cms::alpakatools::make_host_buffer<uint16_t[]>(queue_, nLowerModules_);
  auto* index = index_buf_h.data();

  for (uint16_t innerLowerModuleIndex = 0; innerLowerModuleIndex < nLowerModules_; innerLowerModuleIndex++) {
    uint16_t nConnectedModules = module_nConnectedModules[innerLowerModuleIndex];
    unsigned int nInnerSegments = nSegments[innerLowerModuleIndex];
    if (nConnectedModules != 0 and nInnerSegments != 0) {
      index[nonZeroModules] = innerLowerModuleIndex;
      nonZeroModules++;
    }
    max_InnerSeg = std::max(max_InnerSeg, nInnerSegments);
  }

  if (nonZeroModules == 0)
    return;
  // Allocate and copy to device index
  auto index_gpu_buf = cms::alpakatools::make_device_buffer<uint16_t[]>(queue_, nLowerModules_);
  alpaka::memcpy(queue_, index_gpu_buf, index_buf_h, nonZeroModules);

  auto const createTriplets_workDiv = cms::alpakatools::make_workdiv<Acc3D>({nonZeroModules, 1, 1}, {1, 16, 16});

  // Null unless chain tracking is on; the kernel drops every use of them at compile time.
  uint32_t* chainMdT3OutCounts = nullptr;
  uint32_t* chainMdT3InCounts = nullptr;
  uint32_t* chainLsT3OutCounts = nullptr;
  uint32_t* chainLsT3InCounts = nullptr;
  {
    auto mdIncidence = chainMdIncidenceDC_->view();
    auto lsIncidence = chainLsIncidenceDC_->view();
    chainMdT3OutCounts = mdIncidence.metadata().addressOf_t3OutCounts();
    chainMdT3InCounts = mdIncidence.metadata().addressOf_t3InCounts();
    chainLsT3OutCounts = lsIncidence.metadata().addressOf_t3OutCounts();
    chainLsT3InCounts = lsIncidence.metadata().addressOf_t3InCounts();
  }

  auto execCreateTriplets = [&](auto kernel) {
    alpaka::exec<Acc3D>(queue_,
                        createTriplets_workDiv,
                        kernel,
                        modules_.const_view().modules(),
                        miniDoubletsDC_->const_view().miniDoublets(),
                        segmentsDC_->const_view().segments(),
                        segmentsDC_->const_view().segmentsOccupancy(),
                        tripletsDC_->view().triplets(),
                        tripletsDC_->view().tripletsOccupancy(),
                        rangesDC_->const_view(),
                        index_gpu_buf.data(),
                        nonZeroModules,
                        ptCut_,
                        chainMdT3OutCounts,
                        chainMdT3InCounts,
                        chainLsT3OutCounts,
                        chainLsT3InCounts,
                        chainMdKeyBias_buf.data(),
                        chainLsKeyBias_buf.data());
  };
  if (reduceMemByFullPrecompute_)
    execCreateTriplets(CreateTripletsReduceMemChain{});
  else
    execCreateTriplets(CreateTripletsChain{});

  auto const addTripletRangesToEventExplicit_workDiv = cms::alpakatools::make_workdiv<Acc1D>(1, 1024);

  alpaka::exec<Acc1D>(queue_,
                      addTripletRangesToEventExplicit_workDiv,
                      AddTripletRangesToEventExplicit{},
                      modules_.const_view().modules(),
                      tripletsDC_->const_view().tripletsOccupancy(),
                      rangesDC_->view());

  {
    alpaka::wait(queue_);  // fence the T3 build so the chain-build stamp attributes correctly
    auto const chainBuild0 = std::chrono::steady_clock::now();
    buildChainIncidence(chainMdKeyBias_buf.data(), chainLsKeyBias_buf.data());
    buildChainEdges();
    alpaka::wait(queue_);
    chainBuildMs_ = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - chainBuild0).count();
  }

  if (objectsStatistics_) {
    addTripletsToEventExplicit();
  }
}

void LSTEvent::resetChainIncidenceCounts() {
  // Zero the two tally columns of both incidence instances. They serve twice: as the K1a atomic
  // counters before the triplet builder, and as the K1c per-key write cursors after K1b.
  for (auto* incidence : {&chainMdIncidenceDC_.value(), &chainLsIncidenceDC_.value()}) {
    auto view = incidence->view();
    auto outCounts_view = cms::alpakatools::make_device_view(queue_, view.t3OutCounts(), view.metadata().size());
    alpaka::memset(queue_, outCounts_view, 0u);
    auto inCounts_view = cms::alpakatools::make_device_view(queue_, view.t3InCounts(), view.metadata().size());
    alpaka::memset(queue_, inCounts_view, 0u);
  }
}

void LSTEvent::buildChainIncidence(uint32_t const* chainMdKeyBias, uint32_t const* chainLsKeyBias) {
  bool const timing = chainTimingEnabled();
  auto const tStart = std::chrono::steady_clock::now();

  // K0. Compact the module-segmented triplet store into a dense node numbering.
  auto moduleNodeOffsets_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, nLowerModules_ + 1);
  auto nNodes_buf_d = cms::alpakatools::make_device_buffer<uint32_t>(queue_);

  auto const chainScan_workDiv = cms::alpakatools::make_workdiv<Acc1D>(1, kChainScanBlockThreads);

  chainScanTimed(timing, __LINE__, queue_,
                      chainScan_workDiv,
                      ChainPrefixTripletModules{},
                      modules_.const_view().modules(),
                      tripletsDC_->const_view().tripletsOccupancy(),
                      moduleNodeOffsets_buf.data(),
                      nNodes_buf_d.data());

  auto nNodes_buf_h = cms::alpakatools::make_host_buffer<uint32_t>(queue_);
  alpaka::memcpy(queue_, nNodes_buf_h, nNodes_buf_d);
  alpaka::wait(queue_);  // the node count sizes the dense node collection exactly
  nChainNodes_ = *nNodes_buf_h.data();

  if (nChainNodes_ == 0)
    return;

  chainNodesDC_.emplace(queue_, nChainNodes_);
  if (objectsStatistics_) {
    double mb = alpaka::getExtentProduct(chainNodesDC_->buffer()) / 1e6;
    memoryAllocatedMB_ += mb;
    lstWarning(std::format("[MEM] ChainNodes: {} allocated ({:.1f} MB)", nChainNodes_, mb));
  }

  auto const chainFlat_workDiv = cms::alpakatools::make_workdiv<Acc1D>(max_blocks, 256);

  alpaka::exec<Acc1D>(queue_,
                      chainFlat_workDiv,
                      ChainScatterTripletModules{},
                      modules_.const_view().modules(),
                      tripletsDC_->const_view().tripletsOccupancy(),
                      rangesDC_->const_view(),
                      moduleNodeOffsets_buf.data(),
                      chainNodesDC_->view());

  // K1b. Exclusive prefixes plus the exact edge counts E1 (MD keyed) and E2 (Segment keyed).
  unsigned int const nMDKeys = chainMdIncidenceDC_->view().metadata().size() - 1;
  unsigned int const nLSKeys = chainLsIncidenceDC_->view().metadata().size() - 1;

  // K1b is the multi-block tiled scan: nTiles blocks x 256 threads, two passes over the same kernel
  // struct. Both incidence instances share one tileSums scratch because their four launches are
  // queue-ordered.
  //
  // THE SCRATCH IS BORROWED, NOT ALLOCATED, and that is deliberate. moduleNodeOffsets_buf is
  // nLowerModules + 1 = 13201 words and is DEAD from here on: ChainPrefixTripletModules wrote it and
  // ChainScatterTripletModules, enqueued two launches above, is its only reader -- and the queue is
  // ordered, so the scatter has finished before phase 0 starts. 3 * nTiles <= 1536 words fit inside
  // it many times over. An earlier revision of this code used a persistent 6 kB device buffer of its
  // own instead, and THAT COST +43 ms/event ON THE CPU BACKEND, all of it in the pLS stage, which
  // this code does not touch (measured, palindrome-clean, broker tag U3D1; the same signature as
  // round 1's unresolved [T3 10:20] / [T5 ~11:00] item). So: no new buffer, per FINDINGS_GPU2.md's
  // standing rule -- persistent member for a compile-time size, or borrow one that is already dead.
  uint32_t const nTiles = std::max(1u, std::min(chainScanTiles(), (nLowerModules_ + 1u) / 3u));
  auto const chainTile_workDiv = cms::alpakatools::make_workdiv<Acc1D>(nTiles, kChainScanTileThreads);
  uint32_t* const chainTileSums = moduleNodeOffsets_buf.data();
  uint32_t const degCap = chainDegreeCap(chainConfig_.degreeCap);
  chainScanTimed(timing, __LINE__, queue_, chainTile_workDiv, ChainPrefixIncidenceTiled{}, chainMdIncidenceDC_->view(), nMDKeys, chainTileSums, nTiles, 0u, degCap);
  chainScanTimed(timing, __LINE__, queue_, chainTile_workDiv, ChainPrefixIncidenceTiled{}, chainMdIncidenceDC_->view(), nMDKeys, chainTileSums, nTiles, 1u, degCap);
  chainScanTimed(timing, __LINE__, queue_, chainTile_workDiv, ChainPrefixIncidenceTiled{}, chainLsIncidenceDC_->view(), nLSKeys, chainTileSums, nTiles, 0u, degCap);
  chainScanTimed(timing, __LINE__, queue_, chainTile_workDiv, ChainPrefixIncidenceTiled{}, chainLsIncidenceDC_->view(), nLSKeys, chainTileSums, nTiles, 1u, degCap);

  // JET ROUND P0b. Is the uint32 edge count `nEdgesExact` the TRUE count, or a wrapped one? The
  // host can certify it for free: sum_key degIn * degOut <= (sum_key degIn) * max_key degOut, both
  // degrees are capped at degCap and every degree is at most the node count, so the count is bounded
  // by nT3 * min(nT3, degCap). Under that bound below 2^32 no wrap is arithmetically possible. This
  // holds for EVERY PU200 event (nT3 max ~53k) and for every capped event (256 * 601,655 = 154 M),
  // so the recount below does not run in the shipping configuration -- only on a cap-off jet event,
  // which is exactly the case whose count cannot be trusted. Two extra launches over the key range
  // and one 8 kB copy, read at the sync that is already there.
  uint64_t const countBound = static_cast<uint64_t>(nChainNodes_) * std::min<uint64_t>(nChainNodes_, degCap);
  bool const needRecount = countBound >= (1ull << 32);
  std::vector<uint32_t> recount;
  if (needRecount) {
    // The recount borrows lanes 3-6 of the same dead scratch (phases 0 and 1 have consumed lanes
    // 0-2 by now, and the queue is ordered). A geometry with fewer than 7 * 512 lower modules would
    // not have room; that is a build-time invariant of this detector (13,200), not a data condition.
    if (static_cast<uint64_t>(nLowerModules_) + 1u < 7ull * kChainScanTilesMax)
      throw std::runtime_error(
          std::format("[CHAIN] the K1b 64-bit recount needs {} scratch words but only {} are borrowed; "
                      "give ChainPrefixIncidenceTiled phase 2 a buffer of its own",
                      7u * kChainScanTilesMax,
                      nLowerModules_ + 1u));
    uint32_t* const recountMd = chainTileSums + 3u * kChainScanTilesMax;
    uint32_t* const recountLs = chainTileSums + 5u * kChainScanTilesMax;
    chainScanTimed(timing, __LINE__, queue_, chainTile_workDiv, ChainPrefixIncidenceTiled{}, chainMdIncidenceDC_->view(), nMDKeys, recountMd, nTiles, 2u, degCap);
    chainScanTimed(timing, __LINE__, queue_, chainTile_workDiv, ChainPrefixIncidenceTiled{}, chainLsIncidenceDC_->view(), nLSKeys, recountLs, nTiles, 2u, degCap);
    recount.resize(4u * kChainScanTilesMax);
    auto recount_h = cms::alpakatools::make_host_view(recount.data(), recount.size());
    auto recount_d = cms::alpakatools::make_device_view(queue_, recountMd, recount.size());
    alpaka::memcpy(queue_, recount_h, recount_d);
  }

  // The K1a tallies are now captured in the offset columns; reuse them as the K1c write cursors.
  resetChainIncidenceCounts();

  // K1c. Fill the CSR payloads.
  alpaka::exec<Acc1D>(queue_,
                      chainFlat_workDiv,
                      ChainScatterIncidence{},
                      segmentsDC_->const_view().segments(),
                      tripletsDC_->const_view().triplets(),
                      miniDoubletsDC_->const_view().miniDoublets(),
                      chainNodesDC_->view(),
                      chainMdIncidenceDC_->view(),
                      chainLsIncidenceDC_->view(),
                      chainMdKeyBias,
                      chainLsKeyBias);

  auto nE1_buf_h = cms::alpakatools::make_host_buffer<uint32_t>(queue_);
  auto nE2_buf_h = cms::alpakatools::make_host_buffer<uint32_t>(queue_);
  auto nE1_buf_d = cms::alpakatools::make_device_view(queue_, chainMdIncidenceDC_->view().nEdgesExact());
  auto nE2_buf_d = cms::alpakatools::make_device_view(queue_, chainLsIncidenceDC_->view().nEdgesExact());
  alpaka::memcpy(queue_, nE1_buf_h, nE1_buf_d);
  alpaka::memcpy(queue_, nE2_buf_h, nE2_buf_d);
  alpaka::wait(queue_);
  nChainE1Edges_ = *nE1_buf_h.data();
  nChainE2Edges_ = *nE2_buf_h.data();

  // The 64-bit counts the allocation guard reads. Equal to the two above unless the uint32
  // accumulation wrapped, which is what the recount is for; the guard vetoes on any inequality, so
  // no wrapped count can reach an emplace.
  nChainE1Edges64_ = nChainE1Edges_;
  nChainE2Edges64_ = nChainE2Edges_;
  if (!recount.empty()) {
    auto sum64 = [&](uint32_t const* base) {
      uint64_t s = 0;
      for (uint32_t t = 0; t < nTiles; ++t)
        s += (static_cast<uint64_t>(base[kChainScanTilesMax + t]) << 32) | base[t];
      return s;
    };
    nChainE1Edges64_ = sum64(&recount[0]);
    nChainE2Edges64_ = sum64(&recount[2u * kChainScanTilesMax]);
  }

  if (timing) {
    alpaka::wait(queue_);
    double const ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tStart).count();
    lstWarning(std::format("[CHAIN TIMING] K0+K1 incidence {:.3f} ms", ms));
  }

  if (objectsStatistics_) {
    chainIncidenceStatistics();
  }
}

void LSTEvent::chainIncidenceStatistics() {
  // Host-side check of every invariant the CSR must satisfy. This is the phase P2.0 sanity gate;
  // it only runs with chain tracking on and verbose statistics enabled.
  alpaka::wait(queue_);

  // Pull the columns down one by one so this works unchanged on every backend.
  auto pull = [&](auto column, unsigned int n) {
    std::vector<uint32_t> host(n);
    auto host_view = cms::alpakatools::make_host_view(host.data(), n);
    auto dev_view = cms::alpakatools::make_device_view(queue_, column, n);
    alpaka::memcpy(queue_, host_view, dev_view, n);
    alpaka::wait(queue_);
    return host;
  };

  auto nodes = chainNodesDC_->view();
  auto mdInc = chainMdIncidenceDC_->view();
  auto lsInc = chainLsIncidenceDC_->view();

  auto checkFamily = [&](char const* name, auto incidence, auto outItemsCol, auto inItemsCol) {
    unsigned int const nKeys = incidence.metadata().size() - 1;
    std::vector<uint32_t> const outOff = pull(incidence.t3OutOffsets(), nKeys + 1);
    std::vector<uint32_t> const inOff = pull(incidence.t3InOffsets(), nKeys + 1);
    std::vector<uint32_t> const prodPrefix = pull(incidence.edgeProdPrefix(), nKeys + 1);
    std::vector<uint32_t> const outItems = pull(outItemsCol, nChainNodes_);
    std::vector<uint32_t> const inItems = pull(inItemsCol, nChainNodes_);

    // `edges` is the CAPPED count, i.e. what K1b's prefix claims and what K2 will enumerate;
    // `edgesUncapped` is what the same event would have produced with the cap off, so the two
    // together are the per-event measurement of what the cap removed. With the cap off they are
    // equal by construction and this reproduces the pre-cap output exactly.
    uint32_t const degCap = chainDegreeCap(chainConfig_.degreeCap);
    unsigned long long edges = 0;
    unsigned long long edgesUncapped = 0;
    unsigned int nonEmptyKeys = 0;
    unsigned int maxDegIn = 0, maxDegOut = 0;
    bool monotonic = true;
    for (unsigned int k = 0; k < nKeys; ++k) {
      monotonic =
          monotonic && outOff[k + 1] >= outOff[k] && inOff[k + 1] >= inOff[k] && prodPrefix[k + 1] >= prodPrefix[k];
      unsigned int const degOut = outOff[k + 1] - outOff[k];
      unsigned int const degIn = inOff[k + 1] - inOff[k];
      edges += static_cast<unsigned long long>(std::min<uint32_t>(degIn, degCap)) * std::min<uint32_t>(degOut, degCap);
      edgesUncapped += static_cast<unsigned long long>(degIn) * degOut;
      nonEmptyKeys += (degIn != 0 || degOut != 0);
      maxDegIn = std::max(maxDegIn, degIn);
      maxDegOut = std::max(maxDegOut, degOut);
    }

    // Every triplet contributes exactly one entry to each of the two item arrays, so the degree
    // sums must both equal the node count and each item array must be a permutation of [0, nNodes).
    std::vector<unsigned char> seenOut(nChainNodes_, 0), seenIn(nChainNodes_, 0);
    bool outOfBounds = false, duplicated = false;
    for (unsigned int i = 0; i < nChainNodes_; ++i) {
      uint32_t const o = outItems[i];
      uint32_t const n = inItems[i];
      if (o >= nChainNodes_ || n >= nChainNodes_) {
        outOfBounds = true;
        continue;
      }
      duplicated = duplicated || seenOut[o] || seenIn[n];
      seenOut[o] = 1;
      seenIn[n] = 1;
    }
    unsigned int coveredOut = 0, coveredIn = 0;
    for (unsigned int i = 0; i < nChainNodes_; ++i) {
      coveredOut += seenOut[i];
      coveredIn += seenIn[i];
    }

    bool const degreeOk = (outOff[nKeys] == nChainNodes_) && (inOff[nKeys] == nChainNodes_);
    bool const permutationOk = !duplicated && !outOfBounds && coveredOut == nChainNodes_ && coveredIn == nChainNodes_;
    lstWarning(
        std::format("[CHAIN] {}: keys={} used={} sumDegOut={} sumDegIn={} nT3={} maxDegIn={} maxDegOut={} "
                    "E={} Euncapped={} cap={} prefixE={} degreeSum={} monotonic={} permutation={}",
                    name,
                    nKeys,
                    nonEmptyKeys,
                    outOff[nKeys],
                    inOff[nKeys],
                    nChainNodes_,
                    maxDegIn,
                    maxDegOut,
                    edges,
                    edgesUncapped,
                    degCap,
                    prodPrefix[nKeys],
                    degreeOk ? "ok" : "FAIL",
                    monotonic ? "ok" : "FAIL",
                    permutationOk ? "ok" : "FAIL"));
    // The prefix column is uint32, so the comparison is modulo 2^32 -- otherwise this line would
    // cry "mismatch" on exactly the events where the count wraps, which is a real condition the
    // allocation guard handles rather than a CSR defect.
    if ((edges & 0xffffffffull) != prodPrefix[nKeys])
      lstWarning(std::format("[CHAIN] {}: EDGE PREFIX MISMATCH", name));
    return edges;
  };

  unsigned long long const e1 = checkFamily("MD/E1", mdInc, nodes.mdT3OutItems(), nodes.mdT3InItems());
  unsigned long long const e2 = checkFamily("LS/E2", lsInc, nodes.lsT3OutItems(), nodes.lsT3InItems());
  lstWarning(std::format("[CHAIN] nodes(nT3)={} E1={} E2={} E={}", nChainNodes_, e1, e2, e1 + e2));
}

void LSTEvent::buildChainEdges() {
  // Phase P2.1. K3 fills the frozen node-feature rows, K2 enumerates the exactly-counted edge
  // list, K5 builds the edge features in registers and scores them with the edge head. Nothing
  // reads the result yet: this phase is measurement only.
  if (nChainNodes_ == 0)
    return;

  auto const chainFlat_workDiv = cms::alpakatools::make_workdiv<Acc1D>(max_blocks, 256);

  bool const timing = chainTimingEnabled();
  auto stamp = [&]() {
    if (timing)
      alpaka::wait(queue_);
    return std::chrono::steady_clock::now();
  };
  auto const t0 = stamp();

  // K3 first: K5 gathers node rows, so they must exist before it runs. Both are on the same
  // in-order queue, so no explicit synchronization is needed between the two.
  alpaka::exec<Acc1D>(queue_,
                      chainFlat_workDiv,
                      ChainNodeFeatures{},
                      modules_.const_view().modules(),
                      miniDoubletsDC_->const_view().miniDoublets(),
                      segmentsDC_->const_view().segments(),
                      tripletsDC_->const_view().triplets(),
                      chainNodesDC_->view());

  auto const t1 = stamp();

  // Exact allocation by pure degree arithmetic (K1b), so there is no capped reservation and no
  // ungated-writer hazard: every row below is written by exactly one thread at its own index.
  uint32_t const nEdges = nChainE1Edges_ + nChainE2Edges_;

  // ------------------------------------------------------------------------------------------
  // JET ROUND P0. THE ALLOCATION GUARD. Everything is decided on the 64-BIT counts and the 64-bit
  // byte size; the uint32 `nEdges` above is only used after the guard has proved it is the true
  // count and that it can be allocated. Both facts have failed in practice: on the jet sample 8
  // events of 1000 asked for more than 4 GiB and had the byte extent silently truncated modulo 2^32
  // (SIGSEGV / cudaErrorIllegalAddress inside ChainBuildEdges, ending the whole job) and 90 of 1000
  // are over the 1 GiB device allocator bin. There was no check of any kind here.
  // ------------------------------------------------------------------------------------------
  uint64_t const nEdges64 = nChainE1Edges64_ + nChainE2Edges64_;
  if (nEdges64 == 0)
    return;
  uint64_t edgeBytes = 0;
  char const* veto = chainAllocationVeto<ChainEdgesDeviceCollection::Layout>(
      nEdges64, std::is_same_v<Device, alpaka::DevCpu>, edgeBytes);
  if (veto == nullptr && nEdges64 != static_cast<uint64_t>(nEdges))
    veto = "uint32 edge count WRAPPED (the K1b 64-bit recount disagrees with nEdgesExact)";
  if (veto != nullptr) {
    // The event's chain block is skipped, not the job. This is the same state as `nChainNodes_ == 0`:
    // chainEdgesDC_ stays empty, so buildChains() returns on its `!chainEdgesDC_.has_value()` test
    // and every later chain stage on `nChainCount_ == 0`. The event still gets its non-chain track
    // candidates; it does not get chain ones, which is a real physics loss and is why this is LOUD
    // and counted rather than silent.
    static std::atomic<uint32_t> overflowCensus{0};
    uint32_t const nSoFar = overflowCensus.fetch_add(1) + 1;
    std::string const msg = std::format(
        "[CHAIN OVERFLOW] ChainEdges needs {} rows ({} B) for nT3={} (E1={} E2={}): {}. "
        "SKIPPING the chain block for this event ({} skipped in this process so far). "
        "Lower ChainConfig::degreeCap (LST_CHAIN_DEG_CAP, currently {}) to bring the edge count down.",
        nEdges64,
        edgeBytes,
        nChainNodes_,
        nChainE1Edges64_,
        nChainE2Edges64_,
        veto,
        nSoFar,
        chainDegreeCap(chainConfig_.degreeCap));
    if (chainOverflowThrows())
      throw std::runtime_error(msg);
    lstWarning(msg);
    return;
  }

  chainEdgesDC_.emplace(queue_, nEdges);
  if (objectsStatistics_) {
    double mb = alpaka::getExtentProduct(chainEdgesDC_->buffer()) / 1e6;
    memoryAllocatedMB_ += mb;
    lstWarning(std::format("[MEM] ChainEdges: {} allocated ({:.1f} MB)", nEdges, mb));
  }

  alpaka::exec<Acc1D>(queue_,
                      chainFlat_workDiv,
                      ChainBuildEdges{},
                      tripletsDC_->const_view().triplets(),
                      segmentsDC_->const_view().segments(),
                      chainNodesDC_->const_view(),
                      chainMdIncidenceDC_->const_view(),
                      chainLsIncidenceDC_->const_view(),
                      chainEdgesDC_->view(),
                      nChainE1Edges_,
                      nChainE2Edges_,
                      chainDegreeCap(chainConfig_.degreeCap));

  auto const t2 = stamp();

  // Debug tap: with LST_CHAIN_FEAT_DUMP set, K5 also stores its 14 edge floats so the parity
  // comparison can localize a mismatch to a feature instead of only seeing the logit.
  // NN-loop training dump (2026-08-10): was first-event-only (a parity tap); now emits EVERY event
  // in append mode with a per-event [magic, ievt] header so the on-policy training pipeline can use
  // it. With the env var unset the behaviour is byte-identical to the old code (wantFeat false).
  static std::atomic<uint32_t> featDumpEvent{0};
  char const* featPath = std::getenv("LST_CHAIN_FEAT_DUMP");
  bool wantFeat = (featPath != nullptr && *featPath != '\0');
  // JET ROUND P0, second site: this tap is 56 B/edge, i.e. 2.7x the edge row itself, so it hits the
  // same uint32 extent wall at 76.7 M edges -- a quarter of where ChainEdges hits it -- and it is a
  // RAW buffer, so nothing downstream would notice the truncation. The tap is a debug/training
  // instrument, so the right action here is to drop the tap for this event and say so, rather than
  // to lose the event.
  if (wantFeat) {
    uint64_t const featBytes = static_cast<uint64_t>(nEdges) * kChainEdgeFeatures * sizeof(float);
    if (featBytes > static_cast<uint64_t>(std::numeric_limits<alpaka_common::Idx>::max())) {
      lstWarning(std::format(
          "[CHAIN OVERFLOW] LST_CHAIN_FEAT_DUMP wants {} B for {} edges, over the alpaka Idx extent; "
          "the feature tap is DISABLED for this event (its edges are still built and scored)",
          featBytes,
          nEdges));
      wantFeat = false;
    }
  }
  auto featBuf = cms::alpakatools::make_device_buffer<float[]>(
      queue_, wantFeat ? static_cast<size_t>(nEdges) * kChainEdgeFeatures : size_t{1});

  // Resolve the per-edge-family weld bars on the HOST so the kernel takes two plain floats. A
  // family left at its inherit sentinel resolves to thetaEdge, so both families then compare
  // against the same number and the arithmetic is unchanged. With chainConfig_.edgeWpTable these
  // two only feed the inert fallback: K5 then takes the bar from the head's own per-cell table.
  float const weldThetaE1 = (chainConfig_.thetaEdgeE1 < 1e29f) ? chainConfig_.thetaEdgeE1 : chainConfig_.thetaEdge;
  float const weldThetaE2 = (chainConfig_.thetaEdgeE2 < 1e29f) ? chainConfig_.thetaEdgeE2 : chainConfig_.thetaEdge;

  alpaka::exec<Acc1D>(queue_,
                      chainFlat_workDiv,
                      ChainEdgeInference{},
                      modules_.const_view().modules(),
                      miniDoubletsDC_->const_view().miniDoublets(),
                      segmentsDC_->const_view().segments(),
                      tripletsDC_->const_view().triplets(),
                      chainNodesDC_->const_view(),
                      chainMdIncidenceDC_->const_view(),
                      chainLsIncidenceDC_->const_view(),
                      chainEdgesDC_->view(),
                      weldThetaE1,
                      weldThetaE2,
                      chainConfig_.edgeWpTable,
                      wantFeat ? featBuf.data() : nullptr);

  auto const t3 = stamp();
  if (timing) {
    auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
    lstWarning(
        std::format("[CHAIN TIMING] nodes={} edges={} | K3 nodeFeatures {:.3f} ms | "
                    "K2 buildEdges {:.3f} ms | K5 edgeInference {:.3f} ms | total {:.3f} ms",
                    nChainNodes_,
                    nEdges,
                    ms(t0, t1),
                    ms(t1, t2),
                    ms(t2, t3),
                    ms(t0, t3)));
  }

  if (wantFeat) {
    alpaka::wait(queue_);
    std::vector<uint32_t> inner(nEdges), outer(nEdges);
    std::vector<uint8_t> type(nEdges);
    std::vector<float> feats(static_cast<size_t>(nEdges) * kChainEdgeFeatures);
    std::vector<float> nodeFeats(static_cast<size_t>(nChainNodes_) * Params_ChainNode::kFeatures);
    auto pull = [&](auto* hostPtr, auto column, unsigned int n) {
      auto host_view = cms::alpakatools::make_host_view(hostPtr, n);
      auto dev_view = cms::alpakatools::make_device_view(queue_, column, n);
      alpaka::memcpy(queue_, host_view, dev_view);
      alpaka::wait(queue_);
    };
    auto ev = chainEdgesDC_->view();
    pull(inner.data(), ev.inner(), nEdges);
    pull(outer.data(), ev.outer(), nEdges);
    pull(type.data(), ev.type(), nEdges);
    {
      auto host_view = cms::alpakatools::make_host_view(feats.data(), feats.size());
      alpaka::memcpy(queue_, host_view, featBuf);
      alpaka::wait(queue_);
    }
    {
      static_assert(sizeof(Params_ChainNode::ArrayFxFeat) == sizeof(float) * Params_ChainNode::kFeatures,
                    "node feature rows must be densely packed for the debug dump");
      auto host_view = cms::alpakatools::make_host_view(
          reinterpret_cast<Params_ChainNode::ArrayFxFeat*>(nodeFeats.data()), nChainNodes_);
      auto dev_view = cms::alpakatools::make_device_view(queue_, chainNodesDC_->view().features(), nChainNodes_);
      alpaka::memcpy(queue_, host_view, dev_view);
      alpaka::wait(queue_);
    }

    uint32_t const featIevt = featDumpEvent.fetch_add(1);
    static std::mutex featDumpMutex;
    std::lock_guard<std::mutex> featLock(featDumpMutex);
    std::FILE* f = std::fopen(featPath, (featIevt == 0) ? "wb" : "ab");
    if (f != nullptr) {
      auto put32 = [&](uint32_t v) { std::fwrite(&v, sizeof(v), 1, f); };
      put32(0x50323146u);  // 'P21F' per-event record magic (new in the all-events format)
      put32(featIevt);
      put32(nChainNodes_);
      put32(static_cast<uint32_t>(Params_ChainNode::kFeatures));
      std::fwrite(nodeFeats.data(), sizeof(float), nodeFeats.size(), f);
      uint32_t nKept = 0;
      for (uint32_t e = 0; e < nEdges; ++e)
        nKept += (type[e] != 0);
      put32(nKept);
      put32(static_cast<uint32_t>(kChainEdgeFeatures));
      for (uint32_t e = 0; e < nEdges; ++e) {
        if (type[e] == 0)
          continue;
        put32(inner[e]);
        put32(outer[e]);
        put32(type[e]);
        std::fwrite(&feats[static_cast<size_t>(e) * kChainEdgeFeatures], sizeof(float), kChainEdgeFeatures, f);
      }
      std::fclose(f);
    }
  }
  dumpChainEdges();
  dumpChainNodes();
  buildChains();
}

void LSTEvent::dumpChainEdges() {
  // Parity sidecar for the P2.1 gate. Off unless LST_CHAIN_EDGE_DUMP names an output file; the
  // ntuple and the track candidate collection are untouched either way, so the harness cannot
  // see whether this ran. Record layout is documented in standalone/p21_ref/p21_ref_dump.cc.
  char const* path = std::getenv("LST_CHAIN_EDGE_DUMP");
  if (path == nullptr || *path == '\0' || !chainEdgesDC_.has_value())
    return;

  alpaka::wait(queue_);

  uint32_t const nEdges = static_cast<uint32_t>(chainEdgesDC_->view().metadata().size());
  std::vector<uint32_t> inner(nEdges), outer(nEdges);
  std::vector<uint8_t> type(nEdges);
  std::vector<float> logOdds(nEdges);

  auto pullTo = [&](auto* hostPtr, auto column, unsigned int n) {
    auto host_view = cms::alpakatools::make_host_view(hostPtr, n);
    auto dev_view = cms::alpakatools::make_device_view(queue_, column, n);
    alpaka::memcpy(queue_, host_view, dev_view);
    alpaka::wait(queue_);
  };
  auto view = chainEdgesDC_->view();
  pullTo(inner.data(), view.inner(), nEdges);
  pullTo(outer.data(), view.outer(), nEdges);
  pullTo(type.data(), view.type(), nEdges);
  pullTo(logOdds.data(), view.logOdds(), nEdges);

  uint32_t nE1Kept = 0, nE2Kept = 0;
  for (uint32_t e = 0; e < nEdges; ++e) {
    nE1Kept += (type[e] == 1);
    nE2Kept += (type[e] == 2);
  }

  // Sequential event counter. The sidecar is only meaningful for single-stream runs, which is
  // how the parity comparison is made.
  static std::atomic<uint32_t> eventCounter{0};
  uint32_t const ievt = eventCounter.fetch_add(1);

  static std::mutex dumpMutex;
  std::lock_guard<std::mutex> lock(dumpMutex);
  std::FILE* f = std::fopen(path, (ievt == 0) ? "wb" : "ab");
  if (f == nullptr)
    return;
  auto put32 = [&](uint32_t v) { std::fwrite(&v, sizeof(v), 1, f); };
  auto put64 = [&](uint64_t v) { std::fwrite(&v, sizeof(v), 1, f); };
  put32(0x50323145u);  // 'P21E'
  put32(ievt);
  put32(0u);  // run   - not known here, the comparison keys on the event order
  put32(0u);  // lumi
  put64(0u);  // event
  put32(nChainNodes_);
  put32(nChainE1Edges_);
  put32(nChainE2Edges_);
  put32(nE1Kept);
  put32(nE2Kept);
  for (uint32_t e = 0; e < nEdges; ++e) {
    if (type[e] == 0)
      continue;
    put32(inner[e]);
    put32(outer[e]);
    put32(type[e]);
    std::fwrite(&logOdds[e], sizeof(float), 1, f);
  }
  std::fclose(f);
}

void LSTEvent::dumpChainNodes() {
  // Determinism sidecar for the P2.5 gates. Off unless LST_CHAIN_NODE_DUMP names an output file.
  // One record per event carrying, for every chain node in dense node order, its stableId and the
  // six hit rows that stableId is built from. Two things read it:
  //   - the weld-tie uniqueness census (p25_ref/p25_nodes.py tie): joined with the LST_CHAIN_EDGE_DUMP
  //     edge list it proves that no node has two neighbours sharing a stableId, which is exactly the
  //     condition under which the packed weld key is unique inside a node's incident-edge list;
  //   - the CPU-vs-GPU attribution (p25_nodes.py attrib): the node set is compared on hit rows, so a
  //     chain that exists on one backend only can be traced to a missing upstream triplet.
  char const* path = std::getenv("LST_CHAIN_NODE_DUMP");
  if (path == nullptr || *path == '\0' || !chainNodesDC_.has_value() || nChainNodes_ == 0)
    return;

  alpaka::wait(queue_);

  auto pullTo = [&](auto* hostPtr, auto column, unsigned int n) {
    auto host_view = cms::alpakatools::make_host_view(hostPtr, n);
    auto dev_view = cms::alpakatools::make_device_view(queue_, column, n);
    alpaka::memcpy(queue_, host_view, dev_view);
    alpaka::wait(queue_);
  };

  uint32_t const nNodes = nChainNodes_;
  std::vector<uint32_t> tripletIndex(nNodes), stableId(nNodes);
  pullTo(tripletIndex.data(), chainNodesDC_->view().tripletIndex(), nNodes);
  pullTo(stableId.data(), chainNodesDC_->view().stableId(), nNodes);

  unsigned int const nT3 = static_cast<unsigned int>(tripletsDC_->view().triplets().metadata().size());
  unsigned int const nLS = static_cast<unsigned int>(segmentsDC_->view().segments().metadata().size());
  unsigned int const nMD = static_cast<unsigned int>(miniDoubletsDC_->view().miniDoublets().metadata().size());
  std::vector<ArrayUx2> t3Seg(nT3);
  std::vector<Params_LS::ArrayUxLayers> lsMD(nLS);
  std::vector<unsigned int> mdAnchor(nMD), mdOuter(nMD);
  pullTo(t3Seg.data(), tripletsDC_->view().triplets().segmentIndices(), nT3);
  pullTo(lsMD.data(), segmentsDC_->view().segments().mdIndices(), nLS);
  pullTo(mdAnchor.data(), miniDoubletsDC_->view().miniDoublets().anchorHitIndices(), nMD);
  pullTo(mdOuter.data(), miniDoubletsDC_->view().miniDoublets().outerHitIndices(), nMD);

  static std::atomic<uint32_t> nodeEventCounter{0};
  uint32_t const ievt = nodeEventCounter.fetch_add(1);
  static std::mutex nodeDumpMutex;
  std::lock_guard<std::mutex> lock(nodeDumpMutex);
  std::FILE* f = std::fopen(path, (ievt == 0) ? "wb" : "ab");
  if (f == nullptr)
    return;
  auto put32 = [&](uint32_t v) { std::fwrite(&v, sizeof(v), 1, f); };

  put32(0x5032354Eu);  // 'P25N'
  put32(ievt);
  put32(nNodes);
  for (uint32_t n = 0; n < nNodes; ++n) {
    uint32_t const t3 = tripletIndex[n];
    unsigned int const innerSeg = t3Seg[t3][0];
    unsigned int const outerSeg = t3Seg[t3][1];
    unsigned int const md[3] = {lsMD[innerSeg][0], lsMD[innerSeg][1], lsMD[outerSeg][1]};
    put32(stableId[n]);
    for (int k = 0; k < 3; ++k) {
      put32(mdAnchor[md[k]]);
      put32(mdOuter[md[k]]);
    }
  }
  std::fclose(f);
}

void LSTEvent::buildChains() {
  // Phase P2.2. K6a/K6b weld the edge graph into disjoint simple paths, K6c-K6e emit them as
  // chains, K6f trims a parasitic terminal, K7a builds the 25 frozen chain features plus the chain
  // dcaXY, and K7b/K7c run the 3-class gate and the -G 6 branch kill. Nothing reads the kill bit:
  // this phase is measurement only, exactly like P2.1.
  if (nChainNodes_ == 0 || !chainEdgesDC_.has_value())
    return;

  bool const timing = chainTimingEnabled();
  auto stamp = [&]() {
    if (timing)
      alpaka::wait(queue_);
    return std::chrono::steady_clock::now();
  };
  auto const t0 = stamp();

  auto const chainFlat_workDiv = cms::alpakatools::make_workdiv<Acc1D>(max_blocks, 256);
  auto const chainScan_workDiv = cms::alpakatools::make_workdiv<Acc1D>(1, kChainScanBlockThreads);

  // K6a / K6b. Weld slots start empty (-1) and the packed argmax keys start at the 0 sentinel.
  auto outWeld_buf = cms::alpakatools::make_device_buffer<int32_t[]>(queue_, nChainNodes_);
  auto inWeld_buf = cms::alpakatools::make_device_buffer<int32_t[]>(queue_, nChainNodes_);
  auto bestOut_buf = cms::alpakatools::make_device_buffer<uint64_t[]>(queue_, nChainNodes_);
  auto bestIn_buf = cms::alpakatools::make_device_buffer<uint64_t[]>(queue_, nChainNodes_);
  alpaka::memset(queue_, outWeld_buf, 0xff);
  alpaka::memset(queue_, inWeld_buf, 0xff);

  // S1: the weld eligibility bar is no longer a kernel argument. K5 resolved it per edge into
  // ChainEdgesSoA::weldBar (from the head's per-family, per-cell table, or from the two per-family
  // scalars when chainConfig_.edgeWpTable is false), so both weld kernels just read the edge row.
  for (int sweep = 0; sweep < kChainWeldSweeps; ++sweep) {
    // A fixed sweep count, no host sync: the reference's "break when nothing welded" early exit is
    // a CPU nicety and a zero-weld sweep is idempotent.
    alpaka::memset(queue_, bestOut_buf, 0);
    alpaka::memset(queue_, bestIn_buf, 0);
    alpaka::exec<Acc1D>(queue_,
                        chainFlat_workDiv,
                        ChainWeldArgmax{},
                        chainEdgesDC_->const_view(),
                        chainNodesDC_->const_view(),
                        chainMdIncidenceDC_->const_view(),
                        chainLsIncidenceDC_->const_view(),
                        outWeld_buf.data(),
                        inWeld_buf.data(),
                        bestOut_buf.data(),
                        bestIn_buf.data());
    alpaka::exec<Acc1D>(queue_,
                        chainFlat_workDiv,
                        ChainWeldMutual{},
                        chainEdgesDC_->const_view(),
                        chainNodesDC_->const_view(),
                        chainMdIncidenceDC_->const_view(),
                        chainLsIncidenceDC_->const_view(),
                        outWeld_buf.data(),
                        inWeld_buf.data(),
                        bestOut_buf.data(),
                        bestIn_buf.data());
  }

  auto const t1 = stamp();

  // K6c / K6d. Head detection, path lengths, and the two exclusive prefixes that name the chains.
  auto headNodeCount_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, nChainNodes_);
  auto chainIndexOf_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, nChainNodes_);
  auto nodeOffsetOf_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, nChainNodes_);
  auto nChains_buf_d = cms::alpakatools::make_device_buffer<uint32_t>(queue_);
  auto nChainNodesTotal_buf_d = cms::alpakatools::make_device_buffer<uint32_t>(queue_);

  alpaka::exec<Acc1D>(queue_,
                      chainFlat_workDiv,
                      ChainCountChains{},
                      chainEdgesDC_->const_view(),
                      nChainNodes_,
                      outWeld_buf.data(),
                      inWeld_buf.data(),
                      headNodeCount_buf.data());

  chainScanTimed(timing, __LINE__, queue_,
                      chainScan_workDiv,
                      ChainPrefixChains{},
                      nChainNodes_,
                      headNodeCount_buf.data(),
                      chainIndexOf_buf.data(),
                      nodeOffsetOf_buf.data(),
                      nChains_buf_d.data(),
                      nChainNodesTotal_buf_d.data());

  auto nChains_buf_h = cms::alpakatools::make_host_buffer<uint32_t>(queue_);
  auto nChainNodesTotal_buf_h = cms::alpakatools::make_host_buffer<uint32_t>(queue_);
  alpaka::memcpy(queue_, nChains_buf_h, nChains_buf_d);
  alpaka::memcpy(queue_, nChainNodesTotal_buf_h, nChainNodesTotal_buf_d);
  alpaka::wait(queue_);  // the chain count sizes both chain collections exactly
  nChainCount_ = *nChains_buf_h.data();
  nChainWeldedNodes_ = *nChainNodesTotal_buf_h.data();

  if (nChainCount_ == 0)
    return;

  // Exact sizing: 3 * (total member nodes) bounds the MD union of every chain at once (see the
  // storage contract in ChainsSoA.h), so no second prefix pass and no reservation are needed.
  chainsDC_.emplace(queue_, nChainCount_);
  chainItemsDC_.emplace(queue_, 3 * nChainWeldedNodes_);
  // COORDINATOR: featValid must start at 0 for EVERY chain. Only chains the learned trim visits
  // (nNodes >= 3) ever set it, and K7a skips a row that claims to be published -- an uninitialised
  // byte here would silently drop feature rows, so this memset is a correctness requirement, not a
  // tidiness one.
  {
    auto fvView = cms::alpakatools::make_device_view(queue_, chainsDC_->view().featValid(), nChainCount_);
    alpaka::memset(queue_, fvView, 0u);
  }
  if (objectsStatistics_) {
    double mb =
        (alpaka::getExtentProduct(chainsDC_->buffer()) + alpaka::getExtentProduct(chainItemsDC_->buffer())) / 1e6;
    memoryAllocatedMB_ += mb;
    lstWarning(std::format(
        "[MEM] Chains: {} chains / {} member nodes allocated ({:.1f} MB)", nChainCount_, nChainWeldedNodes_, mb));
  }

  auto const t2 = stamp();

  alpaka::exec<Acc1D>(queue_,
                      chainFlat_workDiv,
                      ChainEmitChains{},
                      segmentsDC_->const_view().segments(),
                      tripletsDC_->const_view().triplets(),
                      modules_.const_view().modules(),
                      miniDoubletsDC_->const_view().miniDoublets(),
                      chainNodesDC_->const_view(),
                      chainEdgesDC_->const_view(),
                      nChainNodes_,
                      outWeld_buf.data(),
                      headNodeCount_buf.data(),
                      chainIndexOf_buf.data(),
                      nodeOffsetOf_buf.data(),
                      chainsDC_->view(),
                      chainItemsDC_->view(),
                      chainConfig_.lambdaLen);

  auto const t3 = stamp();

  // TRIM-NN probe (env-gated, PRE-TRIM, pure observation). Allocates nothing and runs nothing
  // unless LST_CHAIN_VARIANT_DUMP names an output file.
  if (char const* vpath = std::getenv("LST_CHAIN_VARIANT_DUMP"); vpath != nullptr && *vpath != '\0') {
    auto innerMD_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, 3 * nChainWeldedNodes_);
    auto probe_buf = cms::alpakatools::make_device_buffer<float[]>(
        queue_, static_cast<size_t>(nChainCount_) * chaintrim::kProbeWords);
    alpaka::exec<Acc1D>(queue_,
                        chainFlat_workDiv,
                        ChainVariantProbe{},
                        modules_.const_view().modules(),
                        miniDoubletsDC_->const_view().miniDoublets(),
                        segmentsDC_->const_view().segments(),
                        tripletsDC_->const_view().triplets(),
                        chainNodesDC_->const_view(),
                        chainEdgesDC_->const_view(),
                        chainMdIncidenceDC_->const_view(),
                        chainLsIncidenceDC_->const_view(),
                        chainItemsDC_->const_view(),
                        chainsDC_->const_view(),
                        innerMD_buf.data(),
                        probe_buf.data());
    alpaka::wait(queue_);
    dumpChainVariants(vpath, innerMD_buf.data(), probe_buf.data());
  }

  if (chainConfig_.terminalTrim && chainConfig_.trimFactor > 0.f) {
    for (int pass = 0; pass < chainConfig_.trimPasses; ++pass) {
      if (chainConfig_.trimMode == 0) {
        alpaka::exec<Acc1D>(queue_,
                            chainFlat_workDiv,
                            ChainTrimTerminals{},
                            segmentsDC_->const_view().segments(),
                            tripletsDC_->const_view().triplets(),
                            modules_.const_view().modules(),
                            miniDoubletsDC_->const_view().miniDoublets(),
                            chainNodesDC_->const_view(),
                            chainEdgesDC_->const_view(),
                            chainsDC_->view(),
                            chainItemsDC_->view(),
                            chainConfig_);
      } else {
        alpaka::exec<Acc1D>(queue_,
                            chainFlat_workDiv,
                            ChainTrimLearned{},
                            modules_.const_view().modules(),
                            miniDoubletsDC_->const_view().miniDoublets(),
                            segmentsDC_->const_view().segments(),
                            tripletsDC_->const_view().triplets(),
                            chainNodesDC_->const_view(),
                            chainEdgesDC_->const_view(),
                            chainMdIncidenceDC_->const_view(),
                            chainLsIncidenceDC_->const_view(),
                            chainItemsDC_->const_view(),
                            chainsDC_->view(),
                            chainItemsDC_->view(),
                            chainConfig_);
      }
    }
  }

  auto const t4 = stamp();

  alpaka::exec<Acc1D>(queue_,
                      chainFlat_workDiv,
                      ChainFeaturesKernel{},
                      modules_.const_view().modules(),
                      miniDoubletsDC_->const_view().miniDoublets(),
                      segmentsDC_->const_view().segments(),
                      tripletsDC_->const_view().triplets(),
                      chainNodesDC_->const_view(),
                      chainEdgesDC_->const_view(),
                      chainMdIncidenceDC_->const_view(),
                      chainLsIncidenceDC_->const_view(),
                      chainItemsDC_->const_view(),
                      chainsDC_->view());

  auto const t5 = stamp();

  alpaka::exec<Acc1D>(queue_,
                      chainFlat_workDiv,
                      ChainGateKernel{},
                      miniDoubletsDC_->const_view().miniDoublets(),
                      segmentsDC_->const_view().segments(),
                      tripletsDC_->const_view().triplets(),
                      chainNodesDC_->const_view(),
                      chainItemsDC_->const_view(),
                      chainsDC_->view(),
                      chainConfig_);

  auto const t6 = stamp();
  if (timing) {
    auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
    lstWarning(
        std::format("[CHAIN TIMING] chains={} weldedNodes={} | K6ab weld {:.3f} ms | "
                    "K6cd count+prefix {:.3f} ms | K6e emit {:.3f} ms | K6f trim {:.3f} ms | "
                    "K7a features {:.3f} ms | K7bc gate {:.3f} ms | total {:.3f} ms",
                    nChainCount_,
                    nChainWeldedNodes_,
                    ms(t0, t1),
                    ms(t1, t2),
                    ms(t2, t3),
                    ms(t3, t4),
                    ms(t4, t5),
                    ms(t5, t6),
                    ms(t0, t6)));
  }

  dumpChains();
}

void LSTEvent::arbitrateChains(unsigned int nAllocatedTCs) {
  // Chain-tracking phase P2.3 (port map section 5): the FIRST physics-changing stage. It runs at
  // the very end of createTrackCandidates, after every baseline crossclean and every baseline
  // Add*asTrackCandidate, so the carried pT3 and bare-pLS rows are bit-identical to what LST
  // builds; only then are the replaced classes removed and the accepted chains appended.
  bool const timing = chainTimingEnabled();
  auto stamp = [&]() {
    if (timing)
      alpaka::wait(queue_);
    return std::chrono::steady_clock::now();
  };
  auto const t0 = stamp();

  auto const serial_workDiv = cms::alpakatools::make_workdiv<Acc1D>(1, 1);
  auto const chainFlat_workDiv = cms::alpakatools::make_workdiv<Acc1D>(max_blocks, 256);
  auto const chainScan_workDiv = cms::alpakatools::make_workdiv<Acc1D>(1, kChainScanBlockThreads);

  // The -RT5 1 wholesale drop of the carried type-7 rows plus the removal of the LST T5 / T4 rows
  // whose class the chain pipeline now builds. Runs even with zero chains: the mode is defined by
  // the configuration, not by how many chains an event happened to weld.
  //
  // The in-place serial compaction (1.5 ms/event on CUDA) is done as flags + single-block prefix +
  // gather/scatter through a staging array, on every backend (one form).
  {
    uint32_t const nIn = nAllocatedTCs;
    auto keep_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, std::max(1u, nIn));
    auto offs_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, nIn + 1u);
    auto total_buf = cms::alpakatools::make_device_buffer<uint32_t>(queue_);
    auto stage_buf = cms::alpakatools::make_device_buffer<ChainTCRowPayload[]>(queue_, std::max(1u, nIn));
    alpaka::exec<Acc1D>(queue_,
                        chainFlat_workDiv,
                        ChainTCKeepCompact{},
                        trackCandidatesBaseDC_->const_view(),
                        keep_buf.data(),
                        nIn,
                        chainConfig_);
    chainScanTimed(timing, __LINE__,
        queue_, chainScan_workDiv, ChainSegPrefix{}, keep_buf.data(), offs_buf.data(), total_buf.data(), nIn);
    alpaka::exec<Acc1D>(queue_,
                        chainFlat_workDiv,
                        ChainTCGather{},
                        trackCandidatesBaseDC_->const_view(),
                        trackCandidatesExtendedDC_->const_view(),
                        keep_buf.data(),
                        offs_buf.data(),
                        nIn,
                        stage_buf.data());
    alpaka::exec<Acc1D>(queue_,
                        chainFlat_workDiv,
                        ChainTCScatter{},
                        trackCandidatesBaseDC_->view(),
                        trackCandidatesExtendedDC_->view(),
                        stage_buf.data(),
                        offs_buf.data(),
                        nIn);
    alpaka::exec<Acc1D>(queue_,
                        serial_workDiv,
                        ChainTCFinishCompact{},
                        trackCandidatesBaseDC_->view(),
                        trackCandidatesExtendedDC_->view(),
                        offs_buf.data(),
                        nIn,
                        chainConfig_);
  }
  auto const t1 = stamp();

  if (nChainCount_ == 0 || !chainsDC_.has_value())
    return;

  unsigned int const nHits = static_cast<unsigned int>(lstInputDC_->const_view().hits().metadata().size());
  unsigned int const nMDall = static_cast<unsigned int>(miniDoubletsDC_->view().miniDoublets().metadata().size());

  // K9-0 / K9-1: the claim universe, the order key, the candidate mask and the -WE/-WZ band
  // tolerances -- one per-chain visit (ChainClaimPrep).
  auto claimHits_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, 6u * nChainWeldedNodes_);
  auto candKeep_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, nChainCount_);
  auto bandItems_buf = cms::alpakatools::make_device_buffer<int32_t[]>(queue_, nChainCount_);
  auto bandFrac_buf = cms::alpakatools::make_device_buffer<float[]>(queue_, nChainCount_);
  auto bandBraid_buf = cms::alpakatools::make_device_buffer<float[]>(queue_, nChainCount_);
  // KEY (jet round 3): the ONLY thing this copy changes is the order key's two literals, and only
  // when the environment names them. ChainClaimPrep is the sole reader of orderAlpha/orderHinge in
  // the whole tree, so the override cannot leak into any other stage.
  ChainConfig cfgK9 = chainConfig_;
  cfgK9.orderAlpha = chainOrderAlpha(chainConfig_.orderAlpha);
  cfgK9.orderHinge = chainOrderHinge(chainConfig_.orderHinge);
  cfgK9.orderAlphaCentral = chainOrderEnvF("LST_CHAIN_ORDER_ALPHA_CENTRAL", chainConfig_.orderAlphaCentral);
  cfgK9.orderEtaRampLo = chainOrderEnvF("LST_CHAIN_ORDER_ETA_LO", chainConfig_.orderEtaRampLo);
  cfgK9.orderEtaRampHi = chainOrderEnvF("LST_CHAIN_ORDER_ETA_HI", chainConfig_.orderEtaRampHi);
  if (cfgK9.orderAlpha != chainConfig_.orderAlpha || cfgK9.orderHinge != chainConfig_.orderHinge ||
      cfgK9.orderAlphaCentral != chainConfig_.orderAlphaCentral ||
      cfgK9.orderEtaRampLo != chainConfig_.orderEtaRampLo ||
      cfgK9.orderEtaRampHi != chainConfig_.orderEtaRampHi) {
    static bool once = false;
    if (!once) {
      once = true;
      printf("[CHAIN KEY] order key override ON: orderAlpha=%g orderHinge=%g alphaCentral=%g eta[%g,%g]"
             " (shipped %g / %g / %g / %g / %g)\n",
             cfgK9.orderAlpha, cfgK9.orderHinge, cfgK9.orderAlphaCentral, cfgK9.orderEtaRampLo,
             cfgK9.orderEtaRampHi, chainConfig_.orderAlpha, chainConfig_.orderHinge,
             chainConfig_.orderAlphaCentral, chainConfig_.orderEtaRampLo, chainConfig_.orderEtaRampHi);
    }
  }
  alpaka::exec<Acc1D>(queue_,
                      chainFlat_workDiv,
                      ChainClaimPrep{},
                      miniDoubletsDC_->const_view().miniDoublets(),
                      tripletsDC_->const_view().triplets(),
                      segmentsDC_->const_view().segments(),
                      chainNodesDC_->const_view(),
                      chainItemsDC_->const_view(),
                      chainsDC_->view(),
                      claimHits_buf.data(),
                      candKeep_buf.data(),
                      bandItems_buf.data(),
                      bandFrac_buf.data(),
                      bandBraid_buf.data(),
                      cfgK9);
  auto const t2 = stamp();

  // K9a / K9b / K9c: pre-claim, greedy claim, braid, as conflict-free rounds (see ChainArbitrate.h).
  auto owner_buf = cms::alpakatools::make_device_buffer<int32_t[]>(queue_, nHits);
  auto order_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, nChainCount_);
  auto accepted_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, nChainCount_);
  auto stats_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, kChainArbStats);
  alpaka::memset(queue_, stats_buf, 0u);

  // T4 INSTRUMENT (timing only): the K9-claim block is seven things, and the map lumps them.
  auto k9a = t2, k9b = t2, k9c = t2, k9d = t2, k9e = t2;
  uint32_t nCandDiag = 0u;
  auto nCand_buf = cms::alpakatools::make_device_buffer<uint32_t>(queue_);
  {
    // P2.6a: the same walk, reached by conflict-free rounds instead of a single thread. See the
    // exactness / termination argument in ChainArbitrate.h.
    auto candOffs_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, nChainCount_ + 1u);
    auto candRecs_buf = cms::alpakatools::make_device_buffer<ChainOrderKeyRec[]>(queue_, nChainCount_);
    auto minPos_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, nHits);
    auto nClaimed_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, nChainCount_);
    auto state_buf = cms::alpakatools::make_device_buffer<uint8_t[]>(queue_, nChainCount_);
    auto part_buf = cms::alpakatools::make_device_buffer<uint8_t[]>(queue_, nChainCount_);
    alpaka::memset(queue_, owner_buf, 0xFF);   // chainarb::kFree everywhere
    alpaka::memset(queue_, minPos_buf, 0xFF);  // chainpar::kNoPos everywhere
    k9a = stamp();

    chainScanTimed(timing, __LINE__, queue_,
                        chainScan_workDiv,
                        ChainSegPrefix{},
                        candKeep_buf.data(),
                        candOffs_buf.data(),
                        nCand_buf.data(),
                        nChainCount_);
    k9b = stamp();
    alpaka::exec<Acc1D>(queue_,
                        chainFlat_workDiv,
                        ChainCandScatter{},
                        chainsDC_->const_view(),
                        candKeep_buf.data(),
                        candOffs_buf.data(),
                        candRecs_buf.data());
    k9c = stamp();
    {
      auto rankPart_buf =
          cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, nChainCount_ * kChainRankSlices);
      alpaka::exec<Acc1D>(queue_,
                          chainFlat_workDiv,
                          ChainClaimRankPartial{},
                          candRecs_buf.data(),
                          nCand_buf.data(),
                          nChainCount_,
                          rankPart_buf.data());
      alpaka::exec<Acc1D>(queue_,
                          chainFlat_workDiv,
                          ChainClaimRankFinish{},
                          candRecs_buf.data(),
                          nCand_buf.data(),
                          nChainCount_,
                          rankPart_buf.data(),
                          order_buf.data());
    }
    k9d = stamp();
    if (chainConfig_.preClaim)
      alpaka::exec<Acc1D>(queue_,
                          chainFlat_workDiv,
                          ChainPreClaimPixels{},
                          trackCandidatesBaseDC_->const_view(),
                          trackCandidatesExtendedDC_->const_view(),
                          owner_buf.data(),
                          nHits);
    k9e = stamp();
    chainScanTimed(timing, __LINE__, queue_,
                        chainScan_workDiv,
                        ChainClaimRounds{},
                        chainsDC_->view(),
                        claimHits_buf.data(),
                        order_buf.data(),
                        nCand_buf.data(),
                        owner_buf.data(),
                        minPos_buf.data(),
                        nClaimed_buf.data(),
                        state_buf.data(),
                        part_buf.data(),
                        accepted_buf.data(),
                        bandItems_buf.data(),
                        bandFrac_buf.data(),
                        bandBraid_buf.data(),
                        stats_buf.data(),
                        chainConfig_);
  }
  auto const t3 = stamp();
  if (timing) {
    auto nCand_h = cms::alpakatools::make_host_buffer<uint32_t>(queue_);
    alpaka::memcpy(queue_, nCand_h, nCand_buf);
    alpaka::wait(queue_);
    nCandDiag = *nCand_h.data();
  }

  // K8: pixel attach. It runs on the K9-ACCEPTED chain set and BEFORE the extension, exactly as
  // prototype/main.cc orders it under -A 4: the attach features are built from the chain's
  // post-trim, PRE-extension MiniDoublet list.
  //
  // The pLS-side state is allocated HERE because it must outlive both attach stages: the -CC
  // sweep, the -XC crossclean and the final carried-row retirement all read (and the sweep
  // writes) it after the chain rows have been emitted.
  //   plsOwned      the ONE-pLS-ONE-OWNER authority across both stages (invariant I1)
  //   plsBestChain  chain-side retirement evidence (every scored stage-A pair, pre-threshold),
  //                 read against rpsThetaChain (-RPSA)
  //   plsBestT3     bare-T3 retirement evidence (every scored stage-B pair), read against
  //                 attachThetaT3 (-AT3); -CCR 2 erases entries here
  //   hashKey/Val   the -RD seed-family table stage A fills and stage B's -RDT reuses
  //   xcPairs       the -XC bare-chain-arm pass-1 filtered pair compaction
  //   xcRetired     the -XC verdict, consumed by the final retirement
  uint32_t const nPls = std::max(1u, pixelSize_);
  auto plsPre_buf = cms::alpakatools::make_device_buffer<AttachPlsPre[]>(queue_, nPls);
  auto plsOwned_buf = cms::alpakatools::make_device_buffer<uint8_t[]>(queue_, nPls);
  auto plsBestChain_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, nPls);
  auto plsBestT3_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, nPls);
  auto rdHashKey_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, chainattach::kSeedHashSlots);
  auto rdHashVal_buf = cms::alpakatools::make_device_buffer<int32_t[]>(queue_, chainattach::kSeedHashSlots);
  if (!rdHashOwner_.has_value())
    rdHashOwner_.emplace(cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, chainattach::kSeedHashSlots));
  constexpr uint32_t kXcPairCap = 1u << 17;  // ~131k pairs; measured pass-1 volume is far below
  auto xcPairs_buf = cms::alpakatools::make_device_buffer<ChainXcPair[]>(queue_, kXcPairCap);
  auto xcCursor_buf = cms::alpakatools::make_device_buffer<uint32_t>(queue_);
  auto xcRetired_buf = cms::alpakatools::make_device_buffer<uint8_t[]>(queue_, nPls);
  // JET ROUND 2 (D): the mutual-best retirement flag, one byte per pLS row.
  auto plsMutual_buf = cms::alpakatools::make_device_buffer<uint8_t[]>(queue_, nPls);
  alpaka::memset(queue_, plsOwned_buf, 0u);
  alpaka::memset(queue_, plsBestChain_buf, 0u);  // orderFloat(-inf) == 0
  alpaka::memset(queue_, plsBestT3_buf, 0u);
  alpaka::memset(queue_, rdHashKey_buf, 0xFF);  // chainattach::kSeedHashEmpty everywhere
  alpaka::memset(queue_, xcCursor_buf, 0u);
  alpaka::memset(queue_, xcRetired_buf, 0u);
  alpaka::memset(queue_, plsMutual_buf, 0u);
  if (pixelSize_ > 0)
    alpaka::exec<Acc1D>(queue_,
                        chainFlat_workDiv,
                        ChainAttachPlsPre{},
                        lstInputDC_->const_view().pixelSeeds(),
                        pixelSegmentsDC_->const_view(),
                        plsPre_buf.data(),
                        pixelSize_,
                        chainConfig_);

  // The stage-B TARGET UNIVERSE first: it depends only on the K9 accepted array, and the ONE
  // grid both attach stages share needs both target sets to form its union hull. Stage B's
  // SCORING still runs after stage A, because that is what honours the live ownership.
  prepareBareT3Targets(accepted_buf.data());

  attachPixels(nHits,
               accepted_buf.data(),
               plsPre_buf.data(),
               plsOwned_buf.data(),
               plsBestChain_buf.data(),
               rdHashKey_buf.data(),
               rdHashVal_buf.data(),
               xcPairs_buf.data(),
               xcCursor_buf.data(),
               kXcPairCap,
               plsMutual_buf.data());

  // DEGENERATE PATH: attachPixels builds the shared grid, but it bails out before that when no
  // 5+ layer chain target was accepted (or there is no pLS). Stage B still has its own targets on
  // that path, so it gets the grid from its own hull alone -- same code, one target set.
  if (!attachGridOffs_.has_value() && nBareT3_ > 0 && bareT3TgtPre_.has_value())
    buildAttachGrid(plsPre_buf.data(), bareT3TgtPre_->data(), nBareT3_, nullptr, 0u);

  // Stage B: the bare-T3 attach. After stage A's contention and -RD dedup are final (its scorer
  // honours the live ownership), before the extension (its inputs are extension-invariant) and
  // long before the retirement.
  attachBareT3(nHits,
               plsPre_buf.data(),
               plsOwned_buf.data(),
               plsBestT3_buf.data(),
               rdHashKey_buf.data(),
               rdHashVal_buf.data());
  // The shared grid has no reader left (the -CC sweep reads only the bareT3* owner arrays), so its
  // ~10 MB items payload goes back to the caching allocator here -- the same point in the event at
  // which the two per-stage grids used to be freed.
  attachGridOffs_.reset();
  attachGridItems_.reset();
  attachGridEntries_ = 0;
  bareT3TgtPre_.reset();
  auto const t3b = stamp();

  // EX: chain extension at assembly. Needs the claimed-hit map and the MD -> outgoing-LineSegment
  // adjacency (-EXS 1).
  auto const t4 = stamp();

  // K10: row assignment then emission.
  {
    auto rowKeep_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, nChainCount_);
    auto rowOffs_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, nChainCount_ + 1u);
    auto rowTotal_buf = cms::alpakatools::make_device_buffer<uint32_t>(queue_);
    auto rowClass_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, 3u);
    alpaka::memset(queue_, rowClass_buf, 0u);
    alpaka::exec<Acc1D>(queue_,
                        chainFlat_workDiv,
                        ChainRowFlags{},
                        chainsDC_->const_view(),
                        accepted_buf.data(),
                        nChainCount_,
                        rowKeep_buf.data(),
                        chainConfig_);
    chainScanTimed(timing, __LINE__, queue_,
                        chainScan_workDiv,
                        ChainSegPrefix{},
                        rowKeep_buf.data(),
                        rowOffs_buf.data(),
                        rowTotal_buf.data(),
                        nChainCount_);
    alpaka::exec<Acc1D>(queue_,
                        chainFlat_workDiv,
                        ChainRowAssign{},
                        trackCandidatesBaseDC_->const_view(),
                        chainsDC_->view(),
                        accepted_buf.data(),
                        rowKeep_buf.data(),
                        rowOffs_buf.data(),
                        nChainCount_,
                        nAllocatedTCs,
                        rowClass_buf.data());
    alpaka::exec<Acc1D>(queue_,
                        serial_workDiv,
                        ChainRowFinish{},
                        trackCandidatesBaseDC_->view(),
                        trackCandidatesExtendedDC_->view(),
                        chainsDC_->view(),
                        rowOffs_buf.data(),
                        rowClass_buf.data(),
                        nChainCount_,
                        nAllocatedTCs);
  }
  auto const t4b = stamp();
  // The -CC pre-claim map (MDs of the emitted chain TCs) is filled by the emission itself, so it
  // has to exist before it. Allocated at size 1 when the -CC sweep will not run.
  bool const ccActive = (nBareT3_ > 0 && pixelSize_ > 0);
  auto ccClaimed_buf = cms::alpakatools::make_device_buffer<uint8_t[]>(queue_, ccActive ? nMDall : 1u);
  if (ccActive)
    alpaka::memset(queue_, ccClaimed_buf, 0u);
  alpaka::exec<Acc1D>(queue_,
                      chainFlat_workDiv,
                      ChainEmitTCs{},
                      modules_.const_view().modules(),
                      miniDoubletsDC_->const_view().miniDoublets(),
                      segmentsDC_->const_view().segments(),
                      tripletsDC_->const_view().triplets(),
                      lstInputDC_->const_view().hits(),
                      lstInputDC_->const_view().pixelSeeds(),
                      chainNodesDC_->const_view(),
                      chainItemsDC_->const_view(),
                      chainsDC_->view(),
                      trackCandidatesBaseDC_->view(),
                      trackCandidatesExtendedDC_->view(),
                      nHits,
                      pixelModuleIndex_,
                      ccActive ? ccClaimed_buf.data() : nullptr,
                      stats_buf.data());
  auto const t4c = stamp();

  // ---- -CC contention + the pT3-class delivery -----------------------------------------------
  // The stage-B owners meet the hit-overlap contention and, surviving it, are emitted as type-5
  // rows appended after the chain rows -- one fused serial sweep, exactly the reference loop
  // (main.cc:4293-4489). The pre-claim map holds the MDs of every EMITTED chain TC (post-
  // extension); carried pixel rows contribute nothing (replacePT5 / replacePT3 dropped them all).
  auto postStats_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, chainattacht3::kStats);
  alpaka::memset(queue_, postStats_buf, 0u);
  // T4 INSTRUMENT (timing only): T3CC is prefix(nBareT3) + compact(nBareT3) + a ONE-THREAD sweep.
  auto cc1 = t4c, cc2 = t4c, cc3 = t4c;
  uint32_t nDelivDiag = 0u;
  if (ccActive) {
    // T4: the -CC sweep, split along its only real dependence. The per-delivery lookup (a 3-level
    // dependent chase) and the type-5 row assembly (~55 stores) are pure functions of the delivery
    // and of its assigned row, so they go to the grid; the claim map and the row counter -- the
    // only state an earlier delivery writes -- stay on one thread, reading a 20-byte record
    // sequentially. Exactness argument at ChainT3CCPrep in ChainAttachT3.h.
    auto ccOffs_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, nBareT3_ + 1u);
    auto nDeliv_buf = cms::alpakatools::make_device_buffer<uint32_t>(queue_);
    auto ccOwners_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, nBareT3_);
    auto ccRecs_buf = cms::alpakatools::make_device_buffer<ChainT3CCRec[]>(queue_, nBareT3_);
    auto ccRow_buf = cms::alpakatools::make_device_buffer<int32_t[]>(queue_, nBareT3_);
    chainScanTimed(timing, __LINE__, queue_,
                        chainScan_workDiv,
                        ChainSegPrefix{},
                        bareT3Keep_->data(),
                        ccOffs_buf.data(),
                        nDeliv_buf.data(),
                        nBareT3_);
    cc1 = stamp();
    alpaka::exec<Acc1D>(queue_,
                        chainFlat_workDiv,
                        ChainCompactSelect{},
                        bareT3Keep_->data(),
                        ccOffs_buf.data(),
                        nBareT3_,
                        nullptr,
                        ccOwners_buf.data(),
                        nullptr);
    alpaka::exec<Acc1D>(queue_,
                        chainFlat_workDiv,
                        ChainT3CCPrep{},
                        segmentsDC_->const_view().segments(),
                        tripletsDC_->const_view().triplets(),
                        chainNodesDC_->const_view(),
                        bareT3Targets_->data(),
                        bareT3TgtPls_->data(),
                        ccOwners_buf.data(),
                        nDeliv_buf.data(),
                        nBareT3_,
                        ccRecs_buf.data(),
                        ccRow_buf.data());
    cc2 = stamp();
    alpaka::exec<Acc1D>(queue_,
                        serial_workDiv,
                        ChainT3CCSweep{},
                        chainsDC_->view(),
                        trackCandidatesBaseDC_->view(),
                        trackCandidatesExtendedDC_->view(),
                        ccRecs_buf.data(),
                        ccOwners_buf.data(),
                        nDeliv_buf.data(),
                        bareT3TgtPls_->data(),
                        ccRow_buf.data(),
                        ccClaimed_buf.data(),
                        plsOwned_buf.data(),
                        plsBestT3_buf.data(),
                        chainConfig_.ccMinShared,
                        nAllocatedTCs,
                        postStats_buf.data());
    alpaka::exec<Acc1D>(queue_,
                        chainFlat_workDiv,
                        ChainT3CCEmit{},
                        modules_.const_view().modules(),
                        miniDoubletsDC_->const_view().miniDoublets(),
                        lstInputDC_->const_view().hits(),
                        lstInputDC_->const_view().pixelSeeds(),
                        trackCandidatesBaseDC_->view(),
                        trackCandidatesExtendedDC_->view(),
                        ccRecs_buf.data(),
                        ccRow_buf.data(),
                        nDeliv_buf.data(),
                        nBareT3_,
                        nHits,
                        pixelModuleIndex_,
                        postStats_buf.data());
    cc3 = stamp();
    if (timing) {
      auto nD_h = cms::alpakatools::make_host_buffer<uint32_t>(queue_);
      alpaka::memcpy(queue_, nD_h, nDeliv_buf);
      alpaka::wait(queue_);
      nDelivDiag = *nD_h.data();
    }
  }
  bareT3Targets_.reset();
  bareT3TgtPls_.reset();
  bareT3TgtLogit_.reset();
  bareT3Keep_.reset();
  auto const t4d = stamp();

  // ---- -XC: the ported CrossCleanpLS ---------------------------------------------------------
  // Anchors and candidates are both final now (the -CC sweep can release seeds). One byte per
  // pLS comes out; the final retirement below consumes it.
  auto xcStats_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, 8u);
  alpaka::memset(queue_, xcStats_buf, 0u);
  if (pixelSize_ > 0) {
    auto anchorPls_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, nPls);
    auto nAnchors_buf = cms::alpakatools::make_device_buffer<uint32_t>(queue_);
    auto xcHash_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, chainxc::kHitHashSlots);
    alpaka::memset(queue_, nAnchors_buf, 0u);
    alpaka::memset(queue_, xcHash_buf, 0xFF);  // chainxc::kHitHashEmpty everywhere
    alpaka::exec<Acc1D>(queue_,
                        chainFlat_workDiv,
                        ChainXcAnchorList{},
                        lstInputDC_->const_view().pixelSeeds(),
                        plsOwned_buf.data(),
                        pixelSize_,
                        anchorPls_buf.data(),
                        nAnchors_buf.data());
    alpaka::exec<Acc1D>(queue_,
                        chainFlat_workDiv,
                        ChainXcAnchorHits{},
                        lstInputDC_->const_view().pixelSeeds(),
                        lstInputDC_->const_view().hits(),
                        anchorPls_buf.data(),
                        nAnchors_buf.data(),
                        nPls,
                        nHits,
                        xcHash_buf.data(),
                        xcStats_buf.data());
    alpaka::exec<Acc1D>(queue_,
                        chainFlat_workDiv,
                        ChainXcPixelArm{},
                        lstInputDC_->const_view().pixelSeeds(),
                        lstInputDC_->const_view().hits(),
                        trackCandidatesBaseDC_->const_view(),
                        trackCandidatesExtendedDC_->const_view(),
                        chainsDC_->const_view(),
                        plsOwned_buf.data(),
                        xcHash_buf.data(),
                        anchorPls_buf.data(),
                        pixelSize_,
                        nHits,
                        nAllocatedTCs,
                        xcRetired_buf.data(),
                        xcStats_buf.data());
    alpaka::exec<Acc1D>(queue_,
                        chainFlat_workDiv,
                        ChainXcChainArm{},
                        chainsDC_->const_view(),
                        plsOwned_buf.data(),
                        xcPairs_buf.data(),
                        xcCursor_buf.data(),
                        kXcPairCap,
                        pixelSize_,
                        xcRetired_buf.data(),
                        xcStats_buf.data());
  }
  auto const t4e = stamp();

  // ---- Final carried-row retirement (K8d) ----------------------------------------------------
  // The contention / -RPS / -XC verdicts applied to the carried bare-pLS rows, with the chain
  // rows (the last nChainTCs positions) kept verbatim. This is the reference's last
  // m16RefreshSupp plus the -XC type-8 channel, applied once, at the end.
  {
    uint32_t const nIn = nAllocatedTCs;
    auto keep_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, std::max(1u, nIn));
    auto offs_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, nIn + 1u);
    auto total_buf = cms::alpakatools::make_device_buffer<uint32_t>(queue_);
    auto class_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, 5u);
    auto stage_buf = cms::alpakatools::make_device_buffer<ChainTCRowPayload[]>(queue_, std::max(1u, nIn));
    alpaka::memset(queue_, class_buf, 0u);
    alpaka::exec<Acc1D>(queue_,
                        chainFlat_workDiv,
                        ChainTCKeepSuppress{},
                        trackCandidatesBaseDC_->const_view(),
                        trackCandidatesExtendedDC_->const_view(),
                        chainsDC_->const_view(),
                        plsOwned_buf.data(),
                        plsBestChain_buf.data(),
                        plsBestT3_buf.data(),
                        xcRetired_buf.data(),
                        (chainConfig_.dupMutualDelta >= 0.f) ? plsMutual_buf.data() : nullptr,
                        pixelSize_,
                        keep_buf.data(),
                        class_buf.data(),
                        nIn,
                        postStats_buf.data(),
                        chainConfig_);
    // -CC9 (R3): the T4-class crossclean against the delivered seeded rows, applied to the KEEP
    // array before the prefix so it needs no second compaction. Two passes over the same rows:
    // publish the surviving seeded rows' outer-tracker hits, then clear the keep bit of any type-9
    // row sharing a full mini-doublet with one of them.
    if (chainConfig_.cc9MinShared > 0) {
      auto cc9Key_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, chaincc9::kSlots);
      auto cc9Val_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, chaincc9::kSlots);
      alpaka::memset(queue_, cc9Key_buf, 0xFF);
      alpaka::exec<Acc1D>(queue_,
                          chainFlat_workDiv,
                          ChainCc9Publish{},
                          trackCandidatesBaseDC_->const_view(),
                          trackCandidatesExtendedDC_->const_view(),
                          keep_buf.data(),
                          nIn,
                          cc9Key_buf.data(),
                          cc9Val_buf.data(),
                          postStats_buf.data());
      alpaka::exec<Acc1D>(queue_,
                          chainFlat_workDiv,
                          ChainCc9Apply{},
                          trackCandidatesBaseDC_->const_view(),
                          trackCandidatesExtendedDC_->const_view(),
                          chainsDC_->view(),
                          cc9Key_buf.data(),
                          cc9Val_buf.data(),
                          keep_buf.data(),
                          class_buf.data(),
                          nIn,
                          postStats_buf.data(),
                          chainConfig_);
    }
    chainScanTimed(timing, __LINE__,
        queue_, chainScan_workDiv, ChainSegPrefix{}, keep_buf.data(), offs_buf.data(), total_buf.data(), nIn);
    alpaka::exec<Acc1D>(queue_,
                        chainFlat_workDiv,
                        ChainTCGather{},
                        trackCandidatesBaseDC_->const_view(),
                        trackCandidatesExtendedDC_->const_view(),
                        keep_buf.data(),
                        offs_buf.data(),
                        nIn,
                        stage_buf.data());
    alpaka::exec<Acc1D>(queue_,
                        chainFlat_workDiv,
                        ChainTCScatter{},
                        trackCandidatesBaseDC_->view(),
                        trackCandidatesExtendedDC_->view(),
                        stage_buf.data(),
                        offs_buf.data(),
                        nIn);
    alpaka::exec<Acc1D>(queue_,
                        serial_workDiv,
                        ChainTCFinishSuppress{},
                        trackCandidatesBaseDC_->view(),
                        trackCandidatesExtendedDC_->view(),
                        offs_buf.data(),
                        class_buf.data(),
                        nIn);
  }

  auto const t5 = stamp();

  if (timing || objectsStatistics_) {
    auto stats_h = cms::alpakatools::make_host_buffer<uint32_t[]>(queue_, kChainArbStats);
    auto post_h = cms::alpakatools::make_host_buffer<uint32_t[]>(queue_, chainattacht3::kStats);
    auto nAcc_h = cms::alpakatools::make_host_buffer<uint32_t>(queue_);
    auto nTC_h = cms::alpakatools::make_host_buffer<uint32_t>(queue_);
    alpaka::memcpy(queue_, post_h, postStats_buf);
    alpaka::memcpy(queue_, stats_h, stats_buf);
    alpaka::memcpy(queue_, nAcc_h, cms::alpakatools::make_device_view(queue_, chainsDC_->view().nAccepted()));
    alpaka::memcpy(queue_, nTC_h, cms::alpakatools::make_device_view(queue_, chainsDC_->view().nChainTCs()));
    alpaka::wait(queue_);
    uint32_t const* s = stats_h.data();
    lstWarning(
        std::format("[CHAIN K9] accepted={} chainTCs={} | TC slot fallbacks={} overflow={} | tieK9order={} | "
                    "R3 claimRounds={} claimStuck={} capHit={}",
                    *nAcc_h.data(),
                    *nTC_h.data(),
                    s[7],
                    s[8],
                    s[9],
                    s[11],
                    s[12],
                    s[13]));
    if (timing) {
      auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
      lstWarning(
          std::format("[CHAIN TIMING] compact {:.3f} ms | K9 prep {:.3f} ms | K9 claim {:.3f} ms | "
                      "K8 attach {:.3f} ms | K10 rows {:.3f} ms | "
                      "K10 emit {:.3f} ms | T3CC {:.3f} ms | XC {:.3f} ms | suppress {:.3f} ms | "
                      "total {:.3f} ms",
                      ms(t0, t1),
                      ms(t1, t2),
                      ms(t2, t3),
                      ms(t3, t3b),
                      ms(t4, t4b),
                      ms(t4b, t4c),
                      ms(t4c, t4d),
                      ms(t4d, t4e),
                      ms(t4e, t5),
                      ms(t0, t5)));
      lstWarning(std::format(
          "[CHAIN T4] nCand={} nChains={} nAllocTC={} nBareT3={} nDeliv={} "
          // Completes U5's slot-6 fix, which its exported patch left half-done: slot 6 is the
          // RETIREMENT CENSUS (one atomicAdd per -RPS/-XC retired row), which is all it ever
          // counted, and the -CC sweep's must-be-zero TC-row alarm now has slot 21 to itself.
          // Printing only slot 6 as "ccOverflow" is what made the alarm unreadable at ~700/event.
          "rpsRetired={} ccOverflow={} | "
          "K9: alloc+memset {:.3f} prefix {:.3f} scatter {:.3f} RANK {:.3f} preclaim {:.3f} ROUNDS {:.3f} | "
          "T3CC: prefix {:.3f} PREP {:.3f} SWEEP+EMIT {:.3f} (emit={} ccRevoked={})",
          nCandDiag,
          nChainCount_,
          nAllocatedTCs,
          nBareT3_,
          nDelivDiag,
          post_h.data()[6],
          post_h.data()[21],
          ms(t2, k9a),
          ms(k9a, k9b),
          ms(k9b, k9c),
          ms(k9c, k9d),
          ms(k9d, k9e),
          ms(k9e, t3),
          ms(t4c, cc1),
          ms(cc1, cc2),
          ms(cc2, cc3),
          post_h.data()[4],
          post_h.data()[9]));
      lstWarning(std::format("[CHAIN K8] {}", attachSummary_));
    }
  }
}

void LSTEvent::attachPixels(unsigned int nHits,
                            uint32_t const* accepted,
                            AttachPlsPre const* plsPre,
                            uint8_t* plsOwned,
                            uint32_t* plsBestChain,
                            uint32_t* hashKey,
                            int32_t* hashVal,
                            ChainXcPair* xcPairs,
                            uint32_t* xcCursor,
                            uint32_t xcCap,
                            uint8_t* plsMutual) {
  // Chain-tracking phase P2.4 (port map section 5, K8a-K8c) at the CHAINFINAL2 flags
  // (-A 4 -a 5.0 -a2 5.0 -a3 6.0 -RT5 1 -RT3 1 -RPS 1 -RPSA 5.5 -RD 1 -D4 1e9
  // -XC 3 -XC4 1). Reference: prototype/PixelAttach.cc, prototype/AttachDelivery.cc
  // gaStageChains and the -A 4 blocks of prototype/main.cc. The carried-row retirement (K8d)
  // moved to the end of arbitrateChains -- it must see the -CC and -XC verdicts.
  attachSummary_.clear();
  if (nChainCount_ == 0 || !chainsDC_.has_value())
    return;

  bool const timing = chainTimingEnabled();
  auto stamp = [&]() {
    if (timing)
      alpaka::wait(queue_);
    return std::chrono::steady_clock::now();
  };
  auto const a0 = stamp();

  auto const serial_workDiv = cms::alpakatools::make_workdiv<Acc1D>(1, 1);
  auto const chainFlat_workDiv = cms::alpakatools::make_workdiv<Acc1D>(max_blocks, 256);
  auto const chainScan_workDiv = cms::alpakatools::make_workdiv<Acc1D>(1, kChainScanBlockThreads);

  uint32_t const nPls = std::max(1u, pixelSize_);

  // K8-0b: the stage-A target list (accepted, nLayers >= 5, dca-eligible, in K9 accepted order --
  // the contention tie-break reads that order). The per-pLS records arrive pre-built (plsPre).
  auto targets_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, nChainCount_);
  auto nTargets_buf_d = cms::alpakatools::make_device_buffer<uint32_t>(queue_);
  auto tgtKeep_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, nChainCount_);
  auto tgtOffs_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, nChainCount_ + 1u);
  {
    // The same filtered copy of the K9 accepted array, order-preserving through a prefix sum.
    alpaka::exec<Acc1D>(queue_,
                        chainFlat_workDiv,
                        ChainTargetFlags{},
                        chainsDC_->const_view(),
                        accepted,
                        nChainCount_,
                        tgtKeep_buf.data(),
                        chainConfig_);
    chainScanTimed(timing, __LINE__, queue_,
                        chainScan_workDiv,
                        ChainSegPrefix{},
                        tgtKeep_buf.data(),
                        tgtOffs_buf.data(),
                        nTargets_buf_d.data(),
                        nChainCount_);
    alpaka::exec<Acc1D>(queue_,
                        chainFlat_workDiv,
                        ChainCompactSelect{},
                        tgtKeep_buf.data(),
                        tgtOffs_buf.data(),
                        nChainCount_,
                        accepted,
                        targets_buf.data(),
                        nullptr);  // the prefix above already published the count
  }
  auto const a0b = stamp();
  // A15 -XC4: the aux 4-layer accepted targets, appended after the stage-A list. They
  // are score-only (never delivered) but they join the GRID BOUNDS so the measured radial hull
  // covers them -- the superset argument transfers, and the stage-A scored-pair set is unchanged
  // (a wider hull only adds candidates that the exact predicate re-filters identically).
  auto nTgtAll_buf_d = cms::alpakatools::make_device_buffer<uint32_t>(queue_);
  alpaka::exec<Acc1D>(queue_,
                      serial_workDiv,
                      ChainAttachSelectAux{},
                      chainsDC_->const_view(),
                      accepted,
                      nTargets_buf_d.data(),
                      targets_buf.data(),
                      nTgtAll_buf_d.data(),
                      chainConfig_);
  auto const a0c = stamp();
  auto nTargets_buf_h = cms::alpakatools::make_host_buffer<uint32_t>(queue_);
  auto nTgtAll_buf_h = cms::alpakatools::make_host_buffer<uint32_t>(queue_);
  alpaka::memcpy(queue_, nTargets_buf_h, nTargets_buf_d);
  alpaka::memcpy(queue_, nTgtAll_buf_h, nTgtAll_buf_d);
  alpaka::wait(queue_);  // the target counts size every attach buffer below
  uint32_t const nTargets = *nTargets_buf_h.data();
  uint32_t const nTgtAll = *nTgtAll_buf_h.data();
  if (nTargets == 0 || pixelSize_ == 0)
    return;
  auto const a0d = stamp();

  auto tgtPre_buf = cms::alpakatools::make_device_buffer<AttachTargetPre[]>(queue_, nTgtAll);
  alpaka::exec<Acc1D>(queue_,
                      chainFlat_workDiv,
                      ChainAttachTargetPre{},
                      // U5 dropped segments / triplets / nodes from this kernel with the tcEta /
                      // tcPhi block; they had no other use in it. Its exported patch carried the
                      // signature change but not this call site.
                      miniDoubletsDC_->const_view().miniDoublets(),
                      chainItemsDC_->const_view(),
                      chainsDC_->const_view(),
                      targets_buf.data(),
                      nTgtAll,
                      tgtPre_buf.data());
  auto const a1 = stamp();

  // K8a: THE ONE GRID, over the UNION of this stage's hull (all targets, 5+ plus the aux 4-layer
  // list) and the bare-T3 hull. Built here so both stages share it -- see buildAttachGrid.
  buildAttachGrid(plsPre,
                  tgtPre_buf.data(),
                  nTgtAll,
                  bareT3TgtPre_.has_value() ? bareT3TgtPre_->data() : nullptr,
                  bareT3TgtPre_.has_value() ? nBareT3_ : 0u);
  if (!attachGridOffs_.has_value())
    return;
  uint32_t const nEntries = attachGridEntries_;
  auto& offsets_buf = *attachGridOffs_;
  auto& items_buf = *attachGridItems_;
  auto const a2 = stamp();

  // K8b: candidate iteration, the exact analytic predicate, the 19 features and the r2 head.
  // plsBestChain is written for EVERY scored pair before the banded threshold (invariant I4); the
  // -XC pass-1 candidates are appended here too, for the stage-A 5+ targets AND for the aux
  // 4-layer (-XC4) tail of the same target array, which is score-only.
  auto stats_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, chainattach::kStats);
  auto tgtPls_buf = cms::alpakatools::make_device_buffer<int32_t[]>(queue_, nTargets);
  auto tgtLogit_buf = cms::alpakatools::make_device_buffer<float[]>(queue_, nTargets);
  auto tgtKey_buf = cms::alpakatools::make_device_buffer<uint64_t[]>(queue_, nTargets);
  // JET ROUND 2 (D): the pre-threshold argmax key, for the mutual-best retirement test. 8 B per
  // target (~10 kB at PU200) and it is never written unless cfg.dupMutualDelta >= 0.
  auto tgtKeyPre_buf = cms::alpakatools::make_device_buffer<uint64_t[]>(queue_, nTargets);
  alpaka::memset(queue_, stats_buf, 0u);
  alpaka::memset(queue_, tgtKey_buf, 0);  // key 0 = "no pair reached the margin"
  alpaka::memset(queue_, tgtKeyPre_buf, 0);
  // The device slice count (see ChainAttachScore): nS threads cooperate on one target's candidate
  // walk, so the launch has to be nTargets * nS wide instead of nTargets wide. Host backends run
  // one thread per target and ignore it.
  uint32_t const attachSlices =
      cms::alpakatools::requires_single_thread_per_block_v<Acc1D> ? 1u : chainAttachScoreSlices();
  // nTgtAll, not nTargets: the launch carries the aux 4-layer target list too (T1's -XC4 fold), and
  // each target's candidate walk is sliced attachSlices ways (T2). uniform_elements would stride over
  // any shortfall, but sizing it correctly is what keeps the aux threads from serialising onto others.
  auto const attachScore_workDiv = cms::alpakatools::make_workdiv<Acc1D>(
      std::max<uint32_t>(max_blocks, (nTgtAll * attachSlices + 255u) / 256u), 256);
  alpaka::exec<Acc1D>(queue_,
                      attachScore_workDiv,
                      ChainAttachScore{},
                      plsPre,
                      tgtPre_buf.data(),
                      nTargets,
                      nTgtAll,
                      offsets_buf.data(),
                      items_buf.data(),
                      tgtKey_buf.data(),
                      tgtKeyPre_buf.data(),
                      plsBestChain,
                      xcPairs,
                      xcCursor,
                      xcCap,
                      stats_buf.data(),
                      attachSlices,
                      chainConfig_);
  alpaka::exec<Acc1D>(queue_,
                      chainFlat_workDiv,
                      ChainAttachUnpackBest{},
                      tgtKey_buf.data(),
                      static_cast<uint32_t const*>(nullptr),  // stage A has no "scored" census
                      tgtPls_buf.data(),
                      tgtLogit_buf.data(),
                      nTargets,
                      stats_buf.data(),
                      tgtKeyPre_buf.data(),
                      static_cast<uint32_t const*>(plsBestChain),
                      (chainConfig_.dupMutualDelta >= 0.f) ? plsMutual : nullptr,
                      pixelSize_);
  auto const a3 = stamp();

  // K8c: contention and the -RD seed dedup.
  auto order_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, nTargets);
  // Split point of the contend stage: up to it the one-pLS-one-owner argmax, after it the -RD
  // seed-family dedup, whose hash walk is the one piece of P2.4 that stays sequential.
  auto a3rd = a3;
  // Inside the -RD window: after the gather + concurrent table build, after the parallel partner
  // prefilter, after the serial greedy. The remainder of the window is ChainAttachPublish.
  // (T3's plsOwnerChain buffer is NOT reinstated -- it existed only for -CCS, which T1 deleted.)
  auto a3ow = a3;
  auto a3cf = a3;
  auto a3sd = a3;
  {
    // P2.6a. The one-pLS-one-owner rule is an argmax, so it becomes a packed atomicMax; the -RD
    // visiting order is the gathered ascending-position (K9 accepted) order -- zero sorts, simp
    // change 3, replacing the reference's O(n^2) selection sort (7-10 ms per event on the device,
    // all of it one thread chasing a global load per comparison). The -RD hash walk is split into
    // a concurrent table build, a parallel partner prefilter and a greedy over the few flagged
    // owners -- see ChainAttachSeedDedup for why that leaves the same verdicts.
    auto plsKey_buf = cms::alpakatools::make_device_buffer<uint64_t[]>(queue_, nPls);
    auto ownKeep_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, nTargets);
    auto ownOffs_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, nTargets + 1u);
    auto nOwners_buf = cms::alpakatools::make_device_buffer<uint32_t>(queue_);
    auto ownerHits_buf =
        cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, size_t{nTargets} * kMaxPLSHitsInHitsSoA);
    auto ownerNHits_buf = cms::alpakatools::make_device_buffer<uint8_t[]>(queue_, nTargets);
    auto ownerPls_buf = cms::alpakatools::make_device_buffer<int32_t[]>(queue_, nTargets);
    // The owner index behind each -RD table entry, and the prefilter's per-owner verdict. Neither
    // needs initialising: hashOwner is only ever read at a slot whose key matched (so the same
    // thread wrote it), and cont is written for every owner before it is read.
    // ZERO extra per-event allocations (see rdHashOwner_ in LSTEvent.h for why that matters):
    // the owner-tag array is the persistent member, and the per-owner prefilter verdict BORROWS
    // ownOffs_buf, which is dead the moment ChainCompactSelect has consumed it.
    uint32_t* const hashOwner = rdHashOwner_->data();
    uint32_t* const ownCont = ownOffs_buf.data();

    alpaka::memset(queue_, plsKey_buf, 0);  // no bid yet; plsOwned is already zero (see the caller)
    alpaka::exec<Acc1D>(queue_,
                        chainFlat_workDiv,
                        ChainAttachArgmax{},
                        tgtPls_buf.data(),
                        tgtLogit_buf.data(),
                        nTargets,
                        plsKey_buf.data());
    alpaka::exec<Acc1D>(queue_,
                        chainFlat_workDiv,
                        ChainAttachResolve{},
                        chainsDC_->view(),
                        targets_buf.data(),
                        tgtPls_buf.data(),
                        tgtLogit_buf.data(),
                        nTargets,
                        plsKey_buf.data(),
                        ownKeep_buf.data());
    a3rd = stamp();
    if (chainConfig_.attachSeedDedup) {
      chainScanTimed(timing, __LINE__, queue_,
                          chainScan_workDiv,
                          ChainSegPrefix{},
                          ownKeep_buf.data(),
                          ownOffs_buf.data(),
                          nOwners_buf.data(),
                          nTargets);
      alpaka::exec<Acc1D>(queue_,
                          chainFlat_workDiv,
                          ChainCompactSelect{},
                          ownKeep_buf.data(),
                          ownOffs_buf.data(),
                          nTargets,
                          targets_buf.data(),
                          order_buf.data(),
                          nullptr);
      // Zero-sorts (simp change 3): the -RD walk visits the gathered ascending-position (K9
      // accepted) order directly; the rank kernel is gone.
      alpaka::exec<Acc1D>(queue_,
                          chainFlat_workDiv,
                          ChainAttachOwnerHits{},
                          lstInputDC_->const_view().pixelSeeds(),
                          lstInputDC_->const_view().hits(),
                          chainsDC_->const_view(),
                          static_cast<int32_t const*>(nullptr),  // stage A: the grant is on the chain row
                          order_buf.data(),
                          nOwners_buf.data(),
                          nTargets,
                          nHits,
                          ownerHits_buf.data(),
                          ownerNHits_buf.data(),
                          ownerPls_buf.data(),
                          hashKey,  // stage A builds the -RD table here, concurrently
                          hashVal,
                          hashOwner,
                          stats_buf.data(),
                          chainattach::kSeedOwnerStageA);
      a3ow = stamp();
      alpaka::exec<Acc1D>(queue_,
                          chainFlat_workDiv,
                          ChainAttachSeedConflicts{},
                          nOwners_buf.data(),
                          ownerHits_buf.data(),
                          ownerNHits_buf.data(),
                          ownerPls_buf.data(),
                          hashKey,
                          hashOwner,
                          nTargets,
                          ownCont,
                          stats_buf.data(),
                          chainattach::kSeedOwnerStageA);
      a3cf = stamp();
      alpaka::exec<Acc1D>(queue_,
                          serial_workDiv,
                          ChainAttachSeedDedup{},
                          chainsDC_->view(),
                          order_buf.data(),
                          nOwners_buf.data(),
                          ownerHits_buf.data(),
                          ownerNHits_buf.data(),
                          ownerPls_buf.data(),
                          ownCont,
                          hashKey,
                          hashOwner,
                          stats_buf.data());
      a3sd = stamp();
      // The brute-force check that the prefilter flagged every revocable owner. O(nOwners^2), so
      // it is gated like the grid superset audit; stats[15] must come back zero.
      char const* rdAudit = std::getenv("LST_CHAIN_RD_AUDIT");
      if (rdAudit != nullptr && *rdAudit != '\0' && *rdAudit != '0') {
        alpaka::exec<Acc1D>(queue_,
                            chainFlat_workDiv,
                            ChainAttachSeedAudit{},
                            nOwners_buf.data(),
                            ownerHits_buf.data(),
                            ownerNHits_buf.data(),
                            ownerPls_buf.data(),
                            ownCont,
                            nTargets,
                            stats_buf.data());
        a3sd = stamp();
      }
    }
    alpaka::exec<Acc1D>(queue_,
                        chainFlat_workDiv,
                        ChainAttachPublish{},
                        chainsDC_->const_view(),
                        static_cast<uint32_t const*>(targets_buf.data()),
                        static_cast<int32_t const*>(nullptr),  // stage A: the grant lives on the chain row
                        nTargets,
                        plsOwned,
                        stats_buf.data());
  }
  auto const a3b = stamp();

  attachGridAudit(nTargets, plsPre, tgtPre_buf.data(), offsets_buf.data(), items_buf.data());

  if (timing || objectsStatistics_) {
    auto stats_h = cms::alpakatools::make_host_buffer<uint32_t[]>(queue_, chainattach::kStats);
    alpaka::memcpy(queue_, stats_h, stats_buf);
    alpaka::wait(queue_);
    uint32_t const* st = stats_h.data();
    auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
    attachSummary_ = std::format(
        "targets={} (+{} aux4L) pLS={} gridEntries={} cand={} dup={} scored={} picks={} attached={} "
        "rdRevoked={} xcOverflow={} hashOverflow={} tieRD={} rdCont={} rdMaxEnt={} "
        "rdAuditBad={} | pre {:.3f} ms (p1tgtlist {:.3f} p2aux1thr {:.3f} p3drain {:.3f} "
        "p4tgtpre {:.3f}) | bpre {:.3f} ms | "
        "grid {:.3f} ms ({}) "
        "| score {:.3f} ms | contend {:.3f} ms | RDdedup {:.3f} ms "
        "(gather+table {:.3f} prefilter {:.3f} greedy {:.3f} publish {:.3f})",
        nTargets,
        nTgtAll - nTargets,
        pixelSize_,
        nEntries,
        st[1],
        st[10],
        st[2],
        st[3],
        st[4],
        st[5],
        st[11],
        st[7],
        st[9],
        st[13],
        st[14],
        st[15],
        ms(a0, a1),
        ms(a0, a0b),
        ms(a0b, a0c),
        ms(a0c, a0d),
        ms(a0d, a1),
        bareT3PreMs_,
        attachGridMs_,
        attachGridSummary_,
        ms(a2, a3),
        ms(a3, a3rd),
        ms(a3rd, a3b),
        ms(a3rd, a3ow),
        ms(a3ow, a3cf),
        ms(a3cf, a3sd),
        ms(a3sd, a3b));
  }
}

void LSTEvent::attachGridAudit(unsigned int nTargets,
                               AttachPlsPre const* plsPre,
                               AttachTargetPre const* tgtPre,
                               uint32_t const* offsets,
                               AttachPlsPre const* items) {
  // OFFLINE GRID VERIFICATION (port map section 1.1 parity protocol). Off unless
  // LST_CHAIN_ATTACH_AUDIT is set. Per event it runs the EXHAUSTIVE analytic scan the prototype
  // runs -- every (target, pLS) pair through the frozen windows -- and asserts that every pair the
  // scan accepts is present in the grid's candidate list for that target. It also reports the
  // candidate volume so the grid-vs-scan probe ratio is measured, not assumed.
  char const* on = std::getenv("LST_CHAIN_ATTACH_AUDIT");
  if (on == nullptr || *on == '\0' || *on == '0')
    return;

  auto const chainFlat_workDiv = cms::alpakatools::make_workdiv<Acc1D>(max_blocks, 256);
  auto audit_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, 4u);
  alpaka::memset(queue_, audit_buf, 0u);
  alpaka::exec<Acc1D>(queue_,
                      chainFlat_workDiv,
                      ChainAttachAudit{},
                      plsPre,
                      pixelSize_,
                      tgtPre,
                      nTargets,
                      offsets,
                      items,
                      audit_buf.data(),
                      chainConfig_);
  auto audit_h = cms::alpakatools::make_host_buffer<uint32_t[]>(queue_, 4u);
  alpaka::memcpy(queue_, audit_h, audit_buf);
  alpaka::wait(queue_);
  uint32_t const* a = audit_h.data();
  static std::atomic<uint32_t> auditEvt{0};
  lstWarning(
      std::format("[CHAIN K8 AUDIT] evt={} targets={} pLS={} exactPairs={} gridCand={} "
                  "gridPass={} MISSING={} supersetHolds={}",
                  auditEvt.fetch_add(1),
                  nTargets,
                  pixelSize_,
                  a[0],
                  a[1],
                  a[2],
                  a[3],
                  a[3] == 0u ? "YES" : "NO"));
}

// The stage-B TARGET UNIVERSE ONLY (the old K8B-0a/0b prologue of attachBareT3). It is called
// BEFORE stage A because the shared grid's hull needs both target sets; nothing in it reads a
// stage-A result -- ChainAttachT3MarkConsumed / ChainAttachT3Keep depend on the K9 accepted array,
// the chain items and the node features, all final before the attach block starts.
void LSTEvent::prepareBareT3Targets(uint32_t const* accepted) {
  nBareT3_ = 0;
  bareT3Targets_.reset();
  bareT3TgtPre_.reset();
  bareT3PreMs_ = 0.;
  if (nChainNodes_ == 0 || pixelSize_ == 0 || !chainsDC_.has_value() || !chainNodesDC_.has_value() ||
      !chainItemsDC_.has_value() || !tripletsDC_.has_value() || !segmentsDC_.has_value())
    return;

  bool const timing = chainTimingEnabled();
  auto const b0 = std::chrono::steady_clock::now();
  auto const chainFlat_workDiv = cms::alpakatools::make_workdiv<Acc1D>(max_blocks, 256);
  auto const chainScan_workDiv = cms::alpakatools::make_workdiv<Acc1D>(1, kChainScanBlockThreads);
  uint32_t const nNodes = nChainNodes_;

  // K8B-0a/b: the bare-T3 universe = every triplet no K9-ACCEPTED chain consumed.
  auto consumed_buf = cms::alpakatools::make_device_buffer<uint8_t[]>(queue_, nNodes);
  auto keep_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, nNodes);
  auto offs_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, nNodes + 1u);
  auto nBare_d = cms::alpakatools::make_device_buffer<uint32_t>(queue_);
  alpaka::memset(queue_, consumed_buf, 0x00);
  alpaka::exec<Acc1D>(queue_,
                      chainFlat_workDiv,
                      ChainAttachT3MarkConsumed{},
                      chainsDC_->const_view(),
                      chainItemsDC_->const_view(),
                      accepted,
                      consumed_buf.data());
  alpaka::exec<Acc1D>(queue_,
                      chainFlat_workDiv,
                      ChainAttachT3Keep{},
                      consumed_buf.data(),
                      chainNodesDC_->const_view(),
                      chainConfig_.t3FakeMax,  // -T3F target admission
                      keep_buf.data(),
                      nNodes);
  chainScanTimed(timing, __LINE__,
      queue_, chainScan_workDiv, ChainSegPrefix{}, keep_buf.data(), offs_buf.data(), nBare_d.data(), nNodes);
  auto nBare_h = cms::alpakatools::make_host_buffer<uint32_t>(queue_);
  alpaka::memcpy(queue_, nBare_h, nBare_d);
  alpaka::wait(queue_);  // the target count sizes the two buffers below
  uint32_t const nBare = *nBare_h.data();
  if (nBare == 0)
    return;

  auto targets_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, nBare);
  alpaka::exec<Acc1D>(queue_,
                      chainFlat_workDiv,
                      ChainCompactSelect{},
                      keep_buf.data(),
                      offs_buf.data(),
                      nNodes,
                      nullptr,
                      targets_buf.data(),
                      nullptr);

  auto tgt_buf = cms::alpakatools::make_device_buffer<AttachTargetPre[]>(queue_, nBare);
  alpaka::exec<Acc1D>(queue_,
                      chainFlat_workDiv,
                      ChainAttachT3TargetPre{},
                      miniDoubletsDC_->const_view().miniDoublets(),
                      segmentsDC_->const_view().segments(),
                      tripletsDC_->const_view().triplets(),
                      chainNodesDC_->const_view(),
                      targets_buf.data(),
                      nBare,
                      tgt_buf.data());
  bareT3Targets_.emplace(std::move(targets_buf));
  bareT3TgtPre_.emplace(std::move(tgt_buf));
  nBareT3_ = nBare;
  if (timing) {
    alpaka::wait(queue_);
    bareT3PreMs_ = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - b0).count();
  }
}

// THE ONE K8a GRID. Count / prefix / scatter, no sort anywhere (maintainer policy), over the UNION
// of the two stages' per-r-bin radial hulls -- see the argument on ChainAttachGridBounds and on the
// members in LSTEvent.h. It used to exist TWICE, once per stage, over the same plsPre with the same
// ChainConfig and the same cell layout.
void LSTEvent::buildAttachGrid(AttachPlsPre const* plsPre,
                               AttachTargetPre const* tgtA,
                               uint32_t nA,
                               AttachTargetPre const* tgtB,
                               uint32_t nB) {
  attachGridOffs_.reset();
  attachGridItems_.reset();
  attachGridEntries_ = 0;
  attachGridSummary_.clear();
  attachGridMs_ = 0.;
  uint32_t const nPls = std::max(1u, pixelSize_);
  if (pixelSize_ == 0 || (nA + nB) == 0)
    return;

  bool const timing = chainTimingEnabled();
  auto stamp = [&]() {
    if (timing)
      alpaka::wait(queue_);
    return std::chrono::steady_clock::now();
  };
  auto const g0 = stamp();
  auto const chainFlat_workDiv = cms::alpakatools::make_workdiv<Acc1D>(max_blocks, 256);
  auto const chainScan_workDiv = cms::alpakatools::make_workdiv<Acc1D>(1, kChainScanBlockThreads);
  // The (pLS, r bin) element count of the count / scatter passes, and the launch sized to it.
  uint32_t const gridR = cms::alpakatools::requires_single_thread_per_block_v<Acc1D> ? 1u : chainAttachGridRSlices();
  auto const attachGrid_workDiv =
      cms::alpakatools::make_workdiv<Acc1D>(std::max<uint32_t>(max_blocks, (nPls * gridR + 255u) / 256u), 256);

  auto rMin_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, kAttachRBins);
  auto rMax_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, kAttachRBins);
  alpaka::memset(queue_, rMin_buf, 0xFF);  // 0xFFFFFFFF = "no target in this bin"
  alpaka::memset(queue_, rMax_buf, 0x00);
  alpaka::exec<Acc1D>(queue_,
                      chainFlat_workDiv,
                      ChainAttachGridBounds{},
                      tgtA,
                      nA,
                      tgtB,
                      nB,
                      rMin_buf.data(),
                      rMax_buf.data());

  auto masks_buf = cms::alpakatools::make_device_buffer<uint16_t[]>(queue_, size_t{nPls} * kAttachRBins);
  auto counts_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, kAttachCells);
  auto offsets_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, kAttachCells + 1u);
  auto cursor_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, kAttachCells);
  alpaka::memset(queue_, masks_buf, 0x00);
  alpaka::memset(queue_, counts_buf, 0x00);
  alpaka::memset(queue_, cursor_buf, 0x00);
  auto const g1 = stamp();
  alpaka::exec<Acc1D>(queue_,
                      attachGrid_workDiv,
                      ChainAttachGridCount{},
                      plsPre,
                      pixelSize_,
                      rMin_buf.data(),
                      rMax_buf.data(),
                      masks_buf.data(),
                      counts_buf.data(),
                      gridR,
                      chainConfig_);
  auto const g2 = stamp();
  auto nEntries_buf_d = cms::alpakatools::make_device_buffer<uint32_t>(queue_);
  // U3's per-launch scan instrument covers this call too: with both stages on one grid this is the
  // ONE surviving grid prefix, so it must appear in the [CHAIN SCAN] table like the other 16 sites.
  chainScanTimed(timing,
                 __LINE__,
                 queue_,
                 chainScan_workDiv,
                 ChainSegPrefix{},
                 counts_buf.data(),
                 offsets_buf.data(),
                 nEntries_buf_d.data(),
                 kAttachCells);
  auto const g3 = stamp();
  auto nEntries_buf_h = cms::alpakatools::make_host_buffer<uint32_t>(queue_);
  alpaka::memcpy(queue_, nEntries_buf_h, nEntries_buf_d);
  // The union hull rides down on the SAME drain when the dump is on, so the census costs no extra
  // sync (see chainAttachHullDump).
  bool const hullDump = chainAttachHullDump();
  auto rMin_h = cms::alpakatools::make_host_buffer<uint32_t[]>(queue_, kAttachRBins);
  auto rMax_h = cms::alpakatools::make_host_buffer<uint32_t[]>(queue_, kAttachRBins);
  if (hullDump) {
    alpaka::memcpy(queue_, rMin_h, rMin_buf);
    alpaka::memcpy(queue_, rMax_h, rMax_buf);
  }
  alpaka::wait(queue_);  // the grid payload size
  uint32_t const nEntries = std::max(1u, *nEntries_buf_h.data());
  if (hullDump) {
    std::string h;
    for (int rb = 0; rb < kAttachRBins; ++rb)
      h += (rMin_h.data()[rb] == 0xFFFFFFFFu) ? std::format(" {}:empty", rb)
                                              : std::format(" {}:[{:.2f},{:.2f}]",
                                                            rb,
                                                            std::bit_cast<float>(rMin_h.data()[rb]),
                                                            std::bit_cast<float>(rMax_h.data()[rb]));
    lstWarning(std::format("[U4 HULL U] nA={} nB={} nPls={} entries={} hull{}", nA, nB, pixelSize_, nEntries, h));
  }
  auto const g4 = stamp();

  auto items_buf = cms::alpakatools::make_device_buffer<AttachPlsPre[]>(queue_, nEntries);
  alpaka::exec<Acc1D>(queue_,
                      attachGrid_workDiv,
                      ChainAttachGridScatter{},
                      plsPre,
                      pixelSize_,
                      masks_buf.data(),
                      offsets_buf.data(),
                      cursor_buf.data(),
                      items_buf.data(),
                      gridR,
                      chainConfig_);
  auto const g5 = stamp();
  attachGridOffs_.emplace(std::move(offsets_buf));
  attachGridItems_.emplace(std::move(items_buf));
  attachGridEntries_ = nEntries;
  if (timing || objectsStatistics_) {
    auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
    attachGridMs_ = ms(g0, g5);
    attachGridSummary_ = std::format(
        "g1bounds {:.3f} g2count {:.3f} g3prefix {:.3f} g4drain {:.3f} g5scatter {:.3f}",
        ms(g0, g1),
        ms(g1, g2),
        ms(g2, g3),
        ms(g3, g4),
        ms(g4, g5));
  }
}

void LSTEvent::attachBareT3(unsigned int nHits,
                            AttachPlsPre const* plsPre,
                            uint8_t* plsOwned,
                            uint32_t* plsBestT3,
                            uint32_t* hashKey,
                            int32_t* hashVal) {
  // Stage B of the general attach (src/alpaka/ChainAttachT3.h), production form: scoring the
  // bare-T3 target universe (built by prepareBareT3Targets) against the LIVE ownership array on the
  // SHARED grid (buildAttachGrid), then the stage-B contention and the -RDT dedup (against the -RD
  // table stage A left in hashKey/Val). The surviving owner arrays are kept in the bareT3* members
  // for the -CC sweep + emission that runs after the chain rows are emitted. LST_CHAIN_T3_AUDIT
  // still enables the grid-superset audit on the bare-T3 geometry.
  attachT3Summary_.clear();
  bareT3TgtPls_.reset();
  bareT3TgtLogit_.reset();
  bareT3Keep_.reset();
  if (nBareT3_ == 0 || !bareT3Targets_.has_value() || !bareT3TgtPre_.has_value() || !attachGridOffs_.has_value())
    return;

  float const theta = chainConfig_.attachThetaT3;  // -AT3, GLOBAL (no eta bands)
  ChainConfig const& cfgT3 = chainConfig_;

  bool const timing = chainTimingEnabled();
  auto stamp = [&]() {
    if (timing)
      alpaka::wait(queue_);
    return std::chrono::steady_clock::now();
  };

  auto const serial_workDiv = cms::alpakatools::make_workdiv<Acc1D>(1, 1);
  auto const chainFlat_workDiv = cms::alpakatools::make_workdiv<Acc1D>(max_blocks, 256);
  auto const chainScan_workDiv = cms::alpakatools::make_workdiv<Acc1D>(1, kChainScanBlockThreads);
  uint32_t const nPls = pixelSize_;
  uint32_t const nNodes = nChainNodes_;
  uint32_t const nBare = nBareT3_;
  uint32_t const nEntries = attachGridEntries_;
  auto& tgt_buf = *bareT3TgtPre_;
  auto& goffs_buf = *attachGridOffs_;
  auto& items_buf = *attachGridItems_;
  auto const b2 = stamp();

  // K8B-b: score, against the LIVE ownership array (stage A is final; invariant I4 ordering
  // inside the kernel). The owner-side buffers persist in the bareT3* members: the -CC sweep in
  // arbitrateChains consumes them after the chain rows are emitted.
  bareT3TgtPls_.emplace(cms::alpakatools::make_device_buffer<int32_t[]>(queue_, nBare));
  bareT3TgtLogit_.emplace(cms::alpakatools::make_device_buffer<float[]>(queue_, nBare));
  auto stats_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, chainattacht3::kStats);
  alpaka::memset(queue_, stats_buf, 0u);
  // The sliced scorer reduces into a packed argmax key (see ChainAttachT3Score); tgtScored carries
  // the per-target "scored anything" census the slices can no longer count themselves.
  auto tgtKey_buf = cms::alpakatools::make_device_buffer<uint64_t[]>(queue_, nBare);
  auto tgtScored_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, nBare);
  alpaka::memset(queue_, tgtKey_buf, 0);
  alpaka::memset(queue_, tgtScored_buf, 0u);
  uint32_t const t3Slices =
      cms::alpakatools::requires_single_thread_per_block_v<Acc1D> ? 1u : chainAttachScoreSlices();
  auto const t3Score_workDiv = cms::alpakatools::make_workdiv<Acc1D>(
      std::max<uint32_t>(max_blocks, (nBare * t3Slices + 255u) / 256u), 256);
  alpaka::exec<Acc1D>(queue_,
                      t3Score_workDiv,
                      ChainAttachT3Score{},
                      tgt_buf.data(),
                      nBare,
                      goffs_buf.data(),
                      items_buf.data(),
                      plsOwned,
                      tgtKey_buf.data(),
                      tgtScored_buf.data(),
                      plsBestT3,
                      stats_buf.data(),
                      theta,
                      t3Slices,
                      cfgT3);
  alpaka::exec<Acc1D>(queue_,
                      chainFlat_workDiv,
                      ChainAttachUnpackBest{},
                      tgtKey_buf.data(),
                      tgtScored_buf.data(),
                      bareT3TgtPls_->data(),
                      bareT3TgtLogit_->data(),
                      nBare,
                      stats_buf.data());
  auto const b3 = stamp();

  // K8B-c: contention + the stage-B half of the -RD seed dedup, against the hash table stage A
  // left behind (so a seed already delivering a pT5-class object blocks its siblings here). The
  // winners publish straight into the LIVE plsOwned (invariant I1). Same decomposition as stage
  // A's T7 (argmax key + serial hash residue in the gathered ascending-T3-row order -- zero
  // sorts, simp change 3), one form on both backends; keep[] survives in bareT3Keep_ because
  // after the dedup revocations it flags exactly the DELIVERIES, which is what the -CC sweep's
  // gather in arbitrateChains reads.
  bareT3Keep_.emplace(cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, nBare));
  // The -RDT window's own split points, so the four pieces are attributable separately: after the
  // argmax + compaction, after the gather + concurrent table build, after the parallel partner
  // prefilter, after the serial greedy. The remainder of the window is ChainAttachPublish.
  auto b3c = b3;
  auto b3ow = b3;
  auto b3cf = b3;
  auto b3sd = b3;
  {
    auto plsKey_buf = cms::alpakatools::make_device_buffer<uint64_t[]>(queue_, nPls);
    auto ownOffs_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, nBare + 1u);
    auto nOwners_buf = cms::alpakatools::make_device_buffer<uint32_t>(queue_);
    auto owners_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, nBare);
    auto ownerPls_buf = cms::alpakatools::make_device_buffer<int32_t[]>(queue_, nBare);
    auto ownerHits_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, size_t{nBare} * kMaxPLSHitsInHitsSoA);
    auto ownerNHits_buf = cms::alpakatools::make_device_buffer<uint8_t[]>(queue_, nBare);
    // ZERO extra per-event allocations for the split (the round-1 discipline): the owner tag behind
    // each table entry is the persistent rdHashOwner_ member stage A already allocates, and the
    // per-owner prefilter word BORROWS ownOffs_buf, which is dead the moment ChainCompactSelect has
    // consumed it. Neither needs initialising -- hashOwner is only read at a slot whose key matched
    // (so the inserting thread wrote it) and cont is written for every owner before it is read.
    uint32_t* const hashOwner = rdHashOwner_->data();
    uint32_t* const ownCont = ownOffs_buf.data();
    // The parity sidecar: restores the SHIPPED serial walk (incremental table build inside the
    // greedy) inside this same binary, so an A/B and the seen[]-cap witnesses come from one
    // compilation. Same in-binary-switch discipline as LST_CHAIN_RD_SERIAL was in round 1.
    static bool const rdtSerial = [] {
      char const* e = std::getenv("LST_CHAIN_RDT_SERIAL");
      return e != nullptr && *e != '\0' && *e != '0';
    }();
    bool const rdtSplit = chainConfig_.attachSeedDedup && !rdtSerial;
    alpaka::memset(queue_, plsKey_buf, 0);
    alpaka::exec<Acc1D>(queue_,
                        chainFlat_workDiv,
                        ChainAttachArgmax{},
                        bareT3TgtPls_->data(),
                        bareT3TgtLogit_->data(),
                        nBare,
                        plsKey_buf.data());
    alpaka::exec<Acc1D>(queue_,
                        chainFlat_workDiv,
                        ChainAttachResolve{},
                        chainsDC_->view(),
                        nullptr,  // stage B: the verdicts stay on the position arrays
                        bareT3TgtPls_->data(),
                        bareT3TgtLogit_->data(),
                        nBare,
                        plsKey_buf.data(),
                        bareT3Keep_->data());
    chainScanTimed(timing, __LINE__,
        queue_, chainScan_workDiv, ChainSegPrefix{}, bareT3Keep_->data(), ownOffs_buf.data(), nOwners_buf.data(), nBare);
    alpaka::exec<Acc1D>(queue_,
                        chainFlat_workDiv,
                        ChainCompactSelect{},
                        bareT3Keep_->data(),
                        ownOffs_buf.data(),
                        nBare,
                        nullptr,
                        owners_buf.data(),
                        nullptr);
    b3c = stamp();
    if (chainConfig_.attachSeedDedup) {
      alpaka::exec<Acc1D>(queue_,
                          chainFlat_workDiv,
                          ChainAttachOwnerHits{},
                          lstInputDC_->const_view().pixelSeeds(),
                          lstInputDC_->const_view().hits(),
                          chainsDC_->const_view(),
                          // the same argument TYPES as stage A's call, so the two share ONE ptxas entry
                          static_cast<int32_t const*>(bareT3TgtPls_->data()),  // position-keyed grant
                          owners_buf.data(),
                          nOwners_buf.data(),
                          nBare,
                          nHits,
                          ownerHits_buf.data(),
                          ownerNHits_buf.data(),
                          ownerPls_buf.data(),
                          rdtSplit ? hashKey : static_cast<uint32_t*>(nullptr),  // stage B extends the table
                          rdtSplit ? hashVal : static_cast<int32_t*>(nullptr),
                          rdtSplit ? hashOwner : static_cast<uint32_t*>(nullptr),
                          stats_buf.data(),
                          chainattach::kSeedOwnerStageB);
      b3ow = stamp();
      if (rdtSplit)
        alpaka::exec<Acc1D>(queue_,
                            chainFlat_workDiv,
                            ChainAttachSeedConflicts{},
                            nOwners_buf.data(),
                            ownerHits_buf.data(),
                            ownerNHits_buf.data(),
                            ownerPls_buf.data(),
                            hashKey,
                            hashOwner,
                            nBare,
                            ownCont,
                            stats_buf.data(),
                            chainattach::kSeedOwnerStageB);
      b3cf = stamp();
      alpaka::exec<Acc1D>(queue_,
                          serial_workDiv,
                          ChainAttachT3Dedup{},
                          owners_buf.data(),
                          nOwners_buf.data(),
                          ownerPls_buf.data(),
                          ownerHits_buf.data(),
                          ownerNHits_buf.data(),
                          rdtSplit ? ownCont : nullptr,
                          bareT3TgtPls_->data(),
                          bareT3TgtLogit_->data(),
                          bareT3Keep_->data(),
                          hashKey,
                          hashVal,
                          hashOwner,
                          stats_buf.data(),
                          chainattach::kSeedOwnerStageB);
      b3sd = stamp();
    } else {
      b3ow = b3c;
      b3cf = b3c;
      b3sd = b3c;
    }
    // The surviving grants become the LIVE ownership (invariant I1). One thread per position, and it
    // covers the -RDT-disabled configuration too: a revoked owner and a losing target are both a
    // negative grant.
    alpaka::exec<Acc1D>(queue_,
                        chainFlat_workDiv,
                        ChainAttachPublish{},
                        chainsDC_->const_view(),
                        static_cast<uint32_t const*>(nullptr),
                        static_cast<int32_t const*>(bareT3TgtPls_->data()),  // position-keyed grant
                        nBare,
                        plsOwned,
                        static_cast<uint32_t*>(nullptr));  // stats[4] here is the -CC delivery count
  }
  auto const b4 = stamp();

  if (timing || objectsStatistics_) {
    auto statsH = cms::alpakatools::make_host_buffer<uint32_t[]>(queue_, chainattacht3::kStats);
    alpaka::memcpy(queue_, statsH, stats_buf);
    alpaka::wait(queue_);
    uint32_t const* st = statsH.data();
    auto ms = [](auto a2, auto b) { return std::chrono::duration<double, std::milli>(b - a2).count(); };
    attachT3Summary_ = std::format(
        "bareT3={} ofNodes={} pLS={} gridEntries={} cand={} dup={} scored={} overTheta={} withCand={} "
        "picks={} rdtRevoked={} theta={:.3f} t3FakeMax={:.4g} | "
        "rdtOwners={} rdtCont={} rdtByStageA={} rdtWalked={} rdtMaxEnt={} rdtMaxSeen={} rdtCapDrop={} "
        "rdtIns={} rdtInsRefused={} rdtHashOverflow={} | "
        // No `grid` term: with U4's one union-hull grid there is no stage-B grid left to time. Its
        // cost is in the [CHAIN K8] line's g1..g5 breakdown, charged once to the shared build.
        "pre {:.3f} ms | score {:.3f} ms | contend {:.3f} ms "
        "(argmax+gather {:.3f} table {:.3f} prefilter {:.3f} greedy {:.3f} publish {:.3f})",
        nBare,
        nNodes,
        nPls,
        nEntries,
        st[1],
        st[10],
        st[2],
        st[11],
        st[8],
        st[3],
        st[5],
        theta,
        chainConfig_.t3FakeMax,
        st[19],
        st[13],
        st[12],
        st[20],
        st[14],
        st[15],
        st[16],
        st[17],
        st[18],
        st[7],
        bareT3PreMs_,
        ms(b2, b3),
        ms(b3, b4),
        ms(b3, b3c),
        ms(b3c, b3ow),
        ms(b3ow, b3cf),
        ms(b3cf, b3sd),
        ms(b3sd, b4));
    lstWarning(std::format("[CHAIN K8B] {}", attachT3Summary_));
  }

  // The grid superset audit on the bare-T3 target geometry. Same kernel, same must-be-zero
  // MISSING counter as the chain grid; it is O(nTargets x nPls) so it is separately gated.
  char const* aon = std::getenv("LST_CHAIN_T3_AUDIT");
  if (aon != nullptr && *aon != '\0' && *aon != '0') {
    auto audit_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, 4u);
    alpaka::memset(queue_, audit_buf, 0u);
    alpaka::exec<Acc1D>(queue_,
                        chainFlat_workDiv,
                        ChainAttachAudit{},
                        plsPre,
                        nPls,
                        tgt_buf.data(),
                        nBare,
                        goffs_buf.data(),
                        items_buf.data(),
                        audit_buf.data(),
                        cfgT3);
    auto auditH = cms::alpakatools::make_host_buffer<uint32_t[]>(queue_, 4u);
    alpaka::memcpy(queue_, auditH, audit_buf);
    alpaka::wait(queue_);
    uint32_t const* a = auditH.data();
    static std::atomic<uint32_t> t3AuditEvt{0};
    lstWarning(
        std::format("[CHAIN K8B AUDIT] evt={} bareT3={} pLS={} exactPairs={} gridCand={} "
                    "gridPass={} MISSING={} supersetHolds={}",
                    t3AuditEvt.fetch_add(1),
                    nBare,
                    nPls,
                    a[0],
                    a[1],
                    a[2],
                    a[3],
                    a[3] == 0u ? "YES" : "NO"));
  }
  alpaka::wait(queue_);  // the scratch buffers above die with this scope
}

void LSTEvent::dumpChainTCs() {
  // Parity sidecar for the P2.3 gate (a). Off unless LST_CHAIN_TC_DUMP names an output file.
  // One record per event holding, for EVERY track candidate row, its type and its outer-tracker
  // hit rows in the tracking-ntuple numbering -- exactly the content the reference writes into its
  // tc_type / tc_hitOT branches under PROTO_DUMP_TCHITS, so the two multisets compare directly.
  char const* path = std::getenv("LST_CHAIN_TC_DUMP");
  if (path == nullptr || *path == '\0' || !trackCandidatesBaseDC_.has_value())
    return;

  alpaka::wait(queue_);

  auto base = getTrackCandidatesBase();
  auto ext = getTrackCandidatesExtended();
  uint32_t const nTC = base.nTrackCandidates();

  unsigned int const nHitsTotal = static_cast<unsigned int>(lstInputDC_->const_view().hits().metadata().size());
  std::vector<unsigned int> hitIdx(nHitsTotal);
  {
    auto host_view = cms::alpakatools::make_host_view(hitIdx.data(), nHitsTotal);
    auto dev_view = cms::alpakatools::make_device_view(queue_, lstInputDC_->const_view().hits().idxs(), nHitsTotal);
    alpaka::memcpy(queue_, host_view, dev_view);
    alpaka::wait(queue_);
  }

  static std::atomic<uint32_t> tcEventCounter{0};
  uint32_t const ievt = tcEventCounter.fetch_add(1);
  static std::mutex tcDumpMutex;
  std::lock_guard<std::mutex> lock(tcDumpMutex);
  std::FILE* f = std::fopen(path, (ievt == 0) ? "wb" : "ab");
  if (f == nullptr)
    return;
  auto put32 = [&](uint32_t v) { std::fwrite(&v, sizeof(v), 1, f); };

  put32(0x50323354u);  // 'P23T'
  put32(ievt);
  put32(nTC);
  for (uint32_t t = 0; t < nTC; ++t) {
    std::vector<uint32_t> ot;
    for (int s = 0; s < Params_TC::kLayers; ++s) {
      if (ext.lowerModuleIndices()[t][s] == kTCEmptyLowerModule)
        continue;
      if (ext.logicalLayers()[t][s] == 0)
        continue;  // pixel layer slot
      for (int q = 0; q < Params_TC::kHitsPerLayer; ++q) {
        unsigned int const h = base.hitIndices()[t][s][q];
        if (h == kTCEmptyHitIdx)
          continue;
        ot.push_back(hitIdx[h]);
      }
    }
    put32(static_cast<uint32_t>(base.trackCandidateType()[t]));
    put32(static_cast<uint32_t>(ot.size()));
    for (uint32_t h : ot)
      put32(h);
  }
  std::fclose(f);
}

void LSTEvent::dumpChains() {
  // Parity sidecar for the P2.2 gate. Off unless LST_CHAIN_CHAIN_DUMP names an output file; the
  // ntuple and the track candidate collection are untouched either way. Record layout is
  // documented in standalone/p22_ref/p22_ref_dump.cc.
  char const* path = std::getenv("LST_CHAIN_CHAIN_DUMP");
  if (path == nullptr || *path == '\0' || !chainsDC_.has_value())
    return;

  alpaka::wait(queue_);

  auto pullTo = [&](auto* hostPtr, auto column, unsigned int n) {
    auto host_view = cms::alpakatools::make_host_view(hostPtr, n);
    auto dev_view = cms::alpakatools::make_device_view(queue_, column, n);
    alpaka::memcpy(queue_, host_view, dev_view);
    alpaka::wait(queue_);
  };

  uint32_t const nC = nChainCount_;
  uint32_t const nItems = 3u * nChainWeldedNodes_;
  auto ch = chainsDC_->view();
  auto it = chainItemsDC_->view();

  std::vector<uint32_t> nodeOffset(nC);
  std::vector<uint16_t> nNodes(nC), nMDs(nC);
  std::vector<uint8_t> nLayers(nC), flags(nC);
  std::vector<int8_t> branch(nC), trimAction(nC);
  std::vector<float> score(nC), dcaXY(nC), zF(nC), zP(nC), zD(nC), mP(nC), mD(nC), mX(nC);
  std::vector<float> feats(static_cast<size_t>(nC) * Params_ChainFeat::kFeatures);
  pullTo(nodeOffset.data(), ch.nodeOffset(), nC);
  pullTo(nNodes.data(), ch.nNodes(), nC);
  pullTo(nMDs.data(), ch.nMDs(), nC);
  pullTo(nLayers.data(), ch.nLayers(), nC);
  pullTo(flags.data(), ch.flags(), nC);
  pullTo(branch.data(), ch.branch(), nC);
  pullTo(trimAction.data(), ch.trimAction(), nC);
  pullTo(score.data(), ch.score(), nC);
  pullTo(dcaXY.data(), ch.dcaXY(), nC);
  pullTo(zF.data(), ch.zFake(), nC);
  pullTo(zP.data(), ch.zPrompt(), nC);
  pullTo(zD.data(), ch.zDisp(), nC);
  pullTo(mP.data(), ch.marginP(), nC);
  pullTo(mD.data(), ch.marginD(), nC);
  pullTo(mX.data(), ch.marginX(), nC);
  {
    static_assert(sizeof(Params_ChainFeat::ArrayFxFeat) == sizeof(float) * Params_ChainFeat::kFeatures,
                  "chain feature rows must be densely packed for the debug dump");
    auto host_view =
        cms::alpakatools::make_host_view(reinterpret_cast<Params_ChainFeat::ArrayFxFeat*>(feats.data()), nC);
    auto dev_view = cms::alpakatools::make_device_view(queue_, ch.features(), nC);
    alpaka::memcpy(queue_, host_view, dev_view);
    alpaka::wait(queue_);
  }

  std::vector<uint32_t> nodeItems(nItems), edgeItems(nItems), mdItems(nItems);
  pullTo(nodeItems.data(), it.nodeItems(), nItems);
  pullTo(edgeItems.data(), it.edgeItems(), nItems);
  pullTo(mdItems.data(), it.mdItems(), nItems);

  uint32_t const nEdges = static_cast<uint32_t>(chainEdgesDC_->view().metadata().size());
  std::vector<uint8_t> edgeType(nEdges);
  pullTo(edgeType.data(), chainEdgesDC_->view().type(), nEdges);

  // MD identity for the cross-implementation comparison: the reference numbers MiniDoublets in
  // ntuple order, this side in SoA order, so the stable key is the pair of ph2 hit rows.
  unsigned int const nMDTotal = static_cast<unsigned int>(miniDoubletsDC_->view().miniDoublets().metadata().size());
  unsigned int const nHitsTotal = static_cast<unsigned int>(lstInputDC_->const_view().hits().metadata().size());
  std::vector<unsigned int> mdAnchorHit(nMDTotal), mdOuterHit(nMDTotal), hitIdx(nHitsTotal);
  pullTo(mdAnchorHit.data(), miniDoubletsDC_->view().miniDoublets().anchorHitIndices(), nMDTotal);
  pullTo(mdOuterHit.data(), miniDoubletsDC_->view().miniDoublets().outerHitIndices(), nMDTotal);
  {
    std::vector<unsigned int> tmp(nHitsTotal);
    auto host_view = cms::alpakatools::make_host_view(tmp.data(), nHitsTotal);
    auto dev_view = cms::alpakatools::make_device_view(queue_, lstInputDC_->const_view().hits().idxs(), nHitsTotal);
    alpaka::memcpy(queue_, host_view, dev_view);
    alpaka::wait(queue_);
    hitIdx = std::move(tmp);
  }

  static std::atomic<uint32_t> eventCounter{0};
  uint32_t const ievt = eventCounter.fetch_add(1);
  static std::mutex dumpMutex;
  std::lock_guard<std::mutex> lock(dumpMutex);
  std::FILE* f = std::fopen(path, (ievt == 0) ? "wb" : "ab");
  if (f == nullptr)
    return;
  auto put32 = [&](uint32_t v) { std::fwrite(&v, sizeof(v), 1, f); };
  auto putf = [&](float v) { std::fwrite(&v, sizeof(v), 1, f); };

  put32(0x50323243u);  // 'P22C'
  put32(ievt);
  put32(nChainNodes_);
  put32(nEdges);
  put32(nC);
  for (uint32_t c = 0; c < nC; ++c) {
    uint32_t const off = nodeOffset[c];
    uint32_t const n = nNodes[c];
    uint32_t const m = nMDs[c];
    int const drop = trimAction[c];
    // Pre-trim geometry, recovered from the endpoint move: the untrimmed node run always starts at
    // (off - 1) for an inner drop and at off otherwise, and is one node longer whenever a drop
    // happened. This lets one record carry both the pre-trim weld and the post-trim chain.
    uint32_t const preOff = (drop == 1) ? off - 1u : off;
    uint32_t const preN = (drop == 0) ? n : n + 1u;
    put32(preN);
    put32(n);
    put32(m);
    put32(nLayers[c]);
    put32(static_cast<uint32_t>(static_cast<int32_t>(branch[c])));
    put32(static_cast<uint32_t>(drop));
    put32(flags[c]);
    putf(score[c]);
    putf(dcaXY[c]);
    putf(zF[c]);
    putf(zP[c]);
    putf(zD[c]);
    putf(mP[c]);
    putf(mD[c]);
    putf(mX[c]);
    for (int k = 0; k < Params_ChainFeat::kFeatures; ++k)
      putf(feats[static_cast<size_t>(c) * Params_ChainFeat::kFeatures + k]);
    for (uint32_t k = 0; k < preN; ++k)
      put32(nodeItems[preOff + k]);
    for (uint32_t k = 0; k + 1 < preN; ++k)
      put32(edgeType[edgeItems[preOff + k]]);
    for (uint32_t k = 0; k < m; ++k) {
      uint32_t const md = mdItems[3u * off + k];
      put32(hitIdx[mdAnchorHit[md]]);
      put32(hitIdx[mdOuterHit[md]]);
    }
  }
  std::fclose(f);
}

// TRIM-NN probe sidecar. Emits, in the SAME byte layout dumpChains uses ('P22C', so every existing
// reader and the offline truth join work unchanged), ONE record per TERMINAL VARIANT of every
// PRE-TRIM chain with nNodes >= 3:
//     preN = nNodes = the variant's node count      branch = variant id (0 full, 1 inner-dropped,
//     nMDs / nLayers = the variant's MD union                          2 outer-dropped)
//     score = the variant's COMBINED-FIT chi2       drop   = the chain index within the event
//     dcaXY / logits / margins / features           hits   = the variant's own MD hit list
// so the K6f rule's decision (a ratio of the score column) and the head's decision (the margin
// columns) can be compared to the >= 75% sim-match label of each variant on the same rows.
void LSTEvent::dumpChainVariants(char const* path, uint32_t const* innerMDDev, float const* probeDev) {
  if (path == nullptr || *path == '\0' || !chainsDC_.has_value())
    return;

  alpaka::wait(queue_);

  auto pullTo = [&](auto* hostPtr, auto column, unsigned int n) {
    auto host_view = cms::alpakatools::make_host_view(hostPtr, n);
    auto dev_view = cms::alpakatools::make_device_view(queue_, column, n);
    alpaka::memcpy(queue_, host_view, dev_view);
    alpaka::wait(queue_);
  };

  uint32_t const nC = nChainCount_;
  uint32_t const nItems = 3u * nChainWeldedNodes_;
  auto ch = chainsDC_->view();
  auto it = chainItemsDC_->view();

  std::vector<uint32_t> nodeOffset(nC);
  std::vector<uint16_t> nNodes(nC), nMDs(nC);
  std::vector<uint8_t> nLayers(nC);
  pullTo(nodeOffset.data(), ch.nodeOffset(), nC);
  pullTo(nNodes.data(), ch.nNodes(), nC);
  pullTo(nMDs.data(), ch.nMDs(), nC);
  pullTo(nLayers.data(), ch.nLayers(), nC);

  std::vector<uint32_t> nodeItems(nItems), edgeItems(nItems), mdItems(nItems), innerMD(nItems);
  pullTo(nodeItems.data(), it.nodeItems(), nItems);
  pullTo(edgeItems.data(), it.edgeItems(), nItems);
  pullTo(mdItems.data(), it.mdItems(), nItems);
  pullTo(innerMD.data(), innerMDDev, nItems);

  std::vector<float> probe(static_cast<size_t>(nC) * chaintrim::kProbeWords);
  pullTo(probe.data(), probeDev, static_cast<unsigned int>(probe.size()));

  uint32_t const nEdges = static_cast<uint32_t>(chainEdgesDC_->view().metadata().size());
  std::vector<uint8_t> edgeType(nEdges);
  pullTo(edgeType.data(), chainEdgesDC_->view().type(), nEdges);

  unsigned int const nMDTotal = static_cast<unsigned int>(miniDoubletsDC_->view().miniDoublets().metadata().size());
  unsigned int const nHitsTotal = static_cast<unsigned int>(lstInputDC_->const_view().hits().metadata().size());
  std::vector<unsigned int> mdAnchorHit(nMDTotal), mdOuterHit(nMDTotal), hitIdx(nHitsTotal);
  pullTo(mdAnchorHit.data(), miniDoubletsDC_->view().miniDoublets().anchorHitIndices(), nMDTotal);
  pullTo(mdOuterHit.data(), miniDoubletsDC_->view().miniDoublets().outerHitIndices(), nMDTotal);
  pullTo(hitIdx.data(), lstInputDC_->const_view().hits().idxs(), nHitsTotal);

  uint32_t nRec = 0;
  for (uint32_t c = 0; c < nC; ++c)
    if (nNodes[c] >= 3)
      nRec += chaintrim::kVariants;

  static std::atomic<uint32_t> variantEventCounter{0};
  uint32_t const ievt = variantEventCounter.fetch_add(1);
  static std::mutex variantDumpMutex;
  std::lock_guard<std::mutex> lock(variantDumpMutex);
  std::FILE* f = std::fopen(path, (ievt == 0) ? "wb" : "ab");
  if (f == nullptr)
    return;
  auto put32 = [&](uint32_t v) { std::fwrite(&v, sizeof(v), 1, f); };
  auto putf = [&](float v) { std::fwrite(&v, sizeof(v), 1, f); };

  put32(0x50323243u);  // 'P22C'
  put32(ievt);
  put32(nChainNodes_);
  put32(nEdges);
  put32(nRec);
  for (uint32_t c = 0; c < nC; ++c) {
    if (nNodes[c] < 3)
      continue;
    uint32_t const off = nodeOffset[c];
    float const* row = probe.data() + static_cast<size_t>(c) * chaintrim::kProbeWords;
    for (int v = 0; v < chaintrim::kVariants; ++v) {
      float const* b = row + 3 + v * chaintrim::kVarWords;
      uint32_t const offv = (v == 1) ? off + 1u : off;
      uint32_t const nNv = (v == 0) ? nNodes[c] : nNodes[c] - 1u;
      uint32_t const nMDv = static_cast<uint32_t>(b[0]);
      uint32_t const* mdv = (v == 1) ? &innerMD[3u * off] : &mdItems[3u * off];
      put32(nNv);
      put32(nNv);
      put32(nMDv);
      put32(static_cast<uint32_t>(b[1]));
      put32(static_cast<uint32_t>(v));
      put32(c);
      put32(0u);
      putf(row[v]);  // the variant's combined-fit chi2, in the score slot
      putf(b[2]);    // dcaXY
      putf(b[3]);
      putf(b[4]);
      putf(b[5]);
      putf(b[4] - b[3]);
      putf(b[5] - b[3]);
      putf((b[4] > b[5] ? b[4] : b[5]) - b[3]);
      for (int k = 0; k < Params_ChainFeat::kFeatures; ++k)
        putf(b[6 + k]);
      for (uint32_t k = 0; k < nNv; ++k)
        put32(nodeItems[offv + k]);
      for (uint32_t k = 0; k + 1 < nNv; ++k)
        put32(edgeType[edgeItems[offv + k]]);
      for (uint32_t k = 0; k < nMDv; ++k) {
        uint32_t const md = mdv[k];
        put32(hitIdx[mdAnchorHit[md]]);
        put32(hitIdx[mdOuterHit[md]]);
      }
    }
  }
  std::fclose(f);
}

void LSTEvent::createTrackCandidates(bool no_pls_dupclean, bool tc_pls_triplets) {
  // Post-deletion TC sequence: the chain pipeline is the only outer-tracker track builder. What
  // remains of the baseline sequence is the pLS self-cleaning (CheckHitspLS pass 2 -- pass 1 ran
  // in pixelLineSegmentCleaning), the bare-pLS admission, and arbitrateChains, which welds /
  // arbitrates / attaches the chains and emits every T5- T4- pT5- and pT3-class row.

  if (!no_pls_dupclean) {
    auto const checkHitspLS_workDiv = cms::alpakatools::make_workdiv<Acc2D>({max_blocks * 4, max_blocks / 4}, {16, 16});

    alpaka::exec<Acc2D>(queue_,
                        checkHitspLS_workDiv,
                        CheckHitspLS{},
                        modules_.const_view().modules(),
                        segmentsDC_->const_view().segmentsOccupancy(),
                        lstInputDC_->const_view().pixelSeeds(),
                        pixelSegmentsDC_->view(),
                        true);
  }

  // INSTRUMENT (bookkeeping): seed self-cleaning state after BOTH CheckHitspLS passes, i.e.
  // before CrossCleanpLS overwrites the bitmask.
  if (dupSnapshotsEnabled())
    snapshotByteColumn(
        queue_, pixelSegmentsDC_->view().isDup(), pixelSegmentsDC_->view().metadata().size(), plsIsDupPass2_);

  // Counting kernel: the admitted bare-pLS rows are the only carried class left.
  auto nSurvivingTCs_dev = cms::alpakatools::make_device_buffer<unsigned int>(queue_);
  alpaka::memset(queue_, nSurvivingTCs_dev, 0u);

  auto const countSurvivingTCs_workDiv = cms::alpakatools::make_workdiv<Acc1D>(max_blocks, 256);

  alpaka::exec<Acc1D>(queue_,
                      countSurvivingTCs_workDiv,
                      CountSurvivingTCs{},
                      nLowerModules_,
                      segmentsDC_->const_view().segmentsOccupancy(),
                      lstInputDC_->const_view().pixelSeeds(),
                      pixelSegmentsDC_->const_view(),
                      rangesDC_->const_view(),
                      nSurvivingTCs_dev.data(),
                      tc_pls_triplets);

  auto nSurvivingTCs_host = cms::alpakatools::make_host_buffer<unsigned int>(queue_);
  alpaka::memcpy(queue_, nSurvivingTCs_host, nSurvivingTCs_dev);
  alpaka::wait(queue_);  // wait to get counts before allocation

  constexpr unsigned int nMaxTC = n_max_nonpixel_track_candidates + n_max_pixel_track_candidates;
  // Allocation = admitted bare-pLS rows + one row per welded chain (the K9-accepted set is a
  // subset) + headroom for the stage-B pT3-class deliveries (~120/event measured; the emission
  // sweep guards the bound and counts any overflow).
  unsigned int nTotal = std::min(*nSurvivingTCs_host.data(), nMaxTC);
  nTotal += nChainCount_ + kChainBareT3TCHeadroom;
  if (nTotal == 0)
    nTotal = 1;  // avoid zero-size allocation

  // TC allocation
  trackCandidatesBaseDC_.emplace(queue_, nTotal);
  trackCandidatesBaseDC_->zeroInitialise(queue_);
  trackCandidatesExtendedDC_.emplace(queue_, nTotal);
  trackCandidatesExtendedDC_->zeroInitialise(queue_);
  if (objectsStatistics_) {
    double mb = (alpaka::getExtentProduct(trackCandidatesBaseDC_->buffer()) +
                 alpaka::getExtentProduct(trackCandidatesExtendedDC_->buffer())) /
                1e6;
    memoryAllocatedMB_ += mb;
    lstWarning(std::format("[MEM] TrackCandidates: {} allocated ({:.1f} MB) [dynamic: {} pLS + {} chain headroom]",
                           nTotal,
                           mb,
                           *nSurvivingTCs_host.data(),
                           nChainCount_ + kChainBareT3TCHeadroom));
  }

  // CrossCleanpLS is DELETED (it read the T5 / pT5 / pT3 rows the chain pipeline replaces). The
  // bare-pLS universe is {isQuad && isDup == 0} after both CheckHitspLS self-clean passes -- the
  // reference's -ZP8 6 post-deletion universe. The chain path's own seed crossclean (-XC) and the
  // -RPS retirement act on the admitted rows inside arbitrateChains.
  //
  // INSTRUMENT (bookkeeping): the final admission state (identical to plsIsDupPass2_ now).
  if (dupSnapshotsEnabled())
    snapshotByteColumn(
        queue_, pixelSegmentsDC_->view().isDup(), pixelSegmentsDC_->view().metadata().size(), plsIsDupFinal_);

  auto const addpLSasTrackCandidate_workDiv = cms::alpakatools::make_workdiv<Acc1D>(max_blocks, 384);

  alpaka::exec<Acc1D>(queue_,
                      addpLSasTrackCandidate_workDiv,
                      AddpLSasTrackCandidate{},
                      nLowerModules_,
                      trackCandidatesBaseDC_->view(),
                      trackCandidatesExtendedDC_->view(),
                      segmentsDC_->const_view().segmentsOccupancy(),
                      lstInputDC_->const_view().pixelSeeds(),
                      pixelSegmentsDC_->const_view(),
                      tc_pls_triplets,
                      nTotal);

  // Chain tracking phase P2.3: the carried-row compaction (-RT5 1 plus the T5/T4 class
  // replacement), the K9 hit claim, the chain extension and the K10 assembly. Everything above
  // this line ran exactly as it does at baseline.
  {
    alpaka::wait(queue_);  // fence the pLS admission so the chain-TC stamp attributes correctly
    auto const chainTC0 = std::chrono::steady_clock::now();
    arbitrateChains(nTotal);
    chainTCMs_ = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - chainTC0).count();
  }

  // The TC parity sidecar is written here rather than at the end of arbitrateChains so that it also
  // covers the flag-OFF collection. That leg is what measures the pre-existing LST reproducibility
  // floor at hit level: without it the flag-ON number has nothing to be compared against.
  dumpChainTCs();

  // Check if either n_max_pixel_track_candidates or n_max_nonpixel_track_candidates was reached
  auto nTrackCanTotalHost_buf = cms::alpakatools::make_host_buffer<unsigned int>(queue_);
  alpaka::memcpy(queue_,
                 nTrackCanTotalHost_buf,
                 cms::alpakatools::make_device_view(queue_, (*trackCandidatesBaseDC_)->nTrackCandidates()));
  alpaka::wait(queue_);  // wait to get the value before using it

  auto nTrackCandidatesTotal = *nTrackCanTotalHost_buf.data();
  if (nTrackCandidatesTotal > nTotal) {
    lstWarning(
        "\
        ****************************************************************************************************\n\
        * Track candidates were possibly truncated.                                                        *\n\
        * The dynamically allocated TC buffer was fully used.                                              *\n\
        * Run the code with the WARNINGS flag activated for more details.                                  *\n\
        ****************************************************************************************************");
  }
}

void LSTEvent::pixelLineSegmentCleaning(bool no_pls_dupclean) {
  if (!no_pls_dupclean) {
    auto const checkHitspLS_workDiv = cms::alpakatools::make_workdiv<Acc2D>({max_blocks * 4, max_blocks / 4}, {16, 16});

    alpaka::exec<Acc2D>(queue_,
                        checkHitspLS_workDiv,
                        CheckHitspLS{},
                        modules_.const_view().modules(),
                        segmentsDC_->const_view().segmentsOccupancy(),
                        lstInputDC_->const_view().pixelSeeds(),
                        pixelSegmentsDC_->view(),
                        false);
  }
  // INSTRUMENT (bookkeeping): the seed self-cleaning state after CheckHitspLS pass 1. The
  // second pass ORs bit 2 into the same column and CrossCleanpLS then overwrites it wholesale,
  // so this value is unrecoverable from any later read.
  if (dupSnapshotsEnabled())
    snapshotByteColumn(
        queue_, pixelSegmentsDC_->view().isDup(), pixelSegmentsDC_->view().metadata().size(), plsIsDupSelf_);
}

void LSTEvent::addMiniDoubletsToEventExplicit() {
  auto nMDsCPU_buf = cms::alpakatools::make_host_buffer<unsigned int[]>(queue_, nLowerModules_);
  auto mdsOccupancy = miniDoubletsDC_->const_view().miniDoubletsOccupancy();
  auto nMDs_view =
      cms::alpakatools::make_device_view(queue_, mdsOccupancy.nMDs(), nLowerModules_);  // exclude pixel part
  alpaka::memcpy(queue_, nMDsCPU_buf, nMDs_view, nLowerModules_);

  auto modules = modules_.const_view().modules();

  // FIXME: replace by ES host data
  auto module_subdets_buf = cms::alpakatools::make_host_buffer<short[]>(queue_, nLowerModules_);
  auto module_subdets_view =
      cms::alpakatools::make_device_view(queue_, modules.subdets(), nLowerModules_);  // only lower modules
  alpaka::memcpy(queue_, module_subdets_buf, module_subdets_view, nLowerModules_);

  auto module_layers_buf = cms::alpakatools::make_host_buffer<short[]>(queue_, nLowerModules_);
  auto module_layers_view =
      cms::alpakatools::make_device_view(queue_, modules.layers(), nLowerModules_);  // only lower modules
  alpaka::memcpy(queue_, module_layers_buf, module_layers_view, nLowerModules_);

  alpaka::wait(queue_);  // wait for inputs before using them

  auto const* nMDsCPU = nMDsCPU_buf.data();
  auto const* module_subdets = module_subdets_buf.data();
  auto const* module_layers = module_layers_buf.data();

  for (unsigned int i = 0; i < nLowerModules_; i++) {
    if (nMDsCPU[i] != 0) {
      if (module_subdets[i] == Barrel) {
        n_minidoublets_by_layer_barrel_[module_layers[i] - 1] += nMDsCPU[i];
      } else {
        n_minidoublets_by_layer_endcap_[module_layers[i] - 1] += nMDsCPU[i];
      }
    }
  }
}

void LSTEvent::addSegmentsToEventExplicit() {
  auto nSegmentsCPU_buf = cms::alpakatools::make_host_buffer<unsigned int[]>(queue_, nLowerModules_);
  auto nSegments_buf = cms::alpakatools::make_device_view(
      queue_, segmentsDC_->const_view().segmentsOccupancy().nSegments(), nLowerModules_);
  alpaka::memcpy(queue_, nSegmentsCPU_buf, nSegments_buf, nLowerModules_);

  auto modules = modules_.const_view().modules();

  // FIXME: replace by ES host data
  auto module_subdets_buf = cms::alpakatools::make_host_buffer<short[]>(queue_, nLowerModules_);
  auto module_subdets_view =
      cms::alpakatools::make_device_view(queue_, modules.subdets(), nLowerModules_);  // only lower modules
  alpaka::memcpy(queue_, module_subdets_buf, module_subdets_view, nLowerModules_);

  auto module_layers_buf = cms::alpakatools::make_host_buffer<short[]>(queue_, nLowerModules_);
  auto module_layers_view =
      cms::alpakatools::make_device_view(queue_, modules.layers(), nLowerModules_);  // only lower modules
  alpaka::memcpy(queue_, module_layers_buf, module_layers_view, nLowerModules_);

  alpaka::wait(queue_);  // wait for inputs before using them

  auto const* nSegmentsCPU = nSegmentsCPU_buf.data();
  auto const* module_subdets = module_subdets_buf.data();
  auto const* module_layers = module_layers_buf.data();

  for (unsigned int i = 0; i < nLowerModules_; i++) {
    if (!(nSegmentsCPU[i] == 0)) {
      if (module_subdets[i] == Barrel) {
        n_segments_by_layer_barrel_[module_layers[i] - 1] += nSegmentsCPU[i];
      } else {
        n_segments_by_layer_endcap_[module_layers[i] - 1] += nSegmentsCPU[i];
      }
    }
  }
}

void LSTEvent::addTripletsToEventExplicit() {
  auto tripletsOccupancy = tripletsDC_->const_view().tripletsOccupancy();
  auto nTriplets_view = cms::alpakatools::make_device_view(queue_, tripletsOccupancy.nTriplets(), nLowerModules_);
  auto nTripletsCPU_buf = cms::alpakatools::make_host_buffer<unsigned int[]>(queue_, nLowerModules_);
  alpaka::memcpy(queue_, nTripletsCPU_buf, nTriplets_view);

  auto modules = modules_.const_view().modules();

  // FIXME: replace by ES host data
  auto module_subdets_buf = cms::alpakatools::make_host_buffer<short[]>(queue_, nLowerModules_);
  auto module_subdets_view =
      cms::alpakatools::make_device_view(queue_, modules.subdets(), nLowerModules_);  // only lower modules
  alpaka::memcpy(queue_, module_subdets_buf, module_subdets_view, nLowerModules_);

  auto module_layers_buf = cms::alpakatools::make_host_buffer<short[]>(queue_, nLowerModules_);
  auto module_layers_view =
      cms::alpakatools::make_device_view(queue_, modules.layers(), nLowerModules_);  // only lower modules
  alpaka::memcpy(queue_, module_layers_buf, module_layers_view, nLowerModules_);

  alpaka::wait(queue_);  // wait for inputs before using them

  auto const* nTripletsCPU = nTripletsCPU_buf.data();
  auto const* module_subdets = module_subdets_buf.data();
  auto const* module_layers = module_layers_buf.data();

  for (uint16_t i = 0; i < nLowerModules_; i++) {
    if (nTripletsCPU[i] != 0) {
      if (module_subdets[i] == Barrel) {
        n_triplets_by_layer_barrel_[module_layers[i] - 1] += nTripletsCPU[i];
      } else {
        n_triplets_by_layer_endcap_[module_layers[i] - 1] += nTripletsCPU[i];
      }
    }
  }
}

unsigned int LSTEvent::getNumberOfMiniDoublets() {
  unsigned int miniDoublets = 0;
  for (auto& it : n_minidoublets_by_layer_barrel_) {
    miniDoublets += it;
  }
  for (auto& it : n_minidoublets_by_layer_endcap_) {
    miniDoublets += it;
  }

  return miniDoublets;
}

unsigned int LSTEvent::getNumberOfMiniDoubletsByLayerBarrel(unsigned int layer) {
  return n_minidoublets_by_layer_barrel_[layer];
}

unsigned int LSTEvent::getNumberOfMiniDoubletsByLayerEndcap(unsigned int layer) {
  return n_minidoublets_by_layer_endcap_[layer];
}

unsigned int LSTEvent::getNumberOfSegments() {
  unsigned int segments = 0;
  for (auto& it : n_segments_by_layer_barrel_) {
    segments += it;
  }
  for (auto& it : n_segments_by_layer_endcap_) {
    segments += it;
  }

  return segments;
}

unsigned int LSTEvent::getNumberOfSegmentsByLayerBarrel(unsigned int layer) {
  return n_segments_by_layer_barrel_[layer];
}

unsigned int LSTEvent::getNumberOfSegmentsByLayerEndcap(unsigned int layer) {
  return n_segments_by_layer_endcap_[layer];
}

unsigned int LSTEvent::getNumberOfTriplets() {
  unsigned int triplets = 0;
  for (auto& it : n_triplets_by_layer_barrel_) {
    triplets += it;
  }
  for (auto& it : n_triplets_by_layer_endcap_) {
    triplets += it;
  }

  return triplets;
}

unsigned int LSTEvent::getNumberOfTripletsByLayerBarrel(unsigned int layer) {
  return n_triplets_by_layer_barrel_[layer];
}

unsigned int LSTEvent::getNumberOfTripletsByLayerEndcap(unsigned int layer) {
  return n_triplets_by_layer_endcap_[layer];
}

int LSTEvent::getNumberOfTrackCandidates() {
  auto nTrackCandidates_buf_h = cms::alpakatools::make_host_buffer<unsigned int>(queue_);

  alpaka::memcpy(queue_,
                 nTrackCandidates_buf_h,
                 cms::alpakatools::make_device_view(queue_, (*trackCandidatesBaseDC_)->nTrackCandidates()));
  alpaka::wait(queue_);

  return *nTrackCandidates_buf_h.data();
}

int LSTEvent::getNumberOfPT5TrackCandidates() {
  auto nTrackCandidatesPT5_buf_h = cms::alpakatools::make_host_buffer<unsigned int>(queue_);

  alpaka::memcpy(queue_,
                 nTrackCandidatesPT5_buf_h,
                 cms::alpakatools::make_device_view(queue_, (*trackCandidatesExtendedDC_)->nTrackCandidatespT5()));
  alpaka::wait(queue_);

  return *nTrackCandidatesPT5_buf_h.data();
}

int LSTEvent::getNumberOfPT3TrackCandidates() {
  auto nTrackCandidatesPT3_buf_h = cms::alpakatools::make_host_buffer<unsigned int>(queue_);

  alpaka::memcpy(queue_,
                 nTrackCandidatesPT3_buf_h,
                 cms::alpakatools::make_device_view(queue_, (*trackCandidatesExtendedDC_)->nTrackCandidatespT3()));
  alpaka::wait(queue_);

  return *nTrackCandidatesPT3_buf_h.data();
}

int LSTEvent::getNumberOfPLSTrackCandidates() {
  auto nTrackCandidatesPLS_buf_h = cms::alpakatools::make_host_buffer<unsigned int>(queue_);

  alpaka::memcpy(queue_,
                 nTrackCandidatesPLS_buf_h,
                 cms::alpakatools::make_device_view(queue_, (*trackCandidatesExtendedDC_)->nTrackCandidatespLS()));
  alpaka::wait(queue_);

  return *nTrackCandidatesPLS_buf_h.data();
}

int LSTEvent::getNumberOfPixelTrackCandidates() {
  auto nTrackCandidates_buf_h = cms::alpakatools::make_host_buffer<unsigned int>(queue_);
  auto nTrackCandidatesT5_buf_h = cms::alpakatools::make_host_buffer<unsigned int>(queue_);
  auto nTrackCandidatesT4_buf_h = cms::alpakatools::make_host_buffer<unsigned int>(queue_);

  alpaka::memcpy(queue_,
                 nTrackCandidates_buf_h,
                 cms::alpakatools::make_device_view(queue_, (*trackCandidatesBaseDC_)->nTrackCandidates()));
  alpaka::memcpy(queue_,
                 nTrackCandidatesT5_buf_h,
                 cms::alpakatools::make_device_view(queue_, (*trackCandidatesExtendedDC_)->nTrackCandidatesT5()));
  alpaka::memcpy(queue_,
                 nTrackCandidatesT4_buf_h,
                 cms::alpakatools::make_device_view(queue_, (*trackCandidatesExtendedDC_)->nTrackCandidatesT4()));
  alpaka::wait(queue_);

  return (*nTrackCandidates_buf_h.data()) - (*nTrackCandidatesT5_buf_h.data()) - (*nTrackCandidatesT4_buf_h.data());
}

int LSTEvent::getNumberOfT5TrackCandidates() {
  auto nTrackCandidatesT5_buf_h = cms::alpakatools::make_host_buffer<unsigned int>(queue_);

  alpaka::memcpy(queue_,
                 nTrackCandidatesT5_buf_h,
                 cms::alpakatools::make_device_view(queue_, (*trackCandidatesExtendedDC_)->nTrackCandidatesT5()));
  alpaka::wait(queue_);

  return *nTrackCandidatesT5_buf_h.data();
}

int LSTEvent::getNumberOfT4TrackCandidates() {
  auto nTrackCandidatesT4_buf_h = cms::alpakatools::make_host_buffer<unsigned int>(queue_);

  alpaka::memcpy(queue_,
                 nTrackCandidatesT4_buf_h,
                 cms::alpakatools::make_device_view(queue_, (*trackCandidatesExtendedDC_)->nTrackCandidatesT4()));
  alpaka::wait(queue_);

  return *nTrackCandidatesT4_buf_h.data();
}

template <typename TSoA, typename TDev>
typename TSoA::ConstView LSTEvent::getInput(bool sync) {
  if constexpr (std::is_same_v<TDev, DevHost>) {
    return LSTInputViewAccessor<TSoA>::get(lstInputDC_->const_view());
  } else {
    // In case getTrimmedInput was called first
    if (!lstInputHC_ || lstInputHC_->size()[1] == 0) {
      lstInputHC_.emplace(
          cms::alpakatools::CopyToHost<PortableDeviceCollection<TDev, LSTInputSoA>>::copyAsync(queue_, *lstInputDC_));
      if (sync)
        alpaka::wait(queue_);  // host consumers expect filled data
    }
    return LSTInputViewAccessor<TSoA>::get(lstInputHC_->const_view());
  }
}
template HitsBaseConst LSTEvent::getInput<HitsBaseSoA>(bool);
template PixelSeedsConst LSTEvent::getInput<PixelSeedsSoA>(bool);

template <typename TSoA, typename TDev>
typename TSoA::ConstView LSTEvent::getHits(bool sync) {
  if constexpr (std::is_same_v<TDev, DevHost>) {
    return HitsViewAccessor<TSoA>::get(hitsDC_->const_view());
  } else {
    if (!hitsHC_) {
      hitsHC_.emplace(
          cms::alpakatools::CopyToHost<PortableDeviceCollection<TDev, HitsSoA>>::copyAsync(queue_, *hitsDC_));
      if (sync)
        alpaka::wait(queue_);  // host consumers expect filled data
    }
    return HitsViewAccessor<TSoA>::get(hitsHC_->const_view());
  }
}
template HitsExtendedConst LSTEvent::getHits<HitsExtendedSoA>(bool);
template HitsRangesConst LSTEvent::getHits<HitsRangesSoA>(bool);

template <typename TDev>
ObjectRangesConst LSTEvent::getRanges(bool sync) {
  if constexpr (std::is_same_v<TDev, DevHost>) {
    return rangesDC_->const_view();
  } else {
    if (!rangesHC_) {
      rangesHC_.emplace(
          cms::alpakatools::CopyToHost<PortableDeviceCollection<TDev, ObjectRangesSoA>>::copyAsync(queue_, *rangesDC_));
      if (sync)
        alpaka::wait(queue_);  // host consumers expect filled data
    }
    return rangesHC_->const_view();
  }
}
template ObjectRangesConst LSTEvent::getRanges<>(bool);

template <typename TSoA, typename TDev>
typename TSoA::ConstView LSTEvent::getMiniDoublets(bool sync) {
  if constexpr (std::is_same_v<TDev, DevHost>) {
    return MiniDoubletsViewAccessor<TSoA>::get(miniDoubletsDC_->const_view());
  } else {
    if (!miniDoubletsHC_) {
      miniDoubletsHC_.emplace(
          cms::alpakatools::CopyToHost<PortableDeviceCollection<TDev, MiniDoubletsSoABlocks>>::copyAsync(
              queue_, *miniDoubletsDC_));
      if (sync)
        alpaka::wait(queue_);  // host consumers expect filled data
    }
    return MiniDoubletsViewAccessor<TSoA>::get(miniDoubletsHC_->const_view());
  }
}
template MiniDoubletsConst LSTEvent::getMiniDoublets<MiniDoubletsSoA>(bool);
template MiniDoubletsOccupancyConst LSTEvent::getMiniDoublets<MiniDoubletsOccupancySoA>(bool);

template <typename TSoA, typename TDev>
typename TSoA::ConstView LSTEvent::getSegments(bool sync) {
  if constexpr (std::is_same_v<TDev, DevHost>) {
    return SegmentsViewAccessor<TSoA>::get(segmentsDC_->const_view());
  } else {
    if (!segmentsHC_) {
      segmentsHC_.emplace(cms::alpakatools::CopyToHost<PortableDeviceCollection<TDev, SegmentsSoABlocks>>::copyAsync(
          queue_, *segmentsDC_));
      if (sync)
        alpaka::wait(queue_);  // host consumers expect filled data
    }
    return SegmentsViewAccessor<TSoA>::get(segmentsHC_->const_view());
  }
}
template SegmentsConst LSTEvent::getSegments<SegmentsSoA>(bool);
template SegmentsOccupancyConst LSTEvent::getSegments<SegmentsOccupancySoA>(bool);

template <typename TDev>
PixelSegmentsConst LSTEvent::getPixelSegments(bool sync) {
  if constexpr (std::is_same_v<TDev, DevHost>) {
    return pixelSegmentsDC_->const_view();
  } else {
    if (!pixelSegmentsHC_) {
      pixelSegmentsHC_.emplace(cms::alpakatools::CopyToHost<::PortableCollection<TDev, PixelSegmentsSoA>>::copyAsync(
          queue_, *pixelSegmentsDC_));

      if (sync)
        alpaka::wait(queue_);  // host consumers expect filled data
    }
  }
  return pixelSegmentsHC_->const_view();
}
template PixelSegmentsConst LSTEvent::getPixelSegments<>(bool);

template <typename TSoA, typename TDev>
typename TSoA::ConstView LSTEvent::getTriplets(bool sync) {
  if constexpr (std::is_same_v<TDev, DevHost>) {
    return TripletsViewAccessor<TSoA>::get(tripletsDC_->const_view());
  } else {
    if (!tripletsHC_) {
      tripletsHC_.emplace(cms::alpakatools::CopyToHost<PortableDeviceCollection<TDev, TripletsSoABlocks>>::copyAsync(
          queue_, *tripletsDC_));
      if (sync)
        alpaka::wait(queue_);  // host consumers expect filled data
    }
  }
  return TripletsViewAccessor<TSoA>::get(tripletsHC_->const_view());
}
template TripletsConst LSTEvent::getTriplets<TripletsSoA>(bool);
template TripletsOccupancyConst LSTEvent::getTriplets<TripletsOccupancySoA>(bool);

template <typename TDev>
TrackCandidatesBaseConst LSTEvent::getTrackCandidatesBase(bool sync) {
  if constexpr (std::is_same_v<TDev, DevHost>) {
    return trackCandidatesBaseDC_->const_view();
  } else {
    if (!trackCandidatesBaseHC_) {
      trackCandidatesBaseHC_.emplace(
          cms::alpakatools::CopyToHost<::PortableCollection<TDev, TrackCandidatesBaseSoA>>::copyAsync(
              queue_, *trackCandidatesBaseDC_));

      if (sync)
        alpaka::wait(queue_);  // host consumers expect filled data
    }
  }
  return trackCandidatesBaseHC_->const_view();
}
template TrackCandidatesBaseConst LSTEvent::getTrackCandidatesBase<>(bool);

template <typename TDev>
TrackCandidatesExtendedConst LSTEvent::getTrackCandidatesExtended(bool sync) {
  if constexpr (std::is_same_v<TDev, DevHost>) {
    return trackCandidatesExtendedDC_->const_view();
  } else {
    if (!trackCandidatesExtendedHC_) {
      trackCandidatesExtendedHC_.emplace(
          cms::alpakatools::CopyToHost<::PortableCollection<TDev, TrackCandidatesExtendedSoA>>::copyAsync(
              queue_, *trackCandidatesExtendedDC_));

      if (sync)
        alpaka::wait(queue_);  // host consumers expect filled data
    }
  }
  return trackCandidatesExtendedHC_->const_view();
}
template TrackCandidatesExtendedConst LSTEvent::getTrackCandidatesExtended<>(bool);

template <typename TDev>
ChainsConst LSTEvent::getChains(bool sync) {
  // Host view of the welded chains. The standalone ntuple writer needs it to give a chain TC its
  // pt / eta / phi, which live in ChainsSoA rather than in any object the TC row points at.
  if constexpr (std::is_same_v<TDev, DevHost>) {
    return chainsDC_->const_view();
  } else {
    if (!chainsHC_) {
      chainsHC_.emplace(
          cms::alpakatools::CopyToHost<::PortableCollection<TDev, ChainsSoA>>::copyAsync(queue_, *chainsDC_));
      if (sync)
        alpaka::wait(queue_);  // host consumers expect filled data
    }
  }
  return chainsHC_->const_view();
}
template ChainsConst LSTEvent::getChains<>(bool);

std::unique_ptr<TrackCandidatesBaseDeviceCollection> LSTEvent::releaseTrackCandidatesBaseDeviceCollection() {
  return std::make_unique<TrackCandidatesBaseDeviceCollection>(std::move(trackCandidatesBaseDC_.value()));
}

template <typename TSoA, typename TDev>
typename TSoA::ConstView LSTEvent::getModules(bool sync) {
  if constexpr (std::is_same_v<TDev, DevHost>) {
    return ModulesViewAccessor<TSoA>::get(modules_.const_view());
  } else {
    if (!modulesHC_) {
      modulesHC_.emplace(
          cms::alpakatools::CopyToHost<PortableDeviceCollection<TDev, ModulesSoABlocks>>::copyAsync(queue_, modules_));
      if (sync)
        alpaka::wait(queue_);  // host consumers expect filled data
    }
    return ModulesViewAccessor<TSoA>::get(modulesHC_->const_view());
  }
}
template ModulesConst LSTEvent::getModules<ModulesSoA>(bool);
template ModulesPixelConst LSTEvent::getModules<ModulesPixelSoA>(bool);
