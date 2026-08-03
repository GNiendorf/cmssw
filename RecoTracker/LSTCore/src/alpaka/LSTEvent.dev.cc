#include "HeterogeneousCore/AlpakaInterface/interface/memory.h"
#include "HeterogeneousCore/AlpakaInterface/interface/workdivision.h"
#include "HeterogeneousCore/AlpakaInterface/interface/CopyToDevice.h"

#include "LSTEvent.h"

#include "ChainArbitrate.h"
#include "ChainAttach.h"
#include "ChainEdges.h"
#include "ChainGate.h"
#include "ChainGraph.h"
#include "ChainParallel.h"
#include "ChainWeld.h"
#include "Hit.h"
#include "Kernels.h"
#include "MiniDoublet.h"
#include "PixelQuintuplet.h"
#include "PixelTriplet.h"
#include "Quintuplet.h"
#include "Segment.h"
#include "TrackCandidate.h"
#include "Triplet.h"
#include "Quadruplet.h"

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

  // P2.6a. Which form of the five order-dependent chain stages this backend runs.
  //
  // On a backend that gives one thread per block (the CPU serial accelerator) the P2.3 / P2.4
  // single-thread kernels ARE the fast form -- they cost 0.014 to 0.6 ms/event there -- and the
  // parallel replacements would only add prefix-sum and staging passes. On a device backend the
  // same kernels cost 35 ms/event between them, so the parallel forms of ChainParallel.h run
  // instead. The two forms are required to agree bit for bit; the CPU-vs-GPU leg of the P2.5
  // reproducibility harness is exactly that comparison.
  constexpr bool kChainSerialArb = cms::alpakatools::requires_single_thread_per_block_v<Acc1D>;
}  // namespace

