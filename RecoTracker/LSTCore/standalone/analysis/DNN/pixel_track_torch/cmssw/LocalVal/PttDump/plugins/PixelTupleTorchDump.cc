// PixelTupleTorchDump (training-sample tool, not part of the HLT menu): one TTree row per pixel tuple that enters the
// Torch pixel-track classifier. The selector is run with its cut off (every preselected tuple passes), so its output
// SoA is the classifier's whole input. Per row:
//   - the 17 classifier inputs, computed as the selector's FeaturesExtractorKernel does;
//   - the decision of up to two clones of the selector with deployed settings (matched by fit parameters), for the
//     in-menu decision and the C++ parity check of an exported model;
//   - the pixel-layer pattern of the tuple;
//   - truth from the compact cluster truth of the cache (CompactClusterTruthProducer): the share of the tuple's hits
//     on its best TrackingParticle; true when that share is at least the MTV hit-association cut.
#include <cmath>
#include <map>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "CommonTools/UtilAlgos/interface/TFileService.h"
#include "DataFormats/SiPixelDetId/interface/PixelSubdetector.h"
#include "DataFormats/TrackReco/interface/Track.h"
#include "DataFormats/TrackReco/interface/TrackFwd.h"
#include "DataFormats/TrackSoA/interface/TracksHost.h"
#include "DataFormats/TrackerCommon/interface/TrackerTopology.h"
#include "DataFormats/TrackerRecHit2D/interface/SiPixelRecHit.h"
#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/EventSetup.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/Framework/interface/one/EDAnalyzer.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ServiceRegistry/interface/Service.h"
#include "FWCore/Utilities/interface/InputTag.h"
#include "Geometry/Records/interface/TrackerTopologyRcd.h"
#include "TTree.h"

class PixelTupleTorchDump : public edm::one::EDAnalyzer<edm::one::SharedResources> {
public:
  explicit PixelTupleTorchDump(edm::ParameterSet const& cfg)
      : allToken_(consumes(cfg.getParameter<edm::InputTag>("allTuples"))),
        indToken_(consumes(cfg.getParameter<edm::InputTag>("indToEdm"))),
        trkToken_(consumes(cfg.getParameter<edm::InputTag>("tracks"))),
        topoToken_(esConsumes()),
        cut_(cfg.getParameter<double>("associationCut")) {
    for (auto const& tag : cfg.getParameter<std::vector<edm::InputTag>>("deployedTuples"))
      depTokens_.push_back(consumes<reco::TracksHost>(tag));
    auto const truth = cfg.getParameter<std::string>("truthLabel");
    pixDetTok_ = consumes<std::vector<unsigned int>>(edm::InputTag(truth, "pixDet"));
    pixPosTok_ = consumes<std::vector<unsigned int>>(edm::InputTag(truth, "pixPos"));
    pixTPTok_ = consumes<std::vector<int>>(edm::InputTag(truth, "pixTP"));
    tpPtTok_ = consumes<std::vector<float>>(edm::InputTag(truth, "tpPt"));
    tpEtaTok_ = consumes<std::vector<float>>(edm::InputTag(truth, "tpEta"));
    tpVxTok_ = consumes<std::vector<float>>(edm::InputTag(truth, "tpVx"));
    tpVyTok_ = consumes<std::vector<float>>(edm::InputTag(truth, "tpVy"));
    tpVzTok_ = consumes<std::vector<float>>(edm::InputTag(truth, "tpVz"));
    tpPdgTok_ = consumes<std::vector<int>>(edm::InputTag(truth, "tpPdg"));
    tpBxTok_ = consumes<std::vector<int>>(edm::InputTag(truth, "tpBx"));
    tpEvTok_ = consumes<std::vector<int>>(edm::InputTag(truth, "tpEvent"));
    tpKeyTok_ = consumes<std::vector<int>>(edm::InputTag(truth, "tpKey"));
    usesResource(TFileService::kSharedResource);
    edm::Service<TFileService> fs;
    t_ = fs->make<TTree>("tuples", "Torch pixel-track classifier inputs and truth");
    t_->Branch("run", &run_);
    t_->Branch("lumi", &lumi_);
    t_->Branch("evt", &evt_);
    t_->Branch("idx", &idx_);
    t_->Branch("nPre", &nPre_);
    t_->Branch("f", f_, "f[17]/F");
    t_->Branch("quality", &quality_);
    t_->Branch("nHits", &nHits_);
    t_->Branch("layerMask", &layerMask_);
    t_->Branch("dep", dep_, "dep[2]/O");
    t_->Branch("edm", &edm_);
    t_->Branch("isTrue", &isTrue_);
    t_->Branch("share", &share_);
    t_->Branch("tpPt", &tpPt_);
    t_->Branch("tpEta", &tpEta_);
    t_->Branch("tpVxy", &tpVxy_);
    t_->Branch("tpVz", &tpVz_);
    t_->Branch("tpPdg", &tpPdg_);
    t_->Branch("tpBx", &tpBx_);
    t_->Branch("tpSig", &tpSig_);
    t_->Branch("tpKey", &tpKey_);
  }

private:
  using Key = std::tuple<float, float, float, float, float, float>;
  static Key key(reco::TrackSoAConstView const& v, int i) {
    return Key(v[i].pt(), v[i].eta(), v[i].chi2(), v[i].state()(0), v[i].state()(1), v[i].state()(4));
  }

