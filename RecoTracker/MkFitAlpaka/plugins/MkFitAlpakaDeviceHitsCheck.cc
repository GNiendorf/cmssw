// Device hit input, step 1 (round 5, lane inputs): host check that the HitSoA rows can be made from the LOCAL hit
// quantities available on the device (pixel rechit SoA; raw OT rechits) with a per-module table, bitwise equal to the
// stock hit converters' mkfit::HitVec (MkFitSiPixelHitConverter, MkFitPhase2HitConverter).
// Per hit: position, 6 error elements, packed word, mkFit layer; each with the three contraction variants of
// interface/hits/DeviceHitInput.h. Pixel rows: SoA row r is compared with stock HitVec[r] (the row <-> cluster-index
// identity the device path relies on); the identity itself is checked from the legacy rechits (cluster key k: SoA
// detectorIndex[k] == det index and xLocal/yLocal[k] == the rechit's local position bitwise).
// Output: one CHECK line per event, a summary at endJob.
#include <array>
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#include "DataFormats/TrackerRecHit2D/interface/Phase2TrackerRecHit1D.h"
#include "DataFormats/SiPixelCluster/interface/SiPixelCluster.h"
#include "DataFormats/TrackerRecHit2D/interface/SiPixelRecHitCollection.h"
#include "DataFormats/TrackingRecHitSoA/interface/TrackingRecHitsHost.h"
#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/ESWatcher.h"
#include "FWCore/Framework/interface/EventSetup.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/Framework/interface/one/EDAnalyzer.h"
#include "FWCore/MessageLogger/interface/MessageLogger.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include "Geometry/Records/interface/TrackerDigiGeometryRecord.h"
#include "Geometry/TrackerGeometryBuilder/interface/TrackerGeometry.h"
#include "RecoTracker/MkFit/interface/MkFitGeometry.h"
#include "RecoTracker/MkFit/interface/MkFitHitWrapper.h"
#include "RecoTracker/MkFitAlpaka/interface/hits/DeviceHitInput.h"
#include "RecoTracker/MkFitAlpaka/interface/hits/EventOfHitsHostSetup.h"
#include "RecoTracker/MkFitCore/interface/Hit.h"
#include "RecoTracker/Record/interface/TrackerRecoGeometryRecord.h"

namespace {
  using mkfitdev::Contract;
  using mkfitdev::HitModuleDev;

  constexpr int kNV = 3;  // contraction variants
  struct Counts {
    uint64_t hits = 0, posMis[kNV] = {}, errMis[kNV] = {}, packedMis = 0, packedMisAbs = 0, layerMis = 0;
    uint64_t mapChecked = 0, mapMis = 0, notInStock = 0;
    void add(Counts const& o) {
      hits += o.hits;
      for (int v = 0; v < kNV; ++v) {
        posMis[v] += o.posMis[v];
        errMis[v] += o.errMis[v];
      }
      packedMis += o.packedMis;
      packedMisAbs += o.packedMisAbs;
      layerMis += o.layerMis;
      mapChecked += o.mapChecked;
      mapMis += o.mapMis;
      notInStock += o.notInStock;
    }
  };

  template <Contract M>
  bool posDiffers(HitModuleDev const& m, float lx, float ly, const float* s) {
    float g[3];
    mkfitdev::localToGlobal<M>(m, lx, ly, g);
    return std::memcmp(g, s, sizeof(g)) != 0;
  }
  template <Contract M>
  bool errDiffers(HitModuleDev const& m, float cxx, float cxy, float cyy, const float* s) {
    float e[6];
    mkfitdev::localToGlobalError<M>(m, cxx, cxy, cyy, e);
    return std::memcmp(e, s, sizeof(e)) != 0;
  }
  void compareOne(HitModuleDev const& m, float lx, float ly, float cxx, float cxy, float cyy, const mkfit::Hit& h,
                  Counts& c) {
    const float* sp = h.posArray();
    const float* se = h.errArray();
    c.posMis[0] += posDiffers<Contract::kFuseSecond>(m, lx, ly, sp);
    c.posMis[1] += posDiffers<Contract::kFuseFirst>(m, lx, ly, sp);
    c.posMis[2] += posDiffers<Contract::kNoFuse>(m, lx, ly, sp);
    c.errMis[0] += errDiffers<Contract::kFuseSecond>(m, cxx, cxy, cyy, se);
    c.errMis[1] += errDiffers<Contract::kFuseFirst>(m, cxx, cxy, cyy, se);
    c.errMis[2] += errDiffers<Contract::kNoFuse>(m, cxx, cxy, cyy, se);
  }
}  // namespace

