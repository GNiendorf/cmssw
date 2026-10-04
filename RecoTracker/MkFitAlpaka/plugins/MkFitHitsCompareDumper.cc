// Dumps, per event, the inputs of the stock MkFitEventOfHitsProducer (pixel + strip/OT MkFitHitWrapper hits, the
// per-hit layer maps, the layer q-axis setup, the pixel dead regions as the stock producer builds them) together with
// the stock MkFitEventOfHits result (per layer: internal order, hit infos, bin table, dead bins) into a flat binary
// file. test/testHitsBinning.dev.cc replays the dump through the device EventOfHits and compares.
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "FWCore/Framework/interface/one/EDAnalyzer.h"
#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/EventSetup.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include "FWCore/MessageLogger/interface/MessageLogger.h"

#include "CondFormats/SiPixelObjects/interface/SiPixelQuality.h"
#include "CondFormats/DataRecord/interface/SiPixelQualityRcd.h"
#include "DataFormats/TrackerCommon/interface/TrackerDetSide.h"
#include "DataFormats/TrackerCommon/interface/TrackerTopology.h"
#include "Geometry/TrackerGeometryBuilder/interface/TrackerGeometry.h"
#include "Geometry/Records/interface/TrackerDigiGeometryRecord.h"

#include "RecoTracker/MkFit/interface/MkFitEventOfHits.h"
#include "RecoTracker/MkFit/interface/MkFitGeometry.h"
#include "RecoTracker/MkFit/interface/MkFitHitWrapper.h"
#include "RecoTracker/Record/interface/TrackerRecoGeometryRecord.h"
#include "RecoTracker/MkFitCore/interface/HitStructures.h"
#include "RecoTracker/MkFitCMS/interface/LayerNumberConverter.h"
#include "RecoTracker/MkFitCMS/interface/MkStdSeqs.h"

#include "RecoTracker/MkFitAlpaka/interface/hits/EventOfHitsHostSetup.h"

class MkFitHitsCompareDumper : public edm::one::EDAnalyzer<> {
public:
  explicit MkFitHitsCompareDumper(edm::ParameterSet const& iConfig);
  ~MkFitHitsCompareDumper() override;
  static void fillDescriptions(edm::ConfigurationDescriptions& descriptions);

private:
  void analyze(edm::Event const& iEvent, edm::EventSetup const& iSetup) override;
  template <typename T>
  void put(const T* p, size_t n) {
    fwrite(p, sizeof(T), n, out_);
  }
  template <typename T>
  void put1(T v) {
    fwrite(&v, sizeof(T), 1, out_);
  }

  const edm::EDGetTokenT<MkFitHitWrapper> pixelHitsToken_;
  const edm::EDGetTokenT<MkFitHitWrapper> stripHitsToken_;
  const edm::EDGetTokenT<std::vector<int>> pixelLayerToken_;
  const edm::EDGetTokenT<std::vector<int>> stripLayerToken_;
  const edm::EDGetTokenT<MkFitEventOfHits> eohToken_;
  const edm::ESGetToken<MkFitGeometry, TrackerRecoGeometryRecord> mkFitGeomToken_;
  const edm::ESGetToken<SiPixelQuality, SiPixelQualityRcd> pixelQualityToken_;
  const edm::ESGetToken<TrackerGeometry, TrackerDigiGeometryRecord> geomToken_;
  const bool usePixelQualityDB_;
  const int syntheticDeadsPerLayer_;
  const int timeStockRepeats_;
  const int keepOneHitIn_;
  FILE* out_ = nullptr;
};

