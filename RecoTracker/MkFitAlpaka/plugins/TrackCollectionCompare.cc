// Generic per-seed comparator of two TrackCandidateCollections, two reco::Track collections, or two mkFit-level
// track vectors (MkFitOutputWrapper: candidates / fit output; MkFitSeedWrapper: seeds) (harness lane).
// Tracks are paired by seed reference (fallback: collection index); per pair it checks identical hit lists
// (detId, hit type, cluster product + index) and the parameter differences in units of the reference errors.
// A summary is printed at endJob (lines prefixed "[compare <label>]") and optionally written as JSON.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "DataFormats/Common/interface/View.h"
#include "DataFormats/TrackCandidate/interface/TrackCandidate.h"
#include "DataFormats/TrackCandidate/interface/TrackCandidateCollection.h"
#include "DataFormats/TrackReco/interface/Track.h"
#include "DataFormats/TrackerRecHit2D/interface/BaseTrackerRecHit.h"
#include "DataFormats/TrackerRecHit2D/interface/OmniClusterRef.h"
#include "DataFormats/TrackingRecHit/interface/TrackingRecHit.h"
#include "FWCore/Framework/interface/Event.h"
#include "RecoTracker/MkFit/interface/MkFitOutputWrapper.h"
#include "RecoTracker/MkFit/interface/MkFitSeedWrapper.h"
#include "RecoTracker/MkFitCore/interface/Track.h"
#include "RecoTracker/MkFitAlpaka/interface/tracks/TrackSoAHostCollection.h"
#include "RecoTracker/MkFitAlpaka/interface/tracks/TrackSoAMkFitConversion.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/Framework/interface/one/EDAnalyzer.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include "FWCore/Utilities/interface/Exception.h"
#include "FWCore/Utilities/interface/InputTag.h"

namespace mkfitdev_harness {

  struct HitKey {
    uint32_t det = 0;
    uint32_t clu = 0xffffffff;  // OmniClusterRef raw index (incl. pixel/phase2 flag bits), or none
    uint32_t prod = 0;          // cluster ProductID (process index << 16 | product index)
    uint8_t type = 0;           // TrackingRecHit::Type
    bool operator==(const HitKey& o) const { return det == o.det && clu == o.clu && prod == o.prod && type == o.type; }
  };

  struct TrackInfo {
    int index = -1;
    bool hasSeed = false;
    uint64_t seedKey = 0;
    std::vector<HitKey> hits;
    int nValid = 0;
    uint32_t stateDet = 0;  // detId of the candidate state (0 for reco::Track)
    bool hasErr = false;
    std::array<double, 6> par{};
    std::array<double, 6> sig{};
    double chi2 = 0, ndof = 0;
  };

  inline void addHit(TrackingRecHit const& h, TrackInfo& t) {
    HitKey k;
    k.det = h.geographicalId().rawId();
    k.type = static_cast<uint8_t>(h.getType());
    if (h.isValid()) {
      ++t.nValid;
      if (auto const* bh = dynamic_cast<BaseTrackerRecHit const*>(&h)) {
        auto const& ocr = bh->firstClusterRef();
        k.clu = ocr.rawIndex();
        k.prod = (uint32_t(ocr.id().processIndex()) << 16) | uint32_t(ocr.id().productIndex());
      }
    }
    t.hits.push_back(k);
  }

  inline uint64_t seedKeyOf(edm::ProductID const& id, size_t key) {
    return (uint64_t(id.processIndex()) << 48) | (uint64_t(id.productIndex()) << 32) | uint64_t(key & 0xffffffff);
  }