class MkFitAlpakaDeviceHitsCheck : public edm::one::EDAnalyzer<> {
public:
  explicit MkFitAlpakaDeviceHitsCheck(edm::ParameterSet const& iConfig)
      : soaToken_{consumes(iConfig.getParameter<edm::InputTag>("pixelSoA"))},
        pixelRecHitsToken_{consumes(iConfig.getParameter<edm::InputTag>("pixelRecHits"))},
        otRecHitsToken_{consumes(iConfig.getParameter<edm::InputTag>("otRecHits"))},
        pixelWrapperToken_{consumes(iConfig.getParameter<edm::InputTag>("pixelHits"))},
        stripWrapperToken_{consumes(iConfig.getParameter<edm::InputTag>("stripHits"))},
        pixelLayerToken_{consumes(iConfig.getParameter<edm::InputTag>("pixelHits"))},
        stripLayerToken_{consumes(iConfig.getParameter<edm::InputTag>("stripHits"))},
        geomToken_{esConsumes()},
        mkFitGeomToken_{esConsumes()} {}

  static void fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
    edm::ParameterSetDescription desc;
    desc.add("pixelSoA", edm::InputTag{"hltPhase2SiPixelRecHitsSoA"});
    desc.add("pixelRecHits", edm::InputTag{"hltSiPixelRecHits"});
    desc.add("otRecHits", edm::InputTag{"hltSiPhase2RecHits"});
    desc.add("pixelHits", edm::InputTag{"hltMkFitSiPixelHits"});
    desc.add("stripHits", edm::InputTag{"hltMkFitSiPhase2Hits"});
    descriptions.addWithDefaultLabel(desc);
  }

  void analyze(edm::Event const& iEvent, edm::EventSetup const& iSetup) override {
    if (watcher_.check(iSetup) || table_.empty())
      buildTable(iSetup.getData(geomToken_), iSetup.getData(mkFitGeomToken_));
    const auto& soa = iEvent.get(soaToken_);
    const auto hv = soa.const_view().trackingHits();
    const auto mv = soa.const_view().hitModules();
    const auto& pixStock = iEvent.get(pixelWrapperToken_).hits();
    const auto& strStock = iEvent.get(stripWrapperToken_).hits();
    const auto& pixLay = iEvent.get(pixelLayerToken_);
    const auto& strLay = iEvent.get(stripLayerToken_);
    const auto& geom = iSetup.getData(geomToken_);

    // pixel: the legacy cluster order inside a module is NOT the SoA order (legacy clusters are re-ordered by
    // minPixelRow), so HitVec row k (legacy cluster key) comes from SoA row moduleStart[det] + originalId(k). Persisted
    // clusters (replay) have no originalId: then the SoA row of the module with the bitwise-equal local (x, y) is
    // taken (the legacy rechit position is a copy of the SoA one). rowMap counts keys whose SoA row is found;
    // identity counts keys with SoA row == k (the naive map).
    Counts p;
    const uint32_t nSoa = soa.nHits();
    std::vector<uint8_t> used(nSoa, 0);
    uint64_t identity = 0, viaOriginalId = 0;
    for (const auto& ds : iEvent.get(pixelRecHitsToken_)) {
      const uint32_t gind = geom.idToDetUnit(DetId(ds.detId()))->index();
      const uint32_t b = mv[gind].moduleStart(), e = mv[gind + 1].moduleStart();
      for (const auto& h : ds) {
        const uint32_t k = h.firstClusterRef().index();
        ++p.mapChecked;
        int64_t j = -1;
        const auto& clu = *h.cluster();
        if (clu.originalId() != SiPixelCluster::invalidClusterId) {
          j = b + clu.originalId();
          ++viaOriginalId;
        } else {
          const float lx = h.localPosition().x(), ly = h.localPosition().y();
          for (uint32_t r = b; r < e && r < nSoa; ++r)
            if (!used[r] && hv[r].xLocal() == lx && hv[r].yLocal() == ly) {
              j = r;
              break;
            }
        }
        if (j < 0 || j >= nSoa || k >= pixStock.size()) {
          ++p.mapMis;
          continue;
        }
        used[j] = 1;
        identity += uint32_t(j) == k;
        const auto& m = table_[hv[j].detectorIndex()];
        ++p.hits;
        compareOne(m, hv[j].xLocal(), hv[j].yLocal(), hv[j].xerrLocal(), 0.f, hv[j].yerrLocal(), pixStock[k], p);
        const uint32_t sp = mkfitdev::packedOf(pixStock[k]);
        const bool pm = mkfitdev::packPixel(m.detIdInLayer,
                                            mkfitdev::pixelSpanFromSoA(hv[j].clusterSizeX()),
                                            mkfitdev::pixelSpanFromSoA(hv[j].clusterSizeY())) != sp;
        p.packedMis += pm;
        if (pm && nPrint_ < 20) {
          ++nPrint_;
          edm::LogPrint("MkFitAlpakaDeviceHitsCheck")
              << "PIXEL_PACK k " << k << " stock id/rows/cols " << pixStock[k].detIDinLayer() << "/"
              << pixStock[k].spanRows() << "/" << pixStock[k].spanCols() << " table id " << m.detIdInLayer
              << " soa size " << hv[j].clusterSizeX() << "," << hv[j].clusterSizeY() << " legacy size " << clu.sizeX()
              << "," << clu.sizeY();
        }
        p.packedMisAbs +=
            mkfitdev::packPixel(m.detIdInLayer,
                                mkfitdev::pixelSpanFromSoA(hv[j].clusterSizeX()),
                                mkfitdev::pixelSpanFromSoA(hv[j].clusterSizeY())) != sp;
        p.layerMis += (k < pixLay.size() ? pixLay[k] : -1) != m.layer;
        p.notInStock += hv[j].detectorIndex() != gind;  // here: SoA module != rechit module
      }
    }
    for (uint32_t r = 0; r < nSoa; ++r)
      p.notInStock += !used[r] ? (1ull << 32) : 0;  // SoA rows without a legacy hit, in the high word
    identityTot_ += identity;
    origIdTot_ += viaOriginalId;
    // OT: raw rechit (local position, local error, det index, cluster size) vs stock HitVec[cluster key]
    Counts o;
    for (const auto& ds : iEvent.get(otRecHitsToken_)) {
      if (ds.empty())
        continue;
      const auto& m = table_[ds.begin()->det()->index()];
      for (const auto& h : ds) {
        const uint32_t k = h.firstClusterRef().index();
        if (k >= strStock.size()) {
          ++o.notInStock;
          continue;
        }
        ++o.hits;
        const auto lp = h.localPosition();
        const auto le = h.localPositionError();
        compareOne(m, lp.x(), lp.y(), le.xx(), le.xy(), le.yy(), strStock[k], o);
        const uint32_t pk = mkfitdev::packStrip(m.detIdInLayer, h.cluster()->size());
        o.packedMis += pk != mkfitdev::packedOf(strStock[k]);
        o.layerMis += (k < strLay.size() ? strLay[k] : -1) != m.layer;
      }
    }
    pixTot_.add(p);
    otTot_.add(o);
    ++nEvents_;
    edm::LogPrint("MkFitAlpakaDeviceHitsCheck") << "DEVICE_HITS_CHECK event " << iEvent.id().event() << " " << line(p, "pixel")
                                                << " | " << line(o, "ot");
  }

  void endJob() override {
    edm::LogPrint("MkFitAlpakaDeviceHitsCheck") << "DEVICE_HITS_SUMMARY events " << nEvents_ << " pixelIdentityRows "
                                                << identityTot_ << " pixelRowsViaOriginalId " << origIdTot_ << " "
                                                << line(pixTot_, "pixel")
                                                << " | " << line(otTot_, "ot");
  }

