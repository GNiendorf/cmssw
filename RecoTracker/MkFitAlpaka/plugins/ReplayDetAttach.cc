// Replay helper (harness lane): seeds read from a file carry rechits without GeomDet pointer (TrackingRecHit::det() is
// transient) and MkFitSeedConverter dereferences it. This producer copies the persisted seeds unchanged (same order,
// same starting state, same hits and cluster refs) and re-attaches the det from the TrackerGeometry.
// (Rechit positions/errors, BaseTrackerRecHit pos_/err_, are transient too: rechits are re-made by the menu's producers.)

#include <memory>

#include "DataFormats/Common/interface/OwnVector.h"
#include "DataFormats/TrajectorySeed/interface/TrajectorySeed.h"
#include "DataFormats/TrajectorySeed/interface/TrajectorySeedCollection.h"
#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/EventSetup.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/Framework/interface/global/EDProducer.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include "FWCore/Utilities/interface/Exception.h"
#include "Geometry/Records/interface/TrackerDigiGeometryRecord.h"
#include "Geometry/TrackerGeometryBuilder/interface/TrackerGeometry.h"

namespace mkfitdev_harness {

  inline GeomDet const& detOf(TrackerGeometry const& geom, DetId id) {
    auto const* det = geom.idToDet(id);
    if (det == nullptr)
      throw cms::Exception("LogicError") << "replay: no GeomDet for detId " << id.rawId();
    return *det;
  }

  class ReplaySeedsWithDet : public edm::global::EDProducer<> {
  public:
    explicit ReplaySeedsWithDet(edm::ParameterSet const& cfg)
        : srcToken_(consumes<TrajectorySeedCollection>(cfg.getParameter<edm::InputTag>("src"))),
          geomToken_(esConsumes<TrackerGeometry, TrackerDigiGeometryRecord>()),
          putToken_(produces<TrajectorySeedCollection>()) {}

    static void fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
      edm::ParameterSetDescription desc;
      desc.add<edm::InputTag>("src", edm::InputTag("hltInitialStepTrajectorySeedsLST"));
      descriptions.addWithDefaultLabel(desc);
    }

    void produce(edm::StreamID, edm::Event& ev, edm::EventSetup const& es) const override {
      auto const& geom = es.getData(geomToken_);
      auto const& in = ev.get(srcToken_);
      TrajectorySeedCollection out;
      out.reserve(in.size());
      for (auto const& s : in) {
        TrajectorySeed::RecHitContainer hits;
        hits.reserve(s.nHits());
        for (auto const& h : s.recHits()) {
          TrackingRecHit* c = h.clone();
          if (c->geographicalId().rawId() != 0)
            c->setDet(detOf(geom, c->geographicalId()));
          hits.push_back(c);
        }
        out.emplace_back(s.startingState(), std::move(hits), s.direction());
      }
      ev.emplace(putToken_, std::move(out));
    }

  private:
    edm::EDGetTokenT<TrajectorySeedCollection> srcToken_;
    edm::ESGetToken<TrackerGeometry, TrackerDigiGeometryRecord> geomToken_;
    edm::EDPutTokenT<TrajectorySeedCollection> putToken_;
  };

}  // namespace mkfitdev_harness

using MkFitAlpakaReplaySeeds = mkfitdev_harness::ReplaySeedsWithDet;
DEFINE_FWK_MODULE(MkFitAlpakaReplaySeeds);