  // Collection traits: how to read one element into a TrackInfo.
  struct CandidateTraits {
    using Collection = TrackCandidateCollection;
    static constexpr int kNPar = 5;
    static constexpr const char* kParNames[5] = {"q/p", "dxdz", "dydz", "x", "y"};
    static constexpr bool kWrapPhi[5] = {false, false, false, false, false};
    static void fill(TrackCandidate const& c, int idx, TrackInfo& t) {
      t.index = idx;
      auto const& sr = c.seedRef();
      if (sr.isNonnull()) {
        t.hasSeed = true;
        t.seedKey = seedKeyOf(sr.id(), sr.key());
      }
      for (auto const& h : c.recHits())
        addHit(h, t);
      auto const& st = c.trajectoryStateOnDet();
      t.stateDet = st.detId();
      auto v = st.parameters().vector();
      static constexpr int kDiag[5] = {0, 2, 5, 9, 14};
      t.hasErr = st.hasError();
      for (int i = 0; i < 5; ++i) {
        t.par[i] = v[i];
        t.sig[i] = t.hasErr ? std::sqrt(std::max(0.f, st.error(kDiag[i]))) : 0.;
      }
    }
    template <typename H>
    static size_t size(H const& h) {
      return h->size();
    }
    template <typename H>
    static TrackCandidate const& at(H const& h, size_t i) {
      return (*h)[i];
    }
  };

  struct TrackTraits {
    using Collection = edm::View<reco::Track>;
    static constexpr int kNPar = 5;
    static constexpr const char* kParNames[5] = {"qoverp", "lambda", "phi", "dxy", "dsz"};
    static constexpr bool kWrapPhi[5] = {false, false, true, false, false};
    static void fill(reco::Track const& trk, int idx, TrackInfo& t) {
      t.index = idx;
      if (trk.extra().isNonnull() && trk.extra().isAvailable()) {
        auto const& sr = trk.seedRef();
        if (sr.isNonnull()) {
          t.hasSeed = true;
          t.seedKey = seedKeyOf(sr.id(), sr.key());
        }
        for (auto const* h : trk.recHits())
          addHit(*h, t);
      }
      t.hasErr = true;
      for (int i = 0; i < 5; ++i) {
        t.par[i] = trk.parameter(i);
        t.sig[i] = trk.error(i);
      }
      t.chi2 = trk.chi2();
      t.ndof = trk.ndof();
    }
    template <typename H>
    static size_t size(H const& h) {
      return h->size();
    }
    template <typename H>
    static reco::Track const& at(H const& h, size_t i) {
      return (*h)[i];
    }
  };

  // mkFit-level tracks (MkFitOutputWrapper / MkFitSeedWrapper): label = seed index, hits = (layer, index) incl.
  // negative indices (missed/stopped), state = mkFit's 6 parameters (x, y, z, 1/pT, phi, theta) and its 6x6 errors.
  struct MkFitTrackTraitsBase {
    static constexpr int kNPar = 6;
    static constexpr const char* kParNames[6] = {"x", "y", "z", "1/pT", "phi", "theta"};
    static constexpr bool kWrapPhi[6] = {false, false, false, false, true, false};
    static void fill(mkfit::Track const& trk, int idx, TrackInfo& t) {
      t.index = idx;
      t.hasSeed = true;
      t.seedKey = uint32_t(trk.label());
      for (int h = 0; h < trk.nTotalHits(); ++h) {
        auto hot = trk.getHitOnTrack(h);
        HitKey k;
        k.det = uint32_t(hot.layer);
        k.clu = uint32_t(hot.index);
        k.type = 0;
        t.hits.push_back(k);
        t.nValid += (hot.index >= 0);
      }
      t.hasErr = true;
      for (int i = 0; i < 6; ++i) {
        t.par[i] = trk.parameters()[i];
        t.sig[i] = std::sqrt(std::max(0.f, trk.errors()(i, i)));
      }
      t.chi2 = trk.chi2();
      t.ndof = 1;
    }
  };
  struct MkFitOutputTraits : MkFitTrackTraitsBase {
    using Collection = MkFitOutputWrapper;
    template <typename H>
    static size_t size(H const& h) {
      return h->tracks().size();
    }
    template <typename H>
    static mkfit::Track const& at(H const& h, size_t i) {
      return h->tracks()[i];
    }
  };
  struct MkFitSeedTraits : MkFitTrackTraitsBase {
    using Collection = MkFitSeedWrapper;
    template <typename H>
    static size_t size(H const& h) {
      return h->seeds().size();
    }
    template <typename H>
    static mkfit::Track const& at(H const& h, size_t i) {
      return h->seeds()[i];
    }
  };

