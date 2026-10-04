// MkFitAlpakaHpFeaturesFromTrackSoA (round 7, lane hpsel; doc/hpsel.txt): stage C step 2 PROTOTYPE - the HP Torch
// classifier's 15 features computed on the device from the fitted TrackSoA (MkFitAlpakaFitDeviceProducer output),
// see MkFitAlpakaHpFeaturesKernels.h for which columns are exact, approximate (helix prototype) or missing (NaN).
// VALIDATION ONLY: the row count is read back (one sync per event) to size the output; rows = TrackSoA rows, which
// equal the reco::Track rows of hltInitialStepTracks only when the host converter drops no candidate (its quality
// checks); MkFitAlpakaHpCompare compares such events column by column.

#include <alpaka/alpaka.hpp>

#include "DataFormats/BeamSpot/interface/BeamSpot.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include "FWCore/Utilities/interface/ESInputTag.h"
#include "FWCore/Utilities/interface/InputTag.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/EDGetToken.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/EDPutToken.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/ESGetToken.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/Event.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/EventSetup.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/MakerMacros.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/global/EDProducer.h"
#include "HeterogeneousCore/AlpakaInterface/interface/config.h"
#include "HeterogeneousCore/AlpakaInterface/interface/memory.h"
#include "RecoTracker/FinalTrackSelectors/interface/alpaka/TrackFeaturesDeviceCollection.h"
#include "RecoTracker/MkFitAlpaka/interface/alpaka/tracks/TrackSoADeviceCollection.h"
#include "RecoTracker/MkFitAlpaka/interface/es/ESData.h"
#include "RecoTracker/Record/interface/TrackerRecoGeometryRecord.h"

#include "MkFitAlpakaHpFeaturesKernels.h"

namespace ALPAKA_ACCELERATOR_NAMESPACE {

  class MkFitAlpakaHpFeaturesFromTrackSoA : public global::EDProducer<> {
  public:
    explicit MkFitAlpakaHpFeaturesFromTrackSoA(edm::ParameterSet const& iConfig)
        : EDProducer<>(iConfig),
          tracksToken_{consumes(iConfig.getParameter<edm::InputTag>("tracks"))},
          bsToken_{consumes(iConfig.getParameter<edm::InputTag>("beamSpot"))},
          esToken_{esConsumes(iConfig.getParameter<edm::ESInputTag>("esData"))},
          putToken_{produces()} {}

    static void fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
      edm::ParameterSetDescription desc;
      desc.add<edm::InputTag>("tracks", edm::InputTag("hltInitialStepTrackCandidatesMkFitFitDevice"));
      desc.add<edm::InputTag>("beamSpot", edm::InputTag("hltOnlineBeamSpot"));
      desc.add<edm::ESInputTag>("esData", edm::ESInputTag("", "hltMkFitAlpakaES"));
      descriptions.addWithDefaultLabel(desc);
    }

    void produce(edm::StreamID, device::Event& iEvent, device::EventSetup const& iSetup) const override {
      auto& queue = iEvent.queue();
      auto const& trk = iEvent.get(tracksToken_);
      auto const& bs = iEvent.get(bsToken_);
      int n = 0;
      if (trk.const_view().metadata().size() > 0) {
        auto hn = cms::alpakatools::make_host_buffer<int32_t[]>(queue, 1);  // 1-dim, as the device view below
        // the const view returns scalars by value: take the device address from the metadata
        alpaka::memcpy(
            queue,
            hn,
            cms::alpakatools::make_device_view(alpaka::getDev(queue), trk.const_view().metadata().addressOf_nTracks(), 1));
        alpaka::wait(queue);  // validation module: the output is sized by the device row count
        n = *hn.data();
      }
      TrackFeaturesDeviceCollection out(queue, n);
      if (n > 0) {
        auto const& es = iSetup.getData(esToken_);
        auto const esv = es.view();
        ::mkfitdev::hpsel::Params p;
        p.bsx = bs.position().x();
        p.bsy = bs.position().y();
        p.bsz = bs.position().z();
        p.bField = esv.material.bField;
        mkfitdev::hpsel::launchFeatures(queue, trk.const_view(), n, esv.layers, p, out.view());
      }
      iEvent.emplace(putToken_, std::move(out));
    }

  private:
    const device::EDGetToken<mkfitdev::TrackSoADeviceCollection> tracksToken_;
    const edm::EDGetTokenT<reco::BeamSpot> bsToken_;
    const device::ESGetToken<::mkfitdev::ESData<Device>, TrackerRecoGeometryRecord> esToken_;
    const device::EDPutToken<TrackFeaturesDeviceCollection> putToken_;
  };

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE

DEFINE_FWK_ALPAKA_MODULE(MkFitAlpakaHpFeaturesFromTrackSoA);
