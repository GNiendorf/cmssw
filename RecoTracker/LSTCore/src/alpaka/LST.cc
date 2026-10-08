#include "RecoTracker/LSTCore/interface/alpaka/LST.h"

#include "LSTEvent.h"

#include <format>

using namespace ALPAKA_ACCELERATOR_NAMESPACE::lst;

#include "Math/Vector3D.h"
#include "Math/VectorUtil.h"
using XYZVector = ROOT::Math::XYZVector;

namespace {
  // The LST stages of one event in order. Each co_await of a stage continues at once in the synchronous mode, or after
  // the stage's host reads of device counts in the asynchronous mode (LSTEvent::QueueSync).
  LSTTask runStages(LSTEvent& event,
                    ALPAKA_ACCELERATOR_NAMESPACE::Queue& queue,
                    bool verbose,
                    LSTInputDeviceCollection const* lstInputDC,
                    bool no_pls_dupclean,
                    bool tc_pls_triplets) {
    event.addInputToEvent(lstInputDC);
    event.addHitToEvent();
    event.addPixelSegmentToEventStart();
    co_await event.createMiniDoublets();
    if (verbose) {
      alpaka::wait(queue);  // event calls are asynchronous: wait before printing
      printf("# of Mini-doublets produced: %d\n", event.getNumberOfMiniDoublets());
      printf("# of Mini-doublets produced barrel layer 1: %d\n", event.getNumberOfMiniDoubletsByLayerBarrel(0));
      printf("# of Mini-doublets produced barrel layer 2: %d\n", event.getNumberOfMiniDoubletsByLayerBarrel(1));
      printf("# of Mini-doublets produced barrel layer 3: %d\n", event.getNumberOfMiniDoubletsByLayerBarrel(2));
      printf("# of Mini-doublets produced barrel layer 4: %d\n", event.getNumberOfMiniDoubletsByLayerBarrel(3));
      printf("# of Mini-doublets produced barrel layer 5: %d\n", event.getNumberOfMiniDoubletsByLayerBarrel(4));
      printf("# of Mini-doublets produced barrel layer 6: %d\n", event.getNumberOfMiniDoubletsByLayerBarrel(5));
      printf("# of Mini-doublets produced endcap layer 1: %d\n", event.getNumberOfMiniDoubletsByLayerEndcap(0));
      printf("# of Mini-doublets produced endcap layer 2: %d\n", event.getNumberOfMiniDoubletsByLayerEndcap(1));
      printf("# of Mini-doublets produced endcap layer 3: %d\n", event.getNumberOfMiniDoubletsByLayerEndcap(2));
      printf("# of Mini-doublets produced endcap layer 4: %d\n", event.getNumberOfMiniDoubletsByLayerEndcap(3));
      printf("# of Mini-doublets produced endcap layer 5: %d\n", event.getNumberOfMiniDoubletsByLayerEndcap(4));
    }

    co_await event.createSegmentsWithModuleMap();
    if (verbose) {
      alpaka::wait(queue);  // event calls are asynchronous: wait before printing
      printf("# of Segments produced: %d\n", event.getNumberOfSegments());
      printf("# of Segments produced layer 1-2:  %d\n", event.getNumberOfSegmentsByLayerBarrel(0));
      printf("# of Segments produced layer 2-3:  %d\n", event.getNumberOfSegmentsByLayerBarrel(1));
      printf("# of Segments produced layer 3-4:  %d\n", event.getNumberOfSegmentsByLayerBarrel(2));
      printf("# of Segments produced layer 4-5:  %d\n", event.getNumberOfSegmentsByLayerBarrel(3));
      printf("# of Segments produced layer 5-6:  %d\n", event.getNumberOfSegmentsByLayerBarrel(4));
      printf("# of Segments produced endcap layer 1:  %d\n", event.getNumberOfSegmentsByLayerEndcap(0));
      printf("# of Segments produced endcap layer 2:  %d\n", event.getNumberOfSegmentsByLayerEndcap(1));
      printf("# of Segments produced endcap layer 3:  %d\n", event.getNumberOfSegmentsByLayerEndcap(2));
      printf("# of Segments produced endcap layer 4:  %d\n", event.getNumberOfSegmentsByLayerEndcap(3));
      printf("# of Segments produced endcap layer 5:  %d\n", event.getNumberOfSegmentsByLayerEndcap(4));
    }

    co_await event.createTriplets();
    if (verbose) {
      alpaka::wait(queue);  // event calls are asynchronous: wait before printing
      printf("# of T3s produced: %d\n", event.getNumberOfTriplets());
      printf("# of T3s produced layer 1-2-3: %d\n", event.getNumberOfTripletsByLayerBarrel(0));
      printf("# of T3s produced layer 2-3-4: %d\n", event.getNumberOfTripletsByLayerBarrel(1));
      printf("# of T3s produced layer 3-4-5: %d\n", event.getNumberOfTripletsByLayerBarrel(2));
      printf("# of T3s produced layer 4-5-6: %d\n", event.getNumberOfTripletsByLayerBarrel(3));
      printf("# of T3s produced endcap layer 1-2-3: %d\n", event.getNumberOfTripletsByLayerEndcap(0));
      printf("# of T3s produced endcap layer 2-3-4: %d\n", event.getNumberOfTripletsByLayerEndcap(1));
      printf("# of T3s produced endcap layer 3-4-5: %d\n", event.getNumberOfTripletsByLayerEndcap(2));
      printf("# of T3s produced endcap layer 1: %d\n", event.getNumberOfTripletsByLayerEndcap(0));
      printf("# of T3s produced endcap layer 2: %d\n", event.getNumberOfTripletsByLayerEndcap(1));
      printf("# of T3s produced endcap layer 3: %d\n", event.getNumberOfTripletsByLayerEndcap(2));
      printf("# of T3s produced endcap layer 4: %d\n", event.getNumberOfTripletsByLayerEndcap(3));
      printf("# of T3s produced endcap layer 5: %d\n", event.getNumberOfTripletsByLayerEndcap(4));
    }

    co_await event.createQuintuplets();
    if (verbose) {
      alpaka::wait(queue);  // event calls are asynchronous: wait before printing
      printf("# of Quintuplets produced: %d\n", event.getNumberOfQuintuplets());
      printf("# of Quintuplets produced layer 1-2-3-4-5-6: %d\n", event.getNumberOfQuintupletsByLayerBarrel(0));
      printf("# of Quintuplets produced layer 2: %d\n", event.getNumberOfQuintupletsByLayerBarrel(1));
      printf("# of Quintuplets produced layer 3: %d\n", event.getNumberOfQuintupletsByLayerBarrel(2));
      printf("# of Quintuplets produced layer 4: %d\n", event.getNumberOfQuintupletsByLayerBarrel(3));
      printf("# of Quintuplets produced layer 5: %d\n", event.getNumberOfQuintupletsByLayerBarrel(4));
      printf("# of Quintuplets produced layer 6: %d\n", event.getNumberOfQuintupletsByLayerBarrel(5));
      printf("# of Quintuplets produced endcap layer 1: %d\n", event.getNumberOfQuintupletsByLayerEndcap(0));
      printf("# of Quintuplets produced endcap layer 2: %d\n", event.getNumberOfQuintupletsByLayerEndcap(1));
      printf("# of Quintuplets produced endcap layer 3: %d\n", event.getNumberOfQuintupletsByLayerEndcap(2));
      printf("# of Quintuplets produced endcap layer 4: %d\n", event.getNumberOfQuintupletsByLayerEndcap(3));
      printf("# of Quintuplets produced endcap layer 5: %d\n", event.getNumberOfQuintupletsByLayerEndcap(4));
    }

    event.addPixelSegmentToEventFinalize();

    event.pixelLineSegmentCleaning(no_pls_dupclean);

    co_await event.createPixelQuintuplets();
    if (verbose) {
      alpaka::wait(queue);  // event calls are asynchronous: wait before printing
      printf("# of Pixel Quintuplets produced: %d\n", event.getNumberOfPixelQuintuplets());
    }

    co_await event.createPixelTriplets();
    if (verbose) {
      alpaka::wait(queue);  // event calls are asynchronous: wait before printing
      printf("# of Pixel T3s produced: %d\n", event.getNumberOfPixelTriplets());
    }

    co_await event.createQuadruplets();
    if (verbose) {
      alpaka::wait(queue);  // event calls are asynchronous: wait before printing
      printf("# of Quadruplets produced: %d\n", event.getNumberOfQuadruplets());
      printf("# of Quadruplets produced layer 1-2-3-4: %d\n", event.getNumberOfQuadrupletsByLayerBarrel(0));
      printf("# of Quadruplets produced layer 2: %d\n", event.getNumberOfQuadrupletsByLayerBarrel(1));
      printf("# of Quadruplets produced layer 3: %d\n", event.getNumberOfQuadrupletsByLayerBarrel(2));
      printf("# of Quadruplets produced layer 4: %d\n", event.getNumberOfQuadrupletsByLayerBarrel(3));
      printf("# of Quadruplets produced layer 5: %d\n", event.getNumberOfQuadrupletsByLayerBarrel(4));
      printf("# of Quadruplets produced layer 6: %d\n", event.getNumberOfQuadrupletsByLayerBarrel(5));
      printf("# of Quadruplets produced endcap layer 1: %d\n", event.getNumberOfQuadrupletsByLayerEndcap(0));
      printf("# of Quadruplets produced endcap layer 2: %d\n", event.getNumberOfQuadrupletsByLayerEndcap(1));
      printf("# of Quadruplets produced endcap layer 3: %d\n", event.getNumberOfQuadrupletsByLayerEndcap(2));
      printf("# of Quadruplets produced endcap layer 4: %d\n", event.getNumberOfQuadrupletsByLayerEndcap(3));
      printf("# of Quadruplets produced endcap layer 5: %d\n", event.getNumberOfQuadrupletsByLayerEndcap(4));
    }

    co_await event.createTrackCandidates(no_pls_dupclean, tc_pls_triplets);
    if (verbose) {
      alpaka::wait(queue);  // event calls are asynchronous: wait before printing
      printf("# of TrackCandidates produced: %d\n", event.getNumberOfTrackCandidates());
      printf("        # of Pixel TrackCandidates produced: %d\n", event.getNumberOfPixelTrackCandidates());
      printf("        # of pT5 TrackCandidates produced: %d\n", event.getNumberOfPT5TrackCandidates());
      printf("        # of pT3 TrackCandidates produced: %d\n", event.getNumberOfPT3TrackCandidates());
      printf("        # of pLS TrackCandidates produced: %d\n", event.getNumberOfPLSTrackCandidates());
      printf("        # of T5 TrackCandidates produced: %d\n", event.getNumberOfT5TrackCandidates());
      printf("        # of T4 TrackCandidates produced: %d\n", event.getNumberOfT4TrackCandidates());
      lstWarning(std::format("[MEM] Total: {:.1f} MB", event.getMemoryAllocatedMB()));
    }
  }
}  // namespace

