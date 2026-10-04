// MkFitAlpakaHpCompare (round 7, lane hpsel): validation of the device HP selection against the stock one, in the
// same job on the same tracks.
// - scores: per-track |score_test - score_ref| distribution (TrackTorchClassifierFromSoA "MVAScores" of both arms);
// - decisions: the TrackTorchClassifierFromSoA rule (score >= minScore, or |dxy| > dxyThreshold and score >=
//   highDxyMinScore) evaluated on both score sets; every flip is printed with its distance to the threshold;
// - HP collections: size of both TrackTorchClassifierFromSoA outputs (= decisions) and of both final HP collections;
// - features (optional, featuresTest set): per-column bitwise / relative agreement of two feature SoAs.
// - dump (optional, dumpFile set): per track 19 float32 (event, track index, the 15 features, score ref, score test),
//   + the 15 test features when featuresTest is set (NaN for events with unequal row counts)
//   for offline model studies (feature ablations with the same model.pt).
// One "HPCMP" line per event, a summary at endJob. No physics decision is taken here.

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <limits>
#include <mutex>
#include <string>
#include <vector>

#include "DataFormats/Portable/interface/PortableHostCollection.h"
#include "DataFormats/TrackReco/interface/Track.h"
#include "DataFormats/TrackReco/interface/TrackFwd.h"
#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/Framework/interface/global/EDAnalyzer.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include "FWCore/Utilities/interface/Exception.h"
#include "FWCore/Utilities/interface/InputTag.h"
#include "RecoTracker/FinalTrackSelectors/interface/TrackTorchClassifierFeaturesSoA.h"

class MkFitAlpakaHpCompare : public edm::global::EDAnalyzer<> {
public:
  using Features = PortableHostCollection<TrackTorchClassifierFeaturesSoA>;
  static constexpr int kNCols = 15;
  static constexpr int kNBins = 6;  // |d| > 0, 1e-7, 1e-6, 1e-5, 1e-4, 1e-3

  explicit MkFitAlpakaHpCompare(edm::ParameterSet const& p)
      : scoresRef_{consumes(p.getParameter<edm::InputTag>("scoresRef"))},
        scoresTest_{consumes(p.getParameter<edm::InputTag>("scoresTest"))},
        features_{consumes(p.getParameter<edm::InputTag>("features"))},
        hpRef_{consumes(p.getParameter<edm::InputTag>("hpRef"))},
        hpTest_{consumes(p.getParameter<edm::InputTag>("hpTest"))},
        minScore_(p.getParameter<double>("minScore")),
        dxyThreshold_(p.getParameter<double>("dxyThreshold")),
        highDxyMinScore_(p.getParameter<double>("highDxyMinScore")),
        verbose_(p.getParameter<bool>("verbose")),
        dumpFile_(p.getParameter<std::string>("dumpFile")) {
    if (!dumpFile_.empty()) {
      dump_ = std::fopen(dumpFile_.c_str(), "wb");
      if (dump_ == nullptr)
        throw cms::Exception("Configuration") << "MkFitAlpakaHpCompare: cannot open " << dumpFile_;
    }
    auto const ft = p.getParameter<edm::InputTag>("featuresTest");
    if (!ft.label().empty())
      featuresTest_ = consumes(ft);
    auto const fr = p.getParameter<edm::InputTag>("featuresRef");
    if (!fr.label().empty())
      featuresRef_ = consumes(fr);
  }