  void analyze(edm::Event const& ev, edm::EventSetup const& es) override {
    auto const& topo = es.getData(topoToken_);
    auto const& all = ev.get(allToken_);
    auto const& ind = ev.get(indToken_);
    auto const& trk = ev.get(trkToken_);

    // cluster position -> slim TrackingParticle rows
    auto const& pixDet = ev.get(pixDetTok_);
    auto const& pixPos = ev.get(pixPosTok_);
    auto const& pixTP = ev.get(pixTPTok_);
    std::unordered_multimap<unsigned long long, int> c2tp;
    c2tp.reserve(pixDet.size());
    for (size_t k = 0; k < pixDet.size(); ++k)
      c2tp.emplace((static_cast<unsigned long long>(pixDet[k]) << 32) | pixPos[k], pixTP[k]);
    auto const& tpPt = ev.get(tpPtTok_);
    auto const& tpEta = ev.get(tpEtaTok_);
    auto const& tpVx = ev.get(tpVxTok_);
    auto const& tpVy = ev.get(tpVyTok_);
    auto const& tpVz = ev.get(tpVzTok_);
    auto const& tpPdg = ev.get(tpPdgTok_);
    auto const& tpBx = ev.get(tpBxTok_);
    auto const& tpEv = ev.get(tpEvTok_);
    auto const& tpKey = ev.get(tpKeyTok_);

    auto av = all.const_view().tracks();
    std::vector<std::map<Key, int>> kept(depTokens_.size());
    for (size_t d = 0; d < depTokens_.size(); ++d) {
      auto rv = ev.get(depTokens_[d]).const_view().tracks();
      for (int i = 0; i < rv.nTracks(); ++i)
        kept[d][key(rv, i)] += 1;
    }

    run_ = ev.id().run();
    lumi_ = ev.id().luminosityBlock();
    evt_ = ev.id().event();
    nPre_ = av.nTracks();
    for (int i = 0; i < av.nTracks(); ++i) {
      auto const& tr = av[i];
      auto const& cov = tr.covariance();
      auto const& st = tr.state();
      idx_ = i;
      // same order and definitions as FeaturesExtractorKernel
      f_[0] = tr.chi2();
      f_[1] = std::sqrt(cov(14));
      f_[2] = std::sqrt(cov(5));
      f_[3] = tr.eta();
      f_[4] = reco::nHits(av, i);
      f_[5] = st(0);
      f_[6] = std::sqrt(cov(0));
      f_[7] = tr.pt();
      f_[8] = std::sqrt(cov(9));
      f_[9] = st(4);
      f_[10] = st(1);
      f_[11] = tr.nLayers();
      f_[12] = std::sqrt(cov(12));
      f_[13] = cov(13);
      f_[14] = cov(6);
      f_[15] = cov(1);
      f_[16] = cov(2);
      quality_ = static_cast<int>(tr.quality());
      nHits_ = reco::nHits(av, i);
      for (size_t d = 0; d < 2; ++d) {
        dep_[d] = false;
        if (d < kept.size()) {
          auto it = kept[d].find(key(av, i));
          if (it != kept[d].end() && it->second > 0) {
            dep_[d] = true;
            --it->second;
          }
        }
      }
      edm_ = (i < static_cast<int>(ind.size())) ? static_cast<int>(ind[i]) : -9;
      layerMask_ = 0;
      isTrue_ = false;
      share_ = 0.f;
      tpPt_ = tpEta_ = tpVxy_ = tpVz_ = -1.f;
      tpPdg_ = 0;
      tpBx_ = -99;
      tpKey_ = -1;
      tpSig_ = false;
      if (edm_ >= 0 && edm_ < static_cast<int>(trk.size())) {
        auto const& t = trk[edm_];
        std::unordered_map<int, int> nShared;
        int nValid = 0;
        for (auto const* h : t.recHits()) {
          if (!h->isValid())
            continue;
          ++nValid;
          DetId const id = h->geographicalId();
          if (id.subdetId() == PixelSubdetector::PixelBarrel)
            layerMask_ |= 1 << (topo.pxbLayer(id) - 1);
          else if (id.subdetId() == PixelSubdetector::PixelEndcap)
            layerMask_ |= 1 << (barrelBits_ + topo.pxfDisk(id) - 1);
          auto const* ph = dynamic_cast<SiPixelRecHit const*>(h);
          if (ph == nullptr)
            continue;
          auto const& cl = *ph->cluster();
          unsigned long long const k =
              (static_cast<unsigned long long>(id.rawId()) << 32) |
              ((static_cast<unsigned int>(cl.minPixelRow()) << 16) | static_cast<unsigned int>(cl.minPixelCol()));
          auto range = c2tp.equal_range(k);
          for (auto it = range.first; it != range.second; ++it)
            nShared[it->second] += 1;
        }
        int best = -1, nBest = 0;
        for (auto const& [tp, n] : nShared)
          if (n > nBest || (n == nBest && tp < best)) {
            best = tp;
            nBest = n;
          }
        if (best >= 0 && nValid > 0) {
          share_ = float(nBest) / float(nValid);
          isTrue_ = share_ >= cut_;
          tpPt_ = tpPt[best];
          tpEta_ = tpEta[best];
          tpVxy_ = std::hypot(tpVx[best], tpVy[best]);
          tpVz_ = tpVz[best];
          tpPdg_ = tpPdg[best];
          tpBx_ = tpBx[best];
          tpSig_ = tpBx[best] == 0 && tpEv[best] == 0;
          tpKey_ = tpKey[best];
        }
      }
      t_->Fill();
    }
  }

