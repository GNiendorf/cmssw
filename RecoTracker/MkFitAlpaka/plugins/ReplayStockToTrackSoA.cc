// MkFitAlpakaReplayStockToTrackSoA (harness lane): a stock MkFitOutputWrapper (e.g. a stockStages snapshot or
// hltInitialStepTrackCandidatesMkFit) as an mkfitdev::TrackSoAHostCollection (tracksToSoA, capacity = number of tracks).
// Uses: stock reference in the port's own product type; self-test of MkFitAlpakaTrackSoACompare (must be identical).
#include <algorithm>

#include "FWCore/Framework/interface/global/EDProducer.h"
#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include "RecoTracker/MkFit/interface/MkFitOutputWrapper.h"
#include "RecoTracker/MkFitAlpaka/interface/TrackProduct.h"
#include "RecoTracker/MkFitAlpaka/interface/tracks/TrackSoAMkFitConversion.h"

class MkFitAlpakaReplayStockToTrackSoA : public edm::global::EDProducer<> {
public:
  explicit MkFitAlpakaReplayStockToTrackSoA(edm::ParameterSet const& cfg)
      : srcToken_(consumes(cfg.getParameter<edm::InputTag>("src"))), putToken_(produces()) {}

  static void fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
    edm::ParameterSetDescription desc;
    desc.add<edm::InputTag>("src", edm::InputTag("hltInitialStepTrackCandidatesMkFit"));
    descriptions.addWithDefaultLabel(desc);
  }

  void produce(edm::StreamID, edm::Event& ev, edm::EventSetup const&) const override {
    auto const& tv = ev.get(srcToken_).tracks();
    mkfitdev::TrackSoAHostCollection out(std::max<int>(1, int(tv.size())));
    mkfitdev::tracksToSoA(tv, out.view());
    ev.emplace(putToken_, std::move(out));
  }

private:
  const edm::EDGetTokenT<MkFitOutputWrapper> srcToken_;
  const edm::EDPutTokenT<mkfitdev::TrackSoAHostCollection> putToken_;
};

DEFINE_FWK_MODULE(MkFitAlpakaReplayStockToTrackSoA);
