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

// LSTEvent is the per-event driver: it owns every device collection and launches every kernel, so
// this file is where the ORDER of the pipeline lives. Chain tracking replaces LST's post-triplet
// reconstruction (the T4 / T5 / pT3 / pT5 builders) with a graph over triplets -- nodes are
// triplets, edges join triplets that share a detector element, a weld links them into chains, a
// gate judges each chain, and a claim resolves chains competing for the same hits.
//
// The stages, in the order they run, with the member function that launches each:
//
//   createMiniDoublets, createSegmentsWithModuleMap, createTriplets
//     Upstream LST, unchanged through the triplet build. The triplet builder additionally tallies
//     the two chain INCIDENCE tables (how many triplets enter and leave each shared mini-doublet
//     and each shared line segment) because it is already visiting exactly those objects.
//
//   buildChainIncidence  (called at the end of createTriplets)
//     Compacts the module-segmented triplet store into a dense node numbering, prefix-sums the
//     incidence tallies into CSR offsets, and fills the CSR payloads. The edge count follows from
//     the degrees -- sum over keys of degIn * degOut -- so the edge list is allocated exactly.
//
//   buildChainEdges
//     Node features, then the edge list: an edge joins two triplets sharing a mini-doublet (the E1
//     family, giving 5-layer chains) or a line segment (E2, giving 4-layer ones). The edge head
//     scores every edge and resolves its per-family, per-cell weld eligibility bar. Ends by
//     calling buildChains.
//
//   buildChains
//     Weld sweeps: each node argmaxes over its eligible outgoing and incoming edges, and two nodes
//     weld only where the choice is MUTUAL, which turns the edge graph into disjoint simple paths.
//     Head detection plus two prefix sums then name the chains, the emit kernel materialises each
//     path's node / edge / mini-doublet lists, the terminal trim may drop one end node, and the
//     feature and gate kernels score each chain and kill the ones failing their branch's bar.
//
//   createTrackCandidates
//     Upstream pixel-seed self-cleaning and bare-pLS admission, then arbitrateChains.
//
//   arbitrateChains
//     The output-producing stage: claim, attach, emit, crossclean, retire. Its own header block
//     lists the sub-stages in launch order.
//
// Everything up to arbitrateChains is measurement only -- no track candidate is created, moved or
// destroyed before it. The chain collections are per-event and are sized from device counts that
// each cost one host sync, which is why those syncs are commented individually below.

namespace {
  // Optional per-kernel timing of the chain-tracking stages, enabled by LST_CHAIN_TIMING.
  // It inserts a queue drain around each kernel, so it perturbs an asynchronous backend and is
  // meant for stage attribution only, never for a headline number.
  bool chainTimingEnabled() {
    static bool const enabled = (std::getenv("LST_CHAIN_TIMING") != nullptr);
    return enabled;
  }