  static void fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
    edm::ParameterSetDescription desc;
    desc.add<edm::InputTag>("scoresRef", edm::InputTag("hltInitialStepTrackTorchClassifierOutput", "MVAScores"));
    desc.add<edm::InputTag>("scoresTest", edm::InputTag("hltInitialStepTrackTorchClassifierOutputDev", "MVAScores"));
    desc.add<edm::InputTag>("features", edm::InputTag("hltInitialStepTrackFeatureExtractor"));
    desc.add<edm::InputTag>("hpRef", edm::InputTag("hltInitialStepTrackTorchClassifierOutput"));
    desc.add<edm::InputTag>("hpTest", edm::InputTag("hltInitialStepTrackTorchClassifierOutputDev"));
    desc.add<edm::InputTag>("featuresRef", edm::InputTag(""));
    desc.add<edm::InputTag>("featuresTest", edm::InputTag(""));
    desc.add<double>("minScore", 0.377);
    desc.add<double>("dxyThreshold", 0.5);
    desc.add<double>("highDxyMinScore", 0.267);
    desc.add<bool>("verbose", true);
    desc.add<std::string>("dumpFile", "");
    descriptions.addWithDefaultLabel(desc);
  }

  void analyze(edm::StreamID, edm::Event const& ev, edm::EventSetup const&) const override {
    auto const& sr = ev.get(scoresRef_);
    auto const& st = ev.get(scoresTest_);
    auto const& fe = ev.get(features_);
    auto const fv = fe.const_view();
    size_t const n = sr.size();
    long bins[kNBins] = {0, 0, 0, 0, 0, 0};
    long flips = 0, accRef = 0, accTest = 0, sizeMismatch = 0;
    double maxd = 0., maxFlipDist = 0.;
    if (st.size() != n || static_cast<size_t>(fv.metadata().size()) != n)
      sizeMismatch = 1;
    size_t const m = std::min({n, st.size(), static_cast<size_t>(fv.metadata().size())});
    for (size_t i = 0; i < m; ++i) {
      double const d = std::abs(double(st[i]) - double(sr[i]));
      maxd = std::max(maxd, d);
      double const edges[kNBins] = {0., 1e-7, 1e-6, 1e-5, 1e-4, 1e-3};
      for (int b = 0; b < kNBins; ++b)
        if (d > edges[b])
          ++bins[b];
      bool const hiDxy = std::abs(fv[i].dxyBeamSpot()) > dxyThreshold_;
      float const thr = hiDxy ? std::min(minScore_, highDxyMinScore_) : minScore_;
      bool const pr = sr[i] >= minScore_ || (hiDxy && sr[i] >= highDxyMinScore_);
      bool const pt = st[i] >= minScore_ || (hiDxy && st[i] >= highDxyMinScore_);
      accRef += pr;
      accTest += pt;
      if (pr != pt) {
        ++flips;
        double const dist = std::max(std::abs(double(sr[i]) - thr), std::abs(double(st[i]) - thr));
        maxFlipDist = std::max(maxFlipDist, dist);
        std::lock_guard<std::mutex> g(mtx_);
        std::printf("HPFLIP run %u ev %llu trk %zu ref %.9g test %.9g thr %.4g dxy %.4g\n",
                    ev.id().run(),
                    static_cast<unsigned long long>(ev.id().event()),
                    i,
                    sr[i],
                    st[i],
                    thr,
                    fv[i].dxyBeamSpot());
      }
    }
    size_t const nhr = ev.get(hpRef_).size(), nht = ev.get(hpTest_).size();
    bool const hpCountsOk = (nhr == size_t(accRef)) && (nht == size_t(accTest));

    // optional per-column feature comparison (only events with equal row counts: rows are then aligned)
    long colDiff[kNCols] = {0}, colRel5[kNCols] = {0}, colRel3[kNCols] = {0};
    double colMaxRel[kNCols] = {0.}, colMaxAbs[kNCols] = {0.};
    bool haveFeat = !featuresTest_.isUninitialized();
    long featSizeMismatch = 0;
    std::vector<float> testRows;
    if (haveFeat) {
      auto const& fa = featuresRef_.isUninitialized() ? fe : ev.get(featuresRef_);
      auto const& fb = ev.get(featuresTest_);
      auto const a = fa.const_view();
      auto const b = fb.const_view();
      int mm = std::min(a.metadata().size(), b.metadata().size());
      if (a.metadata().size() != b.metadata().size()) {
        featSizeMismatch = 1;
        mm = 0;
      }
      testRows.assign(m * kNCols, std::numeric_limits<float>::quiet_NaN());
      for (int i = 0; i < mm; ++i) {
        float const va[kNCols] = {a[i].dxyBeamSpot(),
                                  a[i].dzBeamSpot(),
                                  a[i].dxyError(),
                                  a[i].dzError(),
                                  a[i].normalizedChi2(),
                                  a[i].eta(),
                                  a[i].phi(),
                                  a[i].etaError(),
                                  a[i].phiError(),
                                  a[i].ndof(),
                                  a[i].lostInnerHits(),
                                  a[i].lostOuterHits(),
                                  a[i].layersWithoutMeas(),
                                  a[i].validPixelHits(),
                                  a[i].validStripHits()};
        float const vb[kNCols] = {b[i].dxyBeamSpot(),
                                  b[i].dzBeamSpot(),
                                  b[i].dxyError(),
                                  b[i].dzError(),
                                  b[i].normalizedChi2(),
                                  b[i].eta(),
                                  b[i].phi(),
                                  b[i].etaError(),
                                  b[i].phiError(),
                                  b[i].ndof(),
                                  b[i].lostInnerHits(),
                                  b[i].lostOuterHits(),
                                  b[i].layersWithoutMeas(),
                                  b[i].validPixelHits(),
                                  b[i].validStripHits()};
        for (int c = 0; c < kNCols; ++c) {
          if (size_t(i) < m)
            testRows[i * kNCols + c] = vb[c];
          if (!(va[c] == vb[c])) {
            ++colDiff[c];
            double const ad = std::abs(double(vb[c]) - double(va[c]));
            double const rel = ad / std::max(1e-30, std::abs(double(va[c])));
            if (!(rel <= 1e-5))
              ++colRel5[c];
            if (!(rel <= 1e-3))
              ++colRel3[c];
            if (std::isfinite(rel))
              colMaxRel[c] = std::max(colMaxRel[c], rel);
            if (std::isfinite(ad))
              colMaxAbs[c] = std::max(colMaxAbs[c], ad);
          }
        }
      }
      featRows_ += mm;
    }

    if (dump_ != nullptr) {
      int const nrec = haveFeat ? 19 + kNCols : 19;
      std::vector<float> rec;
      rec.reserve(m * nrec);
      for (size_t i = 0; i < m; ++i) {
        float const r[19] = {float(ev.id().event()),
                             float(i),
                             fv[i].dxyBeamSpot(),
                             fv[i].dzBeamSpot(),
                             fv[i].dxyError(),
                             fv[i].dzError(),
                             fv[i].normalizedChi2(),
                             fv[i].eta(),
                             fv[i].phi(),
                             fv[i].etaError(),
                             fv[i].phiError(),
                             fv[i].ndof(),
                             fv[i].lostInnerHits(),
                             fv[i].lostOuterHits(),
                             fv[i].layersWithoutMeas(),
                             fv[i].validPixelHits(),
                             fv[i].validStripHits(),
                             sr[i],
                             st[i]};
        rec.insert(rec.end(), r, r + 19);
        if (haveFeat)
          rec.insert(rec.end(), testRows.begin() + i * kNCols, testRows.begin() + (i + 1) * kNCols);
      }
      std::lock_guard<std::mutex> g(mtx_);
      std::fwrite(rec.data(), sizeof(float), rec.size(), dump_);
    }
    {
      std::lock_guard<std::mutex> g(mtx_);
      if (verbose_)
        std::printf(
            "HPCMP run %u ev %llu n %zu d>0 %ld d>1e-6 %ld d>1e-5 %ld d>1e-4 %ld maxd %.3g accRef %ld accTest %ld "
            "flips %ld hpRef %zu hpTest %zu countsOk %d sizeMismatch %ld\n",
            ev.id().run(),
            static_cast<unsigned long long>(ev.id().event()),
            n,
            bins[0],
            bins[2],
            bins[3],
            bins[4],
            maxd,
            accRef,
            accTest,
            flips,
            nhr,
            nht,
            int(hpCountsOk),
            sizeMismatch);
      events_ += 1;
      tracks_ += m;
      for (int b = 0; b < kNBins; ++b)
        binsTot_[b] += bins[b];
      flipsTot_ += flips;
      accRefTot_ += accRef;
      accTestTot_ += accTest;
      hpRefTot_ += nhr;
      hpTestTot_ += nht;
      badCounts_ += !hpCountsOk;
      sizeMismatchTot_ += sizeMismatch;
      maxdTot_ = std::max(maxdTot_, maxd);
      maxFlipDistTot_ = std::max(maxFlipDistTot_, maxFlipDist);
      featEventsSkipped_ += featSizeMismatch;
      if (haveFeat)
        for (int c = 0; c < kNCols; ++c) {
          colDiffTot_[c] += colDiff[c];
          colRel5Tot_[c] += colRel5[c];
          colRel3Tot_[c] += colRel3[c];
          colMaxRelTot_[c] = std::max(colMaxRelTot_[c], colMaxRel[c]);
          colMaxAbsTot_[c] = std::max(colMaxAbsTot_[c], colMaxAbs[c]);
        }
    }
  }

  void endJob() override {
    std::printf(
        "HPCMP-SUMMARY events %ld tracks %ld | |dscore| >0 %ld >1e-7 %ld >1e-6 %ld >1e-5 %ld >1e-4 %ld >1e-3 %ld "
        "max %.3g | accepted ref %ld test %ld flips %ld (max distance of a flip to its threshold %.3g) | HP outputs "
        "ref %ld test %ld | events with HP count != decisions %ld | size mismatches %ld\n",
        events_,
        tracks_,
        binsTot_[0],
        binsTot_[1],
        binsTot_[2],
        binsTot_[3],
        binsTot_[4],
        binsTot_[5],
        maxdTot_,
        accRefTot_,
        accTestTot_,
        flipsTot_,
        maxFlipDistTot_,
        hpRefTot_,
        hpTestTot_,
        badCounts_,
        sizeMismatchTot_);
    if (!featuresTest_.isUninitialized()) {
      static const char* names[kNCols] = {"dxyBeamSpot",
                                          "dzBeamSpot",
                                          "dxyError",
                                          "dzError",
                                          "normalizedChi2",
                                          "eta",
                                          "phi",
                                          "etaError",
                                          "phiError",
                                          "ndof",
                                          "lostInnerHits",
                                          "lostOuterHits",
                                          "layersWithoutMeas",
                                          "validPixelHits",
                                          "validStripHits"};
      std::printf("HPCMP-FEATURES rows compared %ld (events skipped for unequal row counts: %ld)\n",
                  featRows_,
                  featEventsSkipped_);
      for (int c = 0; c < kNCols; ++c)
        std::printf("HPCMP-FEATURE %-18s differ %ld rel>1e-5 %ld rel>1e-3 %ld maxrel %.3g maxabs %.3g\n",
                    names[c],
                    colDiffTot_[c],
                    colRel5Tot_[c],
                    colRel3Tot_[c],
                    colMaxRelTot_[c],
                    colMaxAbsTot_[c]);
    }
    std::fflush(stdout);
    if (dump_ != nullptr) {
      std::fclose(dump_);
      dump_ = nullptr;
    }
  }