MkFitHitsCompareDumper::MkFitHitsCompareDumper(edm::ParameterSet const& iConfig)
    : pixelHitsToken_{consumes(iConfig.getParameter<edm::InputTag>("pixelHits"))},
      stripHitsToken_{consumes(iConfig.getParameter<edm::InputTag>("stripHits"))},
      pixelLayerToken_{consumes(iConfig.getParameter<edm::InputTag>("pixelHits"))},
      stripLayerToken_{consumes(iConfig.getParameter<edm::InputTag>("stripHits"))},
      eohToken_{consumes(iConfig.getParameter<edm::InputTag>("eventOfHits"))},
      mkFitGeomToken_{esConsumes()},
      pixelQualityToken_{esConsumes()},
      geomToken_{esConsumes()},
      usePixelQualityDB_{iConfig.getParameter<bool>("usePixelQualityDB")},
      syntheticDeadsPerLayer_{iConfig.getParameter<int>("syntheticDeadsPerLayer")},
      timeStockRepeats_{iConfig.getParameter<int>("timeStockRepeats")},
      keepOneHitIn_{iConfig.getParameter<int>("keepOneHitIn")} {
  out_ = fopen(iConfig.getParameter<std::string>("fileName").c_str(), "wb");
  if (!out_)
    throw cms::Exception("MkFitHitsCompareDumper") << "cannot open output file";
}

MkFitHitsCompareDumper::~MkFitHitsCompareDumper() {
  if (out_)
    fclose(out_);
}

void MkFitHitsCompareDumper::fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
  edm::ParameterSetDescription desc;
  desc.add("pixelHits", edm::InputTag{"hltMkFitSiPixelHits"});
  desc.add("stripHits", edm::InputTag{"hltMkFitSiPhase2Hits"});
  desc.add("eventOfHits", edm::InputTag{"hltMkFitEventOfHits"});
  desc.add("usePixelQualityDB", true)->setComment("must match the stock MkFitEventOfHitsProducer setting");
  desc.add<std::string>("fileName", "hits_dump.bin");
  desc.add<int>("timeStockRepeats", 0)
      ->setComment("> 0: re-run the stock EventOfHits fill (as MkFitEventOfHitsProducer, dead vectors included) this "
                   "many times single-threaded and print the best time (STOCK_EOH_MS)");
  desc.add<int>("keepOneHitIn", 0)
      ->setComment("> 1: register only a pseudo-random 1/N of the hits and rebuild the stock EventOfHits from them "
                   "(low-occupancy test: layers below 256 hits take stock's std::sort path)");
  desc.add<int>("syntheticDeadsPerLayer", 0)
      ->setComment("> 0: replace the DB dead regions by this many pseudo-random regions per layer, and dump the dead bins "
                   "that stock LayerOfHits::suckInDeads makes of them (exercises the dead-bin code when the DB list is "
                   "empty)");
  descriptions.addWithDefaultLabel(desc);
}