  // port tracks in mkfitdev::TrackSoA (host copy): rows [0, nTracks) converted with trackFromSoA (label = seed index)
  struct TrackSoATraits : MkFitTrackTraitsBase {
    using Collection = mkfitdev::TrackSoAHostCollection;
    template <typename H>
    static size_t size(H const& h) {
      return size_t(std::max(0, int(h->const_view().nTracks())));
    }
    template <typename H>
    static mkfit::Track at(H const& h, size_t i) {
      return mkfitdev::trackFromSoA(h->const_view(), int(i));
    }
  };

  // |pull| buckets (sort-free distribution summary)
  constexpr int kNB = 9;
  constexpr const char* kBLabel[kNB] = {"==0", "<1e-6", "<1e-5", "<1e-4", "<1e-3", "<1e-2", "<0.1", "<1", ">=1"};
  inline int bucketOf(double a) {
    if (a == 0)
      return 0;
    if (!(a < 1.))  // also NaN
      return 8;
    double lim = 1e-6;
    for (int b = 1; b < 7; ++b, lim *= 10.)
      if (a < lim)
        return b;
    return 7;
  }

  // TgtTraits: the target may be another collection type with the same parameter set (e.g. a port's TrackSoA vs a stock
  // MkFitOutputWrapper)
  template <typename Traits, typename TgtTraits = Traits>
  class TrackCollectionCompare : public edm::one::EDAnalyzer<> {
  public:
    explicit TrackCollectionCompare(edm::ParameterSet const& cfg)
        : refToken_(consumes<typename Traits::Collection>(cfg.getParameter<edm::InputTag>("reference"))),
          tgtToken_(consumes<typename TgtTraits::Collection>(cfg.getParameter<edm::InputTag>("target"))),
          label_(cfg.getParameter<std::string>("label")),
          paramTol_(cfg.getParameter<double>("paramTolerance")),
          validHitsOnly_(cfg.getParameter<bool>("validHitsOnly")),
          seedKeyOnly_(cfg.getParameter<bool>("seedKeyOnly")),
          maxPrint_(cfg.getParameter<int>("maxPrint")),
          printEvents_(cfg.getParameter<bool>("printEvents")),
          summaryFile_(cfg.getParameter<std::string>("summaryFile")),
          requireIdentical_(cfg.getParameter<bool>("requireIdentical")) {
      if (label_.empty())
        label_ = cfg.getParameter<edm::InputTag>("reference").encode() + " vs " +
                 cfg.getParameter<edm::InputTag>("target").encode();
    }

    static void fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
      edm::ParameterSetDescription desc;
      desc.add<edm::InputTag>("reference", edm::InputTag("hltInitialStepTrackCandidates", "", "HLTX"));
      desc.add<edm::InputTag>("target", edm::InputTag("hltInitialStepTrackCandidates"));
      desc.add<std::string>("label", "");
      desc.add<double>("paramTolerance", 1e-3)->setComment("max |pull| for a pair to count as 'params agree'");
      desc.add<bool>("validHitsOnly", false)->setComment("compare only valid hits (else also missing/inactive)");
      desc.add<bool>("seedKeyOnly", true)
          ->setComment("pair by seed index only (ignore the seed ProductID, e.g. replay seeds are a copy)");
      desc.add<int>("maxPrint", 10)->setComment("print details of the first N differing pairs");
      desc.add<bool>("printEvents", false)->setComment("one line per event");
      desc.add<std::string>("summaryFile", "")->setComment("if set, write the summary as JSON");
      desc.add<bool>("requireIdentical", false)
          ->setComment("throw at endJob unless every event is identical (count, hits, seeds, params within tolerance)");
      descriptions.addWithDefaultLabel(desc);
    }

