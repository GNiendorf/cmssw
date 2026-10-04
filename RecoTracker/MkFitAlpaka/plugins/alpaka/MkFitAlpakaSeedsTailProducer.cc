// Test stage of the device chain tail of the mkFit LST step (round 3, seeds lane): post filter and duplicate cleaner
// in stock order (run_OneIteration: ... -> export_best_comb_cands -> clean_duplicates_sharedhits_pixelseed ->
// export_tracks) on a STOCK mkfit::TrackVec in export order (MkFitOutputWrapper of a stock MkFitProducer run with
// removeDuplicates = False), packed into TrackSoA and processed on the device with clean's CleanAlgo.
// Output: the TrackSoA device product (integ TrackProduct). The host module MkFitAlpakaOutputWrapperFromTrackSoA
// turns it (framework copy to host) into an MkFitOutputWrapper for the STOCK MkFitOutputConverter. In the chain the
// TrackSoA comes from mkfitdev::seeds::runChainTail on the engine's candidate store instead.

#include <algorithm>

#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include "FWCore/Utilities/interface/InputTag.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/global/EDProducer.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/Event.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/EventSetup.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/EDPutToken.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/MakerMacros.h"
#include "HeterogeneousCore/AlpakaInterface/interface/config.h"
#include "RecoTracker/MkFit/interface/MkFitOutputWrapper.h"
#include "RecoTracker/MkFitCore/interface/IterationConfig.h"
#include "RecoTracker/Record/interface/TrackerRecoGeometryRecord.h"

#include "RecoTracker/MkFitAlpaka/interface/alpaka/tracks/TrackSoADeviceCollection.h"
#include "RecoTracker/MkFitAlpaka/interface/tracks/TrackSoAHostCollection.h"
#include "RecoTracker/MkFitAlpaka/interface/tracks/TrackSoAMkFitConversion.h"
#include "RecoTracker/MkFitAlpaka/interface/alpaka/TrackProduct.h"
#include "RecoTracker/MkFitAlpaka/src/alpaka/clean/CleanAlgo.h"

namespace ALPAKA_ACCELERATOR_NAMESPACE {

  class MkFitAlpakaSeedsTailProducer : public global::EDProducer<> {
  public:
    explicit MkFitAlpakaSeedsTailProducer(edm::ParameterSet const& iConfig)
        : EDProducer<>(iConfig),
          tracksToken_{consumes(iConfig.getParameter<edm::InputTag>("tracks"))},
          iterConfigToken_{esConsumes(iConfig.getParameter<edm::ESInputTag>("config"))},
          putToken_{produces()},
          filter_{iConfig.getParameter<bool>("filter")},
          removeDuplicates_{iConfig.getParameter<bool>("removeDuplicates")} {}

    static void fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
      edm::ParameterSetDescription desc;
      desc.add("tracks", edm::InputTag{"hltInitialStepTrackCandidatesMkFitNoDC"})
          ->setComment("stock MkFitOutputWrapper in export order, before the duplicate cleaner");
      desc.add("config", edm::ESInputTag{"", "hltInitialStepTrackCandidatesMkFitConfig"});
      desc.add("filter", true)->setComment("LST-step post filter (qfilter_n_hits_pixseed && nan_n_silly, bkw params)");
      desc.add("removeDuplicates", true)->setComment("phase1:clean_duplicates_sharedhits_pixelseed");
      descriptions.addWithDefaultLabel(desc);
    }

    void produce(edm::StreamID, device::Event& iEvent, device::EventSetup const& iSetup) const override {
      auto& queue = iEvent.queue();
      const auto& in = iEvent.get(tracksToken_);
      const auto& itconf = iSetup.getData(iterConfigToken_);
      const int n = in.tracks().size();
      if (n == 0) {
        mkfitdev::TrackSoADeviceCollection empty(queue, 0);
        iEvent.emplace(putToken_, std::move(empty));
        return;
      }
      ::mkfitdev::TrackSoAHostCollection h(queue, n);
      ::mkfitdev::tracksToSoA(in.tracks(), h.view());
      mkfitdev::TrackSoADeviceCollection a(queue, n);
      mkfitdev::TrackSoADeviceCollection b(queue, n);
      alpaka::memcpy(queue, a.buffer(), h.buffer());
      mkfitdev::clean::CleanAlgo algo(queue, n);
      mkfitdev::TrackSoADeviceCollection* cur = &a;
      mkfitdev::TrackSoADeviceCollection* nxt = &b;
      if (filter_) {
        algo.filterTracks(queue, cur->const_view(), nxt->view(), itconf.m_backward_params.minHitsQF);
        std::swap(cur, nxt);
      }
      if (removeDuplicates_) {
        algo.flagDuplicates(
            queue,
            cur->view(),
            {itconf.dc_fracSharedHits, itconf.dc_drth_central, itconf.dc_drth_obarrel, itconf.dc_drth_forward});
        algo.removeDuplicates(queue, cur->const_view(), nxt->view());
        std::swap(cur, nxt);
      }
      iEvent.emplace(putToken_, std::move(*cur));
    }

  private:
    const edm::EDGetTokenT<MkFitOutputWrapper> tracksToken_;
    const edm::ESGetToken<mkfit::IterationConfig, TrackerRecoGeometryRecord> iterConfigToken_;
    const device::EDPutToken<mkfitdev::TrackSoADeviceCollection> putToken_;
    const bool filter_;
    const bool removeDuplicates_;
  };

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE

DEFINE_FWK_ALPAKA_MODULE(MkFitAlpakaSeedsTailProducer);