LST::LST() = default;
LST::~LST() = default;

void LST::run(Queue& queue,
              bool verbose,
              float const ptCut,
              uint16_t const clustSizeCut,
              LSTESData<Device> const* deviceESData,
              LSTInputDeviceCollection const* lstInputDC,
              bool no_pls_dupclean,
              bool tc_pls_triplets,
              bool reduce_mem_by_full_precompute) {
  start(queue,
        verbose,
        ptCut,
        clustSizeCut,
        deviceESData,
        lstInputDC,
        no_pls_dupclean,
        tc_pls_triplets,
        reduce_mem_by_full_precompute,
        false);
}

bool LST::start(Queue& queue,
                bool verbose,
                float const ptCut,
                uint16_t const clustSizeCut,
                LSTESData<Device> const* deviceESData,
                LSTInputDeviceCollection const* lstInputDC,
                bool no_pls_dupclean,
                bool tc_pls_triplets,
                bool reduce_mem_by_full_precompute,
                bool async) {
  try {
    event_ =
        std::make_unique<LSTEvent>(verbose, ptCut, clustSizeCut, queue, deviceESData, reduce_mem_by_full_precompute);
    event_->setAsyncSync(async && !verbose);  // the verbose printouts read the device on the host
    stages_ = runStages(*event_, queue, verbose, lstInputDC, no_pls_dupclean, tc_pls_triplets);
  } catch (...) {
    abandon();
    throw;
  }
  return finishIfDone();
}

bool LST::resume() {
  if (!event_ || !stages_.valid())
    return true;  // finished already, or abandoned after an exception
  try {
    event_->resumeSuspended();
  } catch (...) {
    abandon();
    throw;
  }
  return finishIfDone();
}

void LST::abandon() {
  stages_.reset();  // the stage frames reference the event: destroy them first
  event_.reset();
}

bool LST::finishIfDone() {
  if (!stages_.done())
    return false;
  stages_.reset();
  trackCandidatesBaseDC_ = event_->releaseTrackCandidatesBaseDeviceCollection();
  event_.reset();
  return true;
}