  // Attribution instrument for the single-block scan family, LST_CHAIN_TIMING only. It drains the
  // queue on BOTH sides of the launch so the printed number is that launch and nothing else, which
  // makes it attribution only and never a headline number. With the flag unset it compiles to
  // exactly the launch it replaces. The label is the source line, so a scan site is identified the
  // same way in two binaries as long as this file is not edited between them.
  template <typename TQueue, typename TWorkDiv, typename TKernel, typename... TArgs>
  void chainScanTimed(
      bool timing, int line, TQueue& queue, TWorkDiv const& workDiv, TKernel const& kernel, TArgs&&... args) {
    if (!timing) {
      alpaka::exec<Acc1D>(queue, workDiv, kernel, std::forward<TArgs>(args)...);
      return;
    }
    alpaka::wait(queue);
    auto const tBeforeLaunch = std::chrono::steady_clock::now();
    alpaka::exec<Acc1D>(queue, workDiv, kernel, std::forward<TArgs>(args)...);
    alpaka::wait(queue);
    double const elapsedMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tBeforeLaunch).count();
    lstWarning(std::format("[CHAIN SCAN] L{} {:.4f} ms", line, elapsedMs));
  }

  // Duplicate-flag snapshots, default OFF (LST_DUP_SNAPSHOTS; the standalone driver sets it for
  // --allobj). BOOKKEEPING ONLY: it copies isDup columns to the host at the points where a later
  // kernel overwrites them, so the ntuple can record states the final collection has lost. No
  // kernel is added, removed or reordered and nothing reads the copies back.
  bool dupSnapshotsEnabled() {
    static bool const enabled = (std::getenv("LST_DUP_SNAPSHOTS") != nullptr);
    return enabled;
  }

  // Device slice count of the attach scorer (ChainAttachScore): how many threads cooperate on one
  // target's candidate walk. The default is the constant; LST_CHAIN_ATTACH_SLICES overrides it for
  // measurement. It changes NOTHING about the result -- every slicing visits every candidate
  // exactly once and reduces through the same packed argmax.
  uint32_t chainAttachScoreSlices() {
    static uint32_t const slices = []() {
      char const* envText = std::getenv("LST_CHAIN_ATTACH_SLICES");
      if (envText == nullptr)
        return kAttachScoreSlices;
      int const parsedSlices = std::atoi(envText);
      return (parsedSlices > 0) ? static_cast<uint32_t>(parsedSlices) : kAttachScoreSlices;
    }();
    return slices;
  }

  // How many DEVICE threads share one pLS in the attach-grid count / scatter passes. The natural
  // element of both is a (pLS, r bin) PAIR -- see the launch-shape note on ChainAttachGridCount --
  // so kAttachRBins gives each pair its own thread and 1 restores the serial-over-r form the host
  // backends use. LST_U4_GRIDR is the measurement override; it changes no result.
  uint32_t chainAttachGridRSlices() {
    static uint32_t const slices = []() {
      char const* envText = std::getenv("LST_U4_GRIDR");
      if (envText == nullptr)
        return static_cast<uint32_t>(kAttachRBins);
      int const parsedSlices = std::atoi(envText);
      return (parsedSlices > 0) ? static_cast<uint32_t>(parsedSlices) : 1u;
    }();
    return slices;
  }

  // Prints the per-r-bin radial hull each attach stage measures from its own target set, so the
  // two stages' hulls can be compared before anything is merged. Off unless LST_U4_HULL is set.
  bool chainAttachHullDump() {
    static bool const enabled = (std::getenv("LST_U4_HULL") != nullptr);
    return enabled;
  }

  // Tile count of the multi-block incidence scan (ChainPrefixIncidenceTiled). The default is the
  // constant; LST_CHAIN_SCAN_TILES overrides it for measurement, including nTiles = 1 (one block,
  // but with coalesced loads and no serial tail). It changes NOTHING about the result: every tiling
  // partitions the same keys in the same order and sums the same addends.
  uint32_t chainScanTiles() {
    static uint32_t const tiles = []() {
      char const* envText = std::getenv("LST_CHAIN_SCAN_TILES");
      if (envText == nullptr)
        return kChainScanTilesDefault;
      int const parsedTiles = std::atoi(envText);
      return (parsedTiles > 0 && static_cast<uint32_t>(parsedTiles) <= kChainScanTilesMax)
                 ? static_cast<uint32_t>(parsedTiles)
                 : kChainScanTilesDefault;
    }();
    return tiles;
  }

  // The per-shared-key incidence degree cap, and the edge-list allocation guard.

  // Override of ChainConfig::degreeCap, so one binary can supply both arms of a comparison. 0 or a
  // negative value means OFF (kChainDegreeCapOff). The default lives in ChainConfig.h; an unset
  // environment leaves it exactly there.
  uint32_t chainDegreeCap(uint32_t configured) {
    static long const envCap = []() {
      char const* envText = std::getenv("LST_CHAIN_DEG_CAP");
      return (envText == nullptr || *envText == '\0') ? -1L : std::atol(envText);
    }();
    if (envCap < 0)
      return configured;
    return (envCap <= 0) ? kChainDegreeCapOff : static_cast<uint32_t>(envCap);
  }

  // Environment overrides of the claim order key, so one binary supplies every arm of a comparison
  // and no result can be a build artefact:
  //
  //   orderKey = score - orderAlpha * max(0, orderHinge - marginX)     (see ChainArbitrate.h)
  //
  // All of them are absent-means-configured, and a negative value also means "use the configured
  // value", so an unset environment reproduces the configured key exactly.
  float chainOrderAlpha(float configured) {
    static double const envAlpha = []() {
      char const* envText = std::getenv("LST_CHAIN_ORDER_ALPHA");
      return (envText == nullptr || *envText == '\0') ? -1.0 : std::atof(envText);
    }();
    return (envAlpha < 0.0) ? configured : static_cast<float>(envAlpha);
  }

  float chainOrderHinge(float configured) {
    static double const envHinge = []() {
      char const* envText = std::getenv("LST_CHAIN_ORDER_HINGE");
      return (envText == nullptr || *envText == '\0') ? -1.0 : std::atof(envText);
    }();
    return (envHinge < 0.0) ? configured : static_cast<float>(envHinge);
  }

  // The eta-conditioned central weight and its ramp (ChainConfig::orderAlphaCentral and friends).
  float chainOrderEnvF(char const* name, float configured) {
    char const* envText = std::getenv(name);
    if (envText == nullptr || *envText == '\0')
      return configured;
    double const parsedValue = std::atof(envText);
    return (parsedValue < 0.0) ? configured : static_cast<float>(parsedValue);
  }

  // An event whose edge list cannot be allocated is SKIPPED (loudly) rather than fatal, because one
  // unallocatable event in a thousand must not take the other 999 with it -- on collimated (jet)
  // samples ~9% of events are over the device allocator ceiling. Setting this makes the failure a
  // hard error again for anyone who wants that instead.
  bool chainOverflowThrows() {
    static bool const enabled = (std::getenv("LST_CHAIN_OVERFLOW_THROW") != nullptr);
    return enabled;
  }

  // ---- ATTACH PAIR DUMP (LST_CHAIN_PAIR_DUMP) -----------------------------------------------
  // MEASUREMENT ONLY, default OFF. These are the on-policy training rows for the attach head; see
  // ChainAttachPairRow in ChainAttach.h for the record and standalone/analysis/DNN/PAIRDUMP_FORMAT.md
  // for the byte layout. With the variable unset every accessor below is dead: no buffer is
  // allocated and both scorers receive nullptr.
  char const* chainPairDumpPath() {
    static char const* const path = []() {
      char const* s = std::getenv("LST_CHAIN_PAIR_DUMP");
      return (s != nullptr && *s != '\0') ? s : nullptr;
    }();
    return path;
  }

  // Row capacity of the per-event device buffer. Stage A whole plus stage B downsampled measures
  // 2.0e5 rows/event MEAN at PU200 and 4.7e5 on the busiest of 30 events, so the 1e6-row default
  // (96 MB) is 5x the mean and 2.1x that peak; a production training dump should pass
  // LST_CHAIN_PAIR_CAP=2000000. A pair arriving after the cursor passes the capacity is DROPPED and
  // counted in the header -- never a partial record, never a crash, and read_pairs.py fails any
  // file with a non-zero drop count.
  uint32_t chainPairDumpCap() {
    static uint32_t const cap = []() {
      char const* s = std::getenv("LST_CHAIN_PAIR_CAP");
      if (s == nullptr)
        return uint32_t{1000000};
      long const v = std::atol(s);
      return (v > 0) ? static_cast<uint32_t>(v) : uint32_t{1000000};
    }();
    return cap;
  }

  // Device-side downsample factor of the STAGE-B rows (~1e6 scored pairs/event, against ~1.3e5 for
  // stage A). Keep one in N by attachPairKeep's hash of the row's own two identities: label-free,
  // score-free, deterministic, and recorded in the header so the trainer can weight. Stage A is
  // always kept whole. LST_CHAIN_PAIR_DSB overrides it (1 = keep everything).
  uint32_t chainPairDumpDsB() {
    static uint32_t const factor = []() {
      char const* s = std::getenv("LST_CHAIN_PAIR_DSB");
      if (s == nullptr)
        return uint32_t{16};
      int const v = std::atoi(s);
      return (v > 0) ? static_cast<uint32_t>(v) : uint32_t{16};
    }();
    return factor;
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
    cms::alpakatools::AllocatorConfig const allocatorConfig{};
    uint64_t bytes = 1;
    for (unsigned int i = 0; i < allocatorConfig.maxBin; ++i)
      bytes *= allocatorConfig.binGrowth;
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
  void snapshotByteColumn(TQueue& queue, TSpan column, unsigned int nElements, std::vector<char>& hostOut) {
    using ElemT = std::remove_cv_t<typename TSpan::element_type>;
    static_assert(sizeof(ElemT) == 1, "snapshotByteColumn expects a one byte wide column");
    hostOut.assign(nElements, 0);
    if (nElements == 0)
      return;
    auto deviceView = cms::alpakatools::make_device_view(queue, column, nElements);
    auto hostView = cms::alpakatools::make_host_view(reinterpret_cast<ElemT*>(hostOut.data()), nElements);
    alpaka::memcpy(queue, hostView, deviceView);
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
  // Per-module raw->dense key bias for the two chain incidence tables: filled by
  // ChainPrefixKeyModules in the allocation block below, then read by the incidence tallies inside
  // the triplet builder and again by the CSR fill in buildChainIncidence. Declared at function
  // scope because those consumers straddle the allocation block.
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

    // Build the raw->dense key bias here so that its two totals, which size the incidence
    // collections, ride down on the host sync the triplet count already pays for. No extra wait.
    auto chainKeyTotals_buf_h = cms::alpakatools::make_host_buffer<uint32_t[]>(queue_, 2u);
    auto chainKeyTotals_buf_d = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, 2u);
    {
      chainScanTimed(chainTimingEnabled(),
                     __LINE__,
                     queue_,
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
      // The two incidence tables, keyed by the DENSE MiniDoublet / Segment index that
      // ChainPrefixKeyModules just defined. Keying them by the RAW module-segmented index instead
      // forced them to span the full allocated extent of those two collections, which carries about
      // 2.0x (MD) and 4.7x (Segment) slack over the produced-object count and made this the single
      // largest chain allocation. Allocated and zeroed before the triplet builder runs, since the
      // builder tallies straight into them.
      unsigned int const nMDKeys = chainKeyTotals_buf_h.data()[0];
      unsigned int const nLSKeys = chainKeyTotals_buf_h.data()[1];
      chainMdIncidenceDC_.emplace(queue_, nMDKeys + 1);
      chainLsIncidenceDC_.emplace(queue_, nLSKeys + 1);
      // Only the tallies need clearing: the prefix pass writes every entry of the offset and prefix
      // columns (including the terminating one) and the CSR fill writes every item entry.
      resetChainIncidenceCounts();
      if (objectsStatistics_) {
        double incidenceMB = (alpaka::getExtentProduct(chainMdIncidenceDC_->buffer()) +
                              alpaka::getExtentProduct(chainLsIncidenceDC_->buffer())) /
                             1e6;
        memoryAllocatedMB_ += incidenceMB;
        lstWarning(std::format("[MEM] ChainIncidence: {} dense MD keys + {} dense LS keys allocated ({:.1f} MB)",
                               nMDKeys,
                               nLSKeys,
                               incidenceMB));
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
  // Zero the two tally columns of both incidence tables. They serve twice: as the atomic counters
  // the triplet builder tallies into, and then as the per-key write cursors of the CSR fill.
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

  // Compact the module-segmented triplet store into a dense node numbering: one chain node per
  // triplet, numbered by module then by row, so that every later stage can index nodes densely.
  auto moduleNodeOffsets_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, nLowerModules_ + 1);
  auto nNodes_buf_d = cms::alpakatools::make_device_buffer<uint32_t>(queue_);

  auto const chainScan_workDiv = cms::alpakatools::make_workdiv<Acc1D>(1, kChainScanBlockThreads);

  chainScanTimed(timing,
                 __LINE__,
                 queue_,
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
    double nodesMB = alpaka::getExtentProduct(chainNodesDC_->buffer()) / 1e6;
    memoryAllocatedMB_ += nodesMB;
    lstWarning(std::format("[MEM] ChainNodes: {} allocated ({:.1f} MB)", nChainNodes_, nodesMB));
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

  // Exclusive prefixes over both incidence tables, plus the exact edge counts E1 (MD keyed) and E2
  // (Segment keyed). Each table takes two passes over the same tiled-scan kernel (phase 0 sums the
  // tiles, phase 1 writes the offsets), and both tables share one tileSums scratch because their
  // four launches are queue-ordered.
  unsigned int const nMDKeys = chainMdIncidenceDC_->view().metadata().size() - 1;
  unsigned int const nLSKeys = chainLsIncidenceDC_->view().metadata().size() - 1;

  // THE SCRATCH IS BORROWED, NOT ALLOCATED, and that is deliberate. moduleNodeOffsets_buf is
  // nLowerModules + 1 = 13201 words and is DEAD from here on: ChainPrefixTripletModules wrote it and
  // ChainScatterTripletModules, enqueued two launches above, is its only reader -- and the queue is
  // ordered, so the scatter has finished before phase 0 starts. 3 * nTiles <= 1536 words fit inside
  // it many times over. An earlier revision allocated a persistent 6 kB device buffer of its own
  // instead, and that cost +43 ms/event on the CPU backend, all of it in the pLS stage, which this
  // code does not touch: an extra buffer here makes the caching allocator evict buffers that
  // unrelated stages were reusing. So, in this file: a persistent member for a compile-time size,
  // or borrow one that is provably dead -- never a new per-event buffer.
  uint32_t const nTiles = std::max(1u, std::min(chainScanTiles(), (nLowerModules_ + 1u) / 3u));
  auto const chainTile_workDiv = cms::alpakatools::make_workdiv<Acc1D>(nTiles, kChainScanTileThreads);
  uint32_t* const chainTileSums = moduleNodeOffsets_buf.data();
  uint32_t const degCap = chainDegreeCap(chainConfig_.degreeCap);
  chainScanTimed(timing,
                 __LINE__,
                 queue_,
                 chainTile_workDiv,
                 ChainPrefixIncidenceTiled{},
                 chainMdIncidenceDC_->view(),
                 nMDKeys,
                 chainTileSums,
                 nTiles,
                 0u,
                 degCap);
  chainScanTimed(timing,
                 __LINE__,
                 queue_,
                 chainTile_workDiv,
                 ChainPrefixIncidenceTiled{},
                 chainMdIncidenceDC_->view(),
                 nMDKeys,
                 chainTileSums,
                 nTiles,
                 1u,
                 degCap);
  chainScanTimed(timing,
                 __LINE__,
                 queue_,
                 chainTile_workDiv,
                 ChainPrefixIncidenceTiled{},
                 chainLsIncidenceDC_->view(),
                 nLSKeys,
                 chainTileSums,
                 nTiles,
                 0u,
                 degCap);
  chainScanTimed(timing,
                 __LINE__,
                 queue_,
                 chainTile_workDiv,
                 ChainPrefixIncidenceTiled{},
                 chainLsIncidenceDC_->view(),
                 nLSKeys,
                 chainTileSums,
                 nTiles,
                 1u,
                 degCap);

  // Is the uint32 edge count `nEdgesExact` the TRUE count, or one that wrapped? The host can
  // certify it for free: sum_key degIn * degOut <= (sum_key degIn) * max_key degOut, both degrees
  // are capped at degCap and every degree is at most the node count, so the count is bounded by
  // nNodes * min(nNodes, degCap). With that bound below 2^32 no wrap is arithmetically possible,
  // which covers every PU200 event (nNodes at most ~53k) and every capped event, so the 64-bit
  // recount below runs only on a cap-off collimated event -- exactly the case whose count cannot be
  // trusted. It costs two launches over the key range and one 8 kB copy, read at the sync that is
  // already there.
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
    chainScanTimed(timing,
                   __LINE__,
                   queue_,
                   chainTile_workDiv,
                   ChainPrefixIncidenceTiled{},
                   chainMdIncidenceDC_->view(),
                   nMDKeys,
                   recountMd,
                   nTiles,
                   2u,
                   degCap);
    chainScanTimed(timing,
                   __LINE__,
                   queue_,
                   chainTile_workDiv,
                   ChainPrefixIncidenceTiled{},
                   chainLsIncidenceDC_->view(),
                   nLSKeys,
                   recountLs,
                   nTiles,
                   2u,
                   degCap);
    recount.resize(4u * kChainScanTilesMax);
    auto recount_h = cms::alpakatools::make_host_view(recount.data(), recount.size());
    auto recount_d = cms::alpakatools::make_device_view(queue_, recountMd, recount.size());
    alpaka::memcpy(queue_, recount_h, recount_d);
  }

  // The tallies are now captured in the offset columns; reuse them as the CSR write cursors.
  resetChainIncidenceCounts();

  // Fill the CSR payloads: for each shared key, the triplets entering it and the triplets leaving
  // it. These two lists per key are what the edge enumeration takes its cross product over.
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
  // a wrapped count can never reach an allocation.
  nChainE1Edges64_ = nChainE1Edges_;
  nChainE2Edges64_ = nChainE2Edges_;
  if (!recount.empty()) {
    auto sum64 = [&](uint32_t const* tileBase) {
      uint64_t edgeSum = 0;
      for (uint32_t tile = 0; tile < nTiles; ++tile)
        edgeSum += (static_cast<uint64_t>(tileBase[kChainScanTilesMax + tile]) << 32) | tileBase[tile];
      return edgeSum;
    };
    nChainE1Edges64_ = sum64(&recount[0]);
    nChainE2Edges64_ = sum64(&recount[2u * kChainScanTilesMax]);
  }

  if (timing) {
    alpaka::wait(queue_);
    double const elapsedMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tStart).count();
    lstWarning(std::format("[CHAIN TIMING] K0+K1 incidence {:.3f} ms", elapsedMs));
  }

  if (objectsStatistics_) {
    chainIncidenceStatistics();
  }
}

void LSTEvent::chainIncidenceStatistics() {
  // Host-side check of every invariant the incidence CSR must satisfy (monotone offsets, degree
  // sums equal to the node count, each item array a permutation of the nodes, and the edge prefix
  // agreeing with the recomputed degree product). Runs only under verbose statistics.
  alpaka::wait(queue_);

  // Pull the columns down one by one so this works unchanged on every backend.
  auto pull = [&](auto column, unsigned int nElements) {
    std::vector<uint32_t> host(nElements);
    auto host_view = cms::alpakatools::make_host_view(host.data(), nElements);
    auto dev_view = cms::alpakatools::make_device_view(queue_, column, nElements);
    alpaka::memcpy(queue_, host_view, dev_view, nElements);
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

    // `edges` is the CAPPED count, i.e. what the prefix claims and what the edge build will
    // enumerate; `edgesUncapped` is what the same event would have produced with the cap off, so
    // the two together measure what the cap removed on this event. With the cap off they are equal
    // by construction.
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
      uint32_t const outNode = outItems[i];
      uint32_t const inNode = inItems[i];
      if (outNode >= nChainNodes_ || inNode >= nChainNodes_) {
        outOfBounds = true;
        continue;
      }
      duplicated = duplicated || seenOut[outNode] || seenIn[inNode];
      seenOut[outNode] = 1;
      seenIn[inNode] = 1;
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

  unsigned long long const edgesE1 = checkFamily("MD/E1", mdInc, nodes.mdT3OutItems(), nodes.mdT3InItems());
  unsigned long long const edgesE2 = checkFamily("LS/E2", lsInc, nodes.lsT3OutItems(), nodes.lsT3InItems());
  lstWarning(std::format("[CHAIN] nodes(nT3)={} E1={} E2={} E={}", nChainNodes_, edgesE1, edgesE2, edgesE1 + edgesE2));
}

void LSTEvent::buildChainEdges() {
  // Node features, then the edge list, then the edge head. Three launches:
  //   ChainNodeFeatures   the per-triplet feature row every later stage gathers from
  //   ChainBuildEdges     the exactly-counted edge list: for each shared key, the cross product of
  //                       the triplets entering it with the triplets leaving it
  //   ChainEdgeInference  the edge features (built in registers, never stored) and the edge head's
  //                       log-odds, plus the weld eligibility bar resolved per edge
  // Produces no track candidate; the weld in buildChains is the first consumer.
  if (nChainNodes_ == 0)
    return;

  auto const chainFlat_workDiv = cms::alpakatools::make_workdiv<Acc1D>(max_blocks, 256);

  bool const timing = chainTimingEnabled();
  auto stamp = [&]() {
    if (timing)
      alpaka::wait(queue_);
    return std::chrono::steady_clock::now();
  };
  auto const tBeforeNodeFeatures = stamp();

  // Node features first: the edge inference gathers node rows, so they must exist before it runs.
  // Both are on the same in-order queue, so no explicit synchronization is needed between them.
  alpaka::exec<Acc1D>(queue_,
                      chainFlat_workDiv,
                      ChainNodeFeatures{},
                      modules_.const_view().modules(),
                      miniDoubletsDC_->const_view().miniDoublets(),
                      segmentsDC_->const_view().segments(),
                      tripletsDC_->const_view().triplets(),
                      chainNodesDC_->view());

  auto const tAfterNodeFeatures = stamp();

  // Exact allocation by pure degree arithmetic, so there is no capped reservation and no
  // ungated-writer hazard: every row below is written by exactly one thread at its own index.
  uint32_t const nEdges = nChainE1Edges_ + nChainE2Edges_;

  // THE ALLOCATION GUARD. Everything is decided on the 64-BIT counts and the 64-bit byte size; the
  // uint32 `nEdges` above is used only after the guard has proved it is the true count and that it
  // can be allocated. Both facts fail in practice on collimated events: measured on 1000 jet
  // events, 8 asked for more than 4 GiB (the byte extent then truncates modulo 2^32 and the edge
  // build writes far past the end of the buffer) and 90 were over the 1 GiB device allocator bin.
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
    // candidates; it does not get chain ones, which is a real efficiency loss on that event and is
    // why this is LOUD and counted rather than silent.
    static std::atomic<uint32_t> overflowCensus{0};
    uint32_t const nSoFar = overflowCensus.fetch_add(1) + 1;
    std::string const overflowMsg = std::format(
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
      throw std::runtime_error(overflowMsg);
    lstWarning(overflowMsg);
    return;
  }

  chainEdgesDC_.emplace(queue_, nEdges);
  if (objectsStatistics_) {
    double edgesMB = alpaka::getExtentProduct(chainEdgesDC_->buffer()) / 1e6;
    memoryAllocatedMB_ += edgesMB;
    lstWarning(std::format("[MEM] ChainEdges: {} allocated ({:.1f} MB)", nEdges, edgesMB));
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

  auto const tAfterBuildEdges = stamp();

  // Training / debug tap: with LST_CHAIN_FEAT_DUMP set, the edge inference also stores its edge
  // feature row, so an offline comparison can localise a mismatch to a feature instead of seeing
  // only the logit, and the head can be retrained on the rows the deployed binary actually built.
  // It appends one record per event with a [magic, ievt] header. Unset, nothing is allocated or
  // written and the behaviour is identical.
  static std::atomic<uint32_t> featDumpEvent{0};
  char const* featPath = std::getenv("LST_CHAIN_FEAT_DUMP");
  bool wantFeat = (featPath != nullptr && *featPath != '\0');
  // The tap is 56 B/edge, 2.7x the edge row itself, so it hits the same uint32 extent wall four
  // times sooner than ChainEdges does -- and it is a RAW buffer, so nothing downstream would notice
  // the truncation. It is an instrument, so the right action is to drop the tap for this event and
  // say so, rather than to lose the event.
  if (wantFeat) {
    uint64_t const featBytes = static_cast<uint64_t>(nEdges) * kChainEdgeFeatures * sizeof(float);
    if (featBytes > static_cast<uint64_t>(std::numeric_limits<alpaka_common::Idx>::max())) {
      lstWarning(
          std::format("[CHAIN OVERFLOW] LST_CHAIN_FEAT_DUMP wants {} B for {} edges, over the alpaka Idx extent; "
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
  // against the same number. With chainConfig_.edgeWpTable these two feed only the fallback path:
  // the edge inference then takes each edge's bar from the head's own per-cell table.
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

  auto const tAfterEdgeInference = stamp();
  if (timing) {
    auto elapsedMs = [](auto startStamp, auto endStamp) {
      return std::chrono::duration<double, std::milli>(endStamp - startStamp).count();
    };
    lstWarning(
        std::format("[CHAIN TIMING] nodes={} edges={} | K3 nodeFeatures {:.3f} ms | "
                    "K2 buildEdges {:.3f} ms | K5 edgeInference {:.3f} ms | total {:.3f} ms",
                    nChainNodes_,
                    nEdges,
                    elapsedMs(tBeforeNodeFeatures, tAfterNodeFeatures),
                    elapsedMs(tAfterNodeFeatures, tAfterBuildEdges),
                    elapsedMs(tAfterBuildEdges, tAfterEdgeInference),
                    elapsedMs(tBeforeNodeFeatures, tAfterEdgeInference)));
  }

  if (wantFeat) {
    alpaka::wait(queue_);
    std::vector<uint32_t> inner(nEdges), outer(nEdges);
    std::vector<uint8_t> type(nEdges);
    std::vector<float> feats(static_cast<size_t>(nEdges) * kChainEdgeFeatures);
    std::vector<float> nodeFeats(static_cast<size_t>(nChainNodes_) * Params_ChainNode::kFeatures);
    auto pull = [&](auto* hostPtr, auto column, unsigned int nElements) {
      auto host_view = cms::alpakatools::make_host_view(hostPtr, nElements);
      auto dev_view = cms::alpakatools::make_device_view(queue_, column, nElements);
      alpaka::memcpy(queue_, host_view, dev_view);
      alpaka::wait(queue_);
    };
    auto edgesView = chainEdgesDC_->view();
    pull(inner.data(), edgesView.inner(), nEdges);
    pull(outer.data(), edgesView.outer(), nEdges);
    pull(type.data(), edgesView.type(), nEdges);
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
    std::FILE* featFile = std::fopen(featPath, (featIevt == 0) ? "wb" : "ab");
    if (featFile != nullptr) {
      auto put32 = [&](uint32_t value) { std::fwrite(&value, sizeof(value), 1, featFile); };
      put32(0x50323146u);  // 'P21F' per-event record magic (new in the all-events format)
      put32(featIevt);
      put32(nChainNodes_);
      put32(static_cast<uint32_t>(Params_ChainNode::kFeatures));
      std::fwrite(nodeFeats.data(), sizeof(float), nodeFeats.size(), featFile);
      uint32_t nKept = 0;
      for (uint32_t edgeIdx = 0; edgeIdx < nEdges; ++edgeIdx)
        nKept += (type[edgeIdx] != 0);
      put32(nKept);
      put32(static_cast<uint32_t>(kChainEdgeFeatures));
      for (uint32_t edgeIdx = 0; edgeIdx < nEdges; ++edgeIdx) {
        if (type[edgeIdx] == 0)
          continue;
        put32(inner[edgeIdx]);
        put32(outer[edgeIdx]);
        put32(type[edgeIdx]);
        std::fwrite(
            &feats[static_cast<size_t>(edgeIdx) * kChainEdgeFeatures], sizeof(float), kChainEdgeFeatures, featFile);
      }
      std::fclose(featFile);
    }
  }
  dumpChainEdges();
  dumpChainNodes();
  buildChains();
}

void LSTEvent::dumpChainEdges() {
  // Edge-level sidecar for offline analysis. Off unless LST_CHAIN_EDGE_DUMP names an output file;
  // the ntuple and the track candidate collection are untouched either way, so nothing downstream
  // can see whether this ran. One record per event: the header below, then every surviving edge as
  // (inner node, outer node, family, log-odds).
  char const* path = std::getenv("LST_CHAIN_EDGE_DUMP");
  if (path == nullptr || *path == '\0' || !chainEdgesDC_.has_value())
    return;

  alpaka::wait(queue_);

  uint32_t const nEdges = static_cast<uint32_t>(chainEdgesDC_->view().metadata().size());
  std::vector<uint32_t> inner(nEdges), outer(nEdges);
  std::vector<uint8_t> type(nEdges);
  std::vector<float> logOdds(nEdges);

  auto pullTo = [&](auto* hostPtr, auto column, unsigned int nElements) {
    auto host_view = cms::alpakatools::make_host_view(hostPtr, nElements);
    auto dev_view = cms::alpakatools::make_device_view(queue_, column, nElements);
    alpaka::memcpy(queue_, host_view, dev_view);
    alpaka::wait(queue_);
  };
  auto view = chainEdgesDC_->view();
  pullTo(inner.data(), view.inner(), nEdges);
  pullTo(outer.data(), view.outer(), nEdges);
  pullTo(type.data(), view.type(), nEdges);
  pullTo(logOdds.data(), view.logOdds(), nEdges);

  uint32_t nE1Kept = 0, nE2Kept = 0;
  for (uint32_t edgeIdx = 0; edgeIdx < nEdges; ++edgeIdx) {
    nE1Kept += (type[edgeIdx] == 1);
    nE2Kept += (type[edgeIdx] == 2);
  }

  // Sequential event counter. The sidecar is only meaningful for single-stream runs, which is
  // how the parity comparison is made.
  static std::atomic<uint32_t> eventCounter{0};
  uint32_t const ievt = eventCounter.fetch_add(1);

  static std::mutex dumpMutex;
  std::lock_guard<std::mutex> lock(dumpMutex);
  std::FILE* dumpFile = std::fopen(path, (ievt == 0) ? "wb" : "ab");
  if (dumpFile == nullptr)
    return;
  auto put32 = [&](uint32_t value) { std::fwrite(&value, sizeof(value), 1, dumpFile); };
  auto put64 = [&](uint64_t value) { std::fwrite(&value, sizeof(value), 1, dumpFile); };
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
  for (uint32_t edgeIdx = 0; edgeIdx < nEdges; ++edgeIdx) {
    if (type[edgeIdx] == 0)
      continue;
    put32(inner[edgeIdx]);
    put32(outer[edgeIdx]);
    put32(type[edgeIdx]);
    std::fwrite(&logOdds[edgeIdx], sizeof(float), 1, dumpFile);
  }
  std::fclose(dumpFile);
}

void LSTEvent::dumpChainNodes() {
  // Determinism sidecar. Off unless LST_CHAIN_NODE_DUMP names an output file. One record per event
  // carrying, for every chain node in dense node order, its stableId and the six hit rows that
  // stableId is built from. It supports two offline checks:
  //   - weld-tie uniqueness: joined with the edge dump it shows whether any node has two
  //     neighbours sharing a stableId, which is the condition under which the packed weld key
  //     would not be unique inside a node's incident-edge list;
  //   - backend attribution: the node sets are compared on hit rows rather than on indices, so a
  //     chain that exists on one backend only can be traced back to a missing upstream triplet.
  char const* path = std::getenv("LST_CHAIN_NODE_DUMP");
  if (path == nullptr || *path == '\0' || !chainNodesDC_.has_value() || nChainNodes_ == 0)
    return;

  alpaka::wait(queue_);

  auto pullTo = [&](auto* hostPtr, auto column, unsigned int nElements) {
    auto host_view = cms::alpakatools::make_host_view(hostPtr, nElements);
    auto dev_view = cms::alpakatools::make_device_view(queue_, column, nElements);
    alpaka::memcpy(queue_, host_view, dev_view);
    alpaka::wait(queue_);
  };

  uint32_t const nNodes = nChainNodes_;
  std::vector<uint32_t> tripletIndex(nNodes), stableId(nNodes);
  pullTo(tripletIndex.data(), chainNodesDC_->view().tripletIndex(), nNodes);
  pullTo(stableId.data(), chainNodesDC_->view().stableId(), nNodes);

  unsigned int const nTriplets = static_cast<unsigned int>(tripletsDC_->view().triplets().metadata().size());
  unsigned int const nSegments = static_cast<unsigned int>(segmentsDC_->view().segments().metadata().size());
  unsigned int const nMiniDoublets =
      static_cast<unsigned int>(miniDoubletsDC_->view().miniDoublets().metadata().size());
  std::vector<ArrayUx2> t3Seg(nTriplets);
  std::vector<Params_LS::ArrayUxLayers> lsMD(nSegments);
  std::vector<unsigned int> mdAnchor(nMiniDoublets), mdOuter(nMiniDoublets);
  pullTo(t3Seg.data(), tripletsDC_->view().triplets().segmentIndices(), nTriplets);
  pullTo(lsMD.data(), segmentsDC_->view().segments().mdIndices(), nSegments);
  pullTo(mdAnchor.data(), miniDoubletsDC_->view().miniDoublets().anchorHitIndices(), nMiniDoublets);
  pullTo(mdOuter.data(), miniDoubletsDC_->view().miniDoublets().outerHitIndices(), nMiniDoublets);

  static std::atomic<uint32_t> nodeEventCounter{0};
  uint32_t const ievt = nodeEventCounter.fetch_add(1);
  static std::mutex nodeDumpMutex;
  std::lock_guard<std::mutex> lock(nodeDumpMutex);
  std::FILE* dumpFile = std::fopen(path, (ievt == 0) ? "wb" : "ab");
  if (dumpFile == nullptr)
    return;
  auto put32 = [&](uint32_t value) { std::fwrite(&value, sizeof(value), 1, dumpFile); };

  put32(0x5032354Eu);  // 'P25N'
  put32(ievt);
  put32(nNodes);
  for (uint32_t nodeIdx = 0; nodeIdx < nNodes; ++nodeIdx) {
    uint32_t const tripletIdx = tripletIndex[nodeIdx];
    unsigned int const innerSeg = t3Seg[tripletIdx][0];
    unsigned int const outerSeg = t3Seg[tripletIdx][1];
    unsigned int const mdIndices[3] = {lsMD[innerSeg][0], lsMD[innerSeg][1], lsMD[outerSeg][1]};
    put32(stableId[nodeIdx]);
    for (int k = 0; k < 3; ++k) {
      put32(mdAnchor[mdIndices[k]]);
      put32(mdOuter[mdIndices[k]]);
    }
  }
  std::fclose(dumpFile);
}

void LSTEvent::buildChains() {
  // Edge graph -> scored chains, in launch order:
  //   ChainWeldArgmax + ChainWeldMutual   the weld sweeps: each node argmaxes over its eligible
  //                                       incident edges and a link is made only where the two
  //                                       nodes choose each other, which leaves in- and out-degree
  //                                       at most 1, i.e. disjoint simple paths
  //   ChainCountChains + ChainPrefixChains  find the path heads and name the chains, and size the
  //                                       member-item arrays by two exclusive prefixes
  //   ChainEmitChains                     materialise each path's node / edge / mini-doublet lists
  //   ChainTrimTerminals | ChainTrimLearned  optionally drop one end node of a chain
  //   ChainFeaturesKernel                 the chain feature row, including the chain dcaXY
  //   ChainGateKernel                     the 3-class head, the branch bars, and the kill
  // Produces no track candidate: the claim in arbitrateChains is the first consumer.
  if (nChainNodes_ == 0 || !chainEdgesDC_.has_value())
    return;

  bool const timing = chainTimingEnabled();
  auto stamp = [&]() {
    if (timing)
      alpaka::wait(queue_);
    return std::chrono::steady_clock::now();
  };
  auto const tBeforeWeld = stamp();

  auto const chainFlat_workDiv = cms::alpakatools::make_workdiv<Acc1D>(max_blocks, 256);
  auto const chainScan_workDiv = cms::alpakatools::make_workdiv<Acc1D>(1, kChainScanBlockThreads);

  // Weld slots start empty (-1) and the packed argmax keys start at the 0 sentinel.
  auto outWeld_buf = cms::alpakatools::make_device_buffer<int32_t[]>(queue_, nChainNodes_);
  auto inWeld_buf = cms::alpakatools::make_device_buffer<int32_t[]>(queue_, nChainNodes_);
  auto bestOut_buf = cms::alpakatools::make_device_buffer<uint64_t[]>(queue_, nChainNodes_);
  auto bestIn_buf = cms::alpakatools::make_device_buffer<uint64_t[]>(queue_, nChainNodes_);
  alpaka::memset(queue_, outWeld_buf, 0xff);
  alpaka::memset(queue_, inWeld_buf, 0xff);

  // The weld eligibility bar is not a kernel argument: the edge inference already resolved it per
  // edge into ChainEdgesSoA::weldBar (from the head's per-family, per-cell table, or from the two
  // per-family scalars when chainConfig_.edgeWpTable is false), so both weld kernels read it off
  // the edge row.
  for (int sweep = 0; sweep < kChainWeldSweeps; ++sweep) {
    // A fixed sweep count, deliberately without a host sync: a sweep that welds nothing is
    // idempotent, so an early exit would only buy the cost of the sync that detects it.
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

  auto const tAfterWeld = stamp();

  // Head detection, path lengths, and the two exclusive prefixes that name the chains and give each
  // one its slice of the member-item arrays.
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

  chainScanTimed(timing,
                 __LINE__,
                 queue_,
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
  // featValid must start at 0 for EVERY chain. Only chains the learned trim visits (nNodes >= 3)
  // ever set it, and the feature kernel skips a row that claims to be published already -- an
  // uninitialised byte here would silently drop feature rows, so this memset is a correctness
  // requirement, not tidiness.
  {
    auto featValidView = cms::alpakatools::make_device_view(queue_, chainsDC_->view().featValid(), nChainCount_);
    alpaka::memset(queue_, featValidView, 0u);
  }
  if (objectsStatistics_) {
    double chainsMB =
        (alpaka::getExtentProduct(chainsDC_->buffer()) + alpaka::getExtentProduct(chainItemsDC_->buffer())) / 1e6;
    memoryAllocatedMB_ += chainsMB;
    lstWarning(std::format(
        "[MEM] Chains: {} chains / {} member nodes allocated ({:.1f} MB)", nChainCount_, nChainWeldedNodes_, chainsMB));
  }

  auto const tAfterCountPrefix = stamp();

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

  auto const tAfterEmit = stamp();

  // Terminal-variant probe (env-gated, PRE-TRIM, pure observation): it records the three trim
  // variants of every chain so the trim decision can be studied offline. Allocates nothing and
  // runs nothing unless LST_CHAIN_VARIANT_DUMP names an output file.
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

  // The terminal trim. Both kernels build the same three candidate variants (full chain, inner
  // terminal dropped, outer terminal dropped) and differ only in who chooses between them: mode 0
  // decides on the fit chi2, every other mode on the chain head (see ChainConfig::trimMode).
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

  auto const tAfterTrim = stamp();

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

  auto const tAfterFeatures = stamp();

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

  auto const tAfterGate = stamp();
  if (timing) {
    auto elapsedMs = [](auto startStamp, auto endStamp) {
      return std::chrono::duration<double, std::milli>(endStamp - startStamp).count();
    };
    lstWarning(
        std::format("[CHAIN TIMING] chains={} weldedNodes={} | K6ab weld {:.3f} ms | "
                    "K6cd count+prefix {:.3f} ms | K6e emit {:.3f} ms | K6f trim {:.3f} ms | "
                    "K7a features {:.3f} ms | K7bc gate {:.3f} ms | total {:.3f} ms",
                    nChainCount_,
                    nChainWeldedNodes_,
                    elapsedMs(tBeforeWeld, tAfterWeld),
                    elapsedMs(tAfterWeld, tAfterCountPrefix),
                    elapsedMs(tAfterCountPrefix, tAfterEmit),
                    elapsedMs(tAfterEmit, tAfterTrim),
                    elapsedMs(tAfterTrim, tAfterFeatures),
                    elapsedMs(tAfterFeatures, tAfterGate),
                    elapsedMs(tBeforeWeld, tAfterGate)));
  }

  dumpChains();
}

void LSTEvent::arbitrateChains(unsigned int nAllocatedTCs) {
  // The stage that produces the output. It runs at the very end of createTrackCandidates, after
  // every upstream admission kernel, so the carried rows are exactly what LST built; only then are
  // the replaced classes removed and the accepted chains appended.
  //
  // Sub-stages, in launch order:
  //   1. carried-row compaction   drop the classes the chain pipeline rebuilds (T5, T4, and the
  //                               pT5 / pT3 rows when configured to replace them)
  //   2. ChainClaimPrep           per-chain claim universe, order key, candidate mask, band bars
  //   3. rank + ChainClaimRounds  the greedy hit claim; publishes the accepted chains, best first
  //   4. attach stage A           (chain, pLS) pairs: grid prefilter, pair head, one-pLS-one-owner
  //                               contention, seed-family dedup. Grants live on the chain row.
  //   5. attach stage B           the same for bare triplets no accepted chain consumed, against
  //                               the ownership stage A left behind
  //   6. row assignment + emit    one candidate row per accepted, emittable chain; a granted seed
  //                               upgrades that row in place rather than adding one
  //   7. bare-triplet delivery    hit-overlap contention of stage B's owners against the emitted
  //                               chain rows, then their own candidate rows
  //   8. seed crossclean          retire bare seeds that duplicate a delivered object
  //   9. final compaction         apply the attach / crossclean verdicts to the carried rows
  //
  // The ordering constraints that matter: the attach must see the claim's verdicts (it scores only
  // accepted chains), stage B must see stage A's ownership, the bare-triplet contention must see
  // the emitted chain rows, and the crossclean must see everything that was delivered -- which is
  // why the retirement is last rather than part of step 1.
  bool const timing = chainTimingEnabled();
  auto stamp = [&]() {
    if (timing)
      alpaka::wait(queue_);
    return std::chrono::steady_clock::now();
  };
  auto const tStart = stamp();

  auto const serial_workDiv = cms::alpakatools::make_workdiv<Acc1D>(1, 1);
  auto const chainFlat_workDiv = cms::alpakatools::make_workdiv<Acc1D>(max_blocks, 256);
  auto const chainScan_workDiv = cms::alpakatools::make_workdiv<Acc1D>(1, kChainScanBlockThreads);

  // Step 1: drop the carried rows whose class the chain pipeline rebuilds. This runs even with zero
  // chains, because which classes are replaced is a property of the configuration, not of how many
  // chains an event happened to weld.
  //
  // Flags + single-block prefix + gather/scatter through a staging array, one form on every
  // backend; the in-place serial alternative costs 1.5 ms/event on CUDA.
  {
    uint32_t const nRowsBound = nAllocatedTCs;
    auto keep_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, std::max(1u, nRowsBound));
    auto offs_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, nRowsBound + 1u);
    auto total_buf = cms::alpakatools::make_device_buffer<uint32_t>(queue_);
    auto stage_buf = cms::alpakatools::make_device_buffer<ChainTCRowPayload[]>(queue_, std::max(1u, nRowsBound));
    alpaka::exec<Acc1D>(queue_,
                        chainFlat_workDiv,
                        ChainTCKeepCompact{},
                        trackCandidatesBaseDC_->const_view(),
                        keep_buf.data(),
                        nRowsBound,
                        chainConfig_);
    chainScanTimed(timing,
                   __LINE__,
                   queue_,
                   chainScan_workDiv,
                   ChainSegPrefix{},
                   keep_buf.data(),
                   offs_buf.data(),
                   total_buf.data(),
                   nRowsBound);
    alpaka::exec<Acc1D>(queue_,
                        chainFlat_workDiv,
                        ChainTCGather{},
                        trackCandidatesBaseDC_->const_view(),
                        trackCandidatesExtendedDC_->const_view(),
                        keep_buf.data(),
                        offs_buf.data(),
                        nRowsBound,
                        stage_buf.data());
    alpaka::exec<Acc1D>(queue_,
                        chainFlat_workDiv,
                        ChainTCScatter{},
                        trackCandidatesBaseDC_->view(),
                        trackCandidatesExtendedDC_->view(),
                        stage_buf.data(),
                        offs_buf.data(),
                        nRowsBound);
    alpaka::exec<Acc1D>(queue_,
                        serial_workDiv,
                        ChainTCFinishCompact{},
                        trackCandidatesBaseDC_->view(),
                        trackCandidatesExtendedDC_->view(),
                        offs_buf.data(),
                        nRowsBound,
                        chainConfig_);
  }
  auto const tAfterCompact = stamp();

  if (nChainCount_ == 0 || !chainsDC_.has_value()) {
    // MEASUREMENT ONLY: keep the pair dump's event index aligned with the chain dump's. dumpChains()
    // emits a record for every event that has a chain collection, so this degenerate leg (no attach
    // stage runs at all) emits an empty pair record for the same events. Inert with the env unset.
    if (chainsDC_.has_value()) {
      beginChainPairDump();
      dumpChainPairs();
      dumpChainJoin();
    }
    return;
  }

  unsigned int const nHits = static_cast<unsigned int>(lstInputDC_->const_view().hits().metadata().size());
  unsigned int const nMDall = static_cast<unsigned int>(miniDoubletsDC_->view().miniDoublets().metadata().size());

  // Step 2: the claim universe, the order key, the candidate mask and the band tolerances, in one
  // per-chain visit (ChainClaimPrep).
  auto claimHits_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, 6u * nChainWeldedNodes_);
  auto candKeep_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, nChainCount_);
  auto bandItems_buf = cms::alpakatools::make_device_buffer<int32_t[]>(queue_, nChainCount_);
  auto bandFrac_buf = cms::alpakatools::make_device_buffer<float[]>(queue_, nChainCount_);
  auto bandBraid_buf = cms::alpakatools::make_device_buffer<float[]>(queue_, nChainCount_);
  // The only thing this copy changes is the order key's parameters, and only when the environment
  // names them. ChainClaimPrep is the sole reader of them in the whole tree, so the override cannot
  // leak into any other stage.
  ChainConfig orderCfg = chainConfig_;
  orderCfg.orderAlpha = chainOrderAlpha(chainConfig_.orderAlpha);
  orderCfg.orderHinge = chainOrderHinge(chainConfig_.orderHinge);
  orderCfg.orderAlphaCentral = chainOrderEnvF("LST_CHAIN_ORDER_ALPHA_CENTRAL", chainConfig_.orderAlphaCentral);
  orderCfg.orderEtaRampLo = chainOrderEnvF("LST_CHAIN_ORDER_ETA_LO", chainConfig_.orderEtaRampLo);
  orderCfg.orderEtaRampHi = chainOrderEnvF("LST_CHAIN_ORDER_ETA_HI", chainConfig_.orderEtaRampHi);
  if (orderCfg.orderAlpha != chainConfig_.orderAlpha || orderCfg.orderHinge != chainConfig_.orderHinge ||
      orderCfg.orderAlphaCentral != chainConfig_.orderAlphaCentral ||
      orderCfg.orderEtaRampLo != chainConfig_.orderEtaRampLo ||
      orderCfg.orderEtaRampHi != chainConfig_.orderEtaRampHi) {
    static bool once = false;
    if (!once) {
      once = true;
      printf(
          "[CHAIN KEY] order key override ON: orderAlpha=%g orderHinge=%g alphaCentral=%g eta[%g,%g]"
          " (shipped %g / %g / %g / %g / %g)\n",
          orderCfg.orderAlpha,
          orderCfg.orderHinge,
          orderCfg.orderAlphaCentral,
          orderCfg.orderEtaRampLo,
          orderCfg.orderEtaRampHi,
          chainConfig_.orderAlpha,
          chainConfig_.orderHinge,
          chainConfig_.orderAlphaCentral,
          chainConfig_.orderEtaRampLo,
          chainConfig_.orderEtaRampHi);
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
                      orderCfg);
  auto const tAfterClaimPrep = stamp();

  // Step 3: pre-claim, then the greedy claim and its braid test, as conflict-free rounds. The
  // exactness and termination argument is in ChainArbitrate.h.
  auto owner_buf = cms::alpakatools::make_device_buffer<int32_t[]>(queue_, nHits);
  auto order_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, nChainCount_);
  auto accepted_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, nChainCount_);
  auto stats_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, kChainArbStats);
  // [RESCUE bookkeeping] per-chain max-overlap accepted owner of every claim-rejected candidate,
  // and the count of its claimed hits held by OTHER owners. Filled by ChainClaimRounds, consumed
  // by the attach rescue; allocated here because both attach stages run after the claim scope.
  auto blockedBy_buf = cms::alpakatools::make_device_buffer<int32_t[]>(queue_, nChainCount_);
  auto blockedOther_buf = cms::alpakatools::make_device_buffer<int32_t[]>(queue_, nChainCount_);
  alpaka::memset(queue_, blockedBy_buf, 0xFF);  // -1 everywhere: "not a rejected candidate"
  alpaka::memset(queue_, blockedOther_buf, 0u);
  alpaka::memset(queue_, stats_buf, 0u);

  // Split points of the claim block, so its six pieces are attributable separately under
  // LST_CHAIN_TIMING. Timing only.
  auto tAlloc = tAfterClaimPrep, tPrefix = tAfterClaimPrep, tScatter = tAfterClaimPrep, tRank = tAfterClaimPrep,
       tPreClaim = tAfterClaimPrep;
  uint32_t nCandDiag = 0u;
  auto nCand_buf = cms::alpakatools::make_device_buffer<uint32_t>(queue_);
  {
    auto candOffs_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, nChainCount_ + 1u);
    auto candRecs_buf = cms::alpakatools::make_device_buffer<ChainOrderKeyRec[]>(queue_, nChainCount_);
    auto minPos_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, nHits);
    auto nClaimed_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, nChainCount_);
    auto state_buf = cms::alpakatools::make_device_buffer<uint8_t[]>(queue_, nChainCount_);
    auto part_buf = cms::alpakatools::make_device_buffer<uint8_t[]>(queue_, nChainCount_);
    alpaka::memset(queue_, owner_buf, 0xFF);   // chainarb::kFree everywhere
    alpaka::memset(queue_, minPos_buf, 0xFF);  // chainpar::kNoPos everywhere
    tAlloc = stamp();

    chainScanTimed(timing,
                   __LINE__,
                   queue_,
                   chainScan_workDiv,
                   ChainSegPrefix{},
                   candKeep_buf.data(),
                   candOffs_buf.data(),
                   nCand_buf.data(),
                   nChainCount_);
    tPrefix = stamp();
    alpaka::exec<Acc1D>(queue_,
                        chainFlat_workDiv,
                        ChainCandScatter{},
                        chainsDC_->const_view(),
                        candKeep_buf.data(),
                        candOffs_buf.data(),
                        candRecs_buf.data());
    tScatter = stamp();
    {
      auto rankPart_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, nChainCount_ * kChainRankSlices);
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
    tRank = stamp();
    if (chainConfig_.preClaim)
      alpaka::exec<Acc1D>(queue_,
                          chainFlat_workDiv,
                          ChainPreClaimPixels{},
                          trackCandidatesBaseDC_->const_view(),
                          trackCandidatesExtendedDC_->const_view(),
                          owner_buf.data(),
                          nHits);
    tPreClaim = stamp();
    chainScanTimed(timing,
                   __LINE__,
                   queue_,
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
                   blockedBy_buf.data(),
                   blockedOther_buf.data(),
                   stats_buf.data(),
                   chainConfig_);
  }
  auto const tAfterClaim = stamp();
  if (timing) {
    auto nCand_h = cms::alpakatools::make_host_buffer<uint32_t>(queue_);
    alpaka::memcpy(queue_, nCand_h, nCand_buf);
    alpaka::wait(queue_);
    nCandDiag = *nCand_h.data();
  }

  // Steps 4 and 5: the pixel attach, on the ACCEPTED chain set. Its features are built from the
  // chain's post-trim mini-doublet list.
  //
  // The pLS-side state is allocated HERE because it must outlive both attach stages: the
  // bare-triplet contention sweep, the seed crossclean and the final carried-row retirement all
  // read it (and the sweep writes it) after the chain rows have been emitted.
  //   plsOwned      the ONE-pLS-ONE-OWNER authority, shared by both stages
  //   plsBestChain  chain-side retirement evidence: every scored stage-A pair, recorded BEFORE the
  //                 delivery threshold, and read later against rpsThetaChain
  //   plsBestT3     the same for stage B, read against attachThetaT3; a revoked delivery erases
  //                 its entry here
  //   hashKey/Val   the seed-family dedup table stage A fills and stage B reuses
  //   xcPairs       the crossclean's chain-arm candidate pairs, compacted during scoring
  //   xcRetired     the crossclean verdict, consumed by the final retirement
  uint32_t const nPls = std::max(1u, pixelSize_);
  auto plsPre_buf = cms::alpakatools::make_device_buffer<AttachPlsPre[]>(queue_, nPls);
  auto plsOwned_buf = cms::alpakatools::make_device_buffer<uint8_t[]>(queue_, nPls);
  auto plsBestChain_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, nPls);
  auto plsBestT3_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, nPls);
  auto rdHashKey_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, chainattach::kSeedHashSlots);
  auto rdHashVal_buf = cms::alpakatools::make_device_buffer<int32_t[]>(queue_, chainattach::kSeedHashSlots);
  if (!rdHashOwner_.has_value())
    rdHashOwner_.emplace(cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, chainattach::kSeedHashSlots));
  constexpr uint32_t kXcPairCap = 1u << 17;  // ~131k pairs; the measured volume is far below it
  auto xcPairs_buf = cms::alpakatools::make_device_buffer<ChainXcPair[]>(queue_, kXcPairCap);
  auto xcCursor_buf = cms::alpakatools::make_device_buffer<uint32_t>(queue_);
  auto xcRetired_buf = cms::alpakatools::make_device_buffer<uint8_t[]>(queue_, nPls);
  // The mutual-best retirement flag, one byte per pLS row. Never written unless dupMutualDelta >= 0.
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
                        miniDoubletsDC_->const_view().miniDoublets(),
                        segmentsDC_->const_view().segments(),
                        rangesDC_->const_view(),
                        pixelModuleIndex_,
                        plsPre_buf.data(),
                        pixelSize_,
                        chainConfig_);

  // MEASUREMENT ONLY: arm the attach pair dump for this event (no-op unless LST_CHAIN_PAIR_DUMP).
  beginChainPairDump();

  // The stage-B TARGET UNIVERSE first: it depends only on the accepted-chain array, and the ONE
  // grid both attach stages share needs both target sets to form its union hull. Stage B's
  // SCORING still runs after stage A, because that is what honours the live ownership.
  prepareBareT3Targets(accepted_buf.data());

  attachPixels(nHits,
               accepted_buf.data(),
               blockedBy_buf.data(),
               blockedOther_buf.data(),
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

  // Stage B: the bare-triplet attach, after stage A's contention and dedup are final, because its
  // scorer reads the live ownership those produced.
  attachBareT3(
      nHits, plsPre_buf.data(), plsOwned_buf.data(), plsBestT3_buf.data(), rdHashKey_buf.data(), rdHashVal_buf.data());
  // MEASUREMENT ONLY: both scoring stages have run, so the event's pair rows are complete.
  dumpChainPairs();
  dumpChainJoin();
  // The shared grid has no reader left (the contention sweep reads only the bareT3* owner arrays),
  // so its ~10 MB items payload goes back to the caching allocator here.
  attachGridOffs_.reset();
  attachGridItems_.reset();
  attachGridEntries_ = 0;
  bareT3TgtPre_.reset();
  auto const tAfterAttach = stamp();

  // Timing split point only: nothing is launched between the attach and the row assignment.
  auto const tBeforeRows = stamp();

  // Step 6: row assignment, then emission.
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
    chainScanTimed(timing,
                   __LINE__,
                   queue_,
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
  auto const tAfterRows = stamp();
  // The contention pre-claim map (the mini-doublets of the emitted chain rows) is filled by the
  // emission itself, so it has to exist before it. Allocated at size 1 when the sweep will not run.
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
  auto const tAfterEmit = stamp();

  // ---- Step 7: bare-triplet contention and delivery -------------------------------------------
  // The stage-B owners meet a hit-overlap contention against everything already delivered and,
  // surviving it, are emitted as pT3-class rows appended after the chain rows. The pre-claim map
  // holds the mini-doublets of every EMITTED chain row; carried pixel rows contribute nothing,
  // since replacePT5 / replacePT3 dropped them in step 1.
  auto postStats_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, chainattacht3::kStats);
  alpaka::memset(queue_, postStats_buf, 0u);
  // Split points of this block, timing only.
  auto tCcAfterPrefix = tAfterEmit, tCcAfterPrep = tAfterEmit, tCcAfterEmit = tAfterEmit;
  uint32_t nDelivDiag = 0u;
  if (ccActive) {
    // The sweep is split along its only real dependence. The per-delivery lookup (a 3-level
    // dependent chase) and the row assembly (~55 stores) are pure functions of the delivery and of
    // its assigned row, so they go to the grid; the claim map and the row counter -- the only state
    // an earlier delivery writes -- stay on one thread reading a 20-byte record sequentially. The
    // exactness argument is at ChainT3CCPrep in ChainAttachT3.h.
    auto ccOffs_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, nBareT3_ + 1u);
    auto nDeliv_buf = cms::alpakatools::make_device_buffer<uint32_t>(queue_);
    auto ccOwners_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, nBareT3_);
    auto ccRecs_buf = cms::alpakatools::make_device_buffer<ChainT3CCRec[]>(queue_, nBareT3_);
    auto ccRow_buf = cms::alpakatools::make_device_buffer<int32_t[]>(queue_, nBareT3_);
    chainScanTimed(timing,
                   __LINE__,
                   queue_,
                   chainScan_workDiv,
                   ChainSegPrefix{},
                   bareT3Keep_->data(),
                   ccOffs_buf.data(),
                   nDeliv_buf.data(),
                   nBareT3_);
    tCcAfterPrefix = stamp();
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
    tCcAfterPrep = stamp();
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
    tCcAfterEmit = stamp();
    if (timing) {
      auto nDeliv_h = cms::alpakatools::make_host_buffer<uint32_t>(queue_);
      alpaka::memcpy(queue_, nDeliv_h, nDeliv_buf);
      alpaka::wait(queue_);
      nDelivDiag = *nDeliv_h.data();
    }
  }
  bareT3Targets_.reset();
  bareT3TgtPls_.reset();
  bareT3TgtLogit_.reset();
  bareT3Keep_.reset();
  auto const tAfterT3CC = stamp();

  // ---- Step 8: the pixel-seed crossclean ------------------------------------------------------
  // Anchors and candidates are both final by now, which is why this runs here: the contention sweep
  // above can still release a seed. One byte per pLS comes out and the retirement below consumes
  // it.
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
  auto const tAfterCrossClean = stamp();

  // ---- Step 9: the final carried-row retirement -----------------------------------------------
  // The contention, retirement-bar and crossclean verdicts applied to the carried bare-pLS rows,
  // with the chain rows (the last nChainTCs positions) kept verbatim. Applied once, at the end,
  // because only here is every verdict final.
  {
    uint32_t const nRowsBound = nAllocatedTCs;
    auto keep_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, std::max(1u, nRowsBound));
    auto offs_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, nRowsBound + 1u);
    auto total_buf = cms::alpakatools::make_device_buffer<uint32_t>(queue_);
    auto class_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, 6u);
    auto stage_buf = cms::alpakatools::make_device_buffer<ChainTCRowPayload[]>(queue_, std::max(1u, nRowsBound));
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
                        nRowsBound,
                        postStats_buf.data(),
                        chainConfig_);
    // The T4-class crossclean against the delivered seeded rows, applied to the KEEP array before
    // the prefix so it needs no second compaction. Two passes over the same rows: publish the
    // surviving seeded rows' outer-tracker hits, then clear the keep bit of any T4-class row
    // sharing cc9MinShared of them with one of those.
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
                          nRowsBound,
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
                          nRowsBound,
                          postStats_buf.data(),
                          chainConfig_);
    }
    chainScanTimed(timing,
                   __LINE__,
                   queue_,
                   chainScan_workDiv,
                   ChainSegPrefix{},
                   keep_buf.data(),
                   offs_buf.data(),
                   total_buf.data(),
                   nRowsBound);
    alpaka::exec<Acc1D>(queue_,
                        chainFlat_workDiv,
                        ChainTCGather{},
                        trackCandidatesBaseDC_->const_view(),
                        trackCandidatesExtendedDC_->const_view(),
                        keep_buf.data(),
                        offs_buf.data(),
                        nRowsBound,
                        stage_buf.data());
    alpaka::exec<Acc1D>(queue_,
                        chainFlat_workDiv,
                        ChainTCScatter{},
                        trackCandidatesBaseDC_->view(),
                        trackCandidatesExtendedDC_->view(),
                        stage_buf.data(),
                        offs_buf.data(),
                        nRowsBound);
    alpaka::exec<Acc1D>(queue_,
                        serial_workDiv,
                        ChainTCFinishSuppress{},
                        trackCandidatesBaseDC_->view(),
                        trackCandidatesExtendedDC_->view(),
                        offs_buf.data(),
                        class_buf.data(),
                        nRowsBound);
  }

  auto const tAfterSuppress = stamp();

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
    uint32_t const* statsHost = stats_h.data();
    lstWarning(
        std::format("[CHAIN K9] accepted={} chainTCs={} | TC slot fallbacks={} overflow={} | tieK9order={} | "
                    "R3 claimRounds={} claimStuck={} capHit={}",
                    *nAcc_h.data(),
                    *nTC_h.data(),
                    statsHost[7],
                    statsHost[8],
                    statsHost[9],
                    statsHost[11],
                    statsHost[12],
                    statsHost[13]));
    if (timing) {
      auto elapsedMs = [](auto startStamp, auto endStamp) {
        return std::chrono::duration<double, std::milli>(endStamp - startStamp).count();
      };
      lstWarning(
          std::format("[CHAIN TIMING] compact {:.3f} ms | K9 prep {:.3f} ms | K9 claim {:.3f} ms | "
                      "K8 attach {:.3f} ms | K10 rows {:.3f} ms | "
                      "K10 emit {:.3f} ms | T3CC {:.3f} ms | XC {:.3f} ms | suppress {:.3f} ms | "
                      "total {:.3f} ms",
                      elapsedMs(tStart, tAfterCompact),
                      elapsedMs(tAfterCompact, tAfterClaimPrep),
                      elapsedMs(tAfterClaimPrep, tAfterClaim),
                      elapsedMs(tAfterClaim, tAfterAttach),
                      elapsedMs(tBeforeRows, tAfterRows),
                      elapsedMs(tAfterRows, tAfterEmit),
                      elapsedMs(tAfterEmit, tAfterT3CC),
                      elapsedMs(tAfterT3CC, tAfterCrossClean),
                      elapsedMs(tAfterCrossClean, tAfterSuppress),
                      elapsedMs(tStart, tAfterSuppress)));
      lstWarning(std::format(
          "[CHAIN T4] nCand={} nChains={} nAllocTC={} nBareT3={} nDeliv={} "
          // Two different counters: slot 6 is the retirement census (one add per retired carried
          // row, a normal per-event number), slot 21 is the contention sweep's must-be-zero
          // out-of-rows alarm. They are printed separately because a nonzero slot 21 is a defect.
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
          elapsedMs(tAfterClaimPrep, tAlloc),
          elapsedMs(tAlloc, tPrefix),
          elapsedMs(tPrefix, tScatter),
          elapsedMs(tScatter, tRank),
          elapsedMs(tRank, tPreClaim),
          elapsedMs(tPreClaim, tAfterClaim),
          elapsedMs(tAfterEmit, tCcAfterPrefix),
          elapsedMs(tCcAfterPrefix, tCcAfterPrep),
          elapsedMs(tCcAfterPrep, tCcAfterEmit),
          post_h.data()[4],
          post_h.data()[9]));
      lstWarning(std::format("[CHAIN K8] {}", attachSummary_));
    }
  }
}