    void analyze(edm::Event const& ev, edm::EventSetup const&) override {
      // target first: in the menu the reference is the stock chain run on demand, which must only be asked for in
      // events where the menu ran the tracking (the target exists)
      auto const& hTgt = ev.getHandle(tgtToken_);
      ++nEv_;
      if (!hTgt.isValid()) {
        ++nEvMissing_;
        return;
      }
      auto const& hRef = ev.getHandle(refToken_);
      if (!hRef.isValid()) {
        ++nEvMissing_;
        return;
      }
      std::vector<TrackInfo> ref(Traits::size(hRef)), tgt(TgtTraits::size(hTgt));
      for (size_t i = 0; i < ref.size(); ++i)
        Traits::fill(Traits::at(hRef, i), int(i), ref[i]);
      for (size_t i = 0; i < tgt.size(); ++i)
        TgtTraits::fill(TgtTraits::at(hTgt, i), int(i), tgt[i]);
      if (seedKeyOnly_)
        for (auto* v : {&ref, &tgt})
          for (auto& t : *v)
            t.seedKey &= 0xffffffffULL;
      if (validHitsOnly_) {
        for (auto* v : {&ref, &tgt})
          for (auto& t : *v) {
            std::vector<HitKey> k;
            for (auto const& h : t.hits)
              if (h.type == TrackingRecHit::valid)
                k.push_back(h);
            t.hits.swap(k);
          }
      }
      nRef_ += ref.size();
      nTgt_ += tgt.size();

      // pair by seed key (first unused target with that seed), fallback by index when a track has no seed ref
      std::unordered_map<uint64_t, std::vector<int>> tgtBySeed;
      for (auto const& t : tgt)
        if (t.hasSeed)
          tgtBySeed[t.seedKey].push_back(t.index);
      std::vector<char> tgtUsed(tgt.size(), 0);
      for (auto const& [k, v] : tgtBySeed)
        if (v.size() > 1)
          nMultiSeedTgt_ += v.size() - 1;

      long evMatched = 0, evSameHits = 0, evSameOrder = 0, evParOk = 0, evParExact = 0;
      double evMaxPull = 0;
      for (auto const& r : ref) {
        int j = -1;
        if (r.hasSeed) {
          auto it = tgtBySeed.find(r.seedKey);
          if (it != tgtBySeed.end())
            for (int c : it->second)
              if (!tgtUsed[c]) {
                j = c;
                break;
              }
        } else {
          ++nNoSeed_;
          if (size_t(r.index) < tgt.size() && !tgt[r.index].hasSeed && !tgtUsed[r.index])
            j = r.index;
        }
        if (j < 0)
          continue;
        tgtUsed[j] = 1;
        ++evMatched;
        auto const& t = tgt[j];
        bool sameHits = (r.hits == t.hits);
        evSameHits += sameHits;
        evSameOrder += (r.index == t.index);
        if (!sameHits) {
          if (r.hits.size() == t.hits.size())
            ++nHitSameLenDiff_;
          else if (t.hits.size() > r.hits.size())
            ++nHitTgtLonger_;
          else
            ++nHitTgtShorter_;
          size_t p = 0;
          while (p < r.hits.size() && p < t.hits.size() && r.hits[p] == t.hits[p])
            ++p;
          sumCommonPrefix_ += p;
          sumRefLen_ += r.hits.size();
        }
        nValidDiff_[std::min(4, std::abs(r.nValid - t.nValid))]++;
        if (r.stateDet != t.stateDet)
          ++nStateDetDiff_;
        double maxPull = 0;
        for (int i = 0; i < Traits::kNPar; ++i) {
          double d = t.par[i] - r.par[i];
          if (Traits::kWrapPhi[i]) {
            while (d > M_PI)
              d -= 2 * M_PI;
            while (d < -M_PI)
              d += 2 * M_PI;
          }
          double pull = (r.sig[i] > 0) ? std::abs(d) / r.sig[i] : std::abs(d);
          if (r.stateDet != t.stateDet)
            continue;  // states on different surfaces: parameters not comparable
          pullB_[i][bucketOf(pull)]++;
          sumPull_[i] += std::isfinite(pull) ? pull : 0;
          ++nPull_[i];
          maxPullPar_[i] = std::max(maxPullPar_[i], std::isfinite(pull) ? pull : 1e30);
          maxPull = std::max(maxPull, std::isfinite(pull) ? pull : 1e30);
          if (r.sig[i] > 0 && t.sig[i] >= 0) {
            double rs = std::abs(t.sig[i] / r.sig[i] - 1.);
            sigB_[i][bucketOf(rs)]++;
          }
        }
        if (r.stateDet == t.stateDet) {
          evParOk += (maxPull <= paramTol_);
          evParExact += (maxPull == 0);
        }
        evMaxPull = std::max(evMaxPull, maxPull);
        if (r.ndof > 0 || t.ndof > 0)
          chi2B_[bucketOf(std::abs(t.chi2 - r.chi2) / std::max(1., r.chi2))]++;
        if ((!sameHits || maxPull > paramTol_ || r.stateDet != t.stateDet) && nPrinted_ < maxPrint_) {
          ++nPrinted_;
          std::ostringstream os;
          os << "[compare " << label_ << "] DIFF ev " << ev.id().event() << " ref#" << r.index << " tgt#" << t.index
             << " nhits " << r.hits.size() << "/" << t.hits.size() << " sameHits " << sameHits << " stateDet "
             << r.stateDet << "/" << t.stateDet << " maxPull " << maxPull << "\n";
          for (int i = 0; i < Traits::kNPar; ++i)
            os << "    " << Traits::kParNames[i] << " ref " << r.par[i] << " +- " << r.sig[i] << " tgt " << t.par[i]
               << " +- " << t.sig[i] << "\n";
          if (!sameHits) {
            os << "    hits ref:";
            for (auto const& h : r.hits)
              os << " " << h.det << ":" << (h.clu & 0xffffff) << ":" << int(h.type);
            os << "\n    hits tgt:";
            for (auto const& h : t.hits)
              os << " " << h.det << ":" << (h.clu & 0xffffff) << ":" << int(h.type);
            os << "\n";
          }
          std::cout << os.str() << std::flush;
        }
      }
      long refOnly = long(ref.size()) - evMatched;
      long tgtOnly = long(tgt.size()) - evMatched;
      nMatched_ += evMatched;
      nRefOnly_ += refOnly;
      nTgtOnly_ += tgtOnly;
      nSameHits_ += evSameHits;
      nSameOrder_ += evSameOrder;
      nParOk_ += evParOk;
      nParExact_ += evParExact;
      bool evIdent = (ref.size() == tgt.size()) && refOnly == 0 && tgtOnly == 0 && evSameHits == evMatched;
      nEvIdent_ += evIdent;
      nEvIdentOrder_ += evIdent && evSameOrder == evMatched;
      nEvIdentPar_ += evIdent && evParOk == evMatched;
      nEvIdentExact_ += evIdent && evParExact == evMatched && evSameOrder == evMatched;
      if (printEvents_)
        std::cout << "[compare " << label_ << "] ev " << ev.id().event() << " nRef " << ref.size() << " nTgt "
                  << tgt.size() << " matched " << evMatched << " sameHits " << evSameHits << " parOk " << evParOk
                  << " maxPull " << evMaxPull << " identical " << evIdent << std::endl;
    }

