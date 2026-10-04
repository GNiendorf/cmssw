// MkFitAlpakaPairedTruthCompare (harness lane): PAIRED truth comparison of two track collections of the same events
// (stock vs port, or stock x86-64-v3 vs x86-64-v2), with the MTV association (hltTrackAssociatorByHits) and the MTV
// TrackingParticle selection (pass the hltTrackValidator TP parameters).
//   efficiency: per selected TP, found by reference / by target -> n11 n10 n01 n00; eff_ref, eff_tgt and the paired
//               difference (n01 - n10) / nTP with the McNemar error sqrt(n01 + n10) / nTP
//   fake:       per track paired by seed index: associated (true) or not (fake) in ref / tgt -> t11 t10 t01 t00,
//               plus unpaired tracks; fake rates as MTV (1 - nAssoc / nReco)
//   duplicate:  tracks whose associated TP has > 1 associated tracks (MTV num_duplicate) / nReco
// Summary lines "[paired <label>]" at endJob.
#include <cmath>
#include <cstdio>
#include <iostream>
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

class MkFitAlpakaPairedTruthCompare : public edm::one::EDAnalyzer<> {
public:
  explicit MkFitAlpakaPairedTruthCompare(edm::ParameterSet const& cfg)
      : refToken_(consumes(cfg.getParameter<edm::InputTag>("reference"))),
        tgtToken_(consumes(cfg.getParameter<edm::InputTag>("target"))),
        tpToken_(consumes(cfg.getParameter<edm::InputTag>("trackingParticles"))),
        assocToken_(consumes(cfg.getParameter<edm::InputTag>("associator"))),
        label_(cfg.getParameter<std::string>("label")) {
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

  static void fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
    edm::ParameterSetDescription desc;
    desc.add<edm::InputTag>("reference", edm::InputTag("hltInitialStepTracksMkFitFit", "", "REPLAY"));
    desc.add<edm::InputTag>("target", edm::InputTag("hltInitialStepTracksMkFitFit"));
    desc.add<edm::InputTag>("trackingParticles", edm::InputTag("mix", "MergedTrackTruth"));
    desc.add<edm::InputTag>("associator", edm::InputTag("hltTrackAssociatorByHits"));
    desc.add<std::string>("label", "paired");
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
    auto hTgt = ev.getHandle(tgtToken_);  // target first (see TrackCollectionCompare): skip events without tracking
    if (!hTgt.isValid()) {
      ++nEvMissing_;
      return;
    }
    auto hRef = ev.getHandle(refToken_);
    auto hTP = ev.getHandle(tpToken_);
    auto const& assoc = ev.get(assocToken_);
    ++nEv_;
    auto s2rRef = assoc.associateSimToReco(hRef, hTP);
    auto s2rTgt = assoc.associateSimToReco(hTgt, hTP);
    auto r2sRef = assoc.associateRecoToSim(hRef, hTP);
    auto r2sTgt = assoc.associateRecoToSim(hTgt, hTP);

    // efficiency, paired per selected TP
    for (size_t i = 0; i < hTP->size(); ++i) {
      TrackingParticleRef tp(hTP, i);
      if (!tpSel_(*tp))
        continue;
      auto ir = s2rRef.find(tp);
      auto itg = s2rTgt.find(tp);
      const bool fr = ir != s2rRef.end() && !ir->val.empty();
      const bool ft = itg != s2rTgt.end() && !itg->val.empty();
      ++e_[fr][ft];
    }
    // per track: associated? duplicate? (MTV definitions); paired by seed index
    auto classify =
        [](auto const& h, auto const& r2s, auto const& s2r, std::vector<char>& isTrue, std::vector<char>& isDup) {
          isTrue.assign(h->size(), 0);
          isDup.assign(h->size(), 0);
          for (size_t i = 0; i < h->size(); ++i) {
            auto it = r2s.find(h->refAt(i));
            if (it == r2s.end() || it->val.empty())
              continue;
            isTrue[i] = 1;
            auto const& tp = it->val.front().first;
            auto jt = s2r.find(tp);
            isDup[i] = jt != s2r.end() && jt->val.size() > 1;
          }
        };
    std::vector<char> tRef, dRef, tTgt, dTgt;
    classify(hRef, r2sRef, s2rRef, tRef, dRef);
    classify(hTgt, r2sTgt, s2rTgt, tTgt, dTgt);
    std::unordered_map<unsigned, std::vector<int>> tgtBySeed;
    for (size_t i = 0; i < hTgt->size(); ++i)
      tgtBySeed[(*hTgt)[i].seedRef().key()].push_back(int(i));
    std::vector<char> used(hTgt->size(), 0);
    for (size_t i = 0; i < hRef->size(); ++i) {
      nRef_++;
      nTrueRef_ += tRef[i];
      nDupRef_ += dRef[i];
      int j = -1;
      auto it = tgtBySeed.find((*hRef)[i].seedRef().key());
      if (it != tgtBySeed.end())
        for (int c : it->second)
          if (!used[c]) {
            j = c;
            break;
          }
      if (j < 0) {
        ++refOnly_[tRef[i]];
        continue;
      }
      used[j] = 1;
      ++t_[tRef[i]][tTgt[j]];
      ++d_[dRef[i]][dTgt[j]];
    }
    for (size_t j = 0; j < hTgt->size(); ++j) {
      nTgt_++;
      nTrueTgt_ += tTgt[j];
      nDupTgt_ += dTgt[j];
      if (!used[j])
        ++tgtOnly_[tTgt[j]];
    }
  }