void LSTEvent::attachPixels(unsigned int nHits,
                            uint32_t* accepted,
                            int32_t const* blockedBy,
                            int32_t const* blockedOther,
                            AttachPlsPre const* plsPre,
                            uint8_t* plsOwned,
                            uint32_t* plsBestChain,
                            uint32_t* hashKey,
                            int32_t* hashVal,
                            ChainXcPair* xcPairs,
                            uint32_t* xcCursor,
                            uint32_t xcCap,
                            uint8_t* plsMutual) {
  // Attach stage A: pixel seeds onto accepted chains. Four steps --
  //   target list   accepted chains long enough to deliver, in accepted (best-first) order, plus a
  //                 score-only tail of 4-layer chains that joins the grid bounds and the scored
  //                 pairs but is never delivered
  //   grid          the shared candidate prefilter over the pLS collection (buildAttachGrid)
  //   score         the exact analytic window, then the pair head, reduced into a per-target argmax
  //   contend       one pLS to one owner, then the seed-family dedup
  // The row upgrade itself is applied by ChainEmitTCs, and the carried-row retirement is the final
  // pass of arbitrateChains, because it must see the contention and crossclean verdicts.
  attachSummary_.clear();
  if (nChainCount_ == 0 || !chainsDC_.has_value())
    return;

  bool const timing = chainTimingEnabled();
  auto stamp = [&]() {
    if (timing)
      alpaka::wait(queue_);
    return std::chrono::steady_clock::now();
  };
  auto const tStageAStart = stamp();

  auto const serial_workDiv = cms::alpakatools::make_workdiv<Acc1D>(1, 1);
  auto const chainFlat_workDiv = cms::alpakatools::make_workdiv<Acc1D>(max_blocks, 256);
  auto const chainScan_workDiv = cms::alpakatools::make_workdiv<Acc1D>(1, kChainScanBlockThreads);

  uint32_t const nPls = std::max(1u, pixelSize_);

  // The stage-A target list: accepted, long enough, dca-eligible, kept in accepted order because
  // the contention tie-break reads that order. The per-pLS records arrive pre-built in plsPre.
  auto targets_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, nChainCount_);
  auto nTargets_buf_d = cms::alpakatools::make_device_buffer<uint32_t>(queue_);
  auto tgtKeep_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, nChainCount_);
  auto tgtOffs_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, nChainCount_ + 1u);
  {
    // A filtered copy of the accepted array, order-preserving through a prefix sum.
    alpaka::exec<Acc1D>(queue_,
                        chainFlat_workDiv,
                        ChainTargetFlags{},
                        chainsDC_->const_view(),
                        accepted,
                        nChainCount_,
                        tgtKeep_buf.data(),
                        chainConfig_);
    chainScanTimed(timing,
                   __LINE__,
                   queue_,
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
  auto const tAfterTargetList = stamp();
  // [RESCUE] the claim-rescue targets, appended after the accepted-deliverable list and BEFORE
  // the aux 4-layer tail so that position-is-class still holds:
  //   [0, nEvidence)        accepted deliverable targets
  //   [nEvidence, nTargets) rescue deliverable targets (no evidence, no cross-clean)
  //   [nTargets, nTgtAll)   the score-only aux 4-layer tail
  // The select updates the deliverable count in place, so every delivery kernel below picks the
  // rescues up without change; nEvidence is drained beside the other two counts. With the rescue
  // off the kernel is skipped and nEvidence == nTargets, which is bit-identical to before.
  auto nEvidence_buf_d = cms::alpakatools::make_device_buffer<uint32_t>(queue_);
  if (chainConfig_.attachRescue)
    alpaka::exec<Acc1D>(queue_,
                        serial_workDiv,
                        ChainRescueSelect{},
                        chainsDC_->const_view(),
                        blockedBy,
                        blockedOther,
                        nChainCount_,
                        nTargets_buf_d.data(),
                        targets_buf.data(),
                        nEvidence_buf_d.data(),
                        chainConfig_);
  else
    alpaka::memcpy(queue_, nEvidence_buf_d, nTargets_buf_d);
  // The auxiliary 4-layer accepted targets, appended after the stage-A list. They are score-only --
  // never delivered -- but they join the GRID BOUNDS so the radial hull covers them. That does not
  // disturb stage A: a wider hull only adds candidates, and the exact analytic predicate re-filters
  // them identically.
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
  auto const tAfterAuxSelect = stamp();
  auto nTargets_buf_h = cms::alpakatools::make_host_buffer<uint32_t>(queue_);
  auto nTgtAll_buf_h = cms::alpakatools::make_host_buffer<uint32_t>(queue_);
  auto nEvidence_buf_h = cms::alpakatools::make_host_buffer<uint32_t>(queue_);
  alpaka::memcpy(queue_, nTargets_buf_h, nTargets_buf_d);
  alpaka::memcpy(queue_, nTgtAll_buf_h, nTgtAll_buf_d);
  alpaka::memcpy(queue_, nEvidence_buf_h, nEvidence_buf_d);
  alpaka::wait(queue_);  // the target counts size every attach buffer below
  uint32_t const nTargets = *nTargets_buf_h.data();
  uint32_t const nTgtAll = *nTgtAll_buf_h.data();
  uint32_t const nEvidence = *nEvidence_buf_h.data();
  if (nTargets == 0 || pixelSize_ == 0)
    return;
  auto const tAfterCountDrain = stamp();

  auto tgtPre_buf = cms::alpakatools::make_device_buffer<AttachTargetPre[]>(queue_, nTgtAll);
  alpaka::exec<Acc1D>(queue_,
                      chainFlat_workDiv,
                      ChainAttachTargetPre{},
                      miniDoubletsDC_->const_view().miniDoublets(),
                      chainItemsDC_->const_view(),
                      chainsDC_->const_view(),
                      targets_buf.data(),
                      nTgtAll,
                      tgtPre_buf.data(),
                      chainConfig_);
  auto const tAfterTargetPre = stamp();

  // THE ONE GRID, over the UNION of this stage's hull (all targets, the deliverable ones plus the
  // auxiliary 4-layer list) and the bare-triplet hull. Built here so both stages share it.
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
  auto const tAfterGrid = stamp();

  // Scoring: candidate iteration over the grid, the exact analytic predicate, the pair features and
  // the pair head. plsBestChain is written for EVERY scored pair BEFORE the banded delivery
  // threshold, because it is retirement evidence rather than a delivery record. The crossclean's
  // chain-arm candidate pairs are collected in the same pass, for the deliverable targets and for
  // the score-only 4-layer tail alike.
  auto stats_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, chainattach::kStats);
  auto tgtPls_buf = cms::alpakatools::make_device_buffer<int32_t[]>(queue_, nTargets);
  auto tgtLogit_buf = cms::alpakatools::make_device_buffer<float[]>(queue_, nTargets);
  auto tgtKey_buf = cms::alpakatools::make_device_buffer<uint64_t[]>(queue_, nTargets);
  // The pre-threshold argmax key, for the mutual-best retirement test. 8 B per target (~10 kB at
  // PU200) and it is never written unless cfg.dupMutualDelta >= 0.
  auto tgtKeyPre_buf = cms::alpakatools::make_device_buffer<uint64_t[]>(queue_, nTargets);
  alpaka::memset(queue_, stats_buf, 0u);
  alpaka::memset(queue_, tgtKey_buf, 0);  // key 0 = "no pair reached the margin"
  alpaka::memset(queue_, tgtKeyPre_buf, 0);
  // The device slice count (see ChainAttachScore): nS threads cooperate on one target's candidate
  // walk, so the launch has to be nTargets * nS wide instead of nTargets wide. Host backends run
  // one thread per target and ignore it.
  uint32_t const attachSlices =
      cms::alpakatools::requires_single_thread_per_block_v<Acc1D> ? 1u : chainAttachScoreSlices();
  // nTgtAll, not nTargets: the launch carries the auxiliary 4-layer target list too, and each
  // target's candidate walk is sliced attachSlices ways. uniform_elements would stride over any
  // shortfall, but sizing it correctly keeps the auxiliary threads off other targets' backs.
  auto const attachScore_workDiv = cms::alpakatools::make_workdiv<Acc1D>(
      std::max<uint32_t>(max_blocks, (nTgtAll * attachSlices + 255u) / 256u), 256);
  alpaka::exec<Acc1D>(queue_,
                      attachScore_workDiv,
                      ChainAttachScore{},
                      plsPre,
                      tgtPre_buf.data(),
                      nTargets,
                      nTgtAll,
                      nEvidence,
                      offsets_buf.data(),
                      items_buf.data(),
                      tgtKey_buf.data(),
                      tgtKeyPre_buf.data(),
                      plsBestChain,
                      xcPairs,
                      xcCursor,
                      xcCap,
                      stats_buf.data(),
                      // MEASUREMENT ONLY: nullptr unless LST_CHAIN_PAIR_DUMP is set. Stage A is kept
                      // WHOLE (keep factor 1); only stage B is downsampled.
                      pairDumpRowPtr(),
                      pairDumpCtlPtr(),
                      chainPairDumpCap(),
                      1u,
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
  auto const tAfterScore = stamp();

  // Contention, then the seed-family dedup (two pLS are the same seed when they share enough pixel
  // hit rows, so only one of a family may deliver).
  auto order_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, nTargets);
  // Split points of the contend stage, timing only: up to a3rd the one-pLS-one-owner argmax, then
  // the dedup, whose hash walk is the one piece of the attach that stays sequential; inside that,
  // after the gather + concurrent table build, after the parallel partner prefilter, and after the
  // serial greedy. The remainder of the window is ChainAttachPublish.
  auto tAfterResolve = tAfterScore;
  auto tAfterOwnerHits = tAfterScore;
  auto tAfterSeedConflicts = tAfterScore;
  auto tAfterSeedDedup = tAfterScore;
  {
    // The one-pLS-one-owner rule is an argmax, so it is a packed atomicMax. The dedup visits owners
    // in the gathered ascending-position (accepted) order, which needs no sort at all -- an O(n^2)
    // selection sort here cost 7-10 ms per event on the device, one thread chasing a global load
    // per comparison. The hash walk is split into a concurrent table build, a parallel partner
    // prefilter and a greedy over the few flagged owners; ChainAttachSeedDedup carries the argument
    // for why that leaves the same verdicts.
    auto plsKey_buf = cms::alpakatools::make_device_buffer<uint64_t[]>(queue_, nPls);
    auto ownKeep_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, nTargets);
    auto ownOffs_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, nTargets + 1u);
    auto nOwners_buf = cms::alpakatools::make_device_buffer<uint32_t>(queue_);
    auto ownerHits_buf =
        cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, size_t{nTargets} * kMaxPLSHitsInHitsSoA);
    auto ownerNHits_buf = cms::alpakatools::make_device_buffer<uint8_t[]>(queue_, nTargets);
    auto ownerPls_buf = cms::alpakatools::make_device_buffer<int32_t[]>(queue_, nTargets);
    // The owner index behind each dedup table entry, and the prefilter's per-owner verdict.
    // Neither needs initialising: hashOwner is only ever read at a slot whose key matched (so the
    // thread that claimed the key wrote it), and ownCont is written for every owner before it is
    // read. Neither is a new per-event allocation either -- see rdHashOwner_ in LSTEvent.h for why
    // that matters here: the owner-tag array is the persistent member, and the per-owner verdict
    // BORROWS ownOffs_buf, which is dead the moment ChainCompactSelect has consumed it.
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
    tAfterResolve = stamp();
    if (chainConfig_.attachSeedDedup) {
      chainScanTimed(timing,
                     __LINE__,
                     queue_,
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
                          hashKey,  // stage A builds the dedup table here, concurrently
                          hashVal,
                          hashOwner,
                          stats_buf.data(),
                          chainattach::kSeedOwnerStageA);
      tAfterOwnerHits = stamp();
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
      tAfterSeedConflicts = stamp();
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
      tAfterSeedDedup = stamp();
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
        tAfterSeedDedup = stamp();
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
  // [RESCUE] the seed-driven claim swap, after everything of stage A has resolved. Runs before
  // stage B and before the row assignment, which is what lets a swapped-in chain emit through the
  // ordinary path and a revoked rescue release its seed cleanly.
  if (chainConfig_.attachRescue && nTargets > nEvidence) {
    auto rescueStats_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, 5u);
    alpaka::memset(queue_, rescueStats_buf, 0u);
    alpaka::exec<Acc1D>(queue_,
                        serial_workDiv,
                        ChainRescueSwap{},
                        chainsDC_->view(),
                        static_cast<uint32_t const*>(targets_buf.data()),
                        nEvidence,
                        nTargets,
                        blockedBy,
                        blockedOther,
                        accepted,
                        plsOwned,
                        rescueStats_buf.data(),
                        chainConfig_);
    static bool const rescueDebug = [] {
      char const* dbg = std::getenv("LST_CHAIN_RESCUE_DEBUG");
      return dbg != nullptr && *dbg != '\0' && *dbg != '0';
    }();
    if (rescueDebug) {
      auto rescueStats_h = cms::alpakatools::make_host_buffer<uint32_t[]>(queue_, 5u);
      alpaka::memcpy(queue_, rescueStats_h, rescueStats_buf);
      alpaka::wait(queue_);
      std::printf("[rescue] targets=%u won=%u swapped=%u revBlockerSeeded=%u revNotLonger=%u revOther=%u\n",
                  nTargets - nEvidence,
                  rescueStats_h.data()[0],
                  rescueStats_h.data()[1],
                  rescueStats_h.data()[2],
                  rescueStats_h.data()[3],
                  rescueStats_h.data()[4]);
    }
  }
  auto const tAfterPublish = stamp();

  attachGridAudit(nTargets, plsPre, tgtPre_buf.data(), offsets_buf.data(), items_buf.data());

  if (timing || objectsStatistics_) {
    auto stats_h = cms::alpakatools::make_host_buffer<uint32_t[]>(queue_, chainattach::kStats);
    alpaka::memcpy(queue_, stats_h, stats_buf);
    alpaka::wait(queue_);
    uint32_t const* statsHost = stats_h.data();
    auto elapsedMs = [](auto startStamp, auto endStamp) {
      return std::chrono::duration<double, std::milli>(endStamp - startStamp).count();
    };
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
        statsHost[1],
        statsHost[10],
        statsHost[2],
        statsHost[3],
        statsHost[4],
        statsHost[5],
        statsHost[11],
        statsHost[7],
        statsHost[9],
        statsHost[13],
        statsHost[14],
        statsHost[15],
        elapsedMs(tStageAStart, tAfterTargetPre),
        elapsedMs(tStageAStart, tAfterTargetList),
        elapsedMs(tAfterTargetList, tAfterAuxSelect),
        elapsedMs(tAfterAuxSelect, tAfterCountDrain),
        elapsedMs(tAfterCountDrain, tAfterTargetPre),
        bareT3PreMs_,
        attachGridMs_,
        attachGridSummary_,
        elapsedMs(tAfterGrid, tAfterScore),
        elapsedMs(tAfterScore, tAfterResolve),
        elapsedMs(tAfterResolve, tAfterPublish),
        elapsedMs(tAfterResolve, tAfterOwnerHits),
        elapsedMs(tAfterOwnerHits, tAfterSeedConflicts),
        elapsedMs(tAfterSeedConflicts, tAfterSeedDedup),
        elapsedMs(tAfterSeedDedup, tAfterPublish));
  }
}