    void endJob() override {
      auto frac = [](double a, double b) { return b > 0 ? a / b : 0.; };
      std::ostringstream os;
      const std::string p = "[compare " + label_ + "] ";
      os << p << "events " << nEv_ << " (missing input " << nEvMissing_ << ")\n";
      os << p << "tracks ref " << nRef_ << " tgt " << nTgt_ << " matched-by-seed " << nMatched_ << " ref-only "
         << nRefOnly_ << " tgt-only " << nTgtOnly_ << " no-seed-ref " << nNoSeed_ << " extra-tgt-per-seed "
         << nMultiSeedTgt_ << "\n";
      os << p << "matched pairs: identical hit lists " << nSameHits_ << " (" << frac(nSameHits_, nMatched_)
         << "), same index " << nSameOrder_ << ", params within " << paramTol_ << " sigma " << nParOk_ << " ("
         << frac(nParOk_, nMatched_) << "), params bit-identical " << nParExact_ << " (" << frac(nParExact_, nMatched_)
         << "), state on different det " << nStateDetDiff_ << "\n";
      os << p << "hit-list differences: same length " << nHitSameLenDiff_ << ", tgt longer " << nHitTgtLonger_
         << ", tgt shorter " << nHitTgtShorter_ << ", mean common prefix " << frac(sumCommonPrefix_, nDiffHits())
         << " of " << frac(sumRefLen_, nDiffHits()) << " hits; |dnValid| 0,1,2,3,>=4: " << nValidDiff_[0] << ","
         << nValidDiff_[1] << "," << nValidDiff_[2] << "," << nValidDiff_[3] << "," << nValidDiff_[4] << "\n";
      os << p << "events identical (count, hits, seed refs) " << nEvIdent_ << " / " << (nEv_ - nEvMissing_) << " ("
         << frac(nEvIdent_, nEv_ - nEvMissing_) << "); + same order " << nEvIdentOrder_ << "; + params within tol "
         << nEvIdentPar_ << "; fully bit-identical " << nEvIdentExact_ << "\n";
      os << p << "|pull| buckets (pull = (tgt-ref)/sigma_ref):";
      for (int b = 0; b < kNB; ++b)
        os << " " << kBLabel[b];
      os << "   mean max\n";
      for (int i = 0; i < Traits::kNPar; ++i) {
        os << p << "  " << std::setw(7) << Traits::kParNames[i] << ":";
        for (int b = 0; b < kNB; ++b)
          os << " " << pullB_[i][b];
        os << "   " << frac(sumPull_[i], nPull_[i]) << " " << maxPullPar_[i] << "\n";
      }
      os << p << "|sigma_tgt/sigma_ref - 1| buckets:\n";
      for (int i = 0; i < Traits::kNPar; ++i) {
        os << p << "  " << std::setw(7) << Traits::kParNames[i] << ":";
        for (int b = 0; b < kNB; ++b)
          os << " " << sigB_[i][b];
        os << "\n";
      }
      long nchi = 0;
      for (int b = 0; b < kNB; ++b)
        nchi += chi2B_[b];
      if (nchi > 0) {
        os << p << "|dchi2|/max(1,chi2_ref) buckets:";
        for (int b = 0; b < kNB; ++b)
          os << " " << chi2B_[b];
        os << "\n";
      }
      std::cout << os.str() << std::flush;
      if (!summaryFile_.empty()) {
        std::ofstream f(summaryFile_);
        f << "{\"label\": \"" << label_ << "\", \"events\": " << nEv_ << ", \"eventsMissing\": " << nEvMissing_
          << ", \"nRef\": " << nRef_ << ", \"nTgt\": " << nTgt_ << ", \"matched\": " << nMatched_
          << ", \"refOnly\": " << nRefOnly_ << ", \"tgtOnly\": " << nTgtOnly_ << ", \"sameHits\": " << nSameHits_
          << ", \"sameIndex\": " << nSameOrder_ << ", \"paramsWithinTol\": " << nParOk_
          << ", \"paramsBitIdentical\": " << nParExact_ << ", \"paramTolerance\": " << paramTol_
          << ", \"stateDetDiff\": " << nStateDetDiff_ << ", \"eventsIdentical\": " << nEvIdent_
          << ", \"eventsIdenticalOrder\": " << nEvIdentOrder_ << ", \"eventsIdenticalParams\": " << nEvIdentPar_
          << ", \"eventsBitIdentical\": " << nEvIdentExact_ << ", \"maxPull\": [";
        for (int i = 0; i < Traits::kNPar; ++i)
          f << (i ? ", " : "") << maxPullPar_[i];
        // bucket histograms (edges as kBLabel) for the D-M4 floor check (test/replay_floor_check.py)
        auto arr = [&f](const long* b) {
          f << "[";
          for (int k = 0; k < kNB; ++k)
            f << (k ? ", " : "") << b[k];
          f << "]";
        };
        f << "], \"parNames\": [";
        for (int i = 0; i < Traits::kNPar; ++i)
          f << (i ? ", " : "") << "\"" << Traits::kParNames[i] << "\"";
        f << "], \"pullBuckets\": [";
        for (int i = 0; i < Traits::kNPar; ++i) {
          f << (i ? ", " : "");
          arr(pullB_[i]);
        }
        f << "], \"sigmaBuckets\": [";
        for (int i = 0; i < Traits::kNPar; ++i) {
          f << (i ? ", " : "");
          arr(sigB_[i]);
        }
        f << "], \"chi2Buckets\": ";
        arr(chi2B_);
        f << "}\n";
      }
      if (requireIdentical_ && (nEvIdentPar_ != nEv_ || nEv_ == 0))
        throw cms::Exception("MkFitAlpakaCompare")
            << label_ << ": " << (nEv_ - nEvIdentPar_) << " of " << nEv_ << " events differ (or no events)";
    }

