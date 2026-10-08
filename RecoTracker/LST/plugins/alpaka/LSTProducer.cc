// ExternalWork module: acquire() enqueues the event up to LST's first host read of a device count, and each later part
// runs in a TBB task once the queue has reached it, so no thread blocks on the device.
#include <alpaka/alpaka.hpp>

#include <memory>
#include <optional>
#include <string>

#include "RecoTracker/LSTCore/interface/alpaka/LST.h"
#include "RecoTracker/LSTGeometry/interface/Common.h"

#include "FWCore/Concurrency/interface/Async.h"
#include "FWCore/Concurrency/interface/WaitingTaskWithArenaHolder.h"
#include "FWCore/Framework/interface/stream/EDProducer.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include "FWCore/ServiceRegistry/interface/Service.h"
#include "FWCore/Utilities/interface/InputTag.h"

#include "HeterogeneousCore/AlpakaCore/interface/alpaka/EDGetToken.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/EDMetadataAcquireSentry.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/EDMetadataSentry.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/EDPutToken.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/Event.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/EventSetup.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/ProducerBase.h"
#include "HeterogeneousCore/AlpakaInterface/interface/config.h"

#include "RecoTracker/LSTCore/interface/alpaka/TrackCandidatesDeviceCollection.h"

#include "RecoTracker/Record/interface/TrackerRecoGeometryRecord.h"

namespace ALPAKA_ACCELERATOR_NAMESPACE {

  class LSTProducer : public ProducerBase<edm::stream::EDProducer, edm::ExternalWork> {
    using Base = ProducerBase<edm::stream::EDProducer, edm::ExternalWork>;

  public:
    LSTProducer(edm::ParameterSet const& config)
        : Base(config),
          verbose_(config.getParameter<bool>("verbose")),
          ptCut_(config.getParameter<double>("ptCut")),
          ptCutStr_(lst::floatToStr(ptCut_, 1)),
          clustSizeCut_(static_cast<uint16_t>(config.getParameter<uint32_t>("clustSizeCut"))),
          nopLSDupClean_(config.getParameter<bool>("nopLSDupClean")),
          tcpLSTriplets_(config.getParameter<bool>("tcpLSTriplets")),
          reduceMemByFullPrecompute_(config.getParameter<bool>("reduceMemByFullPrecompute")),
          lstInputToken_{consumes(config.getParameter<edm::InputTag>("lstInput"))},
          lstESToken_{esConsumes(edm::ESInputTag("", ptCutStr_))},
          lstOutputToken_{produces()} {}

    // stream::SynchronizingEDProducer::acquire(), except that the framework's holder is reached through the LST parts.
    void acquire(edm::Event const& iEvent,
                 edm::EventSetup const& iSetup,
                 edm::WaitingTaskWithArenaHolder holder) final {
      started_ = false;  // set only when acquire() returns normally; see nextPart()
#ifdef ALPAKA_ACC_CPU_B_SEQ_T_SEQ_ENABLED
      constexpr bool chained = false;  // blocking queue: the parts run back to back here
#else
      bool const chained = not this->synchronize();
      if (chained) {
        async_ = edm::Service<edm::Async>().operator->();  // services are not reachable from the part tasks
        holder = nextPart(std::move(holder));
      }
#endif
      detail::EDMetadataAcquireSentry sentry(iEvent.streamID(), std::move(holder), this->synchronize());
      device::Event const ev(iEvent, sentry.metadata());
      device::EventSetup const es(iSetup, ev.device());
      lst_.emplace();
      bool done = lst_->start(ev.queue(),
                              verbose_,
                              ptCut_,
                              clustSizeCut_,
                              &es.getData(lstESToken_),
                              &ev.get(lstInputToken_),
                              nopLSDupClean_,
                              tcpLSTriplets_,
                              reduceMemByFullPrecompute_,
                              not this->synchronize());
      while (not chained and not done)  // the blocking queue completed the work at every read already
        done = lst_->resume();
      // Everything the next part reads is set before finish(): the part may start as soon as finish() hands it over.
      metadata_ = sentry.metadata();
      started_ = true;
      sentry.finish();  // asynchronous backends: nextPart runs once the queue has reached this point
    }

