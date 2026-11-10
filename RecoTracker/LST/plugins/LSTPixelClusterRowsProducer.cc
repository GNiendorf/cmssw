#include <cstdint>
#include <vector>

#include "DataFormats/TrackerRecHit2D/interface/SiPixelRecHitCollection.h"
#include "FWCore/Framework/interface/global/EDProducer.h"
#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include "Geometry/CommonTopologies/interface/GeomDet.h"
#include "RecoTracker/LSTCore/interface/OTPull.h"

// Pixel cluster key (the index LSTInputProducer stores for a pixel hit) -> row of the hit in the pixel rechit SoA that
// the CA indexes, for the pixel stubs of LSTProducer's OT-seeded lookup (otpullPixelSrcRows).
class LSTPixelClusterRowsProducer : public edm::global::EDProducer<> {
public:
  explicit LSTPixelClusterRowsProducer(edm::ParameterSet const& iConfig)
      : recHitsToken_(consumes(iConfig.getParameter<edm::InputTag>("pixelRecHits"))),
        hmsToken_(consumes(iConfig.getParameter<edm::InputTag>("pixelRecHits"))),
        putToken_(produces()) {}

  static void fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
    edm::ParameterSetDescription desc;
    desc.add<edm::InputTag>("pixelRecHits", edm::InputTag("hltSiPixelRecHits"))
        ->setComment("legacy pixel rechits made from the SoA, with their hit-module-start product");
    descriptions.addWithDefaultLabel(desc);
  }

  void produce(edm::StreamID, edm::Event& iEvent, edm::EventSetup const&) const override {
    auto const& recHits = iEvent.get(recHitsToken_);
    auto const& moduleStart = iEvent.get(hmsToken_);
    std::vector<uint32_t> rows;
    for (auto const& hit : recHits.data()) {
      auto const& clus = hit.firstClusterRef();
      uint32_t const key = clus.index();
      if (key >= rows.size())
        rows.resize(key + 1, lst::kOTPullNoRow);
      // the SoA row of a hit: hit offset of its module + its index in the module
      rows[key] = moduleStart[hit.det()->index()] + clus.pixelCluster().originalId();
    }
    iEvent.emplace(putToken_, std::move(rows));
  }

private:
  const edm::EDGetTokenT<SiPixelRecHitCollectionNew> recHitsToken_;
  const edm::EDGetTokenT<std::vector<uint32_t>> hmsToken_;
  const edm::EDPutTokenT<std::vector<uint32_t>> putToken_;
};

DEFINE_FWK_MODULE(LSTPixelClusterRowsProducer);