void LSTEvent::initSync() {
  alpaka::wait(queue_);  // other calls can be asynchronous

  //reset the arrays
  for (int i = 0; i < 6; i++) {
    n_minidoublets_by_layer_barrel_[i] = 0;
    n_segments_by_layer_barrel_[i] = 0;
    n_triplets_by_layer_barrel_[i] = 0;
    n_quintuplets_by_layer_barrel_[i] = 0;
    n_quadruplets_by_layer_barrel_[i] = 0;
    if (i < 5) {
      n_minidoublets_by_layer_endcap_[i] = 0;
      n_segments_by_layer_endcap_[i] = 0;
      n_triplets_by_layer_endcap_[i] = 0;
      n_quintuplets_by_layer_endcap_[i] = 0;
      n_quadruplets_by_layer_endcap_[i] = 0;
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
    n_quintuplets_by_layer_barrel_[i] = 0;
    n_quadruplets_by_layer_barrel_[i] = 0;
    if (i < 5) {
      n_minidoublets_by_layer_endcap_[i] = 0;
      n_segments_by_layer_endcap_[i] = 0;
      n_triplets_by_layer_endcap_[i] = 0;
      n_quintuplets_by_layer_endcap_[i] = 0;
      n_quadruplets_by_layer_endcap_[i] = 0;
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
  quintupletsDC_.reset();
  trackCandidatesBaseDC_.reset();
  trackCandidatesExtendedDC_.reset();
  pixelTripletsDC_.reset();
  pixelQuintupletsDC_.reset();
  quadrupletsDC_.reset();
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
  quintupletsHC_.reset();
  pixelTripletsHC_.reset();
  pixelQuintupletsHC_.reset();
  trackCandidatesBaseHC_.reset();
  trackCandidatesExtendedHC_.reset();
  modulesHC_.reset();
  quadrupletsHC_.reset();
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
    auto triplets = tripletsDC_->view().triplets();
    auto partOfPT5_view = cms::alpakatools::make_device_view(queue_, triplets.partOfPT5());
    alpaka::memset(queue_, partOfPT5_view, 0u);
    auto partOfT5_view = cms::alpakatools::make_device_view(queue_, triplets.partOfT5());
    alpaka::memset(queue_, partOfT5_view, 0u);
    auto partOfPT3_view = cms::alpakatools::make_device_view(queue_, triplets.partOfPT3());
    alpaka::memset(queue_, partOfPT3_view, 0u);
    auto connectedMax_view = cms::alpakatools::make_device_view(queue_, triplets.connectedMax());
    alpaka::memset(queue_, connectedMax_view, 0u);
    auto connectedLSMax_view =
        cms::alpakatools::make_device_view(queue_, triplets.connectedLSMax(), triplets.metadata().size());
    alpaka::memset(queue_, connectedLSMax_view, 0u);

    if (useChainTracking_) {
      // Chain-tracking K1a target arrays. They are keyed by the raw MiniDoublet / Segment index, so
      // they must span the whole allocated extent of those collections (which is module-segmented
      // and therefore sparser than the produced-object count). Allocated and zeroed before the
      // triplet builder runs, since the builder tallies straight into them.
      unsigned int const nMDKeys = miniDoubletsDC_->view().miniDoublets().metadata().size();
      unsigned int const nLSKeys = segmentsDC_->view().segments().metadata().size();
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
            "[MEM] ChainIncidence: {} MD keys + {} LS keys allocated ({:.1f} MB)", nMDKeys, nLSKeys, mb));
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
  if (useChainTracking_) {
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
                        chainLsT3InCounts);
  };
  if (useChainTracking_) {
    if (reduceMemByFullPrecompute_)
      execCreateTriplets(CreateTripletsReduceMemChain{});
    else
      execCreateTriplets(CreateTripletsChain{});
  } else {
    if (reduceMemByFullPrecompute_)
      execCreateTriplets(CreateTripletsReduceMem{});
    else
      execCreateTriplets(CreateTriplets{});
  }

  auto const addTripletRangesToEventExplicit_workDiv = cms::alpakatools::make_workdiv<Acc1D>(1, 1024);

  alpaka::exec<Acc1D>(queue_,
                      addTripletRangesToEventExplicit_workDiv,
                      AddTripletRangesToEventExplicit{},
                      modules_.const_view().modules(),
                      tripletsDC_->const_view().tripletsOccupancy(),
                      rangesDC_->view());

  if (useChainTracking_) {
    buildChainIncidence();
    buildChainEdges();
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

void LSTEvent::buildChainIncidence() {
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
                      chainLsIncidenceDC_->view());

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
  // P2.6a: on a backend with real thread parallelism the in-place serial compaction (1.5 ms/event
  // on CUDA) is replaced by flags + single-block prefix + gather/scatter through a staging array.
  // The serial form stays the CPU path, where it costs 14 us and the staging pass would not pay.
  if constexpr (kChainSerialArb) {
    alpaka::exec<Acc1D>(queue_,
                        serial_workDiv,
                        ChainCompactCarriedTCs{},
                        trackCandidatesBaseDC_->view(),
                        trackCandidatesExtendedDC_->view(),
                        chainConfig_);
  } else {
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

  // K9-0 / K9-1: the claim universe and the order key + candidate mask.
  auto claimHits_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, 6u * nChainWeldedNodes_);
  alpaka::exec<Acc1D>(queue_,
                      chainFlat_workDiv,
                      ChainBuildClaimHits{},
                      miniDoubletsDC_->const_view().miniDoublets(),
                      chainItemsDC_->const_view(),
                      chainsDC_->view(),
                      claimHits_buf.data());
  alpaka::exec<Acc1D>(queue_,
                      chainFlat_workDiv,
                      ChainOrderAndSelect{},
                      tripletsDC_->const_view().triplets(),
                      chainNodesDC_->const_view(),
                      chainItemsDC_->const_view(),
                      chainsDC_->view(),
                      chainConfig_);
  auto const t2 = stamp();

  // K9a / K9b / K9c: pre-claim, greedy claim, braid. Serial by construction (see ChainArbitrate.h).
  auto owner_buf = cms::alpakatools::make_device_buffer<int32_t[]>(queue_, nHits);
  auto order_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, nChainCount_);
  auto orderScratch_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, nChainCount_);
  auto accepted_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, nChainCount_);
  auto braidCount_buf = cms::alpakatools::make_device_buffer<int32_t[]>(queue_, nChainCount_);
  constexpr uint32_t kBraidTouchedCapacity = 4096u;  // >= the largest per-chain claim-hit count
  auto braidTouched_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, kBraidTouchedCapacity);
  auto stats_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, kChainArbStats);
  alpaka::memset(queue_, stats_buf, 0u);

  if constexpr (kChainSerialArb) {
    alpaka::exec<Acc1D>(queue_,
                        serial_workDiv,
                        ChainArbitrateSerial{},
                        miniDoubletsDC_->const_view().miniDoublets(),
                        tripletsDC_->const_view().triplets(),
                        segmentsDC_->const_view().segments(),
                        chainNodesDC_->const_view(),
                        chainItemsDC_->const_view(),
                        chainsDC_->view(),
                        claimHits_buf.data(),
                        trackCandidatesBaseDC_->const_view(),
                        trackCandidatesExtendedDC_->const_view(),
                        owner_buf.data(),
                        nHits,
                        order_buf.data(),
                        orderScratch_buf.data(),
                        accepted_buf.data(),
                        braidCount_buf.data(),
                        braidTouched_buf.data(),
                        kBraidTouchedCapacity,
                        stats_buf.data(),
                        chainConfig_);
  } else {
    // P2.6a: the same walk, reached by conflict-free rounds instead of a single thread. See the
    // exactness / termination argument at the top of ChainParallel.h.
    auto bandItems_buf = cms::alpakatools::make_device_buffer<int32_t[]>(queue_, nChainCount_);
    auto bandFrac_buf = cms::alpakatools::make_device_buffer<float[]>(queue_, nChainCount_);
    auto bandBraid_buf = cms::alpakatools::make_device_buffer<float[]>(queue_, nChainCount_);
    auto candKeep_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, nChainCount_);
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
                        chainFlat_workDiv,
                        ChainClaimBands{},
                        miniDoubletsDC_->const_view().miniDoublets(),
                        tripletsDC_->const_view().triplets(),
                        segmentsDC_->const_view().segments(),
                        chainNodesDC_->const_view(),
                        chainItemsDC_->const_view(),
                        chainsDC_->const_view(),
                        bandItems_buf.data(),
                        bandFrac_buf.data(),
                        bandBraid_buf.data(),
                        chainConfig_);
    alpaka::exec<Acc1D>(queue_, chainFlat_workDiv, ChainCandFlags{}, chainsDC_->const_view(), candKeep_buf.data());
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
    alpaka::exec<Acc1D>(queue_,
                        chainFlat_workDiv,
                        ChainClaimTieCensus{},
                        chainsDC_->const_view(),
                        order_buf.data(),
                        nCand_buf.data(),
                        nChainCount_,
                        stats_buf.data());
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
  attachPixels(nHits, accepted_buf.data(), nAllocatedTCs);
  auto const t3b = stamp();

  // EX: chain extension at assembly. Needs the claimed-hit map and the MD -> outgoing-LineSegment
  // adjacency (-EXS 1).
  auto t3c = t3b;
  if (chainConfig_.extendMode > 0) {
    auto claimedHit_buf = cms::alpakatools::make_device_buffer<uint8_t[]>(queue_, nHits);
    alpaka::exec<Acc1D>(
        queue_, chainFlat_workDiv, ChainMarkClaimedHits{}, owner_buf.data(), claimedHit_buf.data(), nHits);

    auto segCounts_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, nMDall);
    auto segOffsets_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, nMDall + 1u);
    auto segCursor_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, nMDall);
    alpaka::memset(queue_, segCounts_buf, 0u);
    alpaka::memset(queue_, segCursor_buf, 0u);
    alpaka::exec<Acc1D>(queue_,
                        chainFlat_workDiv,
                        ChainSegCount{},
                        modules_.const_view().modules(),
                        segmentsDC_->const_view().segments(),
                        segmentsDC_->const_view().segmentsOccupancy(),
                        rangesDC_->const_view(),
                        segCounts_buf.data());
    auto nSegOT_buf_d = cms::alpakatools::make_device_buffer<uint32_t>(queue_);
    alpaka::exec<Acc1D>(queue_,
                        chainScan_workDiv,
                        ChainSegPrefix{},
                        segCounts_buf.data(),
                        segOffsets_buf.data(),
                        nSegOT_buf_d.data(),
                        nMDall);

    auto nSegOT_buf_h = cms::alpakatools::make_host_buffer<uint32_t>(queue_);
    alpaka::memcpy(queue_, nSegOT_buf_h, nSegOT_buf_d);
    alpaka::wait(queue_);  // the adjacency payload size
    uint32_t const nSegOT = std::max(1u, *nSegOT_buf_h.data());

    auto segItems_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, nSegOT);
    alpaka::exec<Acc1D>(queue_,
                        chainFlat_workDiv,
                        ChainSegScatter{},
                        modules_.const_view().modules(),
                        segmentsDC_->const_view().segments(),
                        segmentsDC_->const_view().segmentsOccupancy(),
                        rangesDC_->const_view(),
                        segOffsets_buf.data(),
                        segCursor_buf.data(),
                        segItems_buf.data());
    alpaka::exec<Acc1D>(
        queue_, chainFlat_workDiv, ChainSegSort{}, segOffsets_buf.data(), segItems_buf.data(), nMDall);
    t3c = stamp();

    if constexpr (kChainSerialArb) {
      alpaka::exec<Acc1D>(queue_,
                          serial_workDiv,
                          ChainExtendSerial{},
                          modules_.const_view().modules(),
                          miniDoubletsDC_->const_view().miniDoublets(),
                          segmentsDC_->const_view().segments(),
                          chainItemsDC_->view(),
                          chainsDC_->view(),
                          accepted_buf.data(),
                          claimedHit_buf.data(),
                          nHits,
                          segOffsets_buf.data(),
                          segItems_buf.data(),
                          nMDall,
                          stats_buf.data(),
                          chainConfig_);
    } else {
      // P2.6a: the extension is the single most expensive serial kernel on the device (16.5 ms/event
      // -- it is a double-precision circle+line refit per accepted chain, run by one thread on a
      // part whose FP64 rate is 1/64 of its FP32 rate). Two conflict-free rounds over the static
      // reachable read sets take essentially all of them, and a single-thread finisher sweeps
      // whatever is left in position order. stats[15] reports how many that was.
      // At the frozen -EXN 1 the read set is the terminal MiniDoublet's adjacency slice, walked in
      // place; the breadth-first buffer is then not allocated at all.
      bool const directReach = (chainConfig_.extendMaxPerEnd == 1);
      auto reachMd_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(
          queue_, directReach ? 1u : size_t{nChainCount_} * chainpar::kExtReachCap);
      auto reachN_buf = cms::alpakatools::make_device_buffer<uint8_t[]>(queue_, nChainCount_);
      auto reachOvf_buf = cms::alpakatools::make_device_buffer<uint8_t[]>(queue_, nChainCount_);
      auto reachTerm_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, nChainCount_);
      auto extDone_buf = cms::alpakatools::make_device_buffer<uint8_t[]>(queue_, nChainCount_);
      auto extMinPos_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, nHits);
      auto blockPos_buf = cms::alpakatools::make_device_buffer<uint32_t>(queue_);
      alpaka::memset(queue_, extDone_buf, 0u);
      alpaka::memset(queue_, extMinPos_buf, 0xFF);
      alpaka::memset(queue_, blockPos_buf, 0xFF);

      alpaka::exec<Acc1D>(queue_,
                          chainFlat_workDiv,
                          ChainExtendReach{},
                          segmentsDC_->const_view().segments(),
                          chainItemsDC_->const_view(),
                          chainsDC_->const_view(),
                          accepted_buf.data(),
                          nChainCount_,
                          segOffsets_buf.data(),
                          segItems_buf.data(),
                          nMDall,
                          directReach,
                          reachMd_buf.data(),
                          reachN_buf.data(),
                          reachOvf_buf.data(),
                          reachTerm_buf.data(),
                          stats_buf.data(),
                          chainConfig_);

      // Four rounds, measured: the extension conflict graph is shallow but not trivial (a chain can
      // be blocked by a chain that is itself blocked), and each extra round costs three launches
      // over tiny reach sets -- tens of microseconds -- against the ~16 us per chain that the
      // single-thread finisher pays for anything left over. stats[15] reports the leftover.
      constexpr int kExtendRounds = 4;
      for (int round = 0; round < kExtendRounds; ++round) {
        if (round > 0)
          alpaka::exec<Acc1D>(queue_,
                              chainFlat_workDiv,
                              ChainExtendResetMinPos{},
                              miniDoubletsDC_->const_view().miniDoublets(),
                              segmentsDC_->const_view().segments(),
                              chainsDC_->const_view(),
                              segOffsets_buf.data(),
                              segItems_buf.data(),
                              nMDall,
                              directReach,
                              reachMd_buf.data(),
                              reachN_buf.data(),
                              reachTerm_buf.data(),
                              nChainCount_,
                              extMinPos_buf.data(),
                              nHits,
                              blockPos_buf.data());
        alpaka::exec<Acc1D>(queue_,
                            chainFlat_workDiv,
                            ChainExtendMinPos{},
                            miniDoubletsDC_->const_view().miniDoublets(),
                            segmentsDC_->const_view().segments(),
                            chainsDC_->const_view(),
                            segOffsets_buf.data(),
                            segItems_buf.data(),
                            nMDall,
                            directReach,
                            reachMd_buf.data(),
                            reachN_buf.data(),
                            reachOvf_buf.data(),
                            reachTerm_buf.data(),
                            extDone_buf.data(),
                            nChainCount_,
                            extMinPos_buf.data(),
                            nHits,
                            blockPos_buf.data());
        alpaka::exec<Acc1D>(queue_,
                            chainFlat_workDiv,
                            ChainExtendRound{},
                            modules_.const_view().modules(),
                            miniDoubletsDC_->const_view().miniDoublets(),
                            segmentsDC_->const_view().segments(),
                            chainItemsDC_->view(),
                            chainsDC_->view(),
                            accepted_buf.data(),
                            claimedHit_buf.data(),
                            nHits,
                            segOffsets_buf.data(),
                            segItems_buf.data(),
                            nMDall,
                            directReach,
                            reachMd_buf.data(),
                            reachN_buf.data(),
                            reachTerm_buf.data(),
                            extDone_buf.data(),
                            nChainCount_,
                            extMinPos_buf.data(),
                            blockPos_buf.data(),
                            stats_buf.data(),
                            chainConfig_);
      }
      alpaka::exec<Acc1D>(queue_,
                          serial_workDiv,
                          ChainExtendFinish{},
                          modules_.const_view().modules(),
                          miniDoubletsDC_->const_view().miniDoublets(),
                          segmentsDC_->const_view().segments(),
                          chainItemsDC_->view(),
                          chainsDC_->view(),
                          accepted_buf.data(),
                          claimedHit_buf.data(),
                          nHits,
                          segOffsets_buf.data(),
                          segItems_buf.data(),
                          nMDall,
                          extDone_buf.data(),
                          stats_buf.data(),
                          chainConfig_);
    }
    alpaka::wait(queue_);  // the scratch buffers above die with this scope
  }
  auto const t4 = stamp();

  // K10: row assignment then emission.
  if constexpr (kChainSerialArb) {
    alpaka::exec<Acc1D>(queue_,
                        serial_workDiv,
                        ChainAssignTCRows{},
                        trackCandidatesBaseDC_->view(),
                        trackCandidatesExtendedDC_->view(),
                        chainsDC_->view(),
                        accepted_buf.data(),
                        nAllocatedTCs);
  } else {
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
                      stats_buf.data());
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
        "[CHAIN K9] accepted={} chainTCs={} | extend examined={} cand={} outer={} noFit={} rejChi2={} "
        "rejUniq={} rejFit={} | TC slot fallbacks={} overflow={} | tieK9order={} tieExtend={} | "
        "R3 claimRounds={} claimStuck={} capHit={} | EXreachOvf={} EXserialTail={}",
        *nAcc_h.data(),
        *nTC_h.data(),
        s[0],
        s[2],
        s[3],
        s[1],
        s[5],
        s[4],
        s[6],
        s[7],
        s[8],
        s[9],
        s[10],
        s[11],
        s[12],
        s[13],
        s[14],
        s[15]));
    if (timing) {
      auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
      lstWarning(std::format("[CHAIN TIMING] compact {:.3f} ms | K9 prep {:.3f} ms | K9 claim {:.3f} ms | "
                             "K8 attach {:.3f} ms | EXadj {:.3f} ms | EXwalk {:.3f} ms | K10 rows {:.3f} ms | "
                             "K10 emit {:.3f} ms | total {:.3f} ms",
                             ms(t0, t1),
                             ms(t1, t2),
                             ms(t2, t3),
                             ms(t3, t3b),
                             ms(t3b, t3c),
                             ms(t3c, t4),
                             ms(t4, t4b),
                             ms(t4b, t5),
                             ms(t0, t5)));
      lstWarning(std::format("[CHAIN K8] {}", attachSummary_));
    }
  }

}

