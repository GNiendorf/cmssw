// MkFitAlpakaLostSeedDump (lane stageb, round 8): the "lost-seed dump" of the LST seed fit study (R7, lead 1).
// Two track collections of the same job (reference = switch OFF, target = ON), the MTV association and the MTV TP
// selection (the hltTrackValidator TP parameters). Per selected TP found by only ONE of the two, one line
//   L <event> <side: 0 ref only, 1 tgt only> <tpPt> <tpEta> <tpPhi> <tpCharge> <seedKey> <other: -1 no track from
//     that seed, 0 a fake track, 1 a track associated to another TP> <trackPt> <trackCharge>
// seedKey = seedRef().key() of the finding track = the row of the seed in the build module's seed table (and in the
// device seed-fit dump), so the line joins the per-seed fit status / class / charge flip of that seed.
#include <algorithm>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>
#include "DataFormats/Common/interface/View.h"
#include "DataFormats/TrackReco/interface/Track.h"
#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/Framework/interface/one/EDAnalyzer.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include "FWCore/Utilities/interface/InputTag.h"
#include "SimDataFormats/Associations/interface/TrackToTrackingParticleAssociator.h"
#include "SimDataFormats/TrackingAnalysis/interface/TrackingParticle.h"
#include "SimTracker/Common/interface/TrackingParticleSelector.h"

class MkFitAlpakaLostSeedDump : public edm::one::EDAnalyzer<> {
public:
  explicit MkFitAlpakaLostSeedDump(edm::ParameterSet const& cfg)
      : refToken_(consumes(cfg.getParameter<edm::InputTag>("reference"))),
        tgtToken_(consumes(cfg.getParameter<edm::InputTag>("target"))),
        tpToken_(consumes(cfg.getParameter<edm::InputTag>("trackingParticles"))),
        assocToken_(consumes(cfg.getParameter<edm::InputTag>("associator"))),
        fileName_(cfg.getParameter<std::string>("fileName")) {
    auto const& p = cfg.getParameter<edm::ParameterSet>("tpSelection");
    tpSel_ = TrackingParticleSelector(p.getParameter<double>("ptMinTP"),
                                      p.getParameter<double>("ptMaxTP"),
                                      p.getParameter<double>("minRapidityTP"),
                                      p.getParameter<double>("maxRapidityTP"),
                                      p.getParameter<double>("tipTP"),
                                      p.getParameter<double>("lipTP"),
                                      p.getParameter<int>("minHitTP"),
                                      p.getParameter<bool>("signalOnlyTP"),
                                      p.getParameter<bool>("intimeOnlyTP"),
                                      p.getParameter<bool>("chargedOnlyTP"),
                                      p.getParameter<bool>("stableOnlyTP"),
                                      p.getParameter<std::vector<int>>("pdgIdTP"),
                                      p.getParameter<bool>("invertRapidityCutTP"),
                                      p.getParameter<double>("minPhi"),
                                      p.getParameter<double>("maxPhi"));
  }
  ~MkFitAlpakaLostSeedDump() override {
    if (fp_)
      std::fclose(fp_);
  }

