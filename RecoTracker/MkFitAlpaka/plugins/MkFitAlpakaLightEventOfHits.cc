// D4-H6 (io lane): a hit-less host MkFitEventOfHits for the STOCK output converters of the device menu.
// MkFitOutputConverter (TrackCandidates) and MkFitOutputTrackConverter (final-fit reco::Tracks) read only
// eventOfHits[layer].is_pixel() (MkFit/plugins/MkFitOutputConverter.cc:406, MkFitOutputTrackConverter.cc:424), which
// LayerOfHits takes from the TrackerInfo layer. So the device menu does not need the full host EventOfHits
// (stock hltMkFitEventOfHits: hit loading + binning, ~13 ms/event): this module constructs the layer structure only
// (mkfit::EventOfHits(TrackerInfo), no hits, no dead modules, no beam spot).
// DO NOT feed it to stock MkFitProducer / MkFitFitProducer: they need the hits.
#include <memory>

#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/EventSetup.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/Framework/interface/global/EDProducer.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include "RecoTracker/MkFit/interface/MkFitEventOfHits.h"
#include "RecoTracker/MkFit/interface/MkFitGeometry.h"
#include "RecoTracker/MkFitCore/interface/HitStructures.h"
#include "RecoTracker/Record/interface/TrackerRecoGeometryRecord.h"

class MkFitAlpakaLightEventOfHits : public edm::global::EDProducer<> {
public:
  explicit MkFitAlpakaLightEventOfHits(edm::ParameterSet const&) : geomToken_{esConsumes()}, putToken_{produces()} {}

  static void fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
    edm::ParameterSetDescription desc;
    descriptions.addWithDefaultLabel(desc);
  }

  void produce(edm::StreamID, edm::Event& iEvent, edm::EventSetup const& iSetup) const override {
    // the layers point into the ES TrackerInfo, exactly as the stock MkFitEventOfHitsProducer's
    iEvent.emplace(putToken_, std::make_unique<mkfit::EventOfHits>(iSetup.getData(geomToken_).trackerInfo()));
  }

private:
  const edm::ESGetToken<MkFitGeometry, TrackerRecoGeometryRecord> geomToken_;
  const edm::EDPutTokenT<MkFitEventOfHits> putToken_;
};

DEFINE_FWK_MODULE(MkFitAlpakaLightEventOfHits);