void LSTEvent::attachPixels(unsigned int nHits, uint32_t const* accepted, unsigned int nAllocatedTCs) {
  // Chain-tracking phase P2.4 (port map section 5, K8a-K8d). Reference: prototype/PixelAttach.cc,
  // prototype/AttachDelivery.cc gaStageChains and the -A 4 blocks of prototype/main.cc, at the M19
  // frozen flags (-A 4 -a 6.875 -RT5 1 -RT3 0 -RPS 1 -RD 1 -D4 1e9).
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

  // K8-0 / K8-0b: the per-pLS records and the target list (accepted, nLayers >= 5, dca-eligible,
  // in K9 accepted order -- the contention tie-break reads that order).
  auto plsPre_buf = cms::alpakatools::make_device_buffer<AttachPlsPre[]>(queue_, nPls);
  auto targets_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, nChainCount_);
  auto nTargets_buf_d = cms::alpakatools::make_device_buffer<uint32_t>(queue_);
  alpaka::exec<Acc1D>(queue_,
                      chainFlat_workDiv,
                      ChainAttachPlsPre{},
                      lstInputDC_->const_view().pixelSeeds(),
                      pixelSegmentsDC_->const_view(),
                      plsPre_buf.data(),
                      pixelSize_);
  auto tgtKeep_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, nChainCount_);
  auto tgtOffs_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, nChainCount_ + 1u);
  if constexpr (kChainSerialArb) {
    alpaka::exec<Acc1D>(queue_,
                        serial_workDiv,
                        ChainAttachSelectTargets{},
                        chainsDC_->const_view(),
                        accepted,
                        targets_buf.data(),
                        nTargets_buf_d.data(),
                        chainConfig_);
  } else {
    // The same filtered copy of the K9 accepted array, order-preserving through a prefix sum.
    auto tgtTotal_buf = cms::alpakatools::make_device_buffer<uint32_t>(queue_);
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
                        tgtTotal_buf.data(),
                        nChainCount_);
    alpaka::exec<Acc1D>(queue_,
                        chainFlat_workDiv,
                        ChainTargetScatter{},
                        accepted,
                        tgtKeep_buf.data(),
                        tgtOffs_buf.data(),
                        nChainCount_,
                        targets_buf.data(),
                        nTargets_buf_d.data());
  }
  auto nTargets_buf_h = cms::alpakatools::make_host_buffer<uint32_t>(queue_);
  alpaka::memcpy(queue_, nTargets_buf_h, nTargets_buf_d);
  alpaka::wait(queue_);  // the target count sizes every attach buffer below
  uint32_t const nTargets = *nTargets_buf_h.data();
  if (nTargets == 0 || pixelSize_ == 0)
    return;

  auto tgtPre_buf = cms::alpakatools::make_device_buffer<AttachTargetPre[]>(queue_, nTargets);
  alpaka::exec<Acc1D>(queue_,
                      chainFlat_workDiv,
                      ChainAttachTargetPre{},
                      miniDoubletsDC_->const_view().miniDoublets(),
                      chainItemsDC_->const_view(),
                      chainsDC_->const_view(),
                      targets_buf.data(),
                      nTargets,
                      tgtPre_buf.data());
  auto const a1 = stamp();

  // K8a: the grid. Count / prefix / scatter, no sort anywhere (maintainer policy).
  auto rMin_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, kAttachRBins);
  auto rMax_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, kAttachRBins);
  alpaka::memset(queue_, rMin_buf, 0xFF);  // 0xFFFFFFFF = "no target in this bin"
  alpaka::memset(queue_, rMax_buf, 0x00);
  alpaka::exec<Acc1D>(
      queue_, chainFlat_workDiv, ChainAttachGridBounds{}, tgtPre_buf.data(), nTargets, rMin_buf.data(), rMax_buf.data());

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
                      plsPre_buf.data(),
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
                      plsPre_buf.data(),
                      pixelSize_,
                      masks_buf.data(),
                      offsets_buf.data(),
                      cursor_buf.data(),
                      items_buf.data(),
                      chainConfig_);
  auto const a2 = stamp();

  // K8b: candidate iteration, the exact analytic predicate, the 19 features and the r2 head.
  auto stats_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, chainattach::kStats);
  auto tgtPls_buf = cms::alpakatools::make_device_buffer<int32_t[]>(queue_, nTargets);
  auto tgtLogit_buf = cms::alpakatools::make_device_buffer<float[]>(queue_, nTargets);
  auto plsBest_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, nPls);
  alpaka::memset(queue_, stats_buf, 0u);
  alpaka::memset(queue_, plsBest_buf, 0u);  // orderFloat(-inf) == 0, the identity of the atomicMax
  alpaka::exec<Acc1D>(queue_,
                      chainFlat_workDiv,
                      ChainAttachScore{},
                      plsPre_buf.data(),
                      tgtPre_buf.data(),
                      nTargets,
                      offsets_buf.data(),
                      items_buf.data(),
                      tgtPls_buf.data(),
                      tgtLogit_buf.data(),
                      plsBest_buf.data(),
                      stats_buf.data(),
                      chainConfig_);
  auto const a3 = stamp();

  // K8c / K8d: contention, -RD seed dedup, and the -RPS / contention retirement of carried rows.
  auto plsOwnerPos_buf = cms::alpakatools::make_device_buffer<int32_t[]>(queue_, nPls);
  auto plsOwned_buf = cms::alpakatools::make_device_buffer<uint8_t[]>(queue_, nPls);
  auto hashKey_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, chainattach::kSeedHashSlots);
  auto hashVal_buf = cms::alpakatools::make_device_buffer<int32_t[]>(queue_, chainattach::kSeedHashSlots);
  auto order_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, nTargets);
  // Split point of the contend stage: up to it the one-pLS-one-owner argmax, after it the -RD
  // seed-family dedup, whose hash walk is the one piece of P2.4 that stays sequential.
  auto a3rd = a3;
  if constexpr (kChainSerialArb) {
    alpaka::exec<Acc1D>(queue_,
                        serial_workDiv,
                        ChainAttachContend{},
                        lstInputDC_->const_view().pixelSeeds(),
                        lstInputDC_->const_view().hits(),
                        chainsDC_->view(),
                        targets_buf.data(),
                        nTargets,
                        tgtPls_buf.data(),
                        tgtLogit_buf.data(),
                        plsOwnerPos_buf.data(),
                        plsOwned_buf.data(),
                        pixelSize_,
                        hashKey_buf.data(),
                        hashVal_buf.data(),
                        nHits,
                        order_buf.data(),
                        stats_buf.data(),
                        chainConfig_);
  } else {
    // P2.6a. The one-pLS-one-owner rule is an argmax, so it becomes a packed atomicMax; the -RD
    // visiting order is a rank count instead of the reference's O(n^2) selection sort (7-10 ms per
    // event on the device, all of it one thread chasing a global load per comparison). Only the
    // hash-table walk itself stays sequential -- see ChainAttachSeedDedup.
    auto plsKey_buf = cms::alpakatools::make_device_buffer<uint64_t[]>(queue_, nPls);
    auto ownKeep_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, nTargets);
    auto ownOffs_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, nTargets + 1u);
    auto nOwners_buf = cms::alpakatools::make_device_buffer<uint32_t>(queue_);
    auto orderRanked_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, nTargets);
    auto ownerHits_buf =
        cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, size_t{nTargets} * kMaxPLSHitsInHitsSoA);
    auto ownerNHits_buf = cms::alpakatools::make_device_buffer<uint8_t[]>(queue_, nTargets);
    auto ownerPls_buf = cms::alpakatools::make_device_buffer<int32_t[]>(queue_, nTargets);

    alpaka::exec<Acc1D>(
        queue_, chainFlat_workDiv, ChainAttachInitPls{}, plsOwned_buf.data(), plsKey_buf.data(), pixelSize_);
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
      alpaka::memset(queue_, hashKey_buf, 0xFF);  // chainattach::kSeedHashEmpty everywhere
      alpaka::exec<Acc1D>(queue_,
                          chainScan_workDiv,
                          ChainSegPrefix{},
                          ownKeep_buf.data(),
                          ownOffs_buf.data(),
                          nOwners_buf.data(),
                          nTargets);
      alpaka::exec<Acc1D>(queue_,
                          chainFlat_workDiv,
                          ChainAttachOwnerScatter{},
                          targets_buf.data(),
                          ownKeep_buf.data(),
                          ownOffs_buf.data(),
                          nTargets,
                          order_buf.data());
      alpaka::exec<Acc1D>(queue_,
                          chainFlat_workDiv,
                          ChainAttachRDRank{},
                          chainsDC_->const_view(),
                          order_buf.data(),
                          nOwners_buf.data(),
                          nTargets,
                          orderRanked_buf.data(),
                          stats_buf.data());
      alpaka::exec<Acc1D>(queue_,
                          chainFlat_workDiv,
                          ChainAttachOwnerHits{},
                          lstInputDC_->const_view().pixelSeeds(),
                          lstInputDC_->const_view().hits(),
                          chainsDC_->const_view(),
                          orderRanked_buf.data(),
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
                          orderRanked_buf.data(),
                          nOwners_buf.data(),
                          ownerHits_buf.data(),
                          ownerNHits_buf.data(),
                          ownerPls_buf.data(),
                          hashKey_buf.data(),
                          hashVal_buf.data(),
                          stats_buf.data());
    }
    alpaka::exec<Acc1D>(queue_,
                        chainFlat_workDiv,
                        ChainAttachPublish{},
                        chainsDC_->const_view(),
                        targets_buf.data(),
                        nTargets,
                        plsOwned_buf.data(),
                        stats_buf.data());
    alpaka::exec<Acc1D>(queue_, serial_workDiv, ChainAttachCount{}, chainsDC_->view(), stats_buf.data());
  }
  auto const a3b = stamp();
  if constexpr (kChainSerialArb) {
    alpaka::exec<Acc1D>(queue_,
                        serial_workDiv,
                        ChainSuppressCarriedTCs{},
                        trackCandidatesBaseDC_->view(),
                        trackCandidatesExtendedDC_->view(),
                        pixelTripletsDC_->const_view(),
                        pixelQuintupletsDC_->const_view(),
                        rangesDC_->const_view(),
                        nLowerModules_,
                        plsOwned_buf.data(),
                        plsBest_buf.data(),
                        pixelSize_,
                        stats_buf.data(),
                        chainConfig_);
  } else {
    uint32_t const nIn = nAllocatedTCs;
    auto keep_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, std::max(1u, nIn));
    auto offs_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, nIn + 1u);
    auto total_buf = cms::alpakatools::make_device_buffer<uint32_t>(queue_);
    auto class_buf = cms::alpakatools::make_device_buffer<uint32_t[]>(queue_, 3u);
    auto stage_buf = cms::alpakatools::make_device_buffer<ChainTCRowPayload[]>(queue_, std::max(1u, nIn));
    alpaka::memset(queue_, class_buf, 0u);
    alpaka::exec<Acc1D>(queue_,
                        chainFlat_workDiv,
                        ChainTCKeepSuppress{},
                        trackCandidatesBaseDC_->const_view(),
                        trackCandidatesExtendedDC_->const_view(),
                        pixelTripletsDC_->const_view(),
                        pixelQuintupletsDC_->const_view(),
                        rangesDC_->const_view(),
                        nLowerModules_,
                        plsOwned_buf.data(),
                        plsBest_buf.data(),
                        pixelSize_,
                        keep_buf.data(),
                        class_buf.data(),
                        nIn,
                        stats_buf.data(),
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
  auto const a4 = stamp();

  attachGridAudit(nTargets, plsPre_buf.data(), tgtPre_buf.data(), offsets_buf.data(), items_buf.data());

  if (timing || objectsStatistics_) {
    auto stats_h = cms::alpakatools::make_host_buffer<uint32_t[]>(queue_, chainattach::kStats);
    alpaka::memcpy(queue_, stats_h, stats_buf);
    alpaka::wait(queue_);
    uint32_t const* st = stats_h.data();
    auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
    attachSummary_ = std::format(
        "targets={} pLS={} gridEntries={} cand={} scored={} picks={} attached={} rdRevoked={} "
        "carriedRetired={} hashOverflow={} tieRD={} | pre {:.3f} ms | grid {:.3f} ms | "
        "score {:.3f} ms | contend {:.3f} ms | RDdedup {:.3f} ms | suppress {:.3f} ms",
        nTargets,
        pixelSize_,
        nEntries,
        st[1],
        st[2],
        st[3],
        st[4],
        st[5],
        st[6],
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
  auto const crossCleanpT3_workDiv = cms::alpakatools::make_workdiv<Acc2D>({20, 4}, {64, 16});

  alpaka::exec<Acc2D>(queue_,
                      crossCleanpT3_workDiv,
                      CrossCleanpT3{},
                      modules_.const_view().modules(),
                      rangesDC_->const_view(),
                      pixelTripletsDC_->view(),
                      lstInputDC_->const_view().pixelSeeds(),
                      pixelQuintupletsDC_->const_view());

  // Pull nEligibleT5Modules from the device.
  auto rangesOccupancy = rangesDC_->view();
  auto nEligibleModules_buf_h = cms::alpakatools::make_host_buffer<uint16_t>(queue_);
  auto nEligibleModules_buf_d = cms::alpakatools::make_device_view(queue_, rangesOccupancy.nEligibleT5Modules());
  alpaka::memcpy(queue_, nEligibleModules_buf_h, nEligibleModules_buf_d);
  alpaka::wait(queue_);  // wait to get the value before using
  auto const nEligibleModules = *nEligibleModules_buf_h.data();

  constexpr int threadsPerBlockY = 16;
  constexpr int threadsPerBlockX = 32;
  auto const removeDupQuintupletsBeforeTC_workDiv = cms::alpakatools::make_workdiv<Acc2D>(
      {std::max(nEligibleModules / threadsPerBlockY, 1), std::max(nEligibleModules / threadsPerBlockX, 1)}, {16, 32});

  alpaka::exec<Acc2D>(queue_,
                      removeDupQuintupletsBeforeTC_workDiv,
                      RemoveDupQuintupletsBeforeTC{},
                      quintupletsDC_->view().quintuplets(),
                      quintupletsDC_->view().quintupletsOccupancy(),
                      rangesDC_->const_view());

  constexpr int threadsPerBlock = 32;
  auto const crossCleanT5_workDiv = cms::alpakatools::make_workdiv<Acc3D>(
      {(nLowerModules_ / threadsPerBlock) + 1, 1, max_blocks}, {threadsPerBlock, 1, threadsPerBlock});

  alpaka::exec<Acc3D>(queue_,
                      crossCleanT5_workDiv,
                      CrossCleanT5{},
                      modules_.const_view().modules(),
                      quintupletsDC_->view().quintuplets(),
                      quintupletsDC_->const_view().quintupletsOccupancy(),
                      pixelQuintupletsDC_->const_view(),
                      pixelTripletsDC_->const_view(),
                      rangesDC_->const_view());

  auto nEligibleModulesT4_buf_h = cms::alpakatools::make_host_buffer<uint16_t>(queue_);
  auto nEligibleModulesT4_buf_d = cms::alpakatools::make_device_view(queue_, rangesOccupancy.nEligibleT4Modules());
  alpaka::memcpy(queue_, nEligibleModulesT4_buf_h, nEligibleModulesT4_buf_d);
  alpaka::wait(queue_);  // wait to get the value before using
  auto const nEligibleModulesT4 = *nEligibleModulesT4_buf_h.data();

  auto const removeDupQuadrupletsBeforeTC_workDiv = cms::alpakatools::make_workdiv<Acc2D>(
      {std::max(nEligibleModulesT4 / threadsPerBlockY, 1), std::max(nEligibleModulesT4 / threadsPerBlockX, 1)},
      {16, 32});

  alpaka::exec<Acc2D>(queue_,
                      removeDupQuadrupletsBeforeTC_workDiv,
                      RemoveDupQuadrupletsBeforeTC{},
                      quadrupletsDC_->view().quadruplets(),
                      quadrupletsDC_->view().quadrupletsOccupancy(),
                      rangesDC_->const_view());

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

  // Counting kernel
  auto nSurvivingTCs_dev = cms::alpakatools::make_device_buffer<unsigned int[]>(queue_, 5u);
  alpaka::memset(queue_, nSurvivingTCs_dev, 0u);

  auto const countSurvivingTCs_workDiv = cms::alpakatools::make_workdiv<Acc1D>(max_blocks, 256);

  alpaka::exec<Acc1D>(queue_,
                      countSurvivingTCs_workDiv,
                      CountSurvivingTCs{},
                      nLowerModules_,
                      pixelQuintupletsDC_->const_view(),
                      pixelTripletsDC_->const_view(),
                      quintupletsDC_->const_view().quintuplets(),
                      quintupletsDC_->const_view().quintupletsOccupancy(),
                      quadrupletsDC_->const_view().quadruplets(),
                      quadrupletsDC_->const_view().quadrupletsOccupancy(),
                      segmentsDC_->const_view().segmentsOccupancy(),
                      lstInputDC_->const_view().pixelSeeds(),
                      pixelSegmentsDC_->const_view(),
                      rangesDC_->const_view(),
                      nSurvivingTCs_dev.data(),
                      tc_pls_triplets);

  auto nSurvivingTCs_host = cms::alpakatools::make_host_buffer<unsigned int[]>(queue_, 5u);
  alpaka::memcpy(queue_, nSurvivingTCs_host, nSurvivingTCs_dev);
  alpaka::wait(queue_);  // wait to get counts before allocation

  auto const* counts = nSurvivingTCs_host.data();
  constexpr unsigned int nMaxTC = n_max_nonpixel_track_candidates + n_max_pixel_track_candidates;
  unsigned int nTotal = std::min(counts[0] + counts[1] + counts[2] + counts[3] + counts[4], nMaxTC);
  // Chain tracking (P2.3) runs the whole baseline sequence first -- so every crossclean sees the
  // collection it sees at baseline and the carried pT3 / bare-pLS rows stay bit-identical -- and
  // only afterwards drops the replaced classes and appends the accepted chains. The baseline rows
  // and the chain rows therefore coexist for the length of createTrackCandidates, and the buffer
  // carries headroom for one TC per welded chain (the K9-accepted set is a subset of them).
  unsigned int const nChainTCHeadroom = useChainTracking_ ? nChainCount_ : 0u;
  nTotal += nChainTCHeadroom;
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
        "[MEM] TrackCandidates: {} allocated ({:.1f} MB) [dynamic: {} pT5 + {} pT3 + {} T5 + {} T4 + {} pLS]",
        nTotal,
        mb,
        counts[0],
        counts[1],
        counts[2],
        counts[3],
        counts[4]));
  }

  auto const addpT5asTrackCandidate_workDiv = cms::alpakatools::make_workdiv<Acc1D>(1, 256);

  alpaka::exec<Acc1D>(queue_,
                      addpT5asTrackCandidate_workDiv,
                      AddpT5asTrackCandidate{},
                      nLowerModules_,
                      pixelQuintupletsDC_->const_view(),
                      trackCandidatesBaseDC_->view(),
                      trackCandidatesExtendedDC_->view(),
                      lstInputDC_->const_view().pixelSeeds(),
                      rangesDC_->const_view(),
                      nTotal);

  auto const addpT3asTrackCandidates_workDiv = cms::alpakatools::make_workdiv<Acc1D>(1, 512);

  alpaka::exec<Acc1D>(queue_,
                      addpT3asTrackCandidates_workDiv,
                      AddpT3asTrackCandidates{},
                      nLowerModules_,
                      pixelTripletsDC_->const_view(),
                      trackCandidatesBaseDC_->view(),
                      trackCandidatesExtendedDC_->view(),
                      lstInputDC_->const_view().pixelSeeds(),
                      rangesDC_->const_view(),
                      nTotal);

  auto const addT5asTrackCandidate_workDiv = cms::alpakatools::make_workdiv<Acc2D>({8, 10}, {8, 128});

  alpaka::exec<Acc2D>(queue_,
                      addT5asTrackCandidate_workDiv,
                      AddT5asTrackCandidate{},
                      nLowerModules_,
                      quintupletsDC_->const_view().quintuplets(),
                      quintupletsDC_->const_view().quintupletsOccupancy(),
                      trackCandidatesBaseDC_->view(),
                      trackCandidatesExtendedDC_->view(),
                      rangesDC_->const_view(),
                      nTotal);

  auto const crossCleanT4_workDiv = cms::alpakatools::make_workdiv<Acc3D>(
      {(nLowerModules_ / threadsPerBlock) + 1, 1, max_blocks}, {threadsPerBlock, 1, threadsPerBlock});

  alpaka::exec<Acc3D>(queue_,
                      crossCleanT4_workDiv,
                      CrossCleanT4{},
                      modules_.const_view().modules(),
                      quadrupletsDC_->view().quadruplets(),
                      quadrupletsDC_->const_view().quadrupletsOccupancy(),
                      pixelQuintupletsDC_->const_view(),
                      pixelTripletsDC_->const_view(),
                      quintupletsDC_->const_view().quintuplets(),
                      trackCandidatesBaseDC_->view(),
                      trackCandidatesExtendedDC_->view(),
                      miniDoubletsDC_->view().miniDoublets(),
                      segmentsDC_->view().segments(),
                      tripletsDC_->view().triplets(),
                      rangesDC_->const_view());

  auto const addT4asTrackCandidate_workDiv = cms::alpakatools::make_workdiv<Acc2D>({8, 10}, {8, 128});

  alpaka::exec<Acc2D>(queue_,
                      addT4asTrackCandidate_workDiv,
                      AddT4asTrackCandidate{},
                      nLowerModules_,
                      quadrupletsDC_->view().quadruplets(),
                      quadrupletsDC_->const_view().quadrupletsOccupancy(),
                      tripletsDC_->const_view().triplets(),
                      trackCandidatesBaseDC_->view(),
                      trackCandidatesExtendedDC_->view(),
                      rangesDC_->const_view(),
                      nTotal);

  auto const crossCleanpLS_workDiv = cms::alpakatools::make_workdiv<Acc2D>({20, 4}, {32, 16});

  alpaka::exec<Acc2D>(queue_,
                      crossCleanpLS_workDiv,
                      CrossCleanpLS{},
                      modules_.const_view().modules(),
                      rangesDC_->const_view(),
                      pixelTripletsDC_->const_view(),
                      trackCandidatesBaseDC_->view(),
                      trackCandidatesExtendedDC_->view(),
                      segmentsDC_->const_view().segments(),
                      segmentsDC_->const_view().segmentsOccupancy(),
                      lstInputDC_->const_view().pixelSeeds(),
                      pixelSegmentsDC_->view(),
                      miniDoubletsDC_->const_view().miniDoublets(),
                      lstInputDC_->const_view().hits(),
                      quintupletsDC_->const_view().quintuplets(),
                      quadrupletsDC_->const_view().quadruplets());

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
  if (useChainTracking_)
    arbitrateChains(nTotal);

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
  if (nTrackCandidatesTotal > nMaxTC + nChainTCHeadroom) {
    lstWarning(
        "\
        ****************************************************************************************************\n\
        * Track candidates were possibly truncated.                                                        *\n\
        * The dynamically allocated TC buffer was fully used.                                              *\n\
        * Run the code with the WARNINGS flag activated for more details.                                  *\n\
        ****************************************************************************************************");
  }
}

