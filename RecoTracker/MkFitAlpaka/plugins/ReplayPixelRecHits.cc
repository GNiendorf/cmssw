// Replay helper (harness lane): legacy pixel rechits from the persisted pixel rechit SoA + persisted legacy clusters.
// Same output as the menu's SiPixelRecHitFromSoAAlpaka (hltSiPixelRecHits), which cannot run on persisted clusters:
// it needs SiPixelCluster::originalId() (the SoA cluster index inside the module), a transient field. The legacy
// clusters of a module are the SoA clusters re-ordered by minPixelRow (heap sort), so the index is recovered per module:
// a legacy cluster is paired with an unused SoA hit of equal charge (exact integer sum of ADC on both sides), the one
// closest in local (x, y) to the cluster centre; clusters without an equal-charge partner take the nearest leftover. Counters report every ambiguous or failed pairing;
// with checkOriginalId the recovered index is compared with originalId() when it is available (in-memory clusters).

#include <atomic>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <unordered_map>
#include <vector>

#include "DataFormats/Common/interface/DetSetVectorNew.h"
#include "DataFormats/Common/interface/Handle.h"
#include "DataFormats/SiPixelCluster/interface/SiPixelCluster.h"
#include "DataFormats/TrackerRecHit2D/interface/SiPixelRecHitCollection.h"
#include "DataFormats/TrackingRecHitSoA/interface/TrackingRecHitsHost.h"
#include "DataFormats/TrackingRecHitSoA/interface/TrackingRecHitsSoA.h"
#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/EventSetup.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/Framework/interface/global/EDProducer.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include "FWCore/Utilities/interface/Exception.h"
#include "Geometry/CommonTopologies/interface/PixelGeomDetUnit.h"
#include "Geometry/CommonTopologies/interface/PixelTopology.h"
#include "Geometry/Records/interface/TrackerDigiGeometryRecord.h"
#include "Geometry/TrackerGeometryBuilder/interface/TrackerGeometry.h"

namespace mkfitdev_harness {

  class ReplayPixelRecHits : public edm::global::EDProducer<> {
  public:
    explicit ReplayPixelRecHits(edm::ParameterSet const& cfg)
        : maxHitsInModules_(cfg.getParameter<uint32_t>("maxHitsInModules")),
          checkOriginalId_(cfg.getParameter<bool>("checkOriginalId")),
          geomToken_(esConsumes()),
          hitsToken_(consumes(cfg.getParameter<edm::InputTag>("pixelRecHitSrc"))),
          clusterToken_(consumes(cfg.getParameter<edm::InputTag>("src"))),
          putToken_(produces()) {}

    static void fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
      edm::ParameterSetDescription desc;
      desc.add<uint32_t>("maxHitsInModules", 1024)->setComment("as the menu's hltSiPixelRecHits (Phase-2 value)");
      desc.add<bool>("checkOriginalId", true)->setComment("compare with SiPixelCluster::originalId() when valid");
      desc.add<edm::InputTag>("pixelRecHitSrc", edm::InputTag("hltPhase2SiPixelRecHitsSoA", "", "HLTX"));
      desc.add<edm::InputTag>("src", edm::InputTag("hltSiPixelClusters", "", "HLTX"));
      descriptions.addWithDefaultLabel(desc);
    }