  private:
    long nDiffHits() const { return nHitSameLenDiff_ + nHitTgtLonger_ + nHitTgtShorter_; }

    edm::EDGetTokenT<typename Traits::Collection> refToken_;
    edm::EDGetTokenT<typename TgtTraits::Collection> tgtToken_;
    std::string label_;
    double paramTol_;
    bool validHitsOnly_;
    bool seedKeyOnly_;
    int maxPrint_;
    bool printEvents_;
    std::string summaryFile_;
    bool requireIdentical_;

    long nEv_ = 0, nEvMissing_ = 0, nRef_ = 0, nTgt_ = 0, nMatched_ = 0, nRefOnly_ = 0, nTgtOnly_ = 0, nNoSeed_ = 0,
         nMultiSeedTgt_ = 0;
    long nSameHits_ = 0, nSameOrder_ = 0, nParOk_ = 0, nParExact_ = 0, nStateDetDiff_ = 0;
    long nHitSameLenDiff_ = 0, nHitTgtLonger_ = 0, nHitTgtShorter_ = 0;
    double sumCommonPrefix_ = 0, sumRefLen_ = 0;
    long nValidDiff_[5] = {};
    long nEvIdent_ = 0, nEvIdentOrder_ = 0, nEvIdentPar_ = 0, nEvIdentExact_ = 0;
    long pullB_[6][kNB] = {}, sigB_[6][kNB] = {}, chi2B_[kNB] = {};
    double sumPull_[6] = {}, maxPullPar_[6] = {};
    long nPull_[6] = {};
    int nPrinted_ = 0;
  };

}  // namespace mkfitdev_harness

