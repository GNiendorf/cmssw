// CompactClusterTruthProducer (training-sample tool, not part of the HLT menu): writes the cluster -> TrackingParticle
// association of the pixel and outer-tracker clusters as flat vectors, so a cached local-reco file can carry truth for
// training labels without the full TrackingParticle / DigiSimLink collections.
//
// Clusters are keyed by position, not by index, so a re-run of the clusterizer in another job finds them again:
//   pixel: detId and (minPixelRow << 16 | minPixelCol)
//   outer tracker: detId and (column << 16 | firstStrip)
// One row per (cluster, TrackingParticle) pair; the TrackingParticles referenced by any cluster are stored once, as a
// slim table indexed by the "*TP" columns.
#include <unordered_map>
#include <vector>

#include "DataFormats/Common/interface/DetSetVectorNew.h"
#include "DataFormats/Phase2TrackerCluster/interface/Phase2TrackerCluster1D.h"
#include "DataFormats/SiPixelCluster/interface/SiPixelCluster.h"
#include "DataFormats/TrackerRecHit2D/interface/OmniClusterRef.h"
#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/Framework/interface/global/EDProducer.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include "SimDataFormats/TrackingAnalysis/interface/TrackingParticle.h"
#include "SimDataFormats/TrackingAnalysis/interface/TrackingParticleFwd.h"
#include "SimTracker/TrackerHitAssociation/interface/ClusterTPAssociation.h"

class CompactClusterTruthProducer : public edm::global::EDProducer<> {
public:
  explicit CompactClusterTruthProducer(edm::ParameterSet const& cfg)
      : pixToken_(consumes(cfg.getParameter<edm::InputTag>("pixelClusters"))),
        otToken_(consumes(cfg.getParameter<edm::InputTag>("phase2OTClusters"))),
        assocToken_(consumes(cfg.getParameter<edm::InputTag>("clusterTP"))),
        tpToken_(consumes(cfg.getParameter<edm::InputTag>("trackingParticles"))) {
    for (auto const& n : {"pixDet", "pixPos", "otDet", "otPos", "counts"})
      produces<std::vector<unsigned int>>(n);
    for (auto const& n : {"pixTP", "otTP", "tpPdg", "tpCharge", "tpBx", "tpEvent", "tpKey"})
      produces<std::vector<int>>(n);
    for (auto const& n : {"tpPt", "tpEta", "tpPhi", "tpVx", "tpVy", "tpVz"})
      produces<std::vector<float>>(n);
  }

  static void fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
    edm::ParameterSetDescription desc;
    desc.add<edm::InputTag>("pixelClusters", edm::InputTag("hltSiPixelClusters"));
    desc.add<edm::InputTag>("phase2OTClusters", edm::InputTag("hltSiPhase2Clusters"));
    desc.add<edm::InputTag>("clusterTP", edm::InputTag("hltTPClusterProducer"));
    desc.add<edm::InputTag>("trackingParticles", edm::InputTag("mix", "MergedTrackTruth"));
    descriptions.addWithDefaultLabel(desc);
  }