void LSTEvent::attachGridAudit(unsigned int nTargets,
                               AttachPlsPre const* plsPre,
                               AttachTargetPre const* tgtPre,
                               uint32_t const* offsets,
                               AttachPlsPre const* items) {
  // GRID SUPERSET VERIFICATION. Off unless LST_CHAIN_ATTACH_AUDIT is set. Per event it runs the
  // EXHAUSTIVE scan -- every (target, pLS) pair through the analytic windows -- and checks that
  // every pair the scan accepts is present in the grid's candidate list for that target, which is
  // the property the grid must have for it to be a pure prefilter. It also reports the candidate
  // volume, so the grid-vs-scan probe ratio is measured rather than assumed.
  char const* auditEnv = std::getenv("LST_CHAIN_ATTACH_AUDIT");
  if (auditEnv == nullptr || *auditEnv == '\0' || *auditEnv == '0')
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
  uint32_t const* auditHost = audit_h.data();
  static std::atomic<uint32_t> auditEvt{0};
  lstWarning(
      std::format("[CHAIN K8 AUDIT] evt={} targets={} pLS={} exactPairs={} gridCand={} "
                  "gridPass={} MISSING={} supersetHolds={}",
                  auditEvt.fetch_add(1),
                  nTargets,
                  pixelSize_,
                  auditHost[0],
                  auditHost[1],
                  auditHost[2],
                  auditHost[3],
                  auditHost[3] == 0u ? "YES" : "NO"));
}