  void endJob() override {
    const double nTP = e_[0][0] + e_[0][1] + e_[1][0] + e_[1][1];
    auto pr = [&](const char* fmt, auto... a) {
      char b[512];
      snprintf(b, sizeof(b), fmt, a...);
      std::cout << "[paired " << label_ << "] " << b << std::endl;
    };
    pr("events %ld (skipped without target %ld), selected TPs %.0f", nEv_, nEvMissing_, nTP);
    pr("efficiency: ref %.5f tgt %.5f; TPs found by both %ld, ref only %ld, tgt only %ld, neither %ld; paired "
       "difference (tgt-ref) %+.5f +- %.5f",
       (e_[1][0] + e_[1][1]) / nTP,
       (e_[0][1] + e_[1][1]) / nTP,
       e_[1][1],
       e_[1][0],
       e_[0][1],
       e_[0][0],
       (e_[0][1] - e_[1][0]) / nTP,
       std::sqrt(double(e_[0][1] + e_[1][0])) / nTP);
    pr("tracks: ref %ld tgt %ld; fake rate ref %.5f tgt %.5f; duplicate rate ref %.5f tgt %.5f",
       nRef_,
       nTgt_,
       1. - double(nTrueRef_) / nRef_,
       1. - double(nTrueTgt_) / nTgt_,
       double(nDupRef_) / nRef_,
       double(nDupTgt_) / nTgt_);
    pr("seed-paired tracks: true/true %ld, true->fake %ld, fake->true %ld, fake/fake %ld; ref-only (true %ld, fake "
       "%ld), "
       "tgt-only (true %ld, fake %ld); duplicate flag: ref only %ld, tgt only %ld",
       t_[1][1],
       t_[1][0],
       t_[0][1],
       t_[0][0],
       refOnly_[1],
       refOnly_[0],
       tgtOnly_[1],
       tgtOnly_[0],
       d_[1][0],
       d_[0][1]);
  }

private:
  const edm::EDGetTokenT<edm::View<reco::Track>> refToken_, tgtToken_;
  const edm::EDGetTokenT<TrackingParticleCollection> tpToken_;
  const edm::EDGetTokenT<reco::TrackToTrackingParticleAssociator> assocToken_;
  std::string label_;
  TrackingParticleSelector tpSel_;
  long nEv_ = 0, nEvMissing_ = 0, e_[2][2] = {}, t_[2][2] = {}, d_[2][2] = {}, refOnly_[2] = {}, tgtOnly_[2] = {};
  long nRef_ = 0, nTgt_ = 0, nTrueRef_ = 0, nTrueTgt_ = 0, nDupRef_ = 0, nDupTgt_ = 0;
};

DEFINE_FWK_MODULE(MkFitAlpakaPairedTruthCompare);