void MkFitHitsCompareDumper::analyze(edm::Event const& iEvent, edm::EventSetup const& iSetup) {
  const auto& pixelHits = iEvent.get(pixelHitsToken_).hits();
  const auto& stripHits = iEvent.get(stripHitsToken_).hits();
  const auto& pixLayIn = iEvent.get(pixelLayerToken_);
  const auto& strLayIn = iEvent.get(stripLayerToken_);
  const auto& eohIn = iEvent.get(eohToken_).get();
  // optional low-occupancy subsample, with the stock EventOfHits rebuilt from it exactly as the stock producer does
  std::vector<int> pixLaySub, strLaySub;
  std::unique_ptr<mkfit::EventOfHits> eohSub;
  if (keepOneHitIn_ > 1) {
    auto keep = [&](uint64_t i, int w) {
      uint64_t z = (i * 2 + w + 1) * 0x9E3779B97F4A7C15ull ^ iEvent.id().event();
      z = (z ^ (z >> 31)) * 0xBF58476D1CE4E5B9ull;
      return (z >> 33) % uint64_t(keepOneHitIn_) == 0;
    };
    pixLaySub = pixLayIn;
    strLaySub = strLayIn;
    for (size_t i = 0; i < pixLaySub.size(); ++i)
      if (!keep(i, 0))
        pixLaySub[i] = -1;
    for (size_t i = 0; i < strLaySub.size(); ++i)
      if (!keep(i, 1))
        strLaySub[i] = -1;
    eohSub = std::make_unique<mkfit::EventOfHits>(iSetup.getData(mkFitGeomToken_).trackerInfo());
    mkfit::StdSeq::cmssw_LoadHits_Begin(*eohSub, {&pixelHits, &stripHits});
    for (int w = 0; w < 2; ++w) {
      const auto& lm = w == 0 ? pixLaySub : strLaySub;
      for (unsigned int i = 0, end = lm.size(); i < end; ++i)
        if (lm[i] >= 0)
          (*eohSub)[lm[i]].registerHit(i);
    }
    mkfit::StdSeq::cmssw_LoadHits_End(*eohSub);
  }
  const auto& pixLay = eohSub ? pixLaySub : pixLayIn;
  const auto& strLay = eohSub ? strLaySub : strLayIn;
  const mkfit::EventOfHits& eoh = eohSub ? *eohSub : eohIn;
  const auto& mkFitGeom = iSetup.getData(mkFitGeomToken_);
  const auto& ti = mkFitGeom.trackerInfo();

  // Dead regions, as MkFitEventOfHitsProducer builds them (pixel part; the HLT LST step has useStripStripQualityDB=False).
  std::vector<mkfitdev::DeadRegionDev> deads;
  if (usePixelQualityDB_) {
    const auto& trackerGeom = iSetup.getData(geomToken_);
    const auto& pixelQuality = iSetup.getData(pixelQualityToken_);
    for (const auto& bp : pixelQuality.getBadComponentList()) {
      const DetId detid(bp.DetID);
      const auto& surf = trackerGeom.idToDet(detid)->surface();
      bool isBarrel = (mkFitGeom.topology()->side(detid) == static_cast<unsigned>(TrackerDetSide::Barrel));
      const auto ilay = mkFitGeom.mkFitLayerNumber(detid);
      const auto q1 = isBarrel ? surf.zSpan().first : surf.rSpan().first;
      const auto q2 = isBarrel ? surf.zSpan().second : surf.rSpan().second;
      if (bp.errorType == 0)
        deads.push_back({surf.phiSpan().first, surf.phiSpan().second, q1, q2, ilay});
    }
  }

  // Synthetic dead regions (deterministic per event) run through the stock suckInDeads on a separate EventOfHits.
  std::unique_ptr<mkfit::EventOfHits> eohDead;
  if (syntheticDeadsPerLayer_ > 0) {
    deads.clear();
    uint64_t seed = 0x9E3779B97F4A7C15ull ^ iEvent.id().event();
    auto rnd = [&seed]() {  // splitmix64 -> [0, 1)
      uint64_t z = (seed += 0x9E3779B97F4A7C15ull);
      z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
      z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
      return float((z ^ (z >> 31)) >> 40) / float(1ull << 24);
    };
    std::vector<mkfit::DeadVec> dv(ti.n_layers());
    const float pi = mkfit::Const::PI;
    for (int il = 0; il < ti.n_layers(); ++il) {
      mkfit::LayerOfHits::Initializator ini(ti.layer(il));
      for (int k = 0; k < syntheticDeadsPerLayer_; ++k) {
        float phi1 = -pi + 2.f * pi * rnd();
        float phi2 = phi1 + 0.3f * rnd();
        if (phi2 > pi)
          phi2 -= 2.f * pi;  // wrap-around regions
        float q1 = ini.m_qmin - 2.f + (ini.m_qmax - ini.m_qmin + 4.f) * rnd();  // also beyond the axis ends
        float q2 = q1 + 0.1f * (ini.m_qmax - ini.m_qmin) * rnd();
        dv[il].push_back({phi1, phi2, q1, q2});
        deads.push_back({phi1, phi2, q1, q2, il});
      }
    }
    eohDead = std::make_unique<mkfit::EventOfHits>(ti);
    for (int il = 0; il < ti.n_layers(); ++il)
      eohDead->suckInDeads(il, dv[il]);
  }
  const mkfit::EventOfHits& eohForDeads = eohDead ? *eohDead : eoh;

  if (timeStockRepeats_ > 0) {
    // the dead vectors exactly as the stock producer passes them (pixel DB regions, per layer)
    std::vector<mkfit::DeadVec> dv(ti.n_layers());
    if (usePixelQualityDB_ && syntheticDeadsPerLayer_ == 0)
      for (auto const& d : deads)
        dv[d.layer].push_back({d.phi1, d.phi2, d.q1, d.q2});
    double best = 1e30;
    for (int it = 0; it < timeStockRepeats_; ++it) {
      auto t0 = std::chrono::steady_clock::now();
      auto e2 = std::make_unique<mkfit::EventOfHits>(ti);
      mkfit::StdSeq::cmssw_LoadHits_Begin(*e2, {&pixelHits, &stripHits});
      if (usePixelQualityDB_)
        mkfit::StdSeq::loadDeads(*e2, dv);
      for (int w = 0; w < 2; ++w) {
        const auto& lm = w == 0 ? pixLay : strLay;
        for (unsigned int i = 0, end = lm.size(); i < end; ++i)
          if (lm[i] >= 0)
            (*e2)[lm[i]].registerHit(i);
      }
      mkfit::StdSeq::cmssw_LoadHits_End(*e2);
      auto t1 = std::chrono::steady_clock::now();
      best = std::min(best, std::chrono::duration<double, std::milli>(t1 - t0).count());
    }
    printf("STOCK_EOH_MS event %llu hits %zu best-of-%d %.3f ms\n",
           (unsigned long long)iEvent.id().event(),
           pixelHits.size() + stripHits.size(),
           timeStockRepeats_,
           best);
  }

  const uint32_t magic = 0x4d4b4831;  // "MKH1"
  put1(magic);
  put1<uint32_t>(iEvent.id().run());
  put1<uint32_t>(iEvent.id().luminosityBlock());
  put1<uint64_t>(iEvent.id().event());
  put1<uint32_t>((usePixelQualityDB_ || syntheticDeadsPerLayer_ > 0) ? 1 : 0);

  // layers
  auto axes = mkfitdev::layerAxisInputs(ti);
  put1<uint32_t>(axes.size());
  for (auto const& a : axes) {
    put1(a.qmin);
    put1(a.qmax);
    put1<uint32_t>(a.nq);
    put1<uint32_t>(a.isBarrel);
    put1<uint32_t>(a.isPixel);
  }

  // hits
  for (int w = 0; w < 2; ++w) {
    const auto& hv = w == 0 ? pixelHits : stripHits;
    const auto& lm = w == 0 ? pixLay : strLay;
    put1<uint32_t>(hv.size());
    for (const auto& h : hv) {
      put(h.posArray(), 3);
      put(h.errArray(), 6);
      put1<uint32_t>(mkfitdev::packedOf(h));
    }
    put1<uint32_t>(lm.size());
    put(lm.data(), lm.size());
  }

  // dead regions
  put1<uint32_t>(deads.size());
  put(deads.data(), deads.size());

  // stock result
  for (int il = 0; il < eoh.nLayers(); ++il) {
    const auto& loh = eoh[il];
    const uint32_t n = loh.nHits();
    put1(n);
    for (uint32_t i = 0; i < n; ++i)
      put1<uint32_t>(loh.getOriginalHitIndex(i));
    for (uint32_t i = 0; i < n; ++i) {
      const auto& hi = loh.hit_info(i);
      put1(hi.phi);
      put1(hi.q);
      put1(hi.q_half_length);
      put1(hi.qbar);
    }
    const uint32_t nq = axes[il].nq;
    std::vector<uint32_t> content(nq * mkfitdev::kNPhiBins);
    std::vector<uint8_t> dead(nq * mkfitdev::kNPhiBins);
    for (uint32_t qb = 0; qb < nq; ++qb)
      for (uint32_t pb = 0; pb < mkfitdev::kNPhiBins; ++pb) {
        auto c = loh.phiQBinContent(pb, qb);
        content[qb * mkfitdev::kNPhiBins + pb] = uint32_t(c.first) | (uint32_t(c.count) << mkfitdev::kBinFirstBits);
        dead[qb * mkfitdev::kNPhiBins + pb] = eohForDeads[il].isBinDead(pb, qb) ? 1 : 0;
      }
    put(content.data(), content.size());
    put(dead.data(), dead.size());
  }
  fflush(out_);
  edm::LogInfo("MkFitHitsCompareDumper") << "dumped event " << iEvent.id() << " pixel " << pixelHits.size() << " strip "
                                         << stripHits.size() << " deads " << deads.size();
}

DEFINE_FWK_MODULE(MkFitHitsCompareDumper);