// The stage-B TARGET UNIVERSE ONLY, split out of attachBareT3. It is called BEFORE stage A because
// the shared grid's hull needs both target sets, and nothing in it reads a stage-A result:
// ChainAttachT3MarkConsumed and ChainAttachT3Keep depend only on the accepted-chain array, the
// chain items and the node features, all of which are final before the attach block starts.
void LSTEvent::prepareBareT3Targets(uint32_t const* accepted) {
  nBareT3_ = 0;
  bareT3Targets_.reset();
  bareT3TgtPre_.reset();
  bareT3PreMs_ = 0.;
  if (nChainNodes_ == 0 || pixelSize_ == 0 || !chainsDC_.has_value() || !chainNodesDC_.has_value() ||
      !chainItemsDC_.has_value() || !tripletsDC_.has_value() || !segmentsDC_.has_value())
    return;

  bool const timing = chainTimingEnabled();
  auto const tPrepareStart = std::chrono::steady_clock::now();
  auto const chainFlat_workDiv = cms::alpakatools::make_workdiv<Acc1D>(max_blocks, 256);
  auto const chainScan_workDiv = cms::alpakatools::make_workdiv<Acc1D>(1, kChainScanBlockThreads);
  uint32_t const nNodes = nChainNodes_;

  // The bare-triplet universe: every triplet that no ACCEPTED chain consumed, admitted on its own
  // fake score (t3FakeMax) so that a target rejected here is never scored and never becomes
  // retirement evidence for a seed.
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
                      chainConfig_.t3FakeMax,  // target admission on the triplet fake score
                      keep_buf.data(),
                      nNodes);
  chainScanTimed(timing,
                 __LINE__,
                 queue_,
                 chainScan_workDiv,
                 ChainSegPrefix{},
                 keep_buf.data(),
                 offs_buf.data(),
                 nBare_d.data(),
                 nNodes);
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
    bareT3PreMs_ = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tPrepareStart).count();
  }
}

