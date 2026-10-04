// Lane outconv (round 7): field-by-field comparison of two reco::Track collections made from the SAME mkFit
// candidates (e.g. the stock MkFitOutputTrackConverter vs MkFitAlpakaOutputTrackConverter in one job): same count,
// and per index the 5 parameters, the 15 covariance terms, chi2, ndof, charge, algo, the three hit-pattern categories
// word by word, the seed reference, and every rechit (detId, type, cluster, local position and error).
// Summary at endJob, "[outconv compare <label>]"; requireIdentical throws at endJob on any difference.

#include <atomic>
#include <cmath>
#include <cstring>

#include "DataFormats/TrackReco/interface/Track.h"
#include "DataFormats/TrackReco/interface/TrackExtra.h"
#include "DataFormats/TrackReco/interface/TrackFwd.h"
#include "DataFormats/TrackerRecHit2D/interface/BaseTrackerRecHit.h"
#include "DataFormats/TrackerRecHit2D/interface/OmniClusterRef.h"
#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/Framework/interface/global/EDAnalyzer.h"
#include "FWCore/MessageLogger/interface/MessageLogger.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include "FWCore/Utilities/interface/Exception.h"

class MkFitAlpakaOutputTrackCompare : public edm::global::EDAnalyzer<> {
public:
  explicit MkFitAlpakaOutputTrackCompare(edm::ParameterSet const& cfg)
      : refToken_{consumes(cfg.getParameter<edm::InputTag>("reference"))},
        tgtToken_{consumes(cfg.getParameter<edm::InputTag>("target"))},
        label_{cfg.getParameter<std::string>("label")},
        maxPrint_{cfg.getParameter<int>("maxPrint")},
        requireIdentical_{cfg.getParameter<bool>("requireIdentical")} {}

