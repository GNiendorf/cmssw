// Lane clean, host-only companion of cleanCompare: (1) times the STOCK StdSeq::clean_duplicates_sharedhits_pixelseed
// on the dumped no-cleaning tracks in the same standalone conditions as the device test, (2) checks that the stock
// cleaner reproduces the dumped stock decisions after the round trip mkfit::Track -> TrackSoA -> mkfit::Track
// (TrackSoAMkFitConversion.h), and that the round trip is exact.
// Usage: cleanStockTiming dump.bin [nRepeat]

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "DataFormats/Portable/interface/PortableHostCollection.h"
#include "RecoTracker/MkFitAlpaka/interface/tracks/TrackSoAHostCollection.h"
#include "RecoTracker/MkFitAlpaka/interface/tracks/TrackSoAMkFitConversion.h"
#include "RecoTracker/MkFitAlpaka/src/alpaka/clean/CleanFunctions.h"
#include "RecoTracker/MkFitAlpaka/test/cleanDumpReader.h"
#include "RecoTracker/MkFitCMS/interface/MkStdSeqs.h"
#include "RecoTracker/MkFitCore/interface/IterationConfig.h"
#include "RecoTracker/MkFitCore/interface/Track.h"
#include "RecoTracker/MkFitCore/interface/TrackerInfo.h"

namespace {
  mkfit::Track toTrack(cleandump::DTrack const& d) {
    mkfit::TrackState s;
    for (int k = 0; k < 6; ++k)
      s.parameters[k] = d.par[k];
    std::memcpy(s.errors.Array(), d.err, sizeof(d.err));
    s.charge = d.charge;
    mkfit::Track t;
    t.setState(s);
    t.setChi2(d.chi2);
    t.setScore(d.score);
    t.setLabel(d.label);
    mkfit::TrackBase::Status st;
    std::memcpy(&st, &d.status, 4);
    t.setStatus(st);
    t.resizeHits(d.nTot, d.nFound);
    for (int h = 0; h < d.nTot; ++h) {
      mkfit::HitOnTrack hot;
      std::memcpy(&hot, &d.hots[h], 4);
      t.setHitIdxAtPos(h, hot);
    }
    return t;
  }