// THE ONE ATTACH GRID: a candidate prefilter binning the pixel seeds so a target visits only the
// seeds that can possibly pass its analytic window. Count / prefix / scatter, no sort anywhere,
// over the UNION of the two stages' per-r-bin radial hulls. Both stages share it because a union
// hull is a SUPERSET generator and both scorers apply the exact predicate before any observable
// write; ChainAttachGridBounds carries that argument.
void LSTEvent::buildAttachGrid(AttachPlsPre const* plsPre,
                               AttachTargetPre const* targetsA,
                               uint32_t nTargetsA,
                               AttachTargetPre const* targetsB,
                               uint32_t nTargetsB) {
  attachGridOffs_.reset();
  attachGridItems_.reset();
  attachGridEntries_ = 0;
  attachGridSummary_.clear();
  attachGridMs_ = 0.;
  uint32_t const nPls = std::max(1u, pixelSize_);
  if (pixelSize_ == 0 || (nTargetsA + nTargetsB) == 0)
    return;

  bool const timing = chainTimingEnabled();
  auto stamp = [&]() {
    if (timing)
      alpaka::wait(queue_);
    return std::chrono::steady_clock::now();
  };
  auto const tGridStart = stamp();
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
                      targetsA,
                      nTargetsA,
                      targetsB,
                      nTargetsB,
                      rMin_buf.data(),
                      rMax_buf.data());

  auto masks_buf = cms::alpakatools::make_device_buffer<uint16_t[]>(queue_, size_t{nPls} * kAttachRBins);
  auto counts_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, kAttachCells);
  auto offsets_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, kAttachCells + 1u);
  auto cursor_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, kAttachCells);
  alpaka::memset(queue_, masks_buf, 0x00);
  alpaka::memset(queue_, counts_buf, 0x00);
  alpaka::memset(queue_, cursor_buf, 0x00);
  auto const tAfterBounds = stamp();
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
  auto const tAfterCount = stamp();
  auto nEntries_buf_d = cms::alpakatools::make_device_buffer<uint32_t>(queue_);
  // Timed like every other single-block scan: with both stages on one grid this is the only grid
  // prefix in the event, so it belongs in the same [CHAIN SCAN] table as the rest.
  chainScanTimed(timing,
                 __LINE__,
                 queue_,
                 chainScan_workDiv,
                 ChainSegPrefix{},
                 counts_buf.data(),
                 offsets_buf.data(),
                 nEntries_buf_d.data(),
                 kAttachCells);
  auto const tAfterPrefix = stamp();
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
    std::string hullText;
    for (int rBin = 0; rBin < kAttachRBins; ++rBin)
      hullText += (rMin_h.data()[rBin] == 0xFFFFFFFFu) ? std::format(" {}:empty", rBin)
                                                       : std::format(" {}:[{:.2f},{:.2f}]",
                                                                     rBin,
                                                                     std::bit_cast<float>(rMin_h.data()[rBin]),
                                                                     std::bit_cast<float>(rMax_h.data()[rBin]));
    lstWarning(std::format(
        "[U4 HULL U] nA={} nB={} nPls={} entries={} hull{}", nTargetsA, nTargetsB, pixelSize_, nEntries, hullText));
  }
  auto const tAfterDrain = stamp();

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
  auto const tAfterScatter = stamp();
  attachGridOffs_.emplace(std::move(offsets_buf));
  attachGridItems_.emplace(std::move(items_buf));
  attachGridEntries_ = nEntries;
  if (timing || objectsStatistics_) {
    auto elapsedMs = [](auto startStamp, auto endStamp) {
      return std::chrono::duration<double, std::milli>(endStamp - startStamp).count();
    };
    attachGridMs_ = elapsedMs(tGridStart, tAfterScatter);
    attachGridSummary_ = std::format("g1bounds {:.3f} g2count {:.3f} g3prefix {:.3f} g4drain {:.3f} g5scatter {:.3f}",
                                     elapsedMs(tGridStart, tAfterBounds),
                                     elapsedMs(tAfterBounds, tAfterCount),
                                     elapsedMs(tAfterCount, tAfterPrefix),
                                     elapsedMs(tAfterPrefix, tAfterDrain),
                                     elapsedMs(tAfterDrain, tAfterScatter));
  }
}