void LSTEvent::createPixelTriplets() {
  if (!pixelTripletsDC_) {
    pixelTripletsDC_.emplace(queue_, n_max_pixel_triplets);
    auto nPixelTriplets_view = cms::alpakatools::make_device_view(queue_, (*pixelTripletsDC_)->nPixelTriplets());
    alpaka::memset(queue_, nPixelTriplets_view, 0u);
    auto totOccupancyPixelTriplets_view =
        cms::alpakatools::make_device_view(queue_, (*pixelTripletsDC_)->totOccupancyPixelTriplets());
    alpaka::memset(queue_, totOccupancyPixelTriplets_view, 0u);
    if (objectsStatistics_) {
      double mb = alpaka::getExtentProduct(pixelTripletsDC_->buffer()) / 1e6;
      memoryAllocatedMB_ += mb;
      lstWarning(std::format("[MEM] PixelTriplets: {} allocated ({:.1f} MB) [fixed]", n_max_pixel_triplets, mb));
    }
  }
  SegmentsOccupancy segmentsOccupancy = segmentsDC_->view().segmentsOccupancy();
  PixelSeedsConst pixelSeeds = lstInputDC_->const_view().pixelSeeds();

  auto superbins_buf = cms::alpakatools::make_host_buffer<int[]>(queue_, pixelSize_);
  auto pixelTypes_buf = cms::alpakatools::make_host_buffer<PixelType[]>(queue_, pixelSize_);

  alpaka::memcpy(queue_, superbins_buf, cms::alpakatools::make_device_view(queue_, pixelSeeds.superbin(), pixelSize_));
  alpaka::memcpy(
      queue_, pixelTypes_buf, cms::alpakatools::make_device_view(queue_, pixelSeeds.pixelType(), pixelSize_));
  auto const* superbins = superbins_buf.data();
  auto const* pixelTypes = pixelTypes_buf.data();

  unsigned int nInnerSegments;
  auto nInnerSegments_src_view = cms::alpakatools::make_host_view(nInnerSegments);

  // Create a sub-view for the device buffer
  auto dev_view_nSegments = cms::alpakatools::make_device_view(queue_, segmentsOccupancy.nSegments()[nLowerModules_]);

  alpaka::memcpy(queue_, nInnerSegments_src_view, dev_view_nSegments);
  alpaka::wait(queue_);  // wait to get nInnerSegments (also superbins and pixelTypes) before using

  auto connectedPixelSize_host_buf = cms::alpakatools::make_host_buffer<unsigned int[]>(queue_, nInnerSegments);
  auto connectedPixelIndex_host_buf = cms::alpakatools::make_host_buffer<unsigned int[]>(queue_, nInnerSegments);
  auto connectedPixelSize_dev_buf = cms::alpakatools::make_device_buffer<unsigned int[]>(queue_, nInnerSegments);
  auto connectedPixelIndex_dev_buf = cms::alpakatools::make_device_buffer<unsigned int[]>(queue_, nInnerSegments);

  unsigned int* connectedPixelSize_host = connectedPixelSize_host_buf.data();
  unsigned int* connectedPixelIndex_host = connectedPixelIndex_host_buf.data();

  int pixelIndexOffsetPos =
      pixelMapping_.connectedPixelsIndex[size_superbins - 1] + pixelMapping_.connectedPixelsSizes[size_superbins - 1];
  int pixelIndexOffsetNeg = pixelMapping_.connectedPixelsIndexPos[size_superbins - 1] +
                            pixelMapping_.connectedPixelsSizesPos[size_superbins - 1] + pixelIndexOffsetPos;

  // TODO: check if a map/reduction to just eligible pLSs would speed up the kernel
  // the current selection still leaves a significant fraction of unmatchable pLSs
  for (unsigned int i = 0; i < nInnerSegments; i++) {  // loop over # pLS
    PixelType pixelType = pixelTypes[i];               // Get pixel type for this pLS
    int superbin = superbins[i];                       // Get superbin for this pixel
    if ((superbin < 0) or (superbin >= (int)size_superbins) or
        ((pixelType != PixelType::kHighPt) and (pixelType != PixelType::kLowPtPosCurv) and
         (pixelType != PixelType::kLowPtNegCurv))) {
      connectedPixelSize_host[i] = 0;
      connectedPixelIndex_host[i] = 0;
      continue;
    }

    // Used pixel type to select correct size-index arrays
    switch (pixelType) {
      case PixelType::kInvalid:
        break;
      case PixelType::kHighPt:
        // number of connected modules to this pixel
        connectedPixelSize_host[i] = pixelMapping_.connectedPixelsSizes[superbin];
        // index to get start of connected modules for this superbin in map
        connectedPixelIndex_host[i] = pixelMapping_.connectedPixelsIndex[superbin];
        break;
      case PixelType::kLowPtPosCurv:
        // number of connected modules to this pixel
        connectedPixelSize_host[i] = pixelMapping_.connectedPixelsSizesPos[superbin];
        // index to get start of connected modules for this superbin in map
        connectedPixelIndex_host[i] = pixelMapping_.connectedPixelsIndexPos[superbin] + pixelIndexOffsetPos;
        break;
      case PixelType::kLowPtNegCurv:
        // number of connected modules to this pixel
        connectedPixelSize_host[i] = pixelMapping_.connectedPixelsSizesNeg[superbin];
        // index to get start of connected modules for this superbin in map
        connectedPixelIndex_host[i] = pixelMapping_.connectedPixelsIndexNeg[superbin] + pixelIndexOffsetNeg;
        break;
    }
  }

  alpaka::memcpy(queue_, connectedPixelSize_dev_buf, connectedPixelSize_host_buf, nInnerSegments);
  alpaka::memcpy(queue_, connectedPixelIndex_dev_buf, connectedPixelIndex_host_buf, nInnerSegments);

  auto const createPixelTripletsFromMap_workDiv =
      cms::alpakatools::make_workdiv<Acc3D>({4096, 16 /* above median of connected modules*/, 1}, {4, 1, 32});

  alpaka::exec<Acc3D>(queue_,
                      createPixelTripletsFromMap_workDiv,
                      CreatePixelTripletsFromMap{},
                      modules_.const_view().modules(),
                      modules_.const_view().modulesPixel(),
                      rangesDC_->const_view(),
                      miniDoubletsDC_->const_view().miniDoublets(),
                      segmentsDC_->const_view().segments(),
                      lstInputDC_->const_view().pixelSeeds(),
                      pixelSegmentsDC_->const_view(),
                      tripletsDC_->view().triplets(),
                      tripletsDC_->const_view().tripletsOccupancy(),
                      pixelTripletsDC_->view(),
                      connectedPixelSize_dev_buf.data(),
                      connectedPixelIndex_dev_buf.data(),
                      nInnerSegments,
                      ptCut_);

#ifdef WARNINGS
  auto nPixelTriplets_buf = cms::alpakatools::make_host_buffer<unsigned int>(queue_);

  alpaka::memcpy(
      queue_, nPixelTriplets_buf, cms::alpakatools::make_device_view(queue_, (*pixelTripletsDC_)->nPixelTriplets()));
  alpaka::wait(queue_);  // wait to get the value before using it

  std::cout << "number of pixel triplets = " << *nPixelTriplets_buf.data() << std::endl;
#endif

  //pT3s can be cleaned here because they're not used in making pT5s!
  //seems like more blocks lead to conflicting writes
  auto const removeDupPixelTripletsFromMap_workDiv = cms::alpakatools::make_workdiv<Acc2D>({40, 1}, {16, 16});

  alpaka::exec<Acc2D>(
      queue_, removeDupPixelTripletsFromMap_workDiv, RemoveDupPixelTripletsFromMap{}, pixelTripletsDC_->view());
}