  static void fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
    edm::ParameterSetDescription desc;
    desc.add<edm::InputTag>("reference", edm::InputTag("lstOffTracks"));
    desc.add<edm::InputTag>("target", edm::InputTag("hltInitialStepTracks"));
    desc.add<edm::InputTag>("trackingParticles", edm::InputTag("mix", "MergedTrackTruth"));
    desc.add<edm::InputTag>("associator", edm::InputTag("hltTrackAssociatorByHits"));
    desc.add<std::string>("fileName", "lostseeds.txt");
    edm::ParameterSetDescription sel;  // = MultiTrackValidator TP selection parameters
    sel.add<double>("ptMinTP", 0.9);
    sel.add<double>("ptMaxTP", 1e100);
    sel.add<double>("minRapidityTP", -2.5);
    sel.add<double>("maxRapidityTP", 2.5);
    sel.add<double>("tipTP", 3.5);
    sel.add<double>("lipTP", 30.);
    sel.add<int>("minHitTP", 0);
    sel.add<bool>("signalOnlyTP", true);
    sel.add<bool>("intimeOnlyTP", false);
    sel.add<bool>("chargedOnlyTP", true);
    sel.add<bool>("stableOnlyTP", false);
    sel.add<std::vector<int>>("pdgIdTP", {});
    sel.add<bool>("invertRapidityCutTP", false);
    sel.add<double>("minPhi", -3.2);
    sel.add<double>("maxPhi", 3.2);
    desc.add<edm::ParameterSetDescription>("tpSelection", sel);
    descriptions.addWithDefaultLabel(desc);
  }

  void analyze(edm::Event const& ev, edm::EventSetup const&) override {
    auto hTgt = ev.getHandle(tgtToken_);  // target first: skip events without tracking
    if (!hTgt.isValid())
      return;
    auto hRef = ev.getHandle(refToken_);
    auto hTP = ev.getHandle(tpToken_);
    auto const& assoc = ev.get(assocToken_);
    auto s2rRef = assoc.associateSimToReco(hRef, hTP);
    auto s2rTgt = assoc.associateSimToReco(hTgt, hTP);
    auto r2sRef = assoc.associateRecoToSim(hRef, hTP);
    auto r2sTgt = assoc.associateRecoToSim(hTgt, hTP);
    // per collection: seed key -> track indices
    auto bySeed = [](auto const& h) {
      std::unordered_map<unsigned, std::vector<int>> m;
      for (size_t i = 0; i < h->size(); ++i)
        m[(*h)[i].seedRef().key()].push_back(int(i));
      return m;
    };
    const auto refBySeed = bySeed(hRef), tgtBySeed = bySeed(hTgt);
    if (!fp_)
      fp_ = std::fopen(fileName_.c_str(), "w");
    if (!fp_)
      return;
    for (size_t i = 0; i < hTP->size(); ++i) {
      TrackingParticleRef tp(hTP, i);
      if (!tpSel_(*tp))
        continue;
      auto ir = s2rRef.find(tp);
      auto itg = s2rTgt.find(tp);
      const bool fr = ir != s2rRef.end() && !ir->val.empty();
      const bool ft = itg != s2rTgt.end() && !itg->val.empty();
      if (fr == ft)
        continue;
      const int side = fr ? 0 : 1;
      auto const& trk = fr ? ir->val.front().first : itg->val.front().first;
      const unsigned key = trk->seedRef().key();
      // the other collection's track(s) from the same seed: -1 none, 0 fake, 1 associated (to another TP)
      int other = -1;
      auto const& otherMap = fr ? tgtBySeed : refBySeed;
      if (auto it = otherMap.find(key); it != otherMap.end())
        for (int j : it->second) {
          const bool tr = fr ? (r2sTgt.find(hTgt->refAt(j)) != r2sTgt.end())
                             : (r2sRef.find(hRef->refAt(j)) != r2sRef.end());
          other = std::max(other, tr ? 1 : 0);
        }
      std::fprintf(fp_,
                   "L %llu %d %.5g %.5g %.5g %d %u %d %.5g %d\n",
                   ev.id().event(),
                   side,
                   tp->pt(),
                   tp->eta(),
                   tp->phi(),
                   int(tp->charge()),
                   key,
                   other,
                   trk->pt(),
                   trk->charge());
    }
  }

private:
  const edm::EDGetTokenT<edm::View<reco::Track>> refToken_, tgtToken_;
  const edm::EDGetTokenT<TrackingParticleCollection> tpToken_;
  const edm::EDGetTokenT<reco::TrackToTrackingParticleAssociator> assocToken_;
  const std::string fileName_;
  TrackingParticleSelector tpSel_;
  std::FILE* fp_ = nullptr;
};

DEFINE_FWK_MODULE(MkFitAlpakaLostSeedDump);
