// HOST converter at the end of the device chain: TrackSoA product (device product copied to the host by the
// framework, or the host product on CPU backends) -> stock MkFitOutputWrapper, so that the STOCK MkFitOutputConverter
// (TrackCandidates) and MkFitOutputTrackConverter (final-fit reco::Tracks) run unchanged downstream.
#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/Framework/interface/global/EDProducer.h"
#include "FWCore/MessageLogger/interface/MessageLogger.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include <string>

#include "FWCore/Utilities/interface/InputTag.h"
#include "RecoTracker/MkFit/interface/MkFitOutputWrapper.h"
#include "RecoTracker/MkFitAlpaka/interface/StatusProduct.h"
#include "RecoTracker/MkFitAlpaka/interface/StatusReport.h"
#include "RecoTracker/MkFitAlpaka/interface/TrackProduct.h"
#include "RecoTracker/MkFitAlpaka/plugins/MkFitAlpakaOutputWrapper.h"

class MkFitAlpakaOutputWrapperFromTrackSoA : public edm::global::EDProducer<> {
public:
  explicit MkFitAlpakaOutputWrapperFromTrackSoA(edm::ParameterSet const& iConfig)
      : tracksToken_{consumes(iConfig.getParameter<edm::InputTag>("tracks"))},
        putToken_{produces()},
        propagatedToFirstLayer_{iConfig.getParameter<bool>("propagatedToFirstLayer")} {
    // review H4: the device chain's per-event status (host copy); LogWarning on any non-zero counter
    const auto status = iConfig.getParameter<edm::InputTag>("status");
    if (!status.label().empty()) {
      statusToken_ = consumes(status);
      statusLabel_ = status.encode();
    }
  }

  static void fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
    edm::ParameterSetDescription desc;
    desc.add("tracks", edm::InputTag{"hltInitialStepTrackSoA"})->setComment("mkfitdev TrackSoA product");
    desc.add("propagatedToFirstLayer", true)
        ->setComment("as stock MkFitProducer: !backwardFitInCMSSW (true in the HLT LST step)");
    desc.add("status", edm::InputTag())
        ->setComment("MkFitStatusHostObject of the device chain (review H4); empty = not checked");
    descriptions.addWithDefaultLabel(desc);
  }

  void produce(edm::StreamID, edm::Event& iEvent, edm::EventSetup const&) const override {
    if (!statusLabel_.empty())
      ::mkfitdev::warnIfNotClean(iEvent.get(statusToken_).value(), statusLabel_);
    const auto& soa = iEvent.get(tracksToken_);
    const auto v = soa.const_view();
    // without a status product: the TrackSoA writers' own overflow counters (the status product carries them as
    // trackOverflow / trackHitsOverflow otherwise)
    if (statusLabel_.empty() && (v.nOverflowTracks() != 0 || v.nOverflowHits() != 0))
      edm::LogWarning("MkFitAlpakaOutputWrapper")
          << "event " << iEvent.id().event() << ": TrackSoA overflow, " << v.nOverflowTracks()
          << " tracks dropped (capacity " << v.metadata().size() << "), " << v.nOverflowHits()
          << " hit lists truncated to " << ::mkfitdev::kMaxTrkHits << " hits";
    // R4-M1: a skipped or seedless event carries a zero-capacity TrackSoA; its scalars are not read
    if (v.metadata().size() == 0) {
      iEvent.emplace(putToken_, mkfit::TrackVec{}, propagatedToFirstLayer_);
      return;
    }
    iEvent.emplace(putToken_, ::mkfitdev::makeMkFitOutputWrapper(v, propagatedToFirstLayer_));
  }

private:
  const edm::EDGetTokenT<::mkfitdev::TrackSoAHostCollection> tracksToken_;
  const edm::EDPutTokenT<MkFitOutputWrapper> putToken_;
  const bool propagatedToFirstLayer_;
  edm::EDGetTokenT<::mkfitdev::MkFitStatusHostObject> statusToken_;
  std::string statusLabel_;
};

DEFINE_FWK_MODULE(MkFitAlpakaOutputWrapperFromTrackSoA);
