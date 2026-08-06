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
#include "ChainParallel.h"
#include "ChainWeld.h"
#include "Hit.h"
#include "Kernels.h"
#include "MiniDoublet.h"
#include "Segment.h"
#include "TrackCandidate.h"
#include "Triplet.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <mutex>
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

  // P1 RE-BASELINE INSTRUMENT, default OFF (LST_DUP_SNAPSHOTS; the standalone driver sets it
  // for --allobj). BOOKKEEPING ONLY: it copies isDup columns to the host at the points where a
  // later kernel overwrites them, so the ntuple can record states the final collection has
  // lost. No kernel is added, removed or reordered and nothing reads the copies back.
  bool dupSnapshotsEnabled() {
    static bool const enabled = (std::getenv("LST_DUP_SNAPSHOTS") != nullptr);
    return enabled;
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
      alpaka::exec<Acc1D>(queue_,
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
            "[MEM] ChainIncidence: {} dense MD keys + {} dense LS keys allocated ({:.1f} MB)",
            nMDKeys,
            nLSKeys,
            mb));
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

  alpaka::exec<Acc1D>(queue_,
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

  alpaka::exec<Acc1D>(queue_, chainScan_workDiv, ChainPrefixIncidence{}, chainMdIncidenceDC_->view(), nMDKeys);
  alpaka::exec<Acc1D>(queue_, chainScan_workDiv, ChainPrefixIncidence{}, chainLsIncidenceDC_->view(), nLSKeys);

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

    unsigned long long edges = 0;
    unsigned int nonEmptyKeys = 0;
    unsigned int maxDegIn = 0, maxDegOut = 0;
    bool monotonic = true;
    for (unsigned int k = 0; k < nKeys; ++k) {
      monotonic = monotonic && outOff[k + 1] >= outOff[k] && inOff[k + 1] >= inOff[k] &&
                  prodPrefix[k + 1] >= prodPrefix[k];
      unsigned int const degOut = outOff[k + 1] - outOff[k];
      unsigned int const degIn = inOff[k + 1] - inOff[k];
      edges += static_cast<unsigned long long>(degIn) * degOut;
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
    bool const permutationOk =
        !duplicated && !outOfBounds && coveredOut == nChainNodes_ && coveredIn == nChainNodes_;
    lstWarning(std::format(
        "[CHAIN] {}: keys={} used={} sumDegOut={} sumDegIn={} nT3={} maxDegIn={} maxDegOut={} "
        "E={} prefixE={} degreeSum={} monotonic={} permutation={}",
        name,
        nKeys,
        nonEmptyKeys,
        outOff[nKeys],
        inOff[nKeys],
        nChainNodes_,
        maxDegIn,
        maxDegOut,
        edges,
        prodPrefix[nKeys],
        degreeOk ? "ok" : "FAIL",
        monotonic ? "ok" : "FAIL",
        permutationOk ? "ok" : "FAIL"));
    if (edges != prodPrefix[nKeys])
      lstWarning(std::format("[CHAIN] {}: EDGE PREFIX MISMATCH", name));
    return static_cast<unsigned int>(edges);
  };

  unsigned int const e1 = checkFamily("MD/E1", mdInc, nodes.mdT3OutItems(), nodes.mdT3InItems());
  unsigned int const e2 = checkFamily("LS/E2", lsInc, nodes.lsT3OutItems(), nodes.lsT3InItems());
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
  if (nEdges == 0)
    return;

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
                      nChainE2Edges_);

  auto const t2 = stamp();

  // Debug tap: with LST_CHAIN_FEAT_DUMP set, K5 also stores its 14 edge floats so the parity
  // comparison can localize a mismatch to a feature instead of only seeing the logit.
  static std::atomic<uint32_t> featDumpEvent{0};
  char const* featPath = std::getenv("LST_CHAIN_FEAT_DUMP");
  bool const wantFeat = (featPath != nullptr && *featPath != '\0' && featDumpEvent.fetch_add(1) == 0);
  auto featBuf = cms::alpakatools::make_device_buffer<float[]>(
      queue_, wantFeat ? static_cast<size_t>(nEdges) * kChainEdgeFeatures : size_t{1});

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
                      wantFeat ? featBuf.data() : nullptr);

  auto const t3 = stamp();
  if (timing) {
    auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
    lstWarning(std::format("[CHAIN TIMING] nodes={} edges={} | K3 nodeFeatures {:.3f} ms | "
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

    std::FILE* f = std::fopen(featPath, "wb");
    if (f != nullptr) {
      auto put32 = [&](uint32_t v) { std::fwrite(&v, sizeof(v), 1, f); };
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

  for (int sweep = 0; sweep < kChainWeldSweeps; ++sweep) {
    // A fixed sweep count, no host sync: the reference's "break when nothing welded" early exit is
    // a CPU nicety and a zero-weld sweep is idempotent.
    alpaka::memset(queue_, bestOut_buf, 0);
    alpaka::memset(queue_, bestIn_buf, 0);
    alpaka::exec<Acc1D>(queue_,
                        chainFlat_workDiv,
                        ChainWeldArgmax{},
                        chainEdgesDC_->const_view(),
                        outWeld_buf.data(),
                        inWeld_buf.data(),
                        bestOut_buf.data(),
                        bestIn_buf.data(),
                        chainConfig_.thetaEdge);
    alpaka::exec<Acc1D>(queue_,
                        chainFlat_workDiv,
                        ChainWeldMutual{},
                        chainEdgesDC_->const_view(),
                        outWeld_buf.data(),
                        inWeld_buf.data(),
                        bestOut_buf.data(),
                        bestIn_buf.data(),
                        chainConfig_.thetaEdge);
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

  alpaka::exec<Acc1D>(queue_,
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
  if (objectsStatistics_) {
    double mb =
        (alpaka::getExtentProduct(chainsDC_->buffer()) + alpaka::getExtentProduct(chainItemsDC_->buffer())) / 1e6;
    memoryAllocatedMB_ += mb;
    lstWarning(std::format("[MEM] Chains: {} chains / {} member nodes allocated ({:.1f} MB)",
                           nChainCount_,
                           nChainWeldedNodes_,
                           mb));
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

  if (chainConfig_.terminalTrim && chainConfig_.trimFactor > 0.f) {
    for (int pass = 0; pass < chainConfig_.trimPasses; ++pass)
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
    lstWarning(std::format("[CHAIN TIMING] chains={} weldedNodes={} | K6ab weld {:.3f} ms | "
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
  // gather/scatter through a staging array, on every backend (one form, ChainParallel.h).
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
    alpaka::exec<Acc1D>(
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
                      chainConfig_);
  auto const t2 = stamp();

  // K9a / K9b / K9c: pre-claim, greedy claim, braid, as conflict-free rounds (see ChainParallel.h).
  auto owner_buf = cms::alpakatools::make_device_buffer<int32_t[]>(queue_, nHits);
  auto order_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, nChainCount_);
  auto accepted_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, nChainCount_);
  auto stats_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, kChainArbStats);
  alpaka::memset(queue_, stats_buf, 0u);

  {
    // P2.6a: the same walk, reached by conflict-free rounds instead of a single thread. See the
    // exactness / termination argument at the top of ChainParallel.h.
    auto candOffs_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, nChainCount_ + 1u);
    auto candRecs_buf = cms::alpakatools::make_device_buffer<ChainOrderKeyRec[]>(queue_, nChainCount_);
    auto nCand_buf = cms::alpakatools::make_device_buffer<uint32_t>(queue_);
    auto minPos_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, nHits);
    auto nClaimed_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, nChainCount_);
    auto state_buf = cms::alpakatools::make_device_buffer<uint8_t[]>(queue_, nChainCount_);
    auto part_buf = cms::alpakatools::make_device_buffer<uint8_t[]>(queue_, nChainCount_);
    alpaka::memset(queue_, owner_buf, 0xFF);    // chainarb::kFree everywhere
    alpaka::memset(queue_, minPos_buf, 0xFF);   // chainpar::kNoPos everywhere

    alpaka::exec<Acc1D>(queue_,
                        chainScan_workDiv,
                        ChainSegPrefix{},
                        candKeep_buf.data(),
                        candOffs_buf.data(),
                        nCand_buf.data(),
                        nChainCount_);
    alpaka::exec<Acc1D>(queue_,
                        chainFlat_workDiv,
                        ChainCandScatter{},
                        chainsDC_->const_view(),
                        candKeep_buf.data(),
                        candOffs_buf.data(),
                        candRecs_buf.data());
    alpaka::exec<Acc1D>(queue_,
                        chainFlat_workDiv,
                        ChainClaimRank{},
                        candRecs_buf.data(),
                        nCand_buf.data(),
                        nChainCount_,
                        order_buf.data());
    if (chainConfig_.preClaim)
      alpaka::exec<Acc1D>(queue_,
                          chainFlat_workDiv,
                          ChainPreClaimPixels{},
                          trackCandidatesBaseDC_->const_view(),
                          trackCandidatesExtendedDC_->const_view(),
                          owner_buf.data(),
                          nHits);
    alpaka::exec<Acc1D>(queue_,
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
  constexpr uint32_t kXcPairCap = 1u << 17;  // ~131k pairs; measured pass-1 volume is far below
  auto xcPairs_buf = cms::alpakatools::make_device_buffer<ChainXcPair[]>(queue_, kXcPairCap);
  auto xcCursor_buf = cms::alpakatools::make_device_buffer<uint32_t>(queue_);
  auto xcRetired_buf = cms::alpakatools::make_device_buffer<uint8_t[]>(queue_, nPls);
  alpaka::memset(queue_, plsOwned_buf, 0u);
  alpaka::memset(queue_, plsBestChain_buf, 0u);  // orderFloat(-inf) == 0
  alpaka::memset(queue_, plsBestT3_buf, 0u);
  alpaka::memset(queue_, rdHashKey_buf, 0xFF);  // chainattach::kSeedHashEmpty everywhere
  alpaka::memset(queue_, xcCursor_buf, 0u);
  alpaka::memset(queue_, xcRetired_buf, 0u);
  if (pixelSize_ > 0)
    alpaka::exec<Acc1D>(queue_,
                        chainFlat_workDiv,
                        ChainAttachPlsPre{},
                        lstInputDC_->const_view().pixelSeeds(),
                        pixelSegmentsDC_->const_view(),
                        plsPre_buf.data(),
                        pixelSize_,
                        chainConfig_);

  attachPixels(nHits,
               accepted_buf.data(),
               plsPre_buf.data(),
               plsOwned_buf.data(),
               plsBestChain_buf.data(),
               rdHashKey_buf.data(),
               rdHashVal_buf.data(),
               xcPairs_buf.data(),
               xcCursor_buf.data(),
               kXcPairCap);

  // Stage B: the bare-T3 attach. After stage A's contention and -RD dedup are final (its scorer
  // honours the live ownership), before the extension (its inputs are extension-invariant) and
  // long before the retirement.
  attachBareT3(nHits, accepted_buf.data(), plsPre_buf.data(), plsOwned_buf.data(), plsBestT3_buf.data(),
               rdHashKey_buf.data(), rdHashVal_buf.data());
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
                        rowKeep_buf.data());
    alpaka::exec<Acc1D>(queue_,
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
  if (ccActive) {
    // Compact the keep[] deliveries into an ascending-position list (prefix + compaction preserve
    // position order) so the serial sweep walks the ~n deliveries instead of skip-scanning every
    // target. No rank: the sweep order is the row order (see ChainT3CCSweepEmit).
    auto ccOffs_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, nBareT3_ + 1u);
    auto nDeliv_buf = cms::alpakatools::make_device_buffer<uint32_t>(queue_);
    auto ccOwners_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, nBareT3_);
    alpaka::exec<Acc1D>(queue_,
                        chainScan_workDiv,
                        ChainSegPrefix{},
                        bareT3Keep_->data(),
                        ccOffs_buf.data(),
                        nDeliv_buf.data(),
                        nBareT3_);
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
                        serial_workDiv,
                        ChainT3CCSweepEmit{},
                        modules_.const_view().modules(),
                        miniDoubletsDC_->const_view().miniDoublets(),
                        segmentsDC_->const_view().segments(),
                        tripletsDC_->const_view().triplets(),
                        lstInputDC_->const_view().hits(),
                        lstInputDC_->const_view().pixelSeeds(),
                        chainNodesDC_->const_view(),
                        chainsDC_->view(),
                        trackCandidatesBaseDC_->view(),
                        trackCandidatesExtendedDC_->view(),
                        bareT3Targets_->data(),
                        bareT3TgtPls_->data(),
                        ccOwners_buf.data(),
                        nDeliv_buf.data(),
                        ccClaimed_buf.data(),
                        plsOwned_buf.data(),
                        plsBestT3_buf.data(),
                        chainConfig_.ccMinShared,
                        nHits,
                        pixelModuleIndex_,
                        nAllocatedTCs,
                        postStats_buf.data());
    alpaka::wait(queue_);  // the scratch buffers above die with this scope
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
    auto anchorEta_buf = cms::alpakatools::make_device_buffer<float[]>(queue_, nPls);
    auto anchorPhi_buf = cms::alpakatools::make_device_buffer<float[]>(queue_, nPls);
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
                        anchorEta_buf.data(),
                        anchorPhi_buf.data(),
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
                        anchorEta_buf.data(),
                        anchorPhi_buf.data(),
                        nAnchors_buf.data(),
                        pixelSize_,
                        nHits,
                        nAllocatedTCs,
                        xcRetired_buf.data(),
                        xcStats_buf.data(),
                        chainConfig_);
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
    alpaka::wait(queue_);  // the anchor scratch dies with this scope
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
                        pixelSize_,
                        keep_buf.data(),
                        class_buf.data(),
                        nIn,
                        postStats_buf.data(),
                        chainConfig_);
    alpaka::exec<Acc1D>(
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
  alpaka::wait(queue_);  // every consumer of the pls-side scratch above has run

  auto const t5 = stamp();

  if (timing || objectsStatistics_) {
    auto stats_h = cms::alpakatools::make_host_buffer<uint32_t[]>(queue_, kChainArbStats);
    auto nAcc_h = cms::alpakatools::make_host_buffer<uint32_t>(queue_);
    auto nTC_h = cms::alpakatools::make_host_buffer<uint32_t>(queue_);
    alpaka::memcpy(queue_, stats_h, stats_buf);
    alpaka::memcpy(queue_, nAcc_h, cms::alpakatools::make_device_view(queue_, chainsDC_->view().nAccepted()));
    alpaka::memcpy(queue_, nTC_h, cms::alpakatools::make_device_view(queue_, chainsDC_->view().nChainTCs()));
    alpaka::wait(queue_);
    uint32_t const* s = stats_h.data();
    lstWarning(std::format(
        "[CHAIN K9] accepted={} chainTCs={} | TC slot fallbacks={} overflow={} | tieK9order={} | "
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
      lstWarning(std::format("[CHAIN TIMING] compact {:.3f} ms | K9 prep {:.3f} ms | K9 claim {:.3f} ms | "
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
                            uint32_t xcCap) {
  // Chain-tracking phase P2.4 (port map section 5, K8a-K8c) at the CHAINFINAL2 flags
  // (-A 4 -a 5.0 -a2 5.0 -a3 6.0 -RT5 1 -RT3 1 -RPS 1 -RPSA 5.5 -RD 1 -D4 1e9 -CCS 6.0
  // -CCS2 5.0 -XC 3 -XC4 1). Reference: prototype/PixelAttach.cc, prototype/AttachDelivery.cc
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
    alpaka::exec<Acc1D>(queue_,
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
  // A15 -XC4 / A14 -CCS: the aux 4-layer accepted targets, appended after the stage-A list. They
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
                      nTgtAll_buf_d.data());
  auto nTargets_buf_h = cms::alpakatools::make_host_buffer<uint32_t>(queue_);
  auto nTgtAll_buf_h = cms::alpakatools::make_host_buffer<uint32_t>(queue_);
  alpaka::memcpy(queue_, nTargets_buf_h, nTargets_buf_d);
  alpaka::memcpy(queue_, nTgtAll_buf_h, nTgtAll_buf_d);
  alpaka::wait(queue_);  // the target counts size every attach buffer below
  uint32_t const nTargets = *nTargets_buf_h.data();
  uint32_t const nTgtAll = *nTgtAll_buf_h.data();
  if (nTargets == 0 || pixelSize_ == 0)
    return;

  auto tgtPre_buf = cms::alpakatools::make_device_buffer<AttachTargetPre[]>(queue_, nTgtAll);
  alpaka::exec<Acc1D>(queue_,
                      chainFlat_workDiv,
                      ChainAttachTargetPre{},
                      miniDoubletsDC_->const_view().miniDoublets(),
                      segmentsDC_->const_view().segments(),
                      tripletsDC_->const_view().triplets(),
                      chainNodesDC_->const_view(),
                      chainItemsDC_->const_view(),
                      chainsDC_->const_view(),
                      targets_buf.data(),
                      nTgtAll,
                      tgtPre_buf.data());
  auto const a1 = stamp();

  // K8a: the grid. Count / prefix / scatter, no sort anywhere (maintainer policy). The bounds run
  // over ALL targets (stage-A 5+ plus the aux 4-layer list).
  auto rMin_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, kAttachRBins);
  auto rMax_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, kAttachRBins);
  alpaka::memset(queue_, rMin_buf, 0xFF);  // 0xFFFFFFFF = "no target in this bin"
  alpaka::memset(queue_, rMax_buf, 0x00);
  alpaka::exec<Acc1D>(
      queue_, chainFlat_workDiv, ChainAttachGridBounds{}, tgtPre_buf.data(), nTgtAll, rMin_buf.data(), rMax_buf.data());

  auto masks_buf = cms::alpakatools::make_device_buffer<uint16_t[]>(queue_, size_t{nPls} * kAttachRBins);
  auto counts_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, kAttachCells);
  auto offsets_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, kAttachCells + 1u);
  auto cursor_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, kAttachCells);
  alpaka::memset(queue_, masks_buf, 0x00);
  alpaka::memset(queue_, counts_buf, 0x00);
  alpaka::memset(queue_, cursor_buf, 0x00);
  alpaka::exec<Acc1D>(queue_,
                      chainFlat_workDiv,
                      ChainAttachGridCount{},
                      plsPre,
                      pixelSize_,
                      rMin_buf.data(),
                      rMax_buf.data(),
                      masks_buf.data(),
                      counts_buf.data(),
                      chainConfig_);
  auto nEntries_buf_d = cms::alpakatools::make_device_buffer<uint32_t>(queue_);
  alpaka::exec<Acc1D>(queue_,
                      chainScan_workDiv,
                      ChainSegPrefix{},
                      counts_buf.data(),
                      offsets_buf.data(),
                      nEntries_buf_d.data(),
                      kAttachCells);
  auto nEntries_buf_h = cms::alpakatools::make_host_buffer<uint32_t>(queue_);
  alpaka::memcpy(queue_, nEntries_buf_h, nEntries_buf_d);
  alpaka::wait(queue_);  // the grid payload size
  uint32_t const nEntries = std::max(1u, *nEntries_buf_h.data());

  auto items_buf = cms::alpakatools::make_device_buffer<AttachPlsPre[]>(queue_, nEntries);
  alpaka::exec<Acc1D>(queue_,
                      chainFlat_workDiv,
                      ChainAttachGridScatter{},
                      plsPre,
                      pixelSize_,
                      masks_buf.data(),
                      offsets_buf.data(),
                      cursor_buf.data(),
                      items_buf.data(),
                      chainConfig_);
  auto const a2 = stamp();

  // K8b: candidate iteration, the exact analytic predicate, the 19 features and the r2 head.
  // plsBestChain is written for EVERY scored pair before the banded threshold (invariant I4); the
  // -XC pass-1 candidates are appended here too.
  auto stats_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, chainattach::kStats);
  auto tgtPls_buf = cms::alpakatools::make_device_buffer<int32_t[]>(queue_, nTargets);
  auto tgtLogit_buf = cms::alpakatools::make_device_buffer<float[]>(queue_, nTargets);
  // The per-target winner of the (target, phase) split, as a packed (logit, lowest-row) key.
  auto tgtBestKey_buf = cms::alpakatools::make_device_buffer<uint64_t[]>(queue_, nTargets);
  alpaka::memset(queue_, stats_buf, 0u);
  alpaka::memset(queue_, tgtBestKey_buf, 0u);
  alpaka::exec<Acc1D>(queue_,
                      chainFlat_workDiv,
                      ChainAttachScore{},
                      plsPre,
                      tgtPre_buf.data(),
                      nTargets,
                      kAttachScorePhases,
                      offsets_buf.data(),
                      items_buf.data(),
                      tgtBestKey_buf.data(),
                      plsBestChain,
                      xcPairs,
                      xcCursor,
                      xcCap,
                      stats_buf.data(),
                      chainConfig_);
  alpaka::exec<Acc1D>(queue_,
                      chainFlat_workDiv,
                      ChainAttachUnpackBest{},
                      tgtBestKey_buf.data(),
                      nTargets,
                      tgtPls_buf.data(),
                      tgtLogit_buf.data(),
                      stats_buf.data());
  auto const a3 = stamp();

  // K8c: contention and the -RD seed dedup.
  auto order_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, nTargets);
  // Split point of the contend stage: up to it the one-pLS-one-owner argmax, after it the -RD
  // seed-family dedup, whose hash walk is the one piece of P2.4 that stays sequential.
  auto a3rd = a3;
  // The inverted grant map (pLS row -> owning chain row) the A14 -CCS pass below reads. Filled by
  // ChainAttachPublish, which is the one pass that knows the post-dedup grants.
  auto plsOwnerChain_buf = cms::alpakatools::make_device_buffer<int32_t[]>(queue_, nPls);
  alpaka::memset(queue_, plsOwnerChain_buf, 0xFF);  // -1 everywhere
  {
    // P2.6a. The one-pLS-one-owner rule is an argmax, so it becomes a packed atomicMax; the -RD
    // visiting order is the gathered ascending-position (K9 accepted) order -- zero sorts, simp
    // change 3, replacing the reference's O(n^2) selection sort (7-10 ms per event on the device,
    // all of it one thread chasing a global load per comparison). Only the hash-table walk itself
    // stays sequential -- see ChainAttachSeedDedup.
    auto plsKey_buf = cms::alpakatools::make_device_buffer<uint64_t[]>(queue_, nPls);
    auto ownKeep_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, nTargets);
    auto ownOffs_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, nTargets + 1u);
    auto nOwners_buf = cms::alpakatools::make_device_buffer<uint32_t>(queue_);
    auto ownerHits_buf =
        cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, size_t{nTargets} * kMaxPLSHitsInHitsSoA);
    auto ownerNHits_buf = cms::alpakatools::make_device_buffer<uint8_t[]>(queue_, nTargets);
    auto ownerPls_buf = cms::alpakatools::make_device_buffer<int32_t[]>(queue_, nTargets);

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
      alpaka::exec<Acc1D>(queue_,
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
                          nullptr,  // stage A: the grant lives on the chain row
                          order_buf.data(),
                          nOwners_buf.data(),
                          nTargets,
                          nHits,
                          ownerHits_buf.data(),
                          ownerNHits_buf.data(),
                          ownerPls_buf.data());
      alpaka::exec<Acc1D>(queue_,
                          serial_workDiv,
                          ChainAttachSeedDedup{},
                          chainsDC_->view(),
                          order_buf.data(),
                          nOwners_buf.data(),
                          ownerHits_buf.data(),
                          ownerNHits_buf.data(),
                          ownerPls_buf.data(),
                          hashKey,
                          hashVal,
                          stats_buf.data());
    }
    alpaka::exec<Acc1D>(queue_,
                        chainFlat_workDiv,
                        ChainAttachPublish{},
                        chainsDC_->const_view(),
                        targets_buf.data(),
                        nTargets,
                        plsOwned,
                        plsOwnerChain_buf.data(),
                        stats_buf.data());
  }
  auto const a3b = stamp();

  // A14 -CCS + A15 -XC4: the restricted second pass. Walks the grid for the bare 5+ targets
  // (owned-pLS pairs only) and the aux 4-layer targets (every pair, their -XC4 score-only
  // enumeration), and applies the -CCS suppression flag that K10 row assignment honours. Runs
  // after the grants are final, exactly as the reference builds its map after -RD
  // (main.cc:4109-4127); the map itself came out of ChainAttachPublish above.
  {
    auto ccsLoser_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, nTgtAll);
    alpaka::memset(queue_, ccsLoser_buf, 0u);
    alpaka::exec<Acc1D>(queue_,
                        chainFlat_workDiv,
                        ChainAttachCcsScore{},
                        chainsDC_->view(),
                        tgtPre_buf.data(),
                        nTgtAll,
                        kAttachScorePhases,
                        ccsLoser_buf.data(),
                        offsets_buf.data(),
                        items_buf.data(),
                        plsOwnerChain_buf.data(),
                        xcPairs,
                        xcCursor,
                        xcCap,
                        stats_buf.data(),
                        chainConfig_);
    alpaka::exec<Acc1D>(queue_,
                        chainFlat_workDiv,
                        ChainAttachCcsVerdict{},
                        chainsDC_->view(),
                        tgtPre_buf.data(),
                        nTgtAll,
                        ccsLoser_buf.data(),
                        stats_buf.data(),
                        chainConfig_);
    alpaka::wait(queue_);  // the owner-chain scratch dies with this scope
  }
  auto const a4 = stamp();

  attachGridAudit(nTargets, plsPre, tgtPre_buf.data(), offsets_buf.data(), items_buf.data());


  if (timing || objectsStatistics_) {
    auto stats_h = cms::alpakatools::make_host_buffer<uint32_t[]>(queue_, chainattach::kStats);
    alpaka::memcpy(queue_, stats_h, stats_buf);
    alpaka::wait(queue_);
    uint32_t const* st = stats_h.data();
    auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
    attachSummary_ = std::format(
        "targets={} (+{} aux4L) pLS={} gridEntries={} cand={} dup={} scored={} picks={} attached={} "
        "rdRevoked={} ccsSuppressed={} xcOverflow={} hashOverflow={} tieRD={} | pre {:.3f} ms | "
        "grid {:.3f} ms | score {:.3f} ms | contend {:.3f} ms | RDdedup {:.3f} ms | ccs {:.3f} ms",
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
        st[12],
        st[11],
        st[7],
        st[9],
        ms(a0, a1),
        ms(a1, a2),
        ms(a2, a3),
        ms(a3, a3rd),
        ms(a3rd, a3b),
        ms(a3b, a4));
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
  lstWarning(std::format("[CHAIN K8 AUDIT] evt={} targets={} pLS={} exactPairs={} gridCand={} "
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

void LSTEvent::attachBareT3(unsigned int nHits,
                            uint32_t const* accepted,
                            AttachPlsPre const* plsPre,
                            uint8_t* plsOwned,
                            uint32_t* plsBestT3,
                            uint32_t* hashKey,
                            int32_t* hashVal) {
  // Stage B of the general attach (src/alpaka/ChainAttachT3.h), production form: the bare-T3
  // universe through the -T3F gate, its own grid, scoring against the LIVE ownership array, the
  // stage-B contention and the -RDT dedup (against the -RD table stage A left in hashKey/Val).
  // The surviving owner arrays are kept in the bareT3* members for the -CC sweep + emission that
  // runs after the chain rows are emitted. LST_CHAIN_T3_AUDIT still enables the grid-superset
  // audit on the bare-T3 geometry.
  attachT3Summary_.clear();
  nBareT3_ = 0;
  bareT3Targets_.reset();
  bareT3TgtPls_.reset();
  bareT3TgtLogit_.reset();
  bareT3Keep_.reset();
  if (nChainNodes_ == 0 || pixelSize_ == 0 || !chainsDC_.has_value() || !chainNodesDC_.has_value() ||
      !chainItemsDC_.has_value() || !tripletsDC_.has_value() || !segmentsDC_.has_value())
    return;

  float const theta = chainConfig_.attachThetaT3;  // -AT3, GLOBAL (no eta bands)
  ChainConfig const& cfgT3 = chainConfig_;

  bool const timing = chainTimingEnabled();
  auto stamp = [&]() {
    if (timing)
      alpaka::wait(queue_);
    return std::chrono::steady_clock::now();
  };
  auto const b0 = stamp();

  auto const serial_workDiv = cms::alpakatools::make_workdiv<Acc1D>(1, 1);
  auto const chainFlat_workDiv = cms::alpakatools::make_workdiv<Acc1D>(max_blocks, 256);
  auto const chainScan_workDiv = cms::alpakatools::make_workdiv<Acc1D>(1, kChainScanBlockThreads);
  uint32_t const nPls = pixelSize_;
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
  alpaka::exec<Acc1D>(
      queue_, chainScan_workDiv, ChainSegPrefix{}, keep_buf.data(), offs_buf.data(), nBare_d.data(), nNodes);
  auto nBare_h = cms::alpakatools::make_host_buffer<uint32_t>(queue_);
  alpaka::memcpy(queue_, nBare_h, nBare_d);
  alpaka::wait(queue_);
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
  auto const b1 = stamp();

  // K8a on the bare-T3 target hull -- a SECOND, independent grid. The chain grid is untouched.
  auto rMin_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, kAttachRBins);
  auto rMax_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, kAttachRBins);
  alpaka::memset(queue_, rMin_buf, 0xFF);
  alpaka::memset(queue_, rMax_buf, 0x00);
  alpaka::exec<Acc1D>(
      queue_, chainFlat_workDiv, ChainAttachGridBounds{}, tgt_buf.data(), nBare, rMin_buf.data(), rMax_buf.data());

  auto masks_buf = cms::alpakatools::make_device_buffer<uint16_t[]>(queue_, size_t{nPls} * kAttachRBins);
  auto counts_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, kAttachCells);
  auto goffs_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, kAttachCells + 1u);
  auto cursor_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, kAttachCells);
  alpaka::memset(queue_, masks_buf, 0x00);
  alpaka::memset(queue_, counts_buf, 0x00);
  alpaka::memset(queue_, cursor_buf, 0x00);
  alpaka::exec<Acc1D>(queue_,
                      chainFlat_workDiv,
                      ChainAttachGridCount{},
                      plsPre,
                      nPls,
                      rMin_buf.data(),
                      rMax_buf.data(),
                      masks_buf.data(),
                      counts_buf.data(),
                      cfgT3);
  auto nEnt_d = cms::alpakatools::make_device_buffer<uint32_t>(queue_);
  alpaka::exec<Acc1D>(
      queue_, chainScan_workDiv, ChainSegPrefix{}, counts_buf.data(), goffs_buf.data(), nEnt_d.data(), kAttachCells);
  auto nEnt_h = cms::alpakatools::make_host_buffer<uint32_t>(queue_);
  alpaka::memcpy(queue_, nEnt_h, nEnt_d);
  alpaka::wait(queue_);
  uint32_t const nEntries = std::max(1u, *nEnt_h.data());
  auto items_buf = cms::alpakatools::make_device_buffer<AttachPlsPre[]>(queue_, nEntries);
  alpaka::exec<Acc1D>(queue_,
                      chainFlat_workDiv,
                      ChainAttachGridScatter{},
                      plsPre,
                      nPls,
                      masks_buf.data(),
                      goffs_buf.data(),
                      cursor_buf.data(),
                      items_buf.data(),
                      cfgT3);
  auto const b2 = stamp();

  // K8B-b: score, against the LIVE ownership array (stage A is final; invariant I4 ordering
  // inside the kernel). The owner-side buffers persist in the bareT3* members: the -CC sweep in
  // arbitrateChains consumes them after the chain rows are emitted.
  bareT3TgtPls_.emplace(cms::alpakatools::make_device_buffer<int32_t[]>(queue_, nBare));
  bareT3TgtLogit_.emplace(cms::alpakatools::make_device_buffer<float[]>(queue_, nBare));
  auto stats_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, chainattacht3::kStats);
  alpaka::memset(queue_, stats_buf, 0u);
  alpaka::exec<Acc1D>(queue_,
                      chainFlat_workDiv,
                      ChainAttachT3Score{},
                      tgt_buf.data(),
                      nBare,
                      goffs_buf.data(),
                      items_buf.data(),
                      plsOwned,
                      bareT3TgtPls_->data(),
                      bareT3TgtLogit_->data(),
                      plsBestT3,
                      stats_buf.data(),
                      theta,
                      cfgT3);
  auto const b3 = stamp();

  // K8B-c: contention + the stage-B half of the -RD seed dedup, against the hash table stage A
  // left behind (so a seed already delivering a pT5-class object blocks its siblings here). The
  // winners publish straight into the LIVE plsOwned (invariant I1). Same decomposition as stage
  // A's T7 (argmax key + serial hash residue in the gathered ascending-T3-row order -- zero
  // sorts, simp change 3), one form on both backends; keep[] survives in bareT3Keep_ because
  // after the dedup revocations it flags exactly the DELIVERIES, which is what the -CC sweep's
  // gather in arbitrateChains reads.
  bareT3Keep_.emplace(cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, nBare));
  {
    auto plsKey_buf = cms::alpakatools::make_device_buffer<uint64_t[]>(queue_, nPls);
    auto ownOffs_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, nBare + 1u);
    auto nOwners_buf = cms::alpakatools::make_device_buffer<uint32_t>(queue_);
    auto owners_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, nBare);
    auto ownerPls_buf = cms::alpakatools::make_device_buffer<int32_t[]>(queue_, nBare);
    auto ownerHits_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, size_t{nBare} * kMaxPLSHitsInHitsSoA);
    auto ownerNHits_buf = cms::alpakatools::make_device_buffer<uint8_t[]>(queue_, nBare);
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
    alpaka::exec<Acc1D>(queue_,
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
    alpaka::exec<Acc1D>(queue_,
                        chainFlat_workDiv,
                        ChainAttachOwnerHits{},
                        lstInputDC_->const_view().pixelSeeds(),
                        lstInputDC_->const_view().hits(),
                        chainsDC_->const_view(),
                        bareT3TgtPls_->data(),  // stage B: the grant is position-keyed
                        owners_buf.data(),
                        nOwners_buf.data(),
                        nBare,
                        nHits,
                        ownerHits_buf.data(),
                        ownerNHits_buf.data(),
                        ownerPls_buf.data());
    alpaka::exec<Acc1D>(queue_,
                        serial_workDiv,
                        ChainAttachT3Dedup{},
                        owners_buf.data(),
                        nOwners_buf.data(),
                        ownerPls_buf.data(),
                        ownerHits_buf.data(),
                        ownerNHits_buf.data(),
                        bareT3TgtPls_->data(),
                        bareT3TgtLogit_->data(),
                        bareT3Keep_->data(),
                        plsOwned,
                        hashKey,
                        hashVal,
                        stats_buf.data(),
                        static_cast<uint8_t>(chainConfig_.attachSeedDedup ? 1 : 0));
  }
  auto const b4 = stamp();

  // Keep the target list for the -CC sweep + emission.
  bareT3Targets_.emplace(std::move(targets_buf));
  nBareT3_ = nBare;

  if (timing || objectsStatistics_) {
    auto statsH = cms::alpakatools::make_host_buffer<uint32_t[]>(queue_, chainattacht3::kStats);
    alpaka::memcpy(queue_, statsH, stats_buf);
    alpaka::wait(queue_);
    uint32_t const* st = statsH.data();
    auto ms = [](auto a2, auto b) { return std::chrono::duration<double, std::milli>(b - a2).count(); };
    attachT3Summary_ = std::format(
        "bareT3={} ofNodes={} pLS={} gridEntries={} cand={} dup={} scored={} overTheta={} withCand={} "
        "picks={} rdtRevoked={} theta={:.3f} t3FakeMax={:.4g} | "
        "pre {:.3f} ms | grid {:.3f} ms | score {:.3f} ms | contend {:.3f} ms",
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
        ms(b0, b1),
        ms(b1, b2),
        ms(b2, b3),
        ms(b3, b4));
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
    lstWarning(std::format("[CHAIN K8B AUDIT] evt={} bareT3={} pLS={} exactPairs={} gridCand={} "
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
  alpaka::wait(queue_);  // the grid / scratch buffers above die with this scope
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
    lstWarning(std::format(
        "[MEM] TrackCandidates: {} allocated ({:.1f} MB) [dynamic: {} pLS + {} chain headroom]",
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