void LSTEvent::createQuintuplets() {
  auto const countConn_workDiv = cms::alpakatools::make_workdiv<Acc3D>({nLowerModules_, 1, 1}, {1, 8, 32});

  auto execCountTripletConn = [&](auto kernel) {
    alpaka::exec<Acc3D>(queue_,
                        countConn_workDiv,
                        kernel,
                        modules_.const_view().modules(),
                        miniDoubletsDC_->const_view().miniDoublets(),
                        segmentsDC_->const_view().segments(),
                        tripletsDC_->view().triplets(),
                        tripletsDC_->const_view().tripletsOccupancy(),
                        rangesDC_->const_view(),
                        ptCut_);
  };
  if (reduceMemByFullPrecompute_)
    execCountTripletConn(CountTripletConnectionsReduceMem{});
  else
    execCountTripletConn(CountTripletConnections{});

  auto const createEligibleModulesListForQuintuplets_workDiv = cms::alpakatools::make_workdiv<Acc1D>(1, 1024);

  alpaka::exec<Acc1D>(queue_,
                      createEligibleModulesListForQuintuplets_workDiv,
                      CreateEligibleModulesListForQuintuplets{},
                      modules_.const_view().modules(),
                      tripletsDC_->const_view().tripletsOccupancy(),
                      rangesDC_->view(),
                      tripletsDC_->view().triplets());

  auto nEligibleT5Modules_buf = cms::alpakatools::make_host_buffer<uint16_t>(queue_);
  auto nTotalQuintuplets_buf = cms::alpakatools::make_host_buffer<unsigned int>(queue_);
  auto rangesOccupancy = rangesDC_->view();
  auto nEligibleT5Modules_view_d = cms::alpakatools::make_device_view(queue_, rangesOccupancy.nEligibleT5Modules());
  auto nTotalQuintuplets_view_d = cms::alpakatools::make_device_view(queue_, rangesOccupancy.nTotalQuints());
  alpaka::memcpy(queue_, nEligibleT5Modules_buf, nEligibleT5Modules_view_d);
  alpaka::memcpy(queue_, nTotalQuintuplets_buf, nTotalQuintuplets_view_d);
  alpaka::wait(queue_);  // wait for the values before using them

  auto nEligibleT5Modules = *nEligibleT5Modules_buf.data();
  auto nTotalQuintuplets = *nTotalQuintuplets_buf.data();

  if (!quintupletsDC_) {
    quintupletsDC_.emplace(queue_, nTotalQuintuplets, nLowerModules_);
    if (objectsStatistics_) {
      double mb = alpaka::getExtentProduct(quintupletsDC_->buffer()) / 1e6;
      memoryAllocatedMB_ += mb;
      lstWarning(std::format("[MEM] Quintuplets: {} allocated ({:.1f} MB)", nTotalQuintuplets, mb));
    }
    auto quintupletsOccupancy = quintupletsDC_->view().quintupletsOccupancy();
    auto nQuintuplets_view = cms::alpakatools::make_device_view(queue_, quintupletsOccupancy.nQuintuplets());
    alpaka::memset(queue_, nQuintuplets_view, 0u);
    auto totOccupancyQuintuplets_view =
        cms::alpakatools::make_device_view(queue_, quintupletsOccupancy.totOccupancyQuintuplets());
    alpaka::memset(queue_, totOccupancyQuintuplets_view, 0u);
    auto quintuplets = quintupletsDC_->view().quintuplets();
    auto isDup_view = cms::alpakatools::make_device_view(queue_, quintuplets.isDup());
    alpaka::memset(queue_, isDup_view, 0u);
    auto nLayers_view = cms::alpakatools::make_device_view(queue_, quintuplets.nLayers());
    alpaka::memset(queue_, nLayers_view, 0u);
    auto tightCutFlag_view = cms::alpakatools::make_device_view(queue_, quintuplets.tightCutFlag());
    alpaka::memset(queue_, tightCutFlag_view, 0u);
    auto partOfPT5_view = cms::alpakatools::make_device_view(queue_, quintuplets.partOfPT5());
    alpaka::memset(queue_, partOfPT5_view, 0u);
  }

  auto const createQuintuplets_workDiv =
      cms::alpakatools::make_workdiv<Acc3D>({std::max((int)nEligibleT5Modules, 1), 1, 1}, {1, 8, 32});

  auto execCreateQuintuplets = [&](auto kernel) {
    alpaka::exec<Acc3D>(queue_,
                        createQuintuplets_workDiv,
                        kernel,
                        modules_.const_view().modules(),
                        miniDoubletsDC_->const_view().miniDoublets(),
                        segmentsDC_->const_view().segments(),
                        tripletsDC_->view().triplets(),
                        tripletsDC_->const_view().tripletsOccupancy(),
                        quintupletsDC_->view().quintuplets(),
                        quintupletsDC_->view().quintupletsOccupancy(),
                        rangesDC_->const_view(),
                        nEligibleT5Modules,
                        ptCut_);
  };
  if (reduceMemByFullPrecompute_)
    execCreateQuintuplets(CreateQuintupletsReduceMem{});
  else
    execCreateQuintuplets(CreateQuintuplets{});

  if (nTotalQuintuplets > 0) {
    auto const extendT5_workDiv = cms::alpakatools::make_workdiv<Acc1D>(nTotalQuintuplets, 128);

    alpaka::exec<Acc1D>(queue_,
                        extendT5_workDiv,
                        ExtendT5FromDupT5{},
                        modules_.const_view().modules(),
                        rangesDC_->const_view(),
                        quintupletsDC_->view().quintuplets(),
                        quintupletsDC_->const_view().quintupletsOccupancy());
  }

  auto const removeDupQuintupletsAfterBuild_workDiv =
      cms::alpakatools::make_workdiv<Acc3D>({max_blocks, 1, 1}, {1, 16, 16});

  alpaka::exec<Acc3D>(queue_,
                      removeDupQuintupletsAfterBuild_workDiv,
                      RemoveDupQuintupletsAfterBuild{},
                      modules_.const_view().modules(),
                      quintupletsDC_->view().quintuplets(),
                      quintupletsDC_->const_view().quintupletsOccupancy(),
                      rangesDC_->const_view());

  auto const addQuintupletRangesToEventExplicit_workDiv = cms::alpakatools::make_workdiv<Acc1D>(1, 1024);

  alpaka::exec<Acc1D>(queue_,
                      addQuintupletRangesToEventExplicit_workDiv,
                      AddQuintupletRangesToEventExplicit{},
                      modules_.const_view().modules(),
                      quintupletsDC_->const_view().quintupletsOccupancy(),
                      rangesDC_->view());

  if (objectsStatistics_) {
    addQuintupletsToEventExplicit();
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
}

void LSTEvent::createPixelQuintuplets() {
  if (!pixelQuintupletsDC_) {
    pixelQuintupletsDC_.emplace(queue_, n_max_pixel_quintuplets);
    auto nPixelQuintuplets_view =
        cms::alpakatools::make_device_view(queue_, (*pixelQuintupletsDC_)->nPixelQuintuplets());
    alpaka::memset(queue_, nPixelQuintuplets_view, 0u);
    auto totOccupancyPixelQuintuplets_view =
        cms::alpakatools::make_device_view(queue_, (*pixelQuintupletsDC_)->totOccupancyPixelQuintuplets());
    alpaka::memset(queue_, totOccupancyPixelQuintuplets_view, 0u);
    if (objectsStatistics_) {
      double mb = alpaka::getExtentProduct(pixelQuintupletsDC_->buffer()) / 1e6;
      memoryAllocatedMB_ += mb;
      lstWarning(std::format("[MEM] PixelQuintuplets: {} allocated ({:.1f} MB) [fixed]", n_max_pixel_quintuplets, mb));
    }
  }
  SegmentsOccupancy segmentsOccupancy = segmentsDC_->view().segmentsOccupancy();
  PixelSeedsConst pixelSeeds = lstInputDC_->const_view().pixelSeeds();

  auto superbins_buf = cms::alpakatools::make_host_buffer<int[]>(queue_, pixelSize_);
  auto pixelTypes_buf = cms::alpakatools::make_host_buffer<PixelType[]>(queue_, pixelSize_);

  alpaka::memcpy(queue_, superbins_buf, cms::alpakatools::make_device_view(queue_, pixelSeeds.superbin(), pixelSize_));
  alpaka::memcpy(
      queue_, pixelTypes_buf, cms::alpakatools::make_device_view(queue_, pixelSeeds.pixelType(), pixelSize_));
  auto const* superbins = superbins_buf.data();
  auto const* pixelTypes = pixelTypes_buf.data();

  unsigned int nInnerSegments;
  auto nInnerSegments_src_view = cms::alpakatools::make_host_view(nInnerSegments);

  // Create a sub-view for the device buffer
  unsigned int totalModules = nLowerModules_ + 1;
  auto dev_view_nSegments_buf = cms::alpakatools::make_device_view(queue_, segmentsOccupancy.nSegments(), totalModules);
  auto dev_view_nSegments = cms::alpakatools::make_device_view(queue_, segmentsOccupancy.nSegments()[nLowerModules_]);

  alpaka::memcpy(queue_, nInnerSegments_src_view, dev_view_nSegments);
  alpaka::wait(queue_);  // wait to get nInnerSegments (also superbins and pixelTypes) before using

  auto connectedPixelSize_host_buf = cms::alpakatools::make_host_buffer<unsigned int[]>(queue_, nInnerSegments);
  auto connectedPixelIndex_host_buf = cms::alpakatools::make_host_buffer<unsigned int[]>(queue_, nInnerSegments);
  auto connectedPixelSize_dev_buf = cms::alpakatools::make_device_buffer<unsigned int[]>(queue_, nInnerSegments);
  auto connectedPixelIndex_dev_buf = cms::alpakatools::make_device_buffer<unsigned int[]>(queue_, nInnerSegments);

  auto* connectedPixelSize_host = connectedPixelSize_host_buf.data();
  auto* connectedPixelIndex_host = connectedPixelIndex_host_buf.data();

  int pixelIndexOffsetPos = pixelMapping_.connectedPixelsIndex[::size_superbins - 1] +
                            pixelMapping_.connectedPixelsSizes[::size_superbins - 1];
  int pixelIndexOffsetNeg = pixelMapping_.connectedPixelsIndexPos[::size_superbins - 1] +
                            pixelMapping_.connectedPixelsSizesPos[::size_superbins - 1] + pixelIndexOffsetPos;

  // Loop over # pLS
  for (unsigned int i = 0; i < nInnerSegments; i++) {
    PixelType pixelType = pixelTypes[i];  // Get pixel type for this pLS
    int superbin = superbins[i];          // Get superbin for this pixel
    if ((superbin < 0) or (superbin >= (int)size_superbins) or
        ((pixelType != PixelType::kHighPt) and (pixelType != PixelType::kLowPtPosCurv) and
         (pixelType != PixelType::kLowPtNegCurv))) {
      connectedPixelSize_host[i] = 0;
      connectedPixelIndex_host[i] = 0;
      continue;
    }

    // Used pixel type to select correct size-index arrays
    switch (pixelType) {
      case PixelType::kInvalid:
        break;
      case PixelType::kHighPt:
        // number of connected modules to this pixel
        connectedPixelSize_host[i] = pixelMapping_.connectedPixelsSizes[superbin];
        // index to get start of connected modules for this superbin in map
        connectedPixelIndex_host[i] = pixelMapping_.connectedPixelsIndex[superbin];
        break;
      case PixelType::kLowPtPosCurv:
        // number of connected modules to this pixel
        connectedPixelSize_host[i] = pixelMapping_.connectedPixelsSizesPos[superbin];
        // index to get start of connected modules for this superbin in map
        connectedPixelIndex_host[i] = pixelMapping_.connectedPixelsIndexPos[superbin] + pixelIndexOffsetPos;
        break;
      case PixelType::kLowPtNegCurv:
        // number of connected modules to this pixel
        connectedPixelSize_host[i] = pixelMapping_.connectedPixelsSizesNeg[superbin];
        // index to get start of connected modules for this superbin in map
        connectedPixelIndex_host[i] = pixelMapping_.connectedPixelsIndexNeg[superbin] + pixelIndexOffsetNeg;
        break;
    }
  }

  alpaka::memcpy(queue_, connectedPixelSize_dev_buf, connectedPixelSize_host_buf, nInnerSegments);
  alpaka::memcpy(queue_, connectedPixelIndex_dev_buf, connectedPixelIndex_host_buf, nInnerSegments);

  auto const createPixelQuintupletsFromMap_workDiv =
      cms::alpakatools::make_workdiv<Acc3D>({max_blocks, 16, 1}, {16, 1, 16});

  alpaka::exec<Acc3D>(queue_,
                      createPixelQuintupletsFromMap_workDiv,
                      CreatePixelQuintupletsFromMap{},
                      modules_.const_view().modules(),
                      modules_.const_view().modulesPixel(),
                      miniDoubletsDC_->const_view().miniDoublets(),
                      segmentsDC_->const_view().segments(),
                      lstInputDC_->const_view().pixelSeeds(),
                      pixelSegmentsDC_->view(),
                      tripletsDC_->view().triplets(),
                      quintupletsDC_->view().quintuplets(),
                      quintupletsDC_->const_view().quintupletsOccupancy(),
                      pixelQuintupletsDC_->view(),
                      connectedPixelSize_dev_buf.data(),
                      connectedPixelIndex_dev_buf.data(),
                      nInnerSegments,
                      rangesDC_->const_view(),
                      ptCut_);

  auto const removeDupPixelQuintupletsFromMap_workDiv =
      cms::alpakatools::make_workdiv<Acc2D>({max_blocks, 1}, {16, 16});

  alpaka::exec<Acc2D>(queue_,
                      removeDupPixelQuintupletsFromMap_workDiv,
                      RemoveDupPixelQuintupletsFromMap{},
                      pixelQuintupletsDC_->view());

#ifdef WARNINGS
  auto nPixelQuintuplets_buf = cms::alpakatools::make_host_buffer<unsigned int>(queue_);

  alpaka::memcpy(queue_,
                 nPixelQuintuplets_buf,
                 cms::alpakatools::make_device_view(queue_, (*pixelQuintupletsDC_)->nPixelQuintuplets()));
  alpaka::wait(queue_);  // wait to get the value before using it

  std::cout << "number of pixel quintuplets = " << *nPixelQuintuplets_buf.data() << std::endl;
#endif
}

void LSTEvent::createQuadruplets() {
  auto const countLSConn_workDiv = cms::alpakatools::make_workdiv<Acc3D>({nLowerModules_, 1, 1}, {1, 8, 32});

  auto execCountTripletLSConn = [&](auto kernel) {
    alpaka::exec<Acc3D>(queue_,
                        countLSConn_workDiv,
                        kernel,
                        modules_.const_view().modules(),
                        miniDoubletsDC_->const_view().miniDoublets(),
                        segmentsDC_->const_view().segments(),
                        tripletsDC_->view().triplets(),
                        tripletsDC_->const_view().tripletsOccupancy(),
                        rangesDC_->const_view(),
                        ptCut_);
  };
  if (reduceMemByFullPrecompute_)
    execCountTripletLSConn(CountTripletLSConnectionsReduceMem{});
  else
    execCountTripletLSConn(CountTripletLSConnections{});

  auto const createEligibleModulesListForQuadruplets_workDiv = cms::alpakatools::make_workdiv<Acc1D>(1, 1024);

  alpaka::exec<Acc1D>(queue_,
                      createEligibleModulesListForQuadruplets_workDiv,
                      CreateEligibleModulesListForQuadruplets{},
                      modules_.const_view().modules(),
                      tripletsDC_->const_view().tripletsOccupancy(),
                      rangesDC_->view(),
                      tripletsDC_->view().triplets());

  auto nEligibleT4Modules_buf = cms::alpakatools::make_host_buffer<uint16_t>(queue_);
  auto nTotalQuadruplets_buf = cms::alpakatools::make_host_buffer<unsigned int>(queue_);
  auto rangesOccupancy = rangesDC_->view();
  auto nEligibleT4Modules_view_d = cms::alpakatools::make_device_view(queue_, rangesOccupancy.nEligibleT4Modules());
  auto nTotalQuadruplets_view_d = cms::alpakatools::make_device_view(queue_, rangesOccupancy.nTotalQuads());
  alpaka::memcpy(queue_, nEligibleT4Modules_buf, nEligibleT4Modules_view_d);
  alpaka::memcpy(queue_, nTotalQuadruplets_buf, nTotalQuadruplets_view_d);
  alpaka::wait(queue_);  // wait for the values before using them

  auto nEligibleT4Modules = *nEligibleT4Modules_buf.data();
  auto nTotalQuadruplets = *nTotalQuadruplets_buf.data();

  if (!quadrupletsDC_) {
    quadrupletsDC_.emplace(queue_, nTotalQuadruplets, nLowerModules_);
    if (objectsStatistics_) {
      double mb = alpaka::getExtentProduct(quadrupletsDC_->buffer()) / 1e6;
      memoryAllocatedMB_ += mb;
      lstWarning(std::format("[MEM] Quadruplets: {} allocated ({:.1f} MB)", nTotalQuadruplets, mb));
    }
    auto quadrupletsOccupancy = quadrupletsDC_->view().quadrupletsOccupancy();
    auto nQuadruplets_view = cms::alpakatools::make_device_view(
        queue_, quadrupletsOccupancy.nQuadruplets(), quadrupletsOccupancy.metadata().size());
    alpaka::memset(queue_, nQuadruplets_view, 0u);
    auto totOccupancyQuadruplets_view = cms::alpakatools::make_device_view(
        queue_, quadrupletsOccupancy.totOccupancyQuadruplets(), quadrupletsOccupancy.metadata().size());
    alpaka::memset(queue_, totOccupancyQuadruplets_view, 0u);
    auto quadruplets = quadrupletsDC_->view().quadruplets();
    auto isDup_view = cms::alpakatools::make_device_view(queue_, quadruplets.isDup(), quadruplets.metadata().size());
    alpaka::memset(queue_, isDup_view, 0u);
  }

  auto const createQuadruplets_workDiv =
      cms::alpakatools::make_workdiv<Acc3D>({std::max((int)nEligibleT4Modules, 1), 1, 1}, {1, 8, 32});

  auto execCreateQuadruplets = [&](auto kernel) {
    alpaka::exec<Acc3D>(queue_,
                        createQuadruplets_workDiv,
                        kernel,
                        modules_.const_view().modules(),
                        miniDoubletsDC_->const_view().miniDoublets(),
                        segmentsDC_->const_view().segments(),
                        tripletsDC_->view().triplets(),
                        tripletsDC_->const_view().tripletsOccupancy(),
                        quadrupletsDC_->view().quadruplets(),
                        quadrupletsDC_->view().quadrupletsOccupancy(),
                        rangesDC_->const_view(),
                        nEligibleT4Modules,
                        ptCut_);
  };
  if (reduceMemByFullPrecompute_)
    execCreateQuadruplets(CreateQuadrupletsReduceMem{});
  else
    execCreateQuadruplets(CreateQuadruplets{});

  auto const removeDupQuadrupletsAfterBuild_workDiv =
      cms::alpakatools::make_workdiv<Acc3D>({max_blocks, 1, 1}, {1, 16, 16});

  alpaka::exec<Acc3D>(queue_,
                      removeDupQuadrupletsAfterBuild_workDiv,
                      RemoveDupQuadrupletsAfterBuild{},
                      modules_.const_view().modules(),
                      quadrupletsDC_->view().quadruplets(),
                      quadrupletsDC_->const_view().quadrupletsOccupancy(),
                      rangesDC_->const_view());

  auto const addQuadrupletRangesToEventExplicit_workDiv = cms::alpakatools::make_workdiv<Acc1D>(1, 1024);

  alpaka::exec<Acc1D>(queue_,
                      addQuadrupletRangesToEventExplicit_workDiv,
                      AddQuadrupletRangesToEventExplicit{},
                      modules_.const_view().modules(),
                      quadrupletsDC_->const_view().quadrupletsOccupancy(),
                      rangesDC_->view());

  if (objectsStatistics_) {
    addQuadrupletsToEventExplicit();
  }
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

void LSTEvent::addQuintupletsToEventExplicit() {
  auto quintupletsOccupancy = quintupletsDC_->const_view().quintupletsOccupancy();
  auto nQuintuplets_view =
      cms::alpakatools::make_device_view(queue_, quintupletsOccupancy.nQuintuplets(), nLowerModules_);
  auto nQuintupletsCPU_buf = cms::alpakatools::make_host_buffer<unsigned int[]>(queue_, nLowerModules_);
  alpaka::memcpy(queue_, nQuintupletsCPU_buf, nQuintuplets_view);

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

  auto module_quintupletModuleIndices_buf = cms::alpakatools::make_host_buffer<int[]>(queue_, nLowerModules_);
  auto rangesOccupancy = rangesDC_->view();
  auto quintupletModuleIndices_view_d =
      cms::alpakatools::make_device_view(queue_, rangesOccupancy.quintupletModuleIndices(), nLowerModules_);
  alpaka::memcpy(queue_, module_quintupletModuleIndices_buf, quintupletModuleIndices_view_d);

  alpaka::wait(queue_);  // wait for inputs before using them

  auto const* nQuintupletsCPU = nQuintupletsCPU_buf.data();
  auto const* module_subdets = module_subdets_buf.data();
  auto const* module_layers = module_layers_buf.data();
  auto const* module_quintupletModuleIndices = module_quintupletModuleIndices_buf.data();

  for (uint16_t i = 0; i < nLowerModules_; i++) {
    if (!(nQuintupletsCPU[i] == 0 or module_quintupletModuleIndices[i] == -1)) {
      if (module_subdets[i] == Barrel) {
        n_quintuplets_by_layer_barrel_[module_layers[i] - 1] += nQuintupletsCPU[i];
      } else {
        n_quintuplets_by_layer_endcap_[module_layers[i] - 1] += nQuintupletsCPU[i];
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

void LSTEvent::addQuadrupletsToEventExplicit() {
  auto quadrupletsOccupancy = quadrupletsDC_->const_view().quadrupletsOccupancy();
  auto nQuadruplets_view =
      cms::alpakatools::make_device_view(queue_, quadrupletsOccupancy.nQuadruplets(), nLowerModules_);
  auto nQuadrupletsCPU_buf = cms::alpakatools::make_host_buffer<unsigned int[]>(queue_, nLowerModules_);
  alpaka::memcpy(queue_, nQuadrupletsCPU_buf, nQuadruplets_view);

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

  auto const* nQuadrupletsCPU = nQuadrupletsCPU_buf.data();
  auto const* module_subdets = module_subdets_buf.data();
  auto const* module_layers = module_layers_buf.data();

  for (uint16_t i = 0; i < nLowerModules_; i++) {
    if (nQuadrupletsCPU[i] != 0) {
      if (module_subdets[i] == Barrel) {
        n_quadruplets_by_layer_barrel_[module_layers[i] - 1] += nQuadrupletsCPU[i];
      } else {
        n_quadruplets_by_layer_endcap_[module_layers[i] - 1] += nQuadrupletsCPU[i];
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

int LSTEvent::getNumberOfPixelTriplets() {
  auto nPixelTriplets_buf_h = cms::alpakatools::make_host_buffer<unsigned int>(queue_);

  alpaka::memcpy(
      queue_, nPixelTriplets_buf_h, cms::alpakatools::make_device_view(queue_, (*pixelTripletsDC_)->nPixelTriplets()));
  alpaka::wait(queue_);

  return *nPixelTriplets_buf_h.data();
}

int LSTEvent::getNumberOfPixelQuintuplets() {
  auto nPixelQuintuplets_buf_h = cms::alpakatools::make_host_buffer<unsigned int>(queue_);

  alpaka::memcpy(queue_,
                 nPixelQuintuplets_buf_h,
                 cms::alpakatools::make_device_view(queue_, (*pixelQuintupletsDC_)->nPixelQuintuplets()));
  alpaka::wait(queue_);

  return *nPixelQuintuplets_buf_h.data();
}

unsigned int LSTEvent::getNumberOfQuintuplets() {
  unsigned int quintuplets = 0;
  for (auto& it : n_quintuplets_by_layer_barrel_) {
    quintuplets += it;
  }
  for (auto& it : n_quintuplets_by_layer_endcap_) {
    quintuplets += it;
  }

  return quintuplets;
}

unsigned int LSTEvent::getNumberOfQuintupletsByLayerBarrel(unsigned int layer) {
  return n_quintuplets_by_layer_barrel_[layer];
}

unsigned int LSTEvent::getNumberOfQuintupletsByLayerEndcap(unsigned int layer) {
  return n_quintuplets_by_layer_endcap_[layer];
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

unsigned int LSTEvent::getNumberOfQuadruplets() {
  unsigned int quadruplets = 0;
  for (auto& it : n_quadruplets_by_layer_barrel_) {
    quadruplets += it;
  }
  for (auto& it : n_quadruplets_by_layer_endcap_) {
    quadruplets += it;
  }

  return quadruplets;
}

unsigned int LSTEvent::getNumberOfQuadrupletsByLayerBarrel(unsigned int layer) {
  return n_quadruplets_by_layer_barrel_[layer];
}

unsigned int LSTEvent::getNumberOfQuadrupletsByLayerEndcap(unsigned int layer) {
  return n_quadruplets_by_layer_endcap_[layer];
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

template <typename TSoA, typename TDev>
typename TSoA::ConstView LSTEvent::getQuadruplets(bool sync) {
  if constexpr (std::is_same_v<TDev, DevHost>) {
    return QuadrupletsViewAccessor<TSoA>::get(quadrupletsDC_->const_view());
  } else {
    if (!quadrupletsHC_) {
      quadrupletsHC_.emplace(
          cms::alpakatools::CopyToHost<PortableDeviceCollection<TDev, QuadrupletsSoABlocks>>::copyAsync(
              queue_, *quadrupletsDC_));
      if (sync)
        alpaka::wait(queue_);  // host consumers expect filled data
    }
  }
  return QuadrupletsViewAccessor<TSoA>::get(quadrupletsHC_->const_view());
}
template QuadrupletsConst LSTEvent::getQuadruplets<QuadrupletsSoA>(bool);
template QuadrupletsOccupancyConst LSTEvent::getQuadruplets<QuadrupletsOccupancySoA>(bool);

template <typename TSoA, typename TDev>
typename TSoA::ConstView LSTEvent::getQuintuplets(bool sync) {
  if constexpr (std::is_same_v<TDev, DevHost>) {
    return QuintupletsViewAccessor<TSoA>::get(quintupletsDC_->const_view());
  } else {
    if (!quintupletsHC_) {
      quintupletsHC_.emplace(
          cms::alpakatools::CopyToHost<PortableDeviceCollection<TDev, QuintupletsSoABlocks>>::copyAsync(
              queue_, *quintupletsDC_));
      if (sync)
        alpaka::wait(queue_);  // host consumers expect filled data
    }
  }
  return QuintupletsViewAccessor<TSoA>::get(quintupletsHC_->const_view());
}
template QuintupletsConst LSTEvent::getQuintuplets<QuintupletsSoA>(bool);
template QuintupletsOccupancyConst LSTEvent::getQuintuplets<QuintupletsOccupancySoA>(bool);

template <typename TDev>
PixelTripletsConst LSTEvent::getPixelTriplets(bool sync) {
  if constexpr (std::is_same_v<TDev, DevHost>) {
    return pixelTripletsDC_->const_view();
  } else {
    if (!pixelTripletsHC_) {
      pixelTripletsHC_.emplace(cms::alpakatools::CopyToHost<::PortableCollection<TDev, PixelTripletsSoA>>::copyAsync(
          queue_, *pixelTripletsDC_));

      if (sync)
        alpaka::wait(queue_);  // host consumers expect filled data
    }
  }
  return pixelTripletsHC_->const_view();
}
template PixelTripletsConst LSTEvent::getPixelTriplets<>(bool);

template <typename TDev>
PixelQuintupletsConst LSTEvent::getPixelQuintuplets(bool sync) {
  if constexpr (std::is_same_v<TDev, DevHost>) {
    return pixelQuintupletsDC_->const_view();
  } else {
    if (!pixelQuintupletsHC_) {
      pixelQuintupletsHC_.emplace(
          cms::alpakatools::CopyToHost<::PortableCollection<TDev, PixelQuintupletsSoA>>::copyAsync(
              queue_, *pixelQuintupletsDC_));

      if (sync)
        alpaka::wait(queue_);  // host consumers expect filled data
    }
  }
  return pixelQuintupletsHC_->const_view();
}
template PixelQuintupletsConst LSTEvent::getPixelQuintuplets<>(bool);

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