private:
  static std::string line(Counts const& c, const char* what) {
    std::string s = std::string(what) + " hits " + std::to_string(c.hits) + " posMis[fuse2,fuse1,none] ";
    for (int v = 0; v < kNV; ++v)
      s += std::to_string(c.posMis[v]) + (v + 1 < kNV ? "," : "");
    s += " errMis ";
    for (int v = 0; v < kNV; ++v)
      s += std::to_string(c.errMis[v]) + (v + 1 < kNV ? "," : "");
    s += " packedMis " + std::to_string(c.packedMis) + " packedMisAbs " + std::to_string(c.packedMisAbs) +
         " layerMis " + std::to_string(c.layerMis) + " notInStock " + std::to_string(c.notInStock);
    if (c.mapChecked)
      s += " rowMap " + std::to_string(c.mapChecked - c.mapMis) + "/" + std::to_string(c.mapChecked);
    return s;
  }

  void buildTable(TrackerGeometry const& geom, MkFitGeometry const& mkg) {
    uint32_t n = 0;
    for (auto const* d : geom.detUnits())
      n = std::max<uint32_t>(n, d->index() + 1);
    table_.assign(n, HitModuleDev{{0, 0, 0, 0, 0, 0, 0, 0, 0}, {0, 0, 0}, -1, 0});
    for (auto const* d : geom.detUnits()) {
      const auto& s = d->surface();
      const auto& R = s.rotation();
      HitModuleDev m{{R.xx(), R.xy(), R.xz(), R.yx(), R.yy(), R.yz(), R.zx(), R.zy(), R.zz()},
                     {s.position().x(), s.position().y(), s.position().z()},
                     -1,
                     0};
      const DetId id = d->geographicalId();
      if (id.det() == DetId::Tracker) {
        try {
          m.layer = mkg.mkFitLayerNumber(id);
          m.detIdInLayer = mkg.uniqueIdInLayer(m.layer, id.rawId());
        } catch (std::out_of_range const&) {
          m.layer = -1;
          m.detIdInLayer = 0;
        }
      }
      table_[d->index()] = m;
    }
  }

  const edm::EDGetTokenT<reco::TrackingRecHitHost> soaToken_;
  const edm::EDGetTokenT<SiPixelRecHitCollection> pixelRecHitsToken_;
  const edm::EDGetTokenT<Phase2TrackerRecHit1DCollectionNew> otRecHitsToken_;
  const edm::EDGetTokenT<MkFitHitWrapper> pixelWrapperToken_;
  const edm::EDGetTokenT<MkFitHitWrapper> stripWrapperToken_;
  const edm::EDGetTokenT<std::vector<int>> pixelLayerToken_;
  const edm::EDGetTokenT<std::vector<int>> stripLayerToken_;
  const edm::ESGetToken<TrackerGeometry, TrackerDigiGeometryRecord> geomToken_;
  const edm::ESGetToken<MkFitGeometry, TrackerRecoGeometryRecord> mkFitGeomToken_;
  edm::ESWatcher<TrackerRecoGeometryRecord> watcher_;
  std::vector<HitModuleDev> table_;
  Counts pixTot_, otTot_;
  int nPrint_ = 0;
  uint64_t nEvents_ = 0, identityTot_ = 0, origIdTot_ = 0;
};

DEFINE_FWK_MODULE(MkFitAlpakaDeviceHitsCheck);