void LSTEvent::attachBareT3(unsigned int nHits,
                            AttachPlsPre const* plsPre,
                            uint8_t* plsOwned,
                            uint32_t* plsBestT3,
                            uint32_t* hashKey,
                            int32_t* hashVal) {
  // Attach stage B (kernels in ChainAttachT3.h): the bare-triplet target universe built by
  // prepareBareT3Targets, scored against the LIVE ownership array on the SHARED grid, then the
  // stage-B contention and its half of the seed-family dedup -- against the table stage A left in
  // hashKey/Val, so a seed already delivering a pT5-class object blocks its siblings here. The
  // surviving owner arrays stay in the bareT3* members for the contention sweep and emission that
  // run after the chain rows exist. LST_CHAIN_T3_AUDIT enables the grid-superset audit on this
  // stage's geometry.
  attachT3Summary_.clear();
  bareT3TgtPls_.reset();
  bareT3TgtLogit_.reset();
  bareT3Keep_.reset();
  if (nBareT3_ == 0 || !bareT3Targets_.has_value() || !bareT3TgtPre_.has_value() || !attachGridOffs_.has_value())
    return;

  float const theta = chainConfig_.attachThetaT3;  // global: this stage's margin has no eta bands
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
  auto const tBeforeScore = stamp();

  // Score, against the LIVE ownership array left by stage A. The owner-side buffers persist in the
  // bareT3* members: the contention sweep in arbitrateChains consumes them after the chain rows
  // have been emitted.
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
  uint32_t const t3Slices = cms::alpakatools::requires_single_thread_per_block_v<Acc1D> ? 1u : chainAttachScoreSlices();
  auto const t3Score_workDiv =
      cms::alpakatools::make_workdiv<Acc1D>(std::max<uint32_t>(max_blocks, (nBare * t3Slices + 255u) / 256u), 256);
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
                      // MEASUREMENT ONLY: nullptr unless LST_CHAIN_PAIR_DUMP is set. Stage B is
                      // downsampled 1-in-chainPairDumpDsB() by attachPairKeep's identity hash.
                      pairDumpRowPtr(),
                      pairDumpCtlPtr(),
                      chainPairDumpCap(),
                      chainPairDumpDsB(),
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
  auto const tAfterScore = stamp();

  // Contention plus the stage-B half of the seed dedup. The winners publish straight into the LIVE
  // plsOwned, which keeps one pLS to one owner across both stages. Same decomposition as stage A
  // (packed argmax key, then the serial hash residue in the gathered ascending-row order, no sort),
  // one form on both backends. keep[] survives in bareT3Keep_ because after the dedup revocations
  // it flags exactly the DELIVERIES, which is what the contention sweep gathers on.
  bareT3Keep_.emplace(cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, nBare));
  // Split points of the dedup window, timing only: after the argmax + compaction, after the gather
  // + concurrent table build, after the parallel partner prefilter, after the serial greedy. The
  // remainder of the window is ChainAttachPublish.
  auto tAfterArgmaxGather = tAfterScore;
  auto tAfterOwnerHits = tAfterScore;
  auto tAfterSeedConflicts = tAfterScore;
  auto tAfterSeedDedup = tAfterScore;
  {
    auto plsKey_buf = cms::alpakatools::make_device_buffer<uint64_t[]>(queue_, nPls);
    auto ownOffs_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, nBare + 1u);
    auto nOwners_buf = cms::alpakatools::make_device_buffer<uint32_t>(queue_);
    auto owners_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, nBare);
    auto ownerPls_buf = cms::alpakatools::make_device_buffer<int32_t[]>(queue_, nBare);
    auto ownerHits_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, size_t{nBare} * kMaxPLSHitsInHitsSoA);
    auto ownerNHits_buf = cms::alpakatools::make_device_buffer<uint8_t[]>(queue_, nBare);
    // No extra per-event allocations, for the reason given at rdHashOwner_ in LSTEvent.h: the owner
    // tag behind each table entry is that persistent member, which stage A already allocates, and
    // the per-owner prefilter word BORROWS ownOffs_buf, dead the moment ChainCompactSelect has
    // consumed it. Neither needs initialising -- hashOwner is only read at a slot whose key matched
    // (so the inserting thread wrote it) and ownCont is written for every owner before it is read.
    uint32_t* const hashOwner = rdHashOwner_->data();
    uint32_t* const ownCont = ownOffs_buf.data();
    // Restores the purely serial walk (incremental table build inside the greedy) in this same
    // binary, so that a comparison against the split form cannot be a build artefact.
    static bool const rdtSerial = [] {
      char const* envText = std::getenv("LST_CHAIN_RDT_SERIAL");
      return envText != nullptr && *envText != '\0' && *envText != '0';
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
    chainScanTimed(timing,
                   __LINE__,
                   queue_,
                   chainScan_workDiv,
                   ChainSegPrefix{},
                   bareT3Keep_->data(),
                   ownOffs_buf.data(),
                   nOwners_buf.data(),
                   nBare);
    alpaka::exec<Acc1D>(queue_,
                        chainFlat_workDiv,
                        ChainCompactSelect{},
                        bareT3Keep_->data(),
                        ownOffs_buf.data(),
                        nBare,
                        nullptr,
                        owners_buf.data(),
                        nullptr);
    tAfterArgmaxGather = stamp();
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
      tAfterOwnerHits = stamp();
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
      tAfterSeedConflicts = stamp();
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
      tAfterSeedDedup = stamp();
    } else {
      tAfterOwnerHits = tAfterArgmaxGather;
      tAfterSeedConflicts = tAfterArgmaxGather;
      tAfterSeedDedup = tAfterArgmaxGather;
    }
    // The surviving grants become the LIVE ownership. One thread per position, and it covers the
    // dedup-disabled configuration too: a revoked owner and a losing target are both a negative
    // grant, so the publish needs no knowledge of which of the two happened.
    alpaka::exec<Acc1D>(queue_,
                        chainFlat_workDiv,
                        ChainAttachPublish{},
                        chainsDC_->const_view(),
                        static_cast<uint32_t const*>(nullptr),
                        static_cast<int32_t const*>(bareT3TgtPls_->data()),  // position-keyed grant
                        nBare,
                        plsOwned,
                        static_cast<uint32_t*>(nullptr));  // stats[4] is the later delivery count
  }
  auto const tAfterPublish = stamp();

  if (timing || objectsStatistics_) {
    auto statsH = cms::alpakatools::make_host_buffer<uint32_t[]>(queue_, chainattacht3::kStats);
    alpaka::memcpy(queue_, statsH, stats_buf);
    alpaka::wait(queue_);
    uint32_t const* statsHost = statsH.data();
    auto elapsedMs = [](auto startStamp, auto endStamp) {
      return std::chrono::duration<double, std::milli>(endStamp - startStamp).count();
    };
    attachT3Summary_ = std::format(
        "bareT3={} ofNodes={} pLS={} gridEntries={} cand={} dup={} scored={} overTheta={} withCand={} "
        "picks={} rdtRevoked={} theta={:.3f} t3FakeMax={:.4g} | "
        "rdtOwners={} rdtCont={} rdtByStageA={} rdtWalked={} rdtMaxEnt={} rdtMaxSeen={} rdtCapDrop={} "
        "rdtIns={} rdtInsRefused={} rdtHashOverflow={} | "
        // No `grid` term: the grid is shared, so its cost is charged once, in stage A's breakdown.
        "pre {:.3f} ms | score {:.3f} ms | contend {:.3f} ms "
        "(argmax+gather {:.3f} table {:.3f} prefilter {:.3f} greedy {:.3f} publish {:.3f})",
        nBare,
        nNodes,
        nPls,
        nEntries,
        statsHost[1],
        statsHost[10],
        statsHost[2],
        statsHost[11],
        statsHost[8],
        statsHost[3],
        statsHost[5],
        theta,
        chainConfig_.t3FakeMax,
        statsHost[19],
        statsHost[13],
        statsHost[12],
        statsHost[20],
        statsHost[14],
        statsHost[15],
        statsHost[16],
        statsHost[17],
        statsHost[18],
        statsHost[7],
        bareT3PreMs_,
        elapsedMs(tBeforeScore, tAfterScore),
        elapsedMs(tAfterScore, tAfterPublish),
        elapsedMs(tAfterScore, tAfterArgmaxGather),
        elapsedMs(tAfterArgmaxGather, tAfterOwnerHits),
        elapsedMs(tAfterOwnerHits, tAfterSeedConflicts),
        elapsedMs(tAfterSeedConflicts, tAfterSeedDedup),
        elapsedMs(tAfterSeedDedup, tAfterPublish));
    lstWarning(std::format("[CHAIN K8B] {}", attachT3Summary_));
  }

  // The grid superset audit on this stage's target geometry. Same kernel and same must-be-zero
  // MISSING counter as stage A's; it is O(nTargets x nPls), so it has a gate of its own.
  char const* auditEnv = std::getenv("LST_CHAIN_T3_AUDIT");
  if (auditEnv != nullptr && *auditEnv != '\0' && *auditEnv != '0') {
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
    uint32_t const* auditHost = auditH.data();
    static std::atomic<uint32_t> t3AuditEvt{0};
    lstWarning(
        std::format("[CHAIN K8B AUDIT] evt={} bareT3={} pLS={} exactPairs={} gridCand={} "
                    "gridPass={} MISSING={} supersetHolds={}",
                    t3AuditEvt.fetch_add(1),
                    nBare,
                    nPls,
                    auditHost[0],
                    auditHost[1],
                    auditHost[2],
                    auditHost[3],
                    auditHost[3] == 0u ? "YES" : "NO"));
  }
  alpaka::wait(queue_);  // the scratch buffers above die with this scope
}

void LSTEvent::dumpChainTCs() {
  // Candidate-level sidecar. Off unless LST_CHAIN_TC_DUMP names an output file. One record per
  // event holding, for EVERY track candidate row, its type and its outer-tracker hit rows in the
  // tracking-ntuple hit numbering, so that two runs (or two builds) can be compared as multisets
  // of hit sets rather than through the ntuple.
  char const* path = std::getenv("LST_CHAIN_TC_DUMP");
  if (path == nullptr || *path == '\0' || !trackCandidatesBaseDC_.has_value())
    return;

  alpaka::wait(queue_);

  auto base = getTrackCandidatesBase();
  auto extended = getTrackCandidatesExtended();
  uint32_t const nTrackCands = base.nTrackCandidates();

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
  std::FILE* dumpFile = std::fopen(path, (ievt == 0) ? "wb" : "ab");
  if (dumpFile == nullptr)
    return;
  auto put32 = [&](uint32_t value) { std::fwrite(&value, sizeof(value), 1, dumpFile); };

  put32(0x50323354u);  // 'P23T'
  put32(ievt);
  put32(nTrackCands);
  for (uint32_t tcRow = 0; tcRow < nTrackCands; ++tcRow) {
    std::vector<uint32_t> otHitRows;
    for (int slot = 0; slot < Params_TC::kLayers; ++slot) {
      if (extended.lowerModuleIndices()[tcRow][slot] == kTCEmptyLowerModule)
        continue;
      if (extended.logicalLayers()[tcRow][slot] == 0)
        continue;  // pixel layer slot
      for (int hitInLayer = 0; hitInLayer < Params_TC::kHitsPerLayer; ++hitInLayer) {
        unsigned int const hitRow = base.hitIndices()[tcRow][slot][hitInLayer];
        if (hitRow == kTCEmptyHitIdx)
          continue;
        otHitRows.push_back(hitIdx[hitRow]);
      }
    }
    put32(static_cast<uint32_t>(base.trackCandidateType()[tcRow]));
    put32(static_cast<uint32_t>(otHitRows.size()));
    for (uint32_t hitRow : otHitRows)
      put32(hitRow);
  }
  std::fclose(dumpFile);
}

void LSTEvent::beginChainPairDump() {
  // MEASUREMENT ONLY (LST_CHAIN_PAIR_DUMP). Allocates the row buffer on first use and zeroes the
  // control block for this event. Returns immediately -- with no allocation, no memset and no
  // queue traffic -- when the variable is unset, which is the shipped configuration.
  if (chainPairDumpPath() == nullptr)
    return;
  if (!pairDumpRows_.has_value()) {
    pairDumpRows_.emplace(cms::alpakatools::make_device_buffer<ChainAttachPairRow[]>(queue_, chainPairDumpCap()));
    pairDumpCtl_.emplace(cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, kAttachPairCtl));
  }
  alpaka::memset(queue_, *pairDumpCtl_, 0u);
}

void LSTEvent::dumpChainPairs() {
  // MEASUREMENT ONLY (LST_CHAIN_PAIR_DUMP): the on-policy training rows for the attach head. Called
  // after BOTH attach scoring stages, so one record holds the whole event. It only READS device
  // memory the scorers wrote into a buffer nothing else touches; the ntuple and the track candidate
  // collection are identical with and without it.
  //
  // Byte layout: standalone/analysis/DNN/PAIRDUMP_FORMAT.md.
  char const* path = chainPairDumpPath();
  if (path == nullptr || !pairDumpRows_.has_value() || !pairDumpCtl_.has_value())
    return;

  alpaka::wait(queue_);

  uint32_t const cap = chainPairDumpCap();
  uint32_t ctl[kAttachPairCtl] = {0u, 0u};
  {
    auto host_view = cms::alpakatools::make_host_view(ctl, kAttachPairCtl);
    auto dev_view = cms::alpakatools::make_device_view(queue_, pairDumpCtl_->data(), kAttachPairCtl);
    alpaka::memcpy(queue_, host_view, dev_view);
    alpaka::wait(queue_);
  }
  uint32_t const nRows = std::min(ctl[0], cap);

  std::vector<ChainAttachPairRow> rows(nRows);
  if (nRows > 0) {
    auto host_view = cms::alpakatools::make_host_view(rows.data(), nRows);
    auto dev_view = cms::alpakatools::make_device_view(queue_, pairDumpRows_->data(), nRows);
    alpaka::memcpy(queue_, host_view, dev_view);
    alpaka::wait(queue_);
  }

  uint32_t const nT3 = static_cast<uint32_t>(tripletsDC_->view().triplets().metadata().size());

  // Sequential event counter, as in every other sidecar here: the file is only unambiguous for a
  // single-stream run, which is how the dump is taken.
  static std::atomic<uint32_t> pairEventCounter{0};
  uint32_t const ievt = pairEventCounter.fetch_add(1);
  static std::mutex pairDumpMutex;
  std::lock_guard<std::mutex> lock(pairDumpMutex);
  std::FILE* dumpFile = std::fopen(path, (ievt == 0) ? "wb" : "ab");
  if (dumpFile == nullptr)
    return;
  auto put32 = [&](uint32_t v) { std::fwrite(&v, sizeof(v), 1, dumpFile); };
  put32(0x50414952u);  // 'PAIR'
  // Format 2 appends kAttachProbeColumns probe floats after the head inputs on every row. A version
  // 1 reader must REJECT this file rather than read the head inputs and silently mis-stride.
  put32(2u);
  put32(ievt);
  put32(nChainCount_);
  put32(nT3);
  put32(nRows);
  put32(ctl[1]);  // rows dropped on capacity overflow
  put32(1u);      // stage-A downsample factor (kept whole)
  put32(chainPairDumpDsB());
  put32(static_cast<uint32_t>(kAttachFeatures));
  put32(kAttachProbeColumns);  // probe floats following the head inputs on each row
  static_assert(
      sizeof(ChainAttachPairRow) == 4 * sizeof(uint32_t) + (kAttachFeatures + kAttachProbeColumns) * sizeof(float),
      "the pair record must be densely packed for the raw dump");
  if (nRows > 0)
    std::fwrite(rows.data(), sizeof(ChainAttachPairRow), nRows, dumpFile);
  std::fclose(dumpFile);
}

void LSTEvent::dumpChainJoin() {
  // MEASUREMENT ONLY (LST_CHAIN_JOIN_DUMP). The pair dump carries IDENTITIES (chain row, sparse
  // triplet row, pLS row) and no geometry a truth matcher can use; this sidecar closes that gap with
  // the two lookup tables an offline labeller needs, and nothing else. It is called from exactly the
  // two places dumpChainPairs() is, so record i of this file is record i of the pair file, and the
  // (nChains, nT3) pair is written as a fingerprint. No sim information is read or written.
  char const* path = std::getenv("LST_CHAIN_JOIN_DUMP");
  if (path == nullptr || *path == '\0')
    return;

  alpaka::wait(queue_);

  auto pullTo = [&](auto* hostPtr, auto column, unsigned int nElements) {
    if (nElements == 0u)
      return;
    auto host_view = cms::alpakatools::make_host_view(hostPtr, nElements);
    auto dev_view = cms::alpakatools::make_device_view(queue_, column, nElements);
    alpaka::memcpy(queue_, host_view, dev_view);
    alpaka::wait(queue_);
  };

  uint32_t const nNodes = (chainNodesDC_.has_value()) ? nChainNodes_ : 0u;
  uint32_t const nT3 = static_cast<uint32_t>(tripletsDC_->view().triplets().metadata().size());
  uint32_t const nLS = static_cast<uint32_t>(segmentsDC_->view().segments().metadata().size());
  uint32_t const nMD = static_cast<uint32_t>(miniDoubletsDC_->view().miniDoublets().metadata().size());
  uint32_t const nHits = static_cast<uint32_t>(lstInputDC_->const_view().hits().metadata().size());
  uint32_t const nPls = pixelSize_;

  std::vector<uint32_t> tripletIndex(nNodes);
  pullTo(tripletIndex.data(), chainNodesDC_->view().tripletIndex(), nNodes);
  std::vector<ArrayUx2> t3Seg(nT3);
  std::vector<Params_LS::ArrayUxLayers> lsMD(nLS);
  std::vector<unsigned int> mdAnchor(nMD), mdOuter(nMD), hitIdx(nHits);
  pullTo(t3Seg.data(), tripletsDC_->view().triplets().segmentIndices(), nT3);
  pullTo(lsMD.data(), segmentsDC_->view().segments().mdIndices(), nLS);
  pullTo(mdAnchor.data(), miniDoubletsDC_->view().miniDoublets().anchorHitIndices(), nMD);
  pullTo(mdOuter.data(), miniDoubletsDC_->view().miniDoublets().outerHitIndices(), nMD);
  pullTo(hitIdx.data(), lstInputDC_->const_view().hits().idxs(), nHits);

  std::vector<unsigned int> seedIdx(nPls);
  std::vector<float> plsEta(nPls);
  pullTo(seedIdx.data(), lstInputDC_->const_view().pixelSeeds().seedIdx(), nPls);
  pullTo(plsEta.data(), lstInputDC_->const_view().pixelSeeds().eta(), nPls);

  static std::atomic<uint32_t> joinEventCounter{0};
  uint32_t const ievt = joinEventCounter.fetch_add(1);
  static std::mutex joinDumpMutex;
  std::lock_guard<std::mutex> lock(joinDumpMutex);
  std::FILE* dumpFile = std::fopen(path, (ievt == 0) ? "wb" : "ab");
  if (dumpFile == nullptr)
    return;
  auto put32 = [&](uint32_t v) { std::fwrite(&v, sizeof(v), 1, dumpFile); };
  auto putFloat = [&](float v) { std::fwrite(&v, sizeof(v), 1, dumpFile); };

  put32(0x504A3031u);  // 'PJ01'
  put32(ievt);
  put32(nChainCount_);
  put32(nT3);
  put32(nNodes);
  put32(nPls);
  for (uint32_t n = 0; n < nNodes; ++n) {
    uint32_t const t3 = tripletIndex[n];
    unsigned int const innerSeg = t3Seg[t3][0];
    unsigned int const outerSeg = t3Seg[t3][1];
    unsigned int const md[3] = {lsMD[innerSeg][0], lsMD[innerSeg][1], lsMD[outerSeg][1]};
    put32(t3);
    for (int k = 0; k < 3; ++k) {
      put32(hitIdx[mdAnchor[md[k]]]);
      put32(hitIdx[mdOuter[md[k]]]);
    }
  }
  for (uint32_t p = 0; p < nPls; ++p) {
    put32(static_cast<uint32_t>(seedIdx[p]));
    putFloat(plsEta[p]);
  }
  std::fclose(dumpFile);
}