private:
  const edm::EDGetTokenT<std::vector<float>> scoresRef_;
  const edm::EDGetTokenT<std::vector<float>> scoresTest_;
  const edm::EDGetTokenT<Features> features_;
  const edm::EDGetTokenT<reco::TrackCollection> hpRef_;
  const edm::EDGetTokenT<reco::TrackCollection> hpTest_;
  edm::EDGetTokenT<Features> featuresRef_;
  edm::EDGetTokenT<Features> featuresTest_;
  const double minScore_, dxyThreshold_, highDxyMinScore_;
  const bool verbose_;
  const std::string dumpFile_;
  std::FILE* dump_ = nullptr;

  mutable std::mutex mtx_;
  mutable long events_ = 0, tracks_ = 0, flipsTot_ = 0, accRefTot_ = 0, accTestTot_ = 0, hpRefTot_ = 0,
               hpTestTot_ = 0, badCounts_ = 0, sizeMismatchTot_ = 0, featRows_ = 0;
  mutable long binsTot_[kNBins] = {0, 0, 0, 0, 0, 0};
  mutable double maxdTot_ = 0., maxFlipDistTot_ = 0.;
  mutable long colDiffTot_[kNCols] = {0}, colRel5Tot_[kNCols] = {0}, colRel3Tot_[kNCols] = {0}, featEventsSkipped_ = 0;
  mutable double colMaxRelTot_[kNCols] = {0.}, colMaxAbsTot_[kNCols] = {0.};
};

DEFINE_FWK_MODULE(MkFitAlpakaHpCompare);
