// Index-only replacement of the stock mkFit hit converters (MkFitSiPixelHitConverter, MkFitPhase2HitConverter) for the
// host consumers that need only the cluster index -> rechit map: the stock MkFitOutputConverter reads
// MkFitClusterIndexToHit (mkFitPixelHits / mkFitStripHits) and nothing else of the converter products, and the device
// build module reads its size (the strip row base nPixel). With the device hit input of the device EventOfHits, the
// HitVec (global position, error transform, packed data) is no longer needed on the host.
// The map is filled exactly as stock mkfit::convertHits (RecoTracker/MkFit/plugins/convertHits.h): size
// max(last hit's cluster index + 1, dataSize()), nullptr for indices without a hit, every hit at its cluster index,
// the same "Refs to many cluster collections" check. Neither stock converter applies a charge cut in Phase 2
// (applyCCC() is false for both traits), so every hit is entered.
// sizeFromClusters (stage D, round 8, lane otdev; Phase-2 OT only): no legacy rechits read; the map has one nullptr entry
// per OT cluster (the size is what the build/fit modules read). Only for menus whose remaining reader of the entries
// (the output converter) makes the OT hits on demand (otClustersOnDemand); a dereference would crash, so the
// customise sets both together.
// compareTo (validation): the stock converter's MkFitClusterIndexToHit of the same event; the two maps must be equal
// element by element (same pointers) and of the same size; one line per event, throws on a difference.
#include <algorithm>
#include <cstddef>
#include <vector>

#include "DataFormats/Provenance/interface/ModuleDescription.h"

#include "DataFormats/TrackerRecHit2D/interface/Phase2TrackerRecHit1D.h"
#include "DataFormats/TrackerRecHit2D/interface/SiPixelRecHitCollection.h"
#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/Framework/interface/global/EDProducer.h"
#include "FWCore/MessageLogger/interface/MessageLogger.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include "FWCore/Utilities/interface/Exception.h"
#include "FWCore/Utilities/interface/Likely.h"
#include "RecoTracker/MkFit/interface/MkFitClusterIndexToHit.h"

namespace {
  template <typename HitCollection>
  class MkFitAlpakaClusterIndexToHitT : public edm::global::EDProducer<> {
  public:
    explicit MkFitAlpakaClusterIndexToHitT(edm::ParameterSet const& iConfig)
        : sizeOnly_{!iConfig.getParameter<edm::InputTag>("sizeFromClusters").label().empty()},
          hitsToken_{sizeOnly_ ? edm::EDGetTokenT<HitCollection>{} : consumes<HitCollection>(iConfig.getParameter<edm::InputTag>("hits"))},
          putToken_{produces()},
          compare_{!iConfig.getParameter<edm::InputTag>("compareTo").label().empty()} {
      if (sizeOnly_)
        clustersToken_ = consumes(iConfig.getParameter<edm::InputTag>("sizeFromClusters"));
      if (compare_)
        stockToken_ = consumes(iConfig.getParameter<edm::InputTag>("compareTo"));
    }

    static void fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
      edm::ParameterSetDescription desc;
      desc.add("hits", edm::InputTag{""})->setComment("legacy rechits (the stock converter's 'hits')");
      desc.add("sizeFromClusters", edm::InputTag{""})
          ->setComment("Phase-2 OT only: size-only map (nullptr entries) from these clusters; no rechits read");
      desc.add("compareTo", edm::InputTag{""})
          ->setComment("validation: stock converter's MkFitClusterIndexToHit (empty = no check)");
      descriptions.addWithDefaultLabel(desc);
    }

    void produce(edm::StreamID, edm::Event& iEvent, edm::EventSetup const&) const override {
      MkFitClusterIndexToHit out;
      if (sizeOnly_)
        out.hits().resize(iEvent.get(clustersToken_).dataSize(), nullptr);
      else
        fill(iEvent.get(hitsToken_), out.hits());
      if (compare_)
        compare(iEvent, out.hits(), iEvent.get(stockToken_).hits());
      iEvent.emplace(putToken_, std::move(out));
    }

  private:
    static void fill(HitCollection const& hits, std::vector<TrackingRecHit const*>& map) {
      if (hits.empty())
        return;
      const auto& lastClusterRef = hits.data().back().firstClusterRef();
      const auto clusterID = lastClusterRef.id();
      map.resize(std::max(static_cast<std::size_t>(lastClusterRef.index() + 1), static_cast<std::size_t>(hits.dataSize())),
               nullptr);
      for (const auto& detset : hits) {
        for (const auto& hit : detset) {
          const auto clusterRef = hit.firstClusterRef();
          if UNLIKELY (clusterRef.id() != clusterID)
            throw cms::Exception("LogicError")
                << "Input hit collection has Refs to many cluster collections. Last hit had Ref to product "
                << clusterID << ", but encountered Ref to product " << clusterRef.id() << " on detid "
                << detset.detId();
          const auto clusterIndex = clusterRef.index();
          if UNLIKELY (clusterIndex >= map.size())
            map.resize(clusterIndex + 1, nullptr);
          map[clusterIndex] = &hit;
        }
      }
    }

    void compare(edm::Event const& iEvent,
                 std::vector<TrackingRecHit const*> const& mine,
                 std::vector<TrackingRecHit const*> const& stock) const {
      std::size_t differ = 0, nonNull = 0;
      const std::size_t n = std::min(mine.size(), stock.size());
      for (std::size_t i = 0; i < n; ++i) {
        differ += mine[i] != stock[i];
        nonNull += mine[i] != nullptr;
      }
      edm::LogPrint("MkFitAlpakaClusterIndexToHit")
          << "CLUSTER_INDEX_COMPARE " << moduleDescription().moduleLabel() << " event " << iEvent.id().event()
          << " size " << mine.size() << " stockSize " << stock.size() << " hits " << nonNull << " differ " << differ;
      if (differ != 0 || mine.size() != stock.size())
        throw cms::Exception("MkFitAlpakaClusterIndexToHit")
            << "index-only map differs from the stock converter's in event " << iEvent.id().event();
    }

    const bool sizeOnly_;
    const edm::EDGetTokenT<HitCollection> hitsToken_;
    edm::EDGetTokenT<Phase2TrackerCluster1DCollectionNew> clustersToken_;
    const edm::EDPutTokenT<MkFitClusterIndexToHit> putToken_;
    const bool compare_;
    edm::EDGetTokenT<MkFitClusterIndexToHit> stockToken_;
  };
}  // namespace

using MkFitAlpakaPixelClusterIndexToHit = MkFitAlpakaClusterIndexToHitT<SiPixelRecHitCollection>;
using MkFitAlpakaPhase2ClusterIndexToHit = MkFitAlpakaClusterIndexToHitT<Phase2TrackerRecHit1DCollectionNew>;
DEFINE_FWK_MODULE(MkFitAlpakaPixelClusterIndexToHit);
DEFINE_FWK_MODULE(MkFitAlpakaPhase2ClusterIndexToHit);