private:
  void produce(edm::StreamID, edm::Event& ev, edm::EventSetup const&) const override {
    auto hPix = ev.getHandle(pixToken_);
    auto hOT = ev.getHandle(otToken_);
    auto const& assoc = ev.get(assocToken_);
    auto hTP = ev.getHandle(tpToken_);

    std::vector<unsigned int> pixDet, pixPos, otDet, otPos;
    std::vector<int> pixTP, otTP;
    std::unordered_map<unsigned int, int> slim;  // TrackingParticle key -> row of the slim table
    std::vector<int> tpPdg, tpCharge, tpBx, tpEvent, tpKey;
    std::vector<float> tpPt, tpEta, tpPhi, tpVx, tpVy, tpVz;
    auto slimIndex = [&](TrackingParticleRef const& ref) {
      auto it = slim.find(ref.key());
      if (it != slim.end())
        return it->second;
      int const row = tpKey.size();
      slim.emplace(ref.key(), row);
      auto const& tp = *ref;
      tpKey.push_back(ref.key());
      tpPdg.push_back(tp.pdgId());
      tpCharge.push_back(tp.charge());
      tpBx.push_back(tp.eventId().bunchCrossing());
      tpEvent.push_back(tp.eventId().event());
      tpPt.push_back(tp.pt());
      tpEta.push_back(tp.eta());
      tpPhi.push_back(tp.phi());
      tpVx.push_back(tp.vx());
      tpVy.push_back(tp.vy());
      tpVz.push_back(tp.vz());
      return row;
    };

    unsigned int nPix = 0, nOT = 0;
    for (auto ds = hPix->begin(); ds != hPix->end(); ++ds) {
      for (auto cl = ds->begin(); cl != ds->end(); ++cl) {
        ++nPix;
        auto range = assoc.equal_range(OmniClusterRef(edmNew::makeRefTo(hPix, cl)));
        for (auto a = range.first; a != range.second; ++a) {
          pixDet.push_back(ds->detId());
          pixPos.push_back((static_cast<unsigned int>(cl->minPixelRow()) << 16) | cl->minPixelCol());
          pixTP.push_back(slimIndex(a->second));
        }
      }
    }
    for (auto ds = hOT->begin(); ds != hOT->end(); ++ds) {
      for (auto cl = ds->begin(); cl != ds->end(); ++cl) {
        ++nOT;
        auto range = assoc.equal_range(OmniClusterRef(edmNew::makeRefTo(hOT, cl)));
        for (auto a = range.first; a != range.second; ++a) {
          otDet.push_back(ds->detId());
          otPos.push_back((cl->column() << 16) | cl->firstStrip());
          otTP.push_back(slimIndex(a->second));
        }
      }
    }

    ev.put(std::make_unique<std::vector<unsigned int>>(std::move(pixDet)), "pixDet");
    ev.put(std::make_unique<std::vector<unsigned int>>(std::move(pixPos)), "pixPos");
    ev.put(std::make_unique<std::vector<unsigned int>>(std::move(otDet)), "otDet");
    ev.put(std::make_unique<std::vector<unsigned int>>(std::move(otPos)), "otPos");
    ev.put(std::make_unique<std::vector<unsigned int>>(std::vector<unsigned int>{nPix, nOT}), "counts");
    ev.put(std::make_unique<std::vector<int>>(std::move(pixTP)), "pixTP");
    ev.put(std::make_unique<std::vector<int>>(std::move(otTP)), "otTP");
    ev.put(std::make_unique<std::vector<int>>(std::move(tpPdg)), "tpPdg");
    ev.put(std::make_unique<std::vector<int>>(std::move(tpCharge)), "tpCharge");
    ev.put(std::make_unique<std::vector<int>>(std::move(tpBx)), "tpBx");
    ev.put(std::make_unique<std::vector<int>>(std::move(tpEvent)), "tpEvent");
    ev.put(std::make_unique<std::vector<int>>(std::move(tpKey)), "tpKey");
    ev.put(std::make_unique<std::vector<float>>(std::move(tpPt)), "tpPt");
    ev.put(std::make_unique<std::vector<float>>(std::move(tpEta)), "tpEta");
    ev.put(std::make_unique<std::vector<float>>(std::move(tpPhi)), "tpPhi");
    ev.put(std::make_unique<std::vector<float>>(std::move(tpVx)), "tpVx");
    ev.put(std::make_unique<std::vector<float>>(std::move(tpVy)), "tpVy");
    ev.put(std::make_unique<std::vector<float>>(std::move(tpVz)), "tpVz");
  }

  const edm::EDGetTokenT<edmNew::DetSetVector<SiPixelCluster>> pixToken_;
  const edm::EDGetTokenT<edmNew::DetSetVector<Phase2TrackerCluster1D>> otToken_;
  const edm::EDGetTokenT<ClusterTPAssociation> assocToken_;
  const edm::EDGetTokenT<TrackingParticleCollection> tpToken_;
};

DEFINE_FWK_MODULE(CompactClusterTruthProducer);
