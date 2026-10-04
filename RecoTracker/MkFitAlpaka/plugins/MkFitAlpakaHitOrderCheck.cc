// Stage C (round 8, lane stagec), R7-M4: how often can the TrackExtra hit order of the output converter (stable
// insertion with the stock comparator, no general sort) put a DIFFERENT hit at position 0 (or last) than the stock
// std::sort? TrackListMerger (hltGeneralTracks, allowFirstHitShare) reads recHit(0).
// For every output track, with the converter's keys (global position, barrel flag from the DetId, the TOB-tilted clause):
//   ambiguous first: some later hit j with !hitLess(h0, hj), i.e. the comparator does not force h0 first; std::sort
//                    (introsort, input-order dependent) may then put another hit first = an UPPER BOUND on R7-M4 flips;
//   inverted first : some later hit j with hitLess(hj, h0) (an outright inversion; impossible for a strict weak order);
//   the same for the last hit; and std::sort applied to the output order vs the output order (any / first / last).
// HITORDER-SUMMARY at endJob.
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <vector>

#include "DataFormats/GeometryVector/interface/GlobalPoint.h"
#include "DataFormats/SiPixelDetId/interface/PixelSubdetector.h"
#include "DataFormats/SiStripDetId/interface/StripSubdetector.h"
#include "DataFormats/TrackReco/interface/Track.h"
#include "DataFormats/TrackReco/interface/TrackFwd.h"
#include "DataFormats/TrackerCommon/interface/TrackerTopology.h"
#include "DataFormats/TrackingRecHit/interface/TrackingRecHit.h"
#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/Framework/interface/global/EDAnalyzer.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include "Geometry/Records/interface/TrackerTopologyRcd.h"

namespace {
  struct HitKey {  // = MkFitAlpakaOutputTrackConverter's HitKey
    GlobalPoint pos;
    bool barrel;
    bool tiltedOrNotBarrel;
  };
  inline bool hitLess(const HitKey& a, const HitKey& b) {
    const bool aB = a.barrel, bB = b.barrel;
    if (aB || bB) {
      if (a.tiltedOrNotBarrel || b.tiltedOrNotBarrel || !(aB && bB))
        return a.pos.mag2() < b.pos.mag2();
      return a.pos.perp2() < b.pos.perp2();
    }
    return std::abs(a.pos.z()) < std::abs(b.pos.z());
  }
}  // namespace

class MkFitAlpakaHitOrderCheck : public edm::global::EDAnalyzer<> {
public:
  explicit MkFitAlpakaHitOrderCheck(edm::ParameterSet const& p)
      : tracks_{consumes(p.getParameter<edm::InputTag>("tracks"))}, tTopo_{esConsumes()} {}

  static void fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
    edm::ParameterSetDescription desc;
    desc.add<edm::InputTag>("tracks", edm::InputTag("hltInitialStepTracks"));
    descriptions.addWithDefaultLabel(desc);
  }

  void analyze(edm::StreamID, edm::Event const& ev, edm::EventSetup const& es) const override {
    auto const& trks = ev.get(tracks_);
    auto const& tTopo = es.getData(tTopo_);
    std::vector<HitKey> keys;
    std::vector<int> ord;
    long c[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    for (auto const& trk : trks) {
      keys.clear();
      for (auto it = trk.recHitsBegin(); it != trk.recHitsEnd(); ++it) {
        auto const& h = **it;
        if (!h.isValid())
          continue;
        const DetId id = h.geographicalId();
        HitKey k;
        k.pos = h.globalPosition();
        k.barrel = id.subdetId() == PixelSubdetector::PixelBarrel || id.subdetId() == StripSubdetector::TIB ||
                   id.subdetId() == StripSubdetector::TOB;
        k.tiltedOrNotBarrel = (id.subdetId() == StripSubdetector::TOB && tTopo.tobSide(id) < 3);
        keys.push_back(k);
      }
      const int n = keys.size();
      if (n < 2)
        continue;
      ++c[0];
      bool ambF = false, invF = false, ambL = false, invL = false;
      for (int j = 1; j < n; ++j) {
        ambF = ambF || !hitLess(keys[0], keys[j]);
        invF = invF || hitLess(keys[j], keys[0]);
      }
      for (int j = 0; j < n - 1; ++j) {
        ambL = ambL || !hitLess(keys[j], keys[n - 1]);
        invL = invL || hitLess(keys[n - 1], keys[j]);
      }
      c[1] += ambF;
      c[2] += invF;
      c[3] += ambL;
      c[4] += invL;
      ord.resize(n);
      for (int i = 0; i < n; ++i)
        ord[i] = i;
      std::sort(ord.begin(), ord.end(), [&](int a, int b) { return hitLess(keys[a], keys[b]); });
      bool any = false;
      for (int i = 0; i < n; ++i)
        any = any || ord[i] != i;
      c[5] += any;
      c[6] += ord[0] != 0;
      c[7] += ord[n - 1] != n - 1;
    }
    for (int i = 0; i < 8; ++i)
      cnt_[i] += c[i];
  }

  void endJob() override {
    std::printf(
        "HITORDER-SUMMARY tracks(>=2 hits) %ld | first hit: ambiguous %ld inverted %ld | last hit: ambiguous %ld "
        "inverted %ld | std::sort of the output order differs: any %ld first %ld last %ld\n",
        cnt_[0].load(),
        cnt_[1].load(),
        cnt_[2].load(),
        cnt_[3].load(),
        cnt_[4].load(),
        cnt_[5].load(),
        cnt_[6].load(),
        cnt_[7].load());
    std::fflush(stdout);
  }

private:
  const edm::EDGetTokenT<reco::TrackCollection> tracks_;
  const edm::ESGetToken<TrackerTopology, TrackerTopologyRcd> tTopo_;
  mutable std::array<std::atomic<long>, 8> cnt_{};
};

DEFINE_FWK_MODULE(MkFitAlpakaHitOrderCheck);