  static void fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
    edm::ParameterSetDescription desc;
    desc.add<edm::InputTag>("reference", edm::InputTag("hltInitialStepTracksStock"));
    desc.add<edm::InputTag>("target", edm::InputTag("hltInitialStepTracks"));
    desc.add<std::string>("label", "");
    desc.add<int>("maxPrint", 5);
    desc.add<bool>("requireIdentical", false);
    descriptions.addWithDefaultLabel(desc);
  }

  void analyze(edm::StreamID, edm::Event const& ev, edm::EventSetup const&) const override {
    const auto& ref = ev.get(refToken_);
    const auto& tgt = ev.get(tgtToken_);
    ++nEv_;
    nRef_ += ref.size();
    nTgt_ += tgt.size();
    if (ref.size() != tgt.size()) {
      ++nEvCount_;
      if (nPrinted_++ < maxPrint_)
        edm::LogPrint("MkFitAlpakaOutputTrackCompare")
            << "[outconv compare " << label_ << "] event " << ev.id().event() << " track count " << ref.size()
            << " vs " << tgt.size();
      return;
    }
    for (size_t i = 0; i < ref.size(); ++i) {
      const auto& a = ref[i];
      const auto& b = tgt[i];
      unsigned what = 0;
      for (int k = 0; k < 5; ++k)
        if (a.parameter(k) != b.parameter(k))
          what |= 1;
      for (int k = 0; k < 5; ++k)
        for (int l = 0; l <= k; ++l)
          if (a.covariance(k, l) != b.covariance(k, l))
            what |= 2;
      if (a.chi2() != b.chi2() || a.ndof() != b.ndof() || a.charge() != b.charge() || a.algo() != b.algo())
        what |= 4;
      const auto& pa = a.hitPattern();
      const auto& pb = b.hitPattern();
      for (auto cat : {reco::HitPattern::TRACK_HITS, reco::HitPattern::MISSING_INNER_HITS,
                       reco::HitPattern::MISSING_OUTER_HITS}) {
        const int na = pa.numberOfAllHits(cat);
        const int nb = pb.numberOfAllHits(cat);
        if (na != nb) {
          what |= (cat == reco::HitPattern::TRACK_HITS ? 8 : 16);
          continue;
        }
        for (int h = 0; h < na; ++h)
          if (pa.getHitPattern(cat, h) != pb.getHitPattern(cat, h))
            what |= (cat == reco::HitPattern::TRACK_HITS ? 8 : 16);
      }
      if (a.seedRef().key() != b.seedRef().key())
        what |= 32;
      if (a.recHitsSize() != b.recHitsSize())
        what |= 64;
      else
        for (unsigned h = 0; h < a.recHitsSize(); ++h) {
          const auto& ha = *a.recHit(h);
          const auto& hb = *b.recHit(h);
          if (ha.geographicalId() != hb.geographicalId() || ha.getType() != hb.getType() ||
              ha.localPosition() != hb.localPosition() || ha.localPositionError().xx() != hb.localPositionError().xx() ||
              ha.localPositionError().xy() != hb.localPositionError().xy() ||
              ha.localPositionError().yy() != hb.localPositionError().yy() || ha.dimension() != hb.dimension())
            what |= 64;
          const auto* ba = dynamic_cast<const BaseTrackerRecHit*>(&ha);
          const auto* bb = dynamic_cast<const BaseTrackerRecHit*>(&hb);
          if ((ba == nullptr) != (bb == nullptr) ||
              (ba && (ba->firstClusterRef().rawIndex() != bb->firstClusterRef().rawIndex() ||
                      ba->firstClusterRef().id() != bb->firstClusterRef().id())))
            what |= 64;
        }
      if (what) {
        ++nDiff_;
        for (int bit = 0; bit < 7; ++bit)
          if (what & (1u << bit))
            ++nBit_[bit];
        if (nPrinted_++ < maxPrint_)
          edm::LogPrint("MkFitAlpakaOutputTrackCompare")
              << "[outconv compare " << label_ << "] event " << ev.id().event() << " track " << i << " differs (mask "
              << what << "): pt " << a.pt() << " vs " << b.pt() << ", lost inner " << pa.numberOfAllHits(reco::HitPattern::MISSING_INNER_HITS)
              << " vs " << pb.numberOfAllHits(reco::HitPattern::MISSING_INNER_HITS) << ", lost outer "
              << pa.numberOfAllHits(reco::HitPattern::MISSING_OUTER_HITS) << " vs "
              << pb.numberOfAllHits(reco::HitPattern::MISSING_OUTER_HITS);
      }
    }
  }

  void endJob() override {
    edm::LogPrint("MkFitAlpakaOutputTrackCompare")
        << "[outconv compare " << label_ << "] events " << nEv_ << " tracks " << nRef_ << " vs " << nTgt_
        << " | events with a different count " << nEvCount_ << " | differing tracks " << nDiff_
        << " (params " << nBit_[0] << ", cov " << nBit_[1] << ", chi2/ndof/q/algo " << nBit_[2] << ", track-hit pattern "
        << nBit_[3] << ", missing inner/outer pattern " << nBit_[4] << ", seed ref " << nBit_[5] << ", rechits "
        << nBit_[6] << ") -> " << ((nEvCount_ == 0 && nDiff_ == 0) ? "IDENTICAL" : "DIFFERENT");
    if (requireIdentical_ && (nEvCount_ != 0 || nDiff_ != 0))
      throw cms::Exception("OutConvCompare") << label_ << ": collections differ";
  }

private:
  const edm::EDGetTokenT<reco::TrackCollection> refToken_;
  const edm::EDGetTokenT<reco::TrackCollection> tgtToken_;
  const std::string label_;
  const int maxPrint_;
  const bool requireIdentical_;
  mutable std::atomic<long long> nEv_{0}, nRef_{0}, nTgt_{0}, nEvCount_{0}, nDiff_{0};
  mutable std::atomic<int> nPrinted_{0};
  mutable std::atomic<long long> nBit_[7] = {0, 0, 0, 0, 0, 0, 0};
};

DEFINE_FWK_MODULE(MkFitAlpakaOutputTrackCompare);