    void produce(edm::StreamID, edm::Event& ev, edm::EventSetup const& es) const override {
      auto const& hits = ev.get(hitsToken_);
      auto hitsView = hits.view().trackingHits();
      auto modulesView = hits.view().hitModules();
      auto nHits = hitsView.metadata().size();
      auto nModules = modulesView.metadata().size();
      SiPixelRecHitCollection output;
      output.reserve(nModules, nHits);
      if (nHits == 0) {
        ev.emplace(putToken_, std::move(output));
        return;
      }
      TrackerGeometry const& geom = es.getData(geomToken_);
      auto const hclusters = ev.getHandle(clusterToken_);

      std::vector<int> idOf;         // legacy position -> SoA index in module
      std::vector<char> used;        // SoA hit already paired
      std::vector<LocalPoint> lpos;  // legacy cluster centres
      std::vector<uint32_t> qclu;    // legacy cluster charges
      std::unordered_map<uint32_t, std::vector<uint32_t>> byCharge;
      for (auto const& dsv : *hclusters) {
        unsigned int detid = dsv.detId();
        const GeomDetUnit* genericDet = geom.idToDetUnit(DetId(detid));
        auto gind = genericDet->index();
        const PixelGeomDetUnit* pixDet = dynamic_cast<const PixelGeomDetUnit*>(genericDet);
        if (pixDet == nullptr)
          throw cms::Exception("LogicError") << "replay pixel rechits: not a pixel det " << detid;
        SiPixelRecHitCollection::FastFiller recHitsOnDetUnit(output, detid);
        auto fc = modulesView.moduleStart()[gind];
        auto lc = modulesView.moduleStart()[gind + 1];
        uint32_t nhits = lc - fc;
        if (nhits > maxHitsInModules_)
          nhits = maxHitsInModules_;
        if (nhits == 0)
          continue;
        if (nhits != dsv.size())
          ++nModSizeMismatch_;

        // recover the SoA index of each legacy cluster, in three passes so that a cluster without an exact charge
        // partner cannot take the hit of another one: (1) unique equal-charge partner, (2) nearest among the unused
        // equal-charge partners, (3) nearest among all unused hits
        idOf.assign(dsv.size(), -1);
        used.assign(nhits, 0);
        auto const& topo = pixDet->specificTopology();
        lpos.clear();
        qclu.clear();
        for (auto const& clust : dsv) {
          lpos.push_back(topo.localPosition(MeasurementPoint(clust.x(), clust.y())));
          qclu.push_back(uint32_t(clust.charge()));
        }
        byCharge.clear();  // SoA hits of this module by charge, each list in increasing j
        for (uint32_t j = 0; j < nhits; ++j)
          byCharge[uint32_t(hitsView[fc + j].chargeAndStatus().charge)].push_back(j);
        auto dist2 = [&](int k, uint32_t j) {
          float dx = hitsView[fc + j].xLocal() - lpos[k].x(), dy = hitsView[fc + j].yLocal() - lpos[k].y();
          return dx * dx + dy * dy;
        };
        for (int pass = 1; pass <= 3; ++pass) {
          int k = -1;
          for (size_t kk = 0; kk < dsv.size(); ++kk) {
            ++k;
            if (idOf[k] >= 0)
              continue;
            int best = -1, nEq = 0;
            float bestD = std::numeric_limits<float>::max();
            auto consider = [&](uint32_t j) {
              if (used[j])
                return;
              ++nEq;
              float d = dist2(k, j);
              if (d < bestD) {
                bestD = d;
                best = j;
              }
            };
            if (pass < 3) {
              auto it = byCharge.find(qclu[k]);
              if (it != byCharge.end())
                for (uint32_t j : it->second)
                  consider(j);
            } else {
              for (uint32_t j = 0; j < nhits; ++j)
                consider(j);
            }
            if (best < 0 || (pass == 1 && nEq != 1))
              continue;
            if (pass == 2)
              ++nAmbiguous_;
            if (pass == 3)
              ++nNoChargeMatch_;
            used[best] = 1;
            idOf[k] = best;
          }
        }
        {
          int k = 0;
          for (auto const& clust : dsv) {
            if (idOf[k] < 0)
              ++nUnpaired_;
            if (checkOriginalId_ && clust.originalId() != SiPixelCluster::invalidClusterId) {
              ++nChecked_;
              if (int(clust.originalId()) != idOf[k])
                ++nCheckFailed_;
            }
            ++nClusters_;
            ++k;
          }
        }

        // same conversion as SiPixelRecHitFromSoAAlpaka, with the recovered index in place of originalId()
        int k = 0;
        for (auto const& clust : dsv) {
          int ic = idOf[k++];
          if (ic < 0 || uint32_t(ic) >= nhits)
            continue;
          auto ij = fc + ic;
          LocalPoint lp(hitsView.xLocal()[ij], hitsView.yLocal()[ij]);
          LocalError le(hitsView.xerrLocal()[ij], 0, hitsView.yerrLocal()[ij]);
          SiPixelRecHitQuality::QualWordType rqw = 0;
          edm::Ref<edmNew::DetSetVector<SiPixelCluster>, SiPixelCluster> cluster = edmNew::makeRefTo(hclusters, &clust);
          recHitsOnDetUnit.emplace_back(lp, le, rqw, *genericDet, cluster);
        }
      }
      ev.emplace(putToken_, std::move(output));
    }

    void endJob() override {
      std::cout << "[replayPixelRecHits] clusters " << nClusters_ << " modules with nSoA!=nLegacy " << nModSizeMismatch_
                << " ambiguous-charge " << nAmbiguous_ << " no-charge-match " << nNoChargeMatch_ << " unpaired "
                << nUnpaired_ << " originalId checked " << nChecked_ << " wrong " << nCheckFailed_ << std::endl;
    }

  private:
    const uint32_t maxHitsInModules_;
    const bool checkOriginalId_;
    const edm::ESGetToken<TrackerGeometry, TrackerDigiGeometryRecord> geomToken_;
    const edm::EDGetTokenT<::reco::TrackingRecHitHost> hitsToken_;
    const edm::EDGetTokenT<SiPixelClusterCollectionNew> clusterToken_;
    const edm::EDPutTokenT<SiPixelRecHitCollection> putToken_;
    mutable std::atomic<long> nClusters_{0}, nModSizeMismatch_{0}, nAmbiguous_{0}, nNoChargeMatch_{0}, nUnpaired_{0},
        nChecked_{0}, nCheckFailed_{0};
  };

}  // namespace mkfitdev_harness

using MkFitAlpakaReplayPixelRecHits = mkfitdev_harness::ReplayPixelRecHits;
DEFINE_FWK_MODULE(MkFitAlpakaReplayPixelRecHits);