void LSTEvent::dumpChains() {
  // Chain-level sidecar. Off unless LST_CHAIN_CHAIN_DUMP names an output file; the ntuple and the
  // track candidate collection are untouched either way. One record per chain: its geometry, its
  // gate logits and margins, its feature row, and its member nodes, edge families and hit rows.
  char const* path = std::getenv("LST_CHAIN_CHAIN_DUMP");
  if (path == nullptr || *path == '\0' || !chainsDC_.has_value())
    return;

  alpaka::wait(queue_);

  auto pullTo = [&](auto* hostPtr, auto column, unsigned int nElements) {
    auto host_view = cms::alpakatools::make_host_view(hostPtr, nElements);
    auto dev_view = cms::alpakatools::make_device_view(queue_, column, nElements);
    alpaka::memcpy(queue_, host_view, dev_view);
    alpaka::wait(queue_);
  };

  uint32_t const nChains = nChainCount_;
  uint32_t const nItems = 3u * nChainWeldedNodes_;
  auto chainsView = chainsDC_->view();
  auto itemsView = chainItemsDC_->view();

  std::vector<uint32_t> nodeOffset(nChains);
  std::vector<uint16_t> nNodes(nChains), nMDs(nChains);
  std::vector<uint8_t> nLayers(nChains), flags(nChains);
  std::vector<int8_t> branch(nChains), trimAction(nChains);
  std::vector<float> score(nChains), dcaXY(nChains), zFake(nChains), zPrompt(nChains), zDisp(nChains), marginP(nChains),
      marginD(nChains), marginX(nChains);
  std::vector<float> feats(static_cast<size_t>(nChains) * Params_ChainFeat::kFeatures);
  pullTo(nodeOffset.data(), chainsView.nodeOffset(), nChains);
  pullTo(nNodes.data(), chainsView.nNodes(), nChains);
  pullTo(nMDs.data(), chainsView.nMDs(), nChains);
  pullTo(nLayers.data(), chainsView.nLayers(), nChains);
  pullTo(flags.data(), chainsView.flags(), nChains);
  pullTo(branch.data(), chainsView.branch(), nChains);
  pullTo(trimAction.data(), chainsView.trimAction(), nChains);
  pullTo(score.data(), chainsView.score(), nChains);
  pullTo(dcaXY.data(), chainsView.dcaXY(), nChains);
  pullTo(zFake.data(), chainsView.zFake(), nChains);
  pullTo(zPrompt.data(), chainsView.zPrompt(), nChains);
  pullTo(zDisp.data(), chainsView.zDisp(), nChains);
  pullTo(marginP.data(), chainsView.marginP(), nChains);
  pullTo(marginD.data(), chainsView.marginD(), nChains);
  pullTo(marginX.data(), chainsView.marginX(), nChains);
  {
    static_assert(sizeof(Params_ChainFeat::ArrayFxFeat) == sizeof(float) * Params_ChainFeat::kFeatures,
                  "chain feature rows must be densely packed for the debug dump");
    auto host_view =
        cms::alpakatools::make_host_view(reinterpret_cast<Params_ChainFeat::ArrayFxFeat*>(feats.data()), nChains);
    auto dev_view = cms::alpakatools::make_device_view(queue_, chainsView.features(), nChains);
    alpaka::memcpy(queue_, host_view, dev_view);
    alpaka::wait(queue_);
  }

  std::vector<uint32_t> nodeItems(nItems), edgeItems(nItems), mdItems(nItems);
  pullTo(nodeItems.data(), itemsView.nodeItems(), nItems);
  pullTo(edgeItems.data(), itemsView.edgeItems(), nItems);
  pullTo(mdItems.data(), itemsView.mdItems(), nItems);

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
    std::vector<unsigned int> hitIdxTmp(nHitsTotal);
    auto host_view = cms::alpakatools::make_host_view(hitIdxTmp.data(), nHitsTotal);
    auto dev_view = cms::alpakatools::make_device_view(queue_, lstInputDC_->const_view().hits().idxs(), nHitsTotal);
    alpaka::memcpy(queue_, host_view, dev_view);
    alpaka::wait(queue_);
    hitIdx = std::move(hitIdxTmp);
  }

  static std::atomic<uint32_t> eventCounter{0};
  uint32_t const ievt = eventCounter.fetch_add(1);
  static std::mutex dumpMutex;
  std::lock_guard<std::mutex> lock(dumpMutex);
  std::FILE* dumpFile = std::fopen(path, (ievt == 0) ? "wb" : "ab");
  if (dumpFile == nullptr)
    return;
  auto put32 = [&](uint32_t value) { std::fwrite(&value, sizeof(value), 1, dumpFile); };
  auto putf = [&](float value) { std::fwrite(&value, sizeof(value), 1, dumpFile); };

  put32(0x50323243u);  // 'P22C'
  put32(ievt);
  put32(nChainNodes_);
  put32(nEdges);
  put32(nChains);
  for (uint32_t chainIdx = 0; chainIdx < nChains; ++chainIdx) {
    uint32_t const nodeBase = nodeOffset[chainIdx];
    uint32_t const nNodesOfChain = nNodes[chainIdx];
    uint32_t const nMDsOfChain = nMDs[chainIdx];
    int const drop = trimAction[chainIdx];
    // Pre-trim geometry, recovered from the endpoint move: the untrimmed node run always starts at
    // (off - 1) for an inner drop and at off otherwise, and is one node longer whenever a drop
    // happened. This lets one record carry both the pre-trim weld and the post-trim chain.
    uint32_t const preOff = (drop == 1) ? nodeBase - 1u : nodeBase;
    uint32_t const preN = (drop == 0) ? nNodesOfChain : nNodesOfChain + 1u;
    put32(preN);
    put32(nNodesOfChain);
    put32(nMDsOfChain);
    put32(nLayers[chainIdx]);
    put32(static_cast<uint32_t>(static_cast<int32_t>(branch[chainIdx])));
    put32(static_cast<uint32_t>(drop));
    put32(flags[chainIdx]);
    putf(score[chainIdx]);
    putf(dcaXY[chainIdx]);
    putf(zFake[chainIdx]);
    putf(zPrompt[chainIdx]);
    putf(zDisp[chainIdx]);
    putf(marginP[chainIdx]);
    putf(marginD[chainIdx]);
    putf(marginX[chainIdx]);
    for (int k = 0; k < Params_ChainFeat::kFeatures; ++k)
      putf(feats[static_cast<size_t>(chainIdx) * Params_ChainFeat::kFeatures + k]);
    for (uint32_t k = 0; k < preN; ++k)
      put32(nodeItems[preOff + k]);
    for (uint32_t k = 0; k + 1 < preN; ++k)
      put32(edgeType[edgeItems[preOff + k]]);
    for (uint32_t k = 0; k < nMDsOfChain; ++k) {
      uint32_t const mdIdx = mdItems[3u * nodeBase + k];
      put32(hitIdx[mdAnchorHit[mdIdx]]);
      put32(hitIdx[mdOuterHit[mdIdx]]);
    }
  }
  std::fclose(dumpFile);
}

// Terminal-variant probe sidecar. Emits, in the SAME byte layout dumpChains uses, so that one
// reader and one offline truth join serve both, ONE record per TERMINAL VARIANT of every PRE-TRIM
// chain with nNodes >= 3:
//     preN = nNodes = the variant's node count      branch = variant id (0 full, 1 inner-dropped,
//     nMDs / nLayers = the variant's MD union                          2 outer-dropped)
//     score = the variant's COMBINED-FIT chi2       drop   = the chain index within the event
//     dcaXY / logits / margins / features           hits   = the variant's own MD hit list
// so the chi2 rule's decision (a ratio of the score column) and the head's decision (the margin
// columns) can be compared against each variant's own truth label on the same rows.
void LSTEvent::dumpChainVariants(char const* path, uint32_t const* innerMDDev, float const* probeDev) {
  if (path == nullptr || *path == '\0' || !chainsDC_.has_value())
    return;

  alpaka::wait(queue_);

  auto pullTo = [&](auto* hostPtr, auto column, unsigned int nElements) {
    auto host_view = cms::alpakatools::make_host_view(hostPtr, nElements);
    auto dev_view = cms::alpakatools::make_device_view(queue_, column, nElements);
    alpaka::memcpy(queue_, host_view, dev_view);
    alpaka::wait(queue_);
  };

  uint32_t const nChains = nChainCount_;
  uint32_t const nItems = 3u * nChainWeldedNodes_;
  auto chainsView = chainsDC_->view();
  auto itemsView = chainItemsDC_->view();

  std::vector<uint32_t> nodeOffset(nChains);
  std::vector<uint16_t> nNodes(nChains), nMDs(nChains);
  std::vector<uint8_t> nLayers(nChains);
  pullTo(nodeOffset.data(), chainsView.nodeOffset(), nChains);
  pullTo(nNodes.data(), chainsView.nNodes(), nChains);
  pullTo(nMDs.data(), chainsView.nMDs(), nChains);
  pullTo(nLayers.data(), chainsView.nLayers(), nChains);

  std::vector<uint32_t> nodeItems(nItems), edgeItems(nItems), mdItems(nItems), innerMD(nItems);
  pullTo(nodeItems.data(), itemsView.nodeItems(), nItems);
  pullTo(edgeItems.data(), itemsView.edgeItems(), nItems);
  pullTo(mdItems.data(), itemsView.mdItems(), nItems);
  pullTo(innerMD.data(), innerMDDev, nItems);

  std::vector<float> probe(static_cast<size_t>(nChains) * chaintrim::kProbeWords);
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
  for (uint32_t chainIdx = 0; chainIdx < nChains; ++chainIdx)
    if (nNodes[chainIdx] >= 3)
      nRec += chaintrim::kVariants;

  static std::atomic<uint32_t> variantEventCounter{0};
  uint32_t const ievt = variantEventCounter.fetch_add(1);
  static std::mutex variantDumpMutex;
  std::lock_guard<std::mutex> lock(variantDumpMutex);
  std::FILE* dumpFile = std::fopen(path, (ievt == 0) ? "wb" : "ab");
  if (dumpFile == nullptr)
    return;
  auto put32 = [&](uint32_t value) { std::fwrite(&value, sizeof(value), 1, dumpFile); };
  auto putf = [&](float value) { std::fwrite(&value, sizeof(value), 1, dumpFile); };

  put32(0x50323243u);  // 'P22C'
  put32(ievt);
  put32(nChainNodes_);
  put32(nEdges);
  put32(nRec);
  for (uint32_t chainIdx = 0; chainIdx < nChains; ++chainIdx) {
    if (nNodes[chainIdx] < 3)
      continue;
    uint32_t const nodeBase = nodeOffset[chainIdx];
    float const* probeRow = probe.data() + static_cast<size_t>(chainIdx) * chaintrim::kProbeWords;
    for (int variantIdx = 0; variantIdx < chaintrim::kVariants; ++variantIdx) {
      float const* varWords = probeRow + 3 + variantIdx * chaintrim::kVarWords;
      uint32_t const variantNodeBase = (variantIdx == 1) ? nodeBase + 1u : nodeBase;
      uint32_t const nNodesVar = (variantIdx == 0) ? nNodes[chainIdx] : nNodes[chainIdx] - 1u;
      uint32_t const nMDsVar = static_cast<uint32_t>(varWords[0]);
      uint32_t const* mdListVar = (variantIdx == 1) ? &innerMD[3u * nodeBase] : &mdItems[3u * nodeBase];
      put32(nNodesVar);
      put32(nNodesVar);
      put32(nMDsVar);
      put32(static_cast<uint32_t>(varWords[1]));
      put32(static_cast<uint32_t>(variantIdx));
      put32(chainIdx);
      put32(0u);
      putf(probeRow[variantIdx]);  // the variant's combined-fit chi2, in the score slot
      putf(varWords[2]);           // dcaXY
      putf(varWords[3]);
      putf(varWords[4]);
      putf(varWords[5]);
      putf(varWords[4] - varWords[3]);
      putf(varWords[5] - varWords[3]);
      putf((varWords[4] > varWords[5] ? varWords[4] : varWords[5]) - varWords[3]);
      for (int k = 0; k < Params_ChainFeat::kFeatures; ++k)
        putf(varWords[6 + k]);
      for (uint32_t k = 0; k < nNodesVar; ++k)
        put32(nodeItems[variantNodeBase + k]);
      for (uint32_t k = 0; k + 1 < nNodesVar; ++k)
        put32(edgeType[edgeItems[variantNodeBase + k]]);
      for (uint32_t k = 0; k < nMDsVar; ++k) {
        uint32_t const mdIdx = mdListVar[k];
        put32(hitIdx[mdAnchorHit[mdIdx]]);
        put32(hitIdx[mdOuterHit[mdIdx]]);
      }
    }
  }
  std::fclose(dumpFile);
}

void LSTEvent::createTrackCandidates(bool no_pls_dupclean, bool tc_pls_triplets) {
  // The chain pipeline is the only outer-tracker track builder here, so what remains of the
  // upstream candidate sequence is the pixel-seed self-cleaning (CheckHitspLS pass 2; pass 1 ran in
  // pixelLineSegmentCleaning), the bare-pLS admission, and arbitrateChains, which claims,
  // attaches and emits every T5-, T4-, pT5- and pT3-class row.

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
  // Allocation = admitted bare-pLS rows + one row per welded chain (the accepted set is a subset of
  // those) + headroom for the stage-B pT3-class deliveries (~120/event measured; the delivery sweep
  // guards the bound and counts any overflow).
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

  // There is no CrossCleanpLS here: it read the T5 / pT5 / pT3 rows that the chain pipeline
  // replaces, so it has nothing left to clean against at this point. The bare-pLS universe is
  // {isQuad && isDup == 0} after both CheckHitspLS self-clean passes, and the chain path's own seed
  // crossclean and retirement act on the admitted rows later, inside arbitrateChains.
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

  // The chain pipeline's output stage: the carried-row compaction, the hit claim, the attach and
  // the assembly. Everything above this line runs exactly as it does upstream.
  {
    alpaka::wait(queue_);  // fence the pLS admission so the chain-TC stamp attributes correctly
    auto const chainTC0 = std::chrono::steady_clock::now();
    arbitrateChains(nTotal);
    chainTCMs_ = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - chainTC0).count();
  }

  // The candidate sidecar is written here rather than at the end of arbitrateChains so that it also
  // covers a collection built without the chain pipeline, which is what measures the pre-existing
  // reproducibility floor at hit level.
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
