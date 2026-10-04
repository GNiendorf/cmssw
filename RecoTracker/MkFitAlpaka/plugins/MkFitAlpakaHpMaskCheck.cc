// Stage C (round 8, lane stagec): validation of the device HP decision of MkFitAlpakaHpClassifier (instance "hpMask",
// 0/1 per track, host copy) against TrackTorchClassifierFromSoA's rule evaluated on the host on the same scores, and
// against the HP collection size of the threshold module. HPMASK-SUMMARY at endJob. No physics decision is taken here.
#include <atomic>
#include <cmath>
#include <cstdio>

#include "DataFormats/Portable/interface/PortableHostCollection.h"
#include "DataFormats/TrackReco/interface/Track.h"
#include "DataFormats/TrackReco/interface/TrackFwd.h"
#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/Framework/interface/global/EDAnalyzer.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include "RecoTracker/FinalTrackSelectors/interface/TrackTorchClassifierFeaturesSoA.h"

class MkFitAlpakaHpMaskCheck : public edm::global::EDAnalyzer<> {
public:
  using Features = PortableHostCollection<TrackTorchClassifierFeaturesSoA>;
  using Scores = PortableHostCollection<TrackTorchClassifierScoresSoA>;

  explicit MkFitAlpakaHpMaskCheck(edm::ParameterSet const& p)
      : mask_{consumes(p.getParameter<edm::InputTag>("mask"))},
        scores_{consumes(p.getParameter<edm::InputTag>("scores"))},
        features_{consumes(p.getParameter<edm::InputTag>("features"))},
        hp_{consumes(p.getParameter<edm::InputTag>("hp"))},
        minScore_(p.getParameter<double>("minScore")),
        dxyThreshold_(p.getParameter<double>("dxyThreshold")),
        highDxyMinScore_(p.getParameter<double>("highDxyMinScore")) {}

  static void fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
    edm::ParameterSetDescription desc;
    desc.add<edm::InputTag>("mask", edm::InputTag("hltInitialStepTrackTorchClassifier", "hpMask"));
    desc.add<edm::InputTag>("scores", edm::InputTag("hltInitialStepTrackTorchClassifier"));
    desc.add<edm::InputTag>("features", edm::InputTag("hltInitialStepTrackFeatureExtractor"));
    desc.add<edm::InputTag>("hp", edm::InputTag("hltInitialStepTrackTorchClassifierOutput"));
    desc.add<double>("minScore", 0.377);
    desc.add<double>("dxyThreshold", 0.5);
    desc.add<double>("highDxyMinScore", 0.267);
    descriptions.addWithDefaultLabel(desc);
  }

  void analyze(edm::StreamID, edm::Event const& ev, edm::EventSetup const&) const override {
    auto const m = ev.get(mask_).const_view();
    auto const s = ev.get(scores_).const_view();
    auto const f = ev.get(features_).const_view();
    long const nHp = ev.get(hp_).size();
    int const n = s.metadata().size();
    if (m.metadata().size() != n || f.metadata().size() != n) {
      ++sizeMismatch_;
      return;
    }
    long diff = 0, acc = 0;
    for (int i = 0; i < n; ++i) {
      bool const hi = std::abs(f[i].dxyBeamSpot()) > dxyThreshold_;
      bool const pass = s[i].score() >= minScore_ || (hi && s[i].score() >= highDxyMinScore_);
      bool const dev = m[i].score() != 0.f;
      diff += dev != pass;
      acc += dev;
    }
    tracks_ += n;
    diff_ += diff;
    accepted_ += acc;
    hpTracks_ += nHp;
    events_ += 1;
    badHpCount_ += acc != nHp;
  }

  void endJob() override {
    std::printf(
        "HPMASK-SUMMARY events %ld tracks %ld | device mask vs host rule on the same scores: differ %ld | accepted %ld, "
        "HP collection %ld, events with mask count != HP collection size %ld | size mismatches %ld\n",
        events_.load(),
        tracks_.load(),
        diff_.load(),
        accepted_.load(),
        hpTracks_.load(),
        badHpCount_.load(),
        sizeMismatch_.load());
    std::fflush(stdout);
  }

private:
  const edm::EDGetTokenT<Scores> mask_;
  const edm::EDGetTokenT<Scores> scores_;
  const edm::EDGetTokenT<Features> features_;
  const edm::EDGetTokenT<reco::TrackCollection> hp_;
  const double minScore_, dxyThreshold_, highDxyMinScore_;
  mutable std::atomic<long> events_{0}, tracks_{0}, diff_{0}, accepted_{0}, hpTracks_{0}, badHpCount_{0},
      sizeMismatch_{0};
};

DEFINE_FWK_MODULE(MkFitAlpakaHpMaskCheck);
