// Lane fit (round 6): compares the per-hit smoothed states (trackreco#186 storeHitStates) of two MkFitOutputWrapper
// products, e.g. the stock MkFitFitProducer and the device fit on the same input tracks. Per track (same index) and
// HitOnTrack position: validity, kind, pz sign, chi2 and |pull| of the five local parameters (pull = |tgt - ref| /
// sigma_ref). Printed at endJob.

#include <cmath>
#include <cstdio>
#include <string>

#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/Framework/interface/one/EDAnalyzer.h"
#include "FWCore/MessageLogger/interface/MessageLogger.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include "RecoTracker/MkFit/interface/MkFitOutputWrapper.h"

class MkFitAlpakaHitStatesCheck : public edm::one::EDAnalyzer<> {
public:
  explicit MkFitAlpakaHitStatesCheck(edm::ParameterSet const& cfg)
      : refToken_{consumes(cfg.getParameter<edm::InputTag>("reference"))},
        tgtToken_{consumes(cfg.getParameter<edm::InputTag>("target"))},
        label_{cfg.getParameter<std::string>("@module_label")} {}

  static void fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
    edm::ParameterSetDescription desc;
    desc.add<edm::InputTag>("reference", edm::InputTag("hltInitialStepTrackCandidatesMkFitFit"));
    desc.add<edm::InputTag>("target", edm::InputTag("devFit"));
    descriptions.addWithDefaultLabel(desc);
  }

  void analyze(edm::Event const& ev, edm::EventSetup const&) override {
    auto const& r = ev.get(refToken_);
    auto const& t = ev.get(tgtToken_);
    ++nEvents_;
    if (r.hitStates().empty() && !r.tracks().empty())
      ++nRefNoStates_;
    if (t.hitStates().empty() && !t.tracks().empty())
      ++nTgtNoStates_;
    if (r.hitStates().size() != t.hitStates().size()) {
      ++nTrackCountDiff_;
      return;
    }
    for (size_t i = 0; i < r.hitStates().size(); ++i) {
      auto const& a = r.hitStates()[i];
      auto const& b = t.hitStates()[i];
      ++nTracks_;
      if (a.size() != b.size()) {
        ++nSizeDiff_;
        continue;
      }
      for (size_t p = 0; p < a.size(); ++p) {
        if (!a[p].valid && !b[p].valid)
          continue;
        if (a[p].valid != b[p].valid) {
          (a[p].valid ? nRefOnly_ : nTgtOnly_)++;
          continue;
        }
        ++nBoth_;
        nKindDiff_ += a[p].kind != b[p].kind;
        nPzDiff_ += a[p].pzSign != b[p].pzSign;
        double maxPull = 0;
        for (int k = 0; k < 5; ++k) {
          const double s = std::sqrt(double(a[p].err[k * (k + 3) / 2]));
          const double pull = s > 0 ? std::abs(double(b[p].par[k]) - a[p].par[k]) / s : 0.;
          pullB_[k][bucket(pull)]++;
          maxPull = std::max(maxPull, std::isfinite(pull) ? pull : 1e30);
        }
        const double rs = std::abs(double(b[p].err[0]) / a[p].err[0] - 1.);
        sigB_[bucket(rs)]++;
        chi2B_[bucket(std::abs(double(b[p].chi2) - a[p].chi2) / std::max(1., double(a[p].chi2)))]++;
        maxPullAll_ = std::max(maxPullAll_, maxPull);
      }
    }
  }

  void endJob() override {
    std::string o;
    char buf[512];
    std::snprintf(buf,
                  sizeof(buf),
                  "[hitstates %s] events %d tracks %ld (ref without states %d, tgt without states %d, track-count "
                  "differs %d, per-track size differs %ld)\n",
                  label_.c_str(),
                  nEvents_,
                  nTracks_,
                  nRefNoStates_,
                  nTgtNoStates_,
                  nTrackCountDiff_,
                  nSizeDiff_);
    o += buf;
    std::snprintf(buf,
                  sizeof(buf),
                  "[hitstates %s] states valid in both %ld, ref only %ld, tgt only %ld, kind differs %ld, pz sign differs "
                  "%ld, max pull %g\n",
                  label_.c_str(),
                  nBoth_,
                  nRefOnly_,
                  nTgtOnly_,
                  nKindDiff_,
                  nPzDiff_,
                  maxPullAll_);
    o += buf;
    o += "[hitstates " + label_ + "] |pull| buckets: ==0 <1e-6 <1e-5 <1e-4 <1e-3 <1e-2 <0.1 <1 >=1\n";
    const char* names[5] = {"q/p", "dxdz", "dydz", "x", "y"};
    for (int k = 0; k < 5; ++k) {
      o += "[hitstates " + label_ + "]   " + names[k] + ":";
      for (int b = 0; b < 9; ++b)
        o += " " + std::to_string(pullB_[k][b]);
      o += "\n";
    }
    o += "[hitstates " + label_ + "]   |sigma(q/p) ratio - 1|:";
    for (int b = 0; b < 9; ++b)
      o += " " + std::to_string(sigB_[b]);
    o += "\n[hitstates " + label_ + "]   |dchi2|/max(1,chi2):";
    for (int b = 0; b < 9; ++b)
      o += " " + std::to_string(chi2B_[b]);
    o += "\n";
    edm::LogSystem("MkFitAlpakaHitStatesCheck") << o;
  }

private:
  static int bucket(double a) {
    if (!(a == a))
      return 8;
    if (a == 0)
      return 0;
    const double e[7] = {1e-6, 1e-5, 1e-4, 1e-3, 1e-2, 0.1, 1};
    for (int i = 0; i < 7; ++i)
      if (a < e[i])
        return i + 1;
    return 8;
  }

  const edm::EDGetTokenT<MkFitOutputWrapper> refToken_, tgtToken_;
  const std::string label_;
  int nEvents_ = 0, nRefNoStates_ = 0, nTgtNoStates_ = 0, nTrackCountDiff_ = 0;
  long nTracks_ = 0, nSizeDiff_ = 0, nBoth_ = 0, nRefOnly_ = 0, nTgtOnly_ = 0, nKindDiff_ = 0, nPzDiff_ = 0;
  long pullB_[5][9] = {}, sigB_[9] = {}, chi2B_[9] = {};
  double maxPullAll_ = 0;
};

DEFINE_FWK_MODULE(MkFitAlpakaHitStatesCheck);
