#ifndef RecoTracker_LSTCore_interface_alpaka_LST_h
#define RecoTracker_LSTCore_interface_alpaka_LST_h

#include "RecoTracker/LSTCore/interface/alpaka/Common.h"
#include "RecoTracker/LSTCore/interface/LSTESData.h"
#include "RecoTracker/LSTCore/interface/LSTTask.h"
#include "RecoTracker/LSTCore/interface/alpaka/LSTInputDeviceCollection.h"
#include "RecoTracker/LSTCore/interface/alpaka/TrackCandidatesDeviceCollection.h"

#include <cstdlib>
#include <numeric>
#include <alpaka/alpaka.hpp>

namespace ALPAKA_ACCELERATOR_NAMESPACE::lst {
  class LSTEvent;

  class LST {
  public:
    LST();
    ~LST();

    // Synchronous: the whole event, blocking on the queue at each host read of a device count.
    void run(Queue& queue,
             bool verbose,
             const float ptCut,
             const uint16_t clustSizeCut,
             LSTESData<Device> const* deviceESData,
             LSTInputDeviceCollection const* lstInputDC,
             bool no_pls_dupclean,
             bool tc_pls_triplets,
             bool reduce_mem_by_full_precompute);
    // Same work in the same order, without blocking: start() enqueues up to the first host read of a device count and
    // resume() continues once the queue has reached it. Both return true when the whole event is enqueued.
    bool start(Queue& queue,
               bool verbose,
               const float ptCut,
               const uint16_t clustSizeCut,
               LSTESData<Device> const* deviceESData,
               LSTInputDeviceCollection const* lstInputDC,
               bool no_pls_dupclean,
               bool tc_pls_triplets,
               bool reduce_mem_by_full_precompute,
               bool async);
    bool resume();

    std::unique_ptr<TrackCandidatesBaseDeviceCollection> getTrackCandidates() {
      return std::move(trackCandidatesBaseDC_);
    }

  private:
    bool finishIfDone();
    void abandon();  // after an exception: drop the event so that resume() is a no-op

    std::unique_ptr<LSTEvent> event_;
    ::lst::LSTTask stages_;
    // Output collection
    std::unique_ptr<TrackCandidatesBaseDeviceCollection> trackCandidatesBaseDC_;
  };

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE::lst

#endif