    void produce(edm::Event& iEvent, edm::EventSetup const& iSetup) final {
      detail::EDMetadataSentry sentry(std::move(metadata_), this->synchronize());
      device::Event ev(iEvent, sentry.metadata());
      auto trackCandidates = lst_->getTrackCandidates();
      ev.emplace(lstOutputToken_, std::move(*trackCandidates));
      lst_.reset();
      this->putBackend(iEvent);
      sentry.finish(ev.wasQueueUsed());
    }

    static void fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
      edm::ParameterSetDescription desc;
      desc.add<edm::InputTag>("lstInput", edm::InputTag{"lstInputProducer"});
      desc.add<bool>("verbose", false);
      desc.add<double>("ptCut", 0.8);
      desc.add<uint32_t>("clustSizeCut", 16);
      desc.add<bool>("nopLSDupClean", false);
      desc.add<bool>("tcpLSTriplets", false);
      desc.add<bool>("reduceMemByFullPrecompute", false)
          ->setComment(
              "If true, run extra counting kernels that exactly size the MD/LS/T3/T5/T4 "
              "buffers, reducing average per-event memory at a small CPU/GPU runtime cost. "
              "If false (default), buffers use cheaper, looser occupancy estimates.");
      descriptions.addWithDefaultLabel(desc);
    }

  private:
#ifndef ALPAKA_ACC_CPU_B_SEQ_T_SEQ_ENABLED
    // A holder whose task runs the next LST part, then waits (without blocking a TBB thread) for the queue again: with
    // the holder of the part after it, or with 'holder' itself after the last part.
    edm::WaitingTaskWithArenaHolder nextPart(edm::WaitingTaskWithArenaHolder holder) {
      auto* group = holder.group();
      return edm::WaitingTaskWithArenaHolder(
          *group,
          edm::make_waiting_task_with_holder(std::move(holder), [this](edm::WaitingTaskWithArenaHolder partHolder) {
            // acquire() threw after wrapping the holder: the framework's own holder carries that exception
            if (not started_)
              return;
            bool const done = lst_->resume();
            auto event = metadata_->recordEvent();
            auto handOver = done ? std::move(partHolder) : nextPart(std::move(partHolder));
            // nothing of this module is touched after the hand-over: the next part may already run
            async_->runAsync(
                std::move(handOver),
                [event = std::move(event)]() mutable { alpaka::wait(*event); },
                []() { return "Enqueued via " EDM_STRINGIZE(ALPAKA_ACCELERATOR_NAMESPACE) "::LSTProducer"; });
          }));
    }
#endif

    const bool verbose_;
    const double ptCut_;
    const std::string ptCutStr_;
    const uint16_t clustSizeCut_;
    const bool nopLSDupClean_;
    const bool tcpLSTriplets_;
    const bool reduceMemByFullPrecompute_;
    const device::EDGetToken<lst::LSTInputDeviceCollection> lstInputToken_;
    const device::ESGetToken<lst::LSTESData<Device>, TrackerRecoGeometryRecord> lstESToken_;
    const device::EDPutToken<lst::TrackCandidatesBaseDeviceCollection> lstOutputToken_;

    // One event in flight per stream, between acquire() and produce().
    std::optional<lst::LST> lst_;
    std::shared_ptr<EDMetadata> metadata_;
    edm::Async* async_ = nullptr;
    bool started_ = false;  // acquire() of the event in flight returned normally
  };

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE

#include "HeterogeneousCore/AlpakaCore/interface/alpaka/MakerMacros.h"
DEFINE_FWK_ALPAKA_MODULE(LSTProducer);