  bool same(mkfit::Track const& a, mkfit::Track const& b) {
    if (std::memcmp(a.posArray(), b.posArray(), 6 * sizeof(float)) || std::memcmp(a.errArray(), b.errArray(), 21 * sizeof(float)))
      return false;
    if (a.charge() != b.charge() || a.chi2() != b.chi2() || a.score() != b.score() || a.label() != b.label() ||
        a.nTotalHits() != b.nTotalHits() || a.nFoundHits() != b.nFoundHits() || a.getNSeedHits() != b.getNSeedHits() ||
        a.getEtaRegion() != b.getEtaRegion() || a.algoint() != b.algoint() || a.nOverlapHits() != b.nOverlapHits() ||
        a.getDuplicateValue() != b.getDuplicateValue())
      return false;
    for (int h = 0; h < a.nTotalHits(); ++h)
      if (a.getHitIdx(h) != b.getHitIdx(h) || a.getHitLyr(h) != b.getHitLyr(h))
        return false;
    return true;
  }
  // Robustness of the decisions to floating-point differences (GPU tanf, FMA contraction): for every pair, at the
  // comparison that actually decides it (first cut that rejects, or the dR / shared-hit test that flags), count the
  // cases whose relative margin to the threshold is below tol.
  struct Margins {
    long pairs = 0, near[2][4] = {{0}};  // [tol 1e-6, 1e-4][dctheta, dphi, dR2, d1pt]
    long sensitive = 0;  // pairs near a pre-cut (tol 1e-4) that the remaining tests would flag if the pre-cut flipped
  };
  void marginScan(mkfitdev::TrackSoAConstView v, mkfitdev::clean::DupCleanParams const& p, Margins& m) {
    namespace hc = mkfitdev::clean;
    const int n = v.nTracks();
    std::vector<float> ct(n);
    for (int i = 0; i < n; ++i)
      ct[i] = 1.f / std::tan(v[i].params().v[5]);
    const double tol[2] = {1e-6, 1e-4};
    auto near = [&](int which, double x, double thr) {
      const double r = std::abs(x - thr) / thr;
      for (int k = 0; k < 2; ++k)
        if (r < tol[k])
          ++m.near[k][which];
    };
    for (int i = 0; i < n; ++i)
      for (int j = i + 1; j < n; ++j) {
        ++m.pairs;
        if (v[i].label() == v[j].label())
          continue;
        const float dctheta = std::abs(ct[j] - ct[i]);
        const float dphi = std::abs(hc::squashPhiMinimal(v[i].params().v[4] - v[j].params().v[4]));
        near(0, dctheta, hc::kMaxdcth);
        const bool nearCut = std::abs(dctheta - hc::kMaxdcth) / hc::kMaxdcth < 1e-4 ||
                             (dctheta <= hc::kMaxdcth && std::abs(dphi - hc::kMaxdphi) / hc::kMaxdphi < 1e-4);
        if (nearCut) {
          // decision with both pre-cuts forced open: use the transliterated decision on copies with the pre-cuts bypassed
          // (the pair is at |dcot theta| or |dphi| ~ 0.37, so dR2 cannot flag; only the d(1/pT) + shared-hit test can)
          if (std::abs(v[j].params().v[3] - v[i].params().v[3]) <= hc::kMaxd1pt) {
            int sc = 0;
            const auto& h1 = v[i].hits().hot;
            const auto& h2 = v[j].hits().hot;
            for (int a = 0; a < v[i].nTotalHits(); ++a)
              for (int b = 0; b < v[j].nTotalHits(); ++b)
                if (h1[a].index >= 0 && h1[a].index == h2[b].index && h1[a].layer == h2[b].layer)
                  ++sc;
            const int sf = (h1[0].index >= 0 && h1[0].index == h2[0].index && h1[0].layer == h2[0].layer) ? 1 : 0;
            const int mf = std::min<int>(v[i].nFoundHits(), v[j].nFoundHits());
            if ((sc - sf) >= ((mf - sf) * p.fracSharedHits))
              ++m.sensitive;
          }
        }
        if (dctheta > hc::kMaxdcth)
          continue;
        near(1, dphi, hc::kMaxdphi);
        if (dphi > hc::kMaxdphi)
          continue;
        float maxdR2 = p.drthCentral * p.drthCentral;
        if (std::abs(ct[i]) > hc::kMaxcthFw)
          maxdR2 = p.drthForward * p.drthForward;
        else if (std::abs(ct[i]) > hc::kMaxcthOb)
          maxdR2 = p.drthObarrel * p.drthObarrel;
        const float dr2 = dphi * dphi + dctheta * dctheta;
        near(2, dr2, maxdR2);
        if (dr2 < maxdR2)
          continue;
        near(3, std::abs(v[j].params().v[3] - v[i].params().v[3]), hc::kMaxd1pt);
      }
  }
}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::printf("usage: %s dump.bin [nRepeat]\n", argv[0]);
    return 2;
  }
  const int nRepeat = argc > 2 ? std::atoi(argv[2]) : 5;
  cleandump::Dump dump;
  if (!cleandump::readDump(argv[1], dump)) {
    std::printf("cannot read %s\n", argv[1]);
    return 2;
  }
  mkfit::IterationConfig itc;
  itc.dc_fracSharedHits = dump.fp[0];
  itc.dc_drth_central = dump.fp[1];
  itc.dc_drth_obarrel = dump.fp[2];
  itc.dc_drth_forward = dump.fp[3];

  size_t cap = 16;
  for (auto const& e : dump.events)
    cap = std::max(cap, e.noDC.size());
  mkfitdev::TrackSoAHostCollection soa(cap);

  long nTracks = 0, roundTripBad = 0, flagMismatch = 0;
  Margins marg;
  const mkfitdev::clean::DupCleanParams params{dump.fp[0], dump.fp[1], dump.fp[2], dump.fp[3]};
  double msSum = 0, msMinSum = 0;
  for (auto const& e : dump.events) {
    mkfit::TrackVec tv;
    for (auto const& d : e.noDC)
      tv.push_back(toTrack(d));
    // round trip through the TrackSoA
    mkfitdev::tracksToSoA(tv, soa.view());
    mkfit::TrackVec back = mkfitdev::tracksFromSoA(soa.const_view());
    for (size_t i = 0; i < tv.size(); ++i)
      roundTripBad += !same(tv[i], back[i]);
    nTracks += tv.size();
    marginScan(soa.const_view(), params, marg);
    // stock cleaner on the round-tripped tracks; survivors marked through chi2 (not read by the cleaner)
    double mn = 1e30;
    std::vector<int8_t> dup;
    for (int rep = 0; rep < nRepeat; ++rep) {
      mkfit::TrackVec work(back);
      for (size_t i = 0; i < work.size(); ++i)
        work[i].setChi2(float(i));
      const auto t0 = std::chrono::steady_clock::now();
      mkfit::StdSeq::clean_duplicates_sharedhits_pixelseed(work, itc, mkfit::TrackerInfo{});  // TrackerInfo unread
      const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
      msSum += ms;
      mn = std::min(mn, ms);
      dup.assign(tv.size(), 1);
      for (auto const& t : work)
        dup[int(t.chi2())] = 0;
    }
    msMinSum += mn;
    for (size_t i = 0; i < tv.size(); ++i)
      flagMismatch += (dup[i] != e.noDC[i].a);
  }
  const double nev = dump.events.size();
  std::printf("events %.0f tracks %ld: TrackSoA round-trip mismatches %ld; stock decisions vs dump mismatches %ld\n",
              nev, nTracks, roundTripBad, flagMismatch);
  std::printf("stock clean_duplicates_sharedhits_pixelseed: %.3f ms/event mean, %.3f ms/event (min of %d per event)\n",
              nev ? msSum / (nev * nRepeat) : 0., nev ? msMinSum / nev : 0., nRepeat);
  std::printf("near-threshold pairs (relative margin < 1e-6 | < 1e-4) out of %ld pairs: dcottheta %ld | %ld, dphi %ld | %ld, "
              "dR2 %ld | %ld, d(1/pT) %ld | %ld\n",
              marg.pairs, marg.near[0][0], marg.near[1][0], marg.near[0][1], marg.near[1][1], marg.near[0][2],
              marg.near[1][2], marg.near[0][3], marg.near[1][3]);
  std::printf("pairs near a pre-cut (1e-4) whose decision would flip if the pre-cut flipped: %ld\n", marg.sensitive);
  return (roundTripBad || flagMismatch) ? 1 : 0;
}