  static constexpr int barrelBits_ = 4;
  const edm::EDGetTokenT<reco::TracksHost> allToken_;
  std::vector<edm::EDGetTokenT<reco::TracksHost>> depTokens_;
  const edm::EDGetTokenT<std::vector<uint32_t>> indToken_;
  const edm::EDGetTokenT<reco::TrackCollection> trkToken_;
  const edm::ESGetToken<TrackerTopology, TrackerTopologyRcd> topoToken_;
  const double cut_;
  edm::EDGetTokenT<std::vector<unsigned int>> pixDetTok_, pixPosTok_;
  edm::EDGetTokenT<std::vector<int>> pixTPTok_, tpPdgTok_, tpBxTok_, tpEvTok_, tpKeyTok_;
  edm::EDGetTokenT<std::vector<float>> tpPtTok_, tpEtaTok_, tpVxTok_, tpVyTok_, tpVzTok_;
  TTree* t_;
  unsigned run_, lumi_;
  unsigned long long evt_;
  int idx_, nPre_, quality_, nHits_, layerMask_, edm_, tpPdg_, tpBx_, tpKey_;
  float f_[17];
  bool dep_[2];
  bool isTrue_, tpSig_;
  float share_, tpPt_, tpEta_, tpVxy_, tpVz_;
};

DEFINE_FWK_MODULE(PixelTupleTorchDump);
