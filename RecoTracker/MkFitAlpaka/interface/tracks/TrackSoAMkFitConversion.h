#ifndef RecoTracker_MkFitAlpaka_interface_tracks_TrackSoAMkFitConversion_h
#define RecoTracker_MkFitAlpaka_interface_tracks_TrackSoAMkFitConversion_h

// HOST-ONLY conversions between mkfitdev::TrackSoA rows and stock mkfit::Track (for replay inputs, comparators and for
// feeding the stock output converters through MkFitOutputWrapper). Do not include from device code.

#include <cstring>

#include "RecoTracker/MkFitAlpaka/interface/tracks/TrackSoA.h"
#include "RecoTracker/MkFitCore/interface/Track.h"

namespace mkfitdev {

  // Writes track t into row r. Returns false if its hit list was truncated to kMaxTrkHits (caller counts overflow).
  inline bool trackToSoA(mkfit::Track const& t, TrackSoAView v, int r) {
    for (int k = 0; k < 6; ++k)
      v[r].params().v[k] = t.parameters()[k];
    std::memcpy(v[r].errors().v, t.errors().Array(), sizeof(float) * 21);
    v[r].charge() = t.charge();
    v[r].chi2() = t.chi2();
    v[r].score() = t.score();
    v[r].label() = t.label();
    const int nTot = t.nTotalHits();
    const int nh = nTot < kMaxTrkHits ? nTot : kMaxTrkHits;
    v[r].nTotalHits() = nh;
    v[r].nFoundHits() = t.nFoundHits();
    const auto st = t.getStatus();
    v[r].nSeedHits() = st.n_seed_hits;
    v[r].etaRegion() = st.eta_region;
    v[r].algorithm() = st.algorithm;
    v[r].nOverlaps() = st.n_overlaps;
    v[r].duplicate() = st.duplicate;
    for (int h = 0; h < nh; ++h) {
      const mkfit::HitOnTrack hot = t.getHitOnTrack(h);
      v[r].hits().hot[h].index = hot.index;
      v[r].hits().hot[h].layer = hot.layer;
    }
    return nTot <= kMaxTrkHits;
  }

  // Fills rows [0, n) from a stock track vector; sets nTracks and the overflow counters.
  inline void tracksToSoA(mkfit::TrackVec const& tv, TrackSoAView v) {
    const int cap = v.metadata().size();
    int n = 0, ovHits = 0;
    for (auto const& t : tv) {
      if (n >= cap)
        break;
      if (!trackToSoA(t, v, n))
        ++ovHits;
      ++n;
    }
    v.nTracks() = n;
    v.nOverflowTracks() = int(tv.size()) - n;
    v.nOverflowHits() = ovHits;
  }

  // Row r -> stock mkfit::Track (state, chi2, score, label, status fields, hit list with stock nFoundHits).
  inline mkfit::Track trackFromSoA(TrackSoAConstView v, int r) {
    mkfit::TrackState s;
    for (int k = 0; k < 6; ++k)
      s.parameters[k] = v[r].params().v[k];
    std::memcpy(s.errors.Array(), v[r].errors().v, sizeof(float) * 21);
    s.charge = v[r].charge();
    mkfit::Track t;
    t.setState(s);
    t.setChi2(v[r].chi2());
    t.setScore(v[r].score());
    t.setLabel(v[r].label());
    auto st = t.getStatus();
    st.n_seed_hits = v[r].nSeedHits();
    st.eta_region = v[r].etaRegion();
    st.algorithm = v[r].algorithm();
    st.n_overlaps = v[r].nOverlaps();
    st.duplicate = v[r].duplicate() != 0;
    t.setStatus(st);
    const int nh = v[r].nTotalHits();
    t.resizeHits(nh, v[r].nFoundHits());
    for (int h = 0; h < nh; ++h)
      t.setHitIdxAtPos(h, mkfit::HitOnTrack(v[r].hits().hot[h].index, v[r].hits().hot[h].layer));
    return t;
  }

  inline mkfit::TrackVec tracksFromSoA(TrackSoAConstView v) {
    mkfit::TrackVec tv;
    tv.reserve(v.nTracks());
    for (int r = 0; r < v.nTracks(); ++r)
      tv.push_back(trackFromSoA(v, r));
    return tv;
  }

}  // namespace mkfitdev

#endif