using MkFitAlpakaCandidateCompare = mkfitdev_harness::TrackCollectionCompare<mkfitdev_harness::CandidateTraits>;
using MkFitAlpakaTrackCompare = mkfitdev_harness::TrackCollectionCompare<mkfitdev_harness::TrackTraits>;
DEFINE_FWK_MODULE(MkFitAlpakaCandidateCompare);
DEFINE_FWK_MODULE(MkFitAlpakaTrackCompare);
using MkFitAlpakaMkFitTrackCompare = mkfitdev_harness::TrackCollectionCompare<mkfitdev_harness::MkFitOutputTraits>;
using MkFitAlpakaMkFitSeedCompare = mkfitdev_harness::TrackCollectionCompare<mkfitdev_harness::MkFitSeedTraits>;
DEFINE_FWK_MODULE(MkFitAlpakaMkFitTrackCompare);
DEFINE_FWK_MODULE(MkFitAlpakaMkFitSeedCompare);
// port TrackSoA (host collection) vs a stock MkFitOutputWrapper (e.g. stockStages or hltInitialStepTrackCandidatesMkFit)
using MkFitAlpakaTrackSoACompare =
    mkfitdev_harness::TrackCollectionCompare<mkfitdev_harness::MkFitOutputTraits, mkfitdev_harness::TrackSoATraits>;
DEFINE_FWK_MODULE(MkFitAlpakaTrackSoACompare);
