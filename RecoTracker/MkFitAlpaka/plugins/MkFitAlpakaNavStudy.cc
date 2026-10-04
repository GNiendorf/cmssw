// Stage C (round 8, lane stagec): first piece of the device missing-hit navigation - is the SimpleNavigationSchool
// navigation TABLE-DRIVEN? The output converter calls compatibleLayers(innerLayer, fts, oppositeToMomentum) and
// compatibleLayers(outerLayer, fts, alongMomentum) for every track (fts = the state at the first hit). compatibleLayers
// is the closure of nextLayers(layer, fts, dir); nextLayers (SimpleBarrel/ForwardNavigableLayer) picks ONE static
// candidate list from a few direction bits of fts and keeps, in order, the candidates whose error-free analytical
// propagation lands inside the (thickness-extended) layer bounds, stopping at the first one well inside.
// This analyzer checks, on real tracks, with the same first-hit states as MkFitAlpakaPcaCheck:
//   (1) the closure: an own BFS over nextLayers reproduces compatibleLayers (as sets);
//   (2) per key (layer, direction, bits: transverse in-out, z in-out, pz > 0) every observed nextLayers result is an
//       ordered subsequence of ONE candidate list (=> a static ES table per key + a device propagation test suffices);
//   (3) the sizes: candidate lists, compatible layers per track and direction (fixed device capacities).
//   (5) steps (a)+(b) end to end: compatibleLayers EMULATED from the branch logic of SimpleBarrel/Forward-
//       NavigableLayer::nextLayers, candidate lists rebuilt from the school's public static lists (nextLayers(insideOut /
//       outsideIn) + TkLayerLess, as the school builds them), the stop rule, and the device-style helix (or the stock
//       propagator); the per-track compatible-layer SET compared with the stock one.
//   (4) step (b) of the design: for every layer a nextLayers call RETURNED, the wellInside decision re-evaluated
//       (A) with the stock AnalyticalPropagator (the logic transliteration; must say "pushed" for all) and (B) with a
//       device-style double helix with the field z at the start point (the device model): pushed / well-inside
//       decision flips and the crossing-point differences A vs B.
// NAVSTUDY lines at endJob.
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <map>
#include <mutex>
#include <set>
#include <tuple>
#include <vector>

#include "DataFormats/TrackReco/interface/Track.h"
#include "DataFormats/TrackReco/interface/TrackFwd.h"
#include "DataFormats/TrackingRecHit/interface/TrackingRecHit.h"
#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/Framework/interface/global/EDAnalyzer.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include "MagneticField/Engine/interface/MagneticField.h"
#include "MagneticField/Records/interface/IdealMagneticFieldRecord.h"
#include "RecoTracker/Record/interface/NavigationSchoolRecord.h"
#include "RecoTracker/Record/interface/TrackerRecoGeometryRecord.h"
#include "RecoTracker/TkDetLayers/interface/GeometricSearchTracker.h"
#include "DataFormats/GeometrySurface/interface/BoundCylinder.h"
#include "DataFormats/GeometrySurface/interface/BoundDisk.h"
#include "TrackingTools/DetLayers/interface/BarrelDetLayer.h"
#include "TrackingTools/DetLayers/interface/ForwardDetLayer.h"
#include "TrackingTools/DetLayers/interface/TkLayerLess.h"
#include "TrackingTools/DetLayers/interface/DetLayer.h"
#include "TrackingTools/DetLayers/interface/NavigationSchool.h"
#include "TrackingTools/GeomPropagators/interface/AnalyticalPropagator.h"
#include "TrackingTools/TrajectoryState/interface/FreeTrajectoryState.h"
#include "TrackingTools/TrajectoryState/interface/TrajectoryStateOnSurface.h"
#include "TrackingTools/TrajectoryState/interface/TrajectoryStateTransform.h"

class MkFitAlpakaNavStudy : public edm::global::EDAnalyzer<> {
public:
  explicit MkFitAlpakaNavStudy(edm::ParameterSet const& p)
      : tracks_{consumes(p.getParameter<edm::InputTag>("tracks"))},
        mf_{esConsumes()},
        nav_{esConsumes(p.getParameter<edm::ESInputTag>("NavigationSchool"))},
        gst_{esConsumes()} {}

  static void fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
    edm::ParameterSetDescription desc;
    desc.add<edm::InputTag>("tracks", edm::InputTag("hltInitialStepTracks"));
    desc.add<edm::ESInputTag>("NavigationSchool", edm::ESInputTag{"", "SimpleNavigationSchool"});
    descriptions.addWithDefaultLabel(desc);
  }

  void analyze(edm::StreamID, edm::Event const& ev, edm::EventSetup const& es) const override {
    auto const& trks = ev.get(tracks_);
    auto const& mf = es.getData(mf_);
    auto const& nav = es.getData(nav_);
    auto const& gst = es.getData(gst_);
    const AnalyticalPropagator prop(&mf, anyDirection);
    using Key = std::tuple<int, int, int>;  // layer seqNum, dir (0 opposite, 1 along), bits
    std::vector<std::pair<Key, std::vector<int>>> obs;
    long long nTrk = 0, closureMismatch = 0, closureOverflow = 0;
    std::array<std::array<long long, 32>, 2> hist{};
    Emu emu, emuAll;
    long long refNs = 0, refCalls = 0;
    std::array<std::array<long long, 4>, 2> emuCmp{};  // [stock prop | helix][compared, differ, missing, extra]
    for (auto const& trk : trks) {
      if (trk.recHitsSize() < 2)
        continue;
      const auto first = trk.recHit(0), last = trk.recHit(trk.recHitsSize() - 1);
      if (!first->isValid() || !last->isValid() || first->det() == nullptr)
        continue;
      const FreeTrajectoryState pcaFts = trajectoryStateTransform::initialFreeState(trk, &mf);
      const TrajectoryStateOnSurface t1 = prop.propagate(pcaFts, first->det()->surface());
      if (!t1.isValid())
        continue;
      const FreeTrajectoryState fts = *t1.freeState();
      const DetLayer* inner = gst.idToLayer(first->geographicalId());
      const DetLayer* outer = gst.idToLayer(last->geographicalId());
      if (inner == nullptr || outer == nullptr)
        continue;
      ++nTrk;
      const auto pos = fts.position();
      const auto mom = fts.momentum();
      const int bits = (pos.x() * mom.x() + pos.y() * mom.y() > 0 ? 1 : 0) | (mom.z() * pos.z() > 0 ? 2 : 0) |
                       (mom.z() > 0 ? 4 : 0);
      for (int d = 0; d < 2; ++d) {
        const DetLayer* start = d == 0 ? inner : outer;
        const PropagationDirection dir = d == 0 ? oppositeToMomentum : alongMomentum;
        const auto c0 = std::chrono::steady_clock::now();
        const auto ref = nav.compatibleLayers(*start, fts, dir);
        refNs += std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - c0).count();
        ++refCalls;
        // own closure, as SimpleNavigableLayer::compatibleLayers (sets, <= 150 rounds)
        std::set<const DetLayer*> collect, toTry, nextToTry;
        auto first = nav.nextLayers(*start, fts, dir);
        obs.emplace_back(Key{start->seqNum(), d, bits}, seqs(first));
        for (auto l : first)
          emulate(*start, *l, fts, dir, mf, emu);
        allNeighbours(nav, *start, fts, dir, mf, emuAll);
        toTry.insert(first.begin(), first.end());
        int counter = 0;
        while (!toTry.empty() && (counter++) <= 150) {
          nextToTry.clear();
          for (auto l : toTry) {
            if (!collect.insert(l).second)
              continue;
            auto nl = nav.nextLayers(*l, fts, dir);
            obs.emplace_back(Key{l->seqNum(), d, bits}, seqs(nl));
            for (auto m : nl)
              emulate(*l, *m, fts, dir, mf, emu);
            allNeighbours(nav, *l, fts, dir, mf, emuAll);
            nextToTry.insert(nl.begin(), nl.end());
          }
          toTry.swap(nextToTry);
        }
        if (counter >= 150)
          ++closureOverflow;
        const std::set<const DetLayer*> refSet(ref.begin(), ref.end());
        closureMismatch += (refSet != collect);
        for (int m = 0; m < 2; ++m) {
          const auto emuSet = emulateCompatible(nav, *start, fts, dir, mf, m == 1);
          ++emuCmp[m][0];
          if (emuSet != refSet) {
            ++emuCmp[m][1];
            bool sub = std::includes(refSet.begin(), refSet.end(), emuSet.begin(), emuSet.end());
            bool sup = std::includes(emuSet.begin(), emuSet.end(), refSet.begin(), refSet.end());
            emuCmp[m][2] += sub && !sup;  // emulation misses layers
            emuCmp[m][3] += sup && !sub;  // emulation has extra layers
          }
        }
        ++hist[d][std::min<size_t>(31, ref.size())];
      }
    }
    std::lock_guard<std::mutex> lk(mtx_);
    emu_.add(emu);
    refNs_ += refNs;
    refCalls_ += refCalls;
    for (int m = 0; m < 2; ++m)
      for (int k = 0; k < 4; ++k)
        emuCmp_[m][k] += emuCmp[m][k];
    emuAll_.add(emuAll);
    nTrk_ += nTrk;
    closureMismatch_ += closureMismatch;
    closureOverflow_ += closureOverflow;
    for (int d = 0; d < 2; ++d)
      for (int k = 0; k < 32; ++k)
        hist_[d][k] += hist[d][k];
    for (auto& [key, l] : obs) {
      auto& e = table_[key];
      ++e.calls;
      if (e.distinct.size() < 256)
        ++e.distinct[l];
      else if (!e.distinct.count(l))
        ++e.overflow;
    }
  }

  void endJob() override {
    std::printf("NAVSTUDY-COST host NavigationSchool::compatibleLayers %lld calls, %.2f us per call (this job, loaded box)\n",
                refCalls_,
                refCalls_ ? refNs_ * 1e-3 / refCalls_ : 0.);
    for (int m = 0; m < 2; ++m)
      std::printf(
          "NAVSTUDY-EMUSET (%s) track x direction %lld: compatible-layer set differs from stock %lld (emulation misses "
          "layers %lld, has extra layers %lld, other %lld)\n",
          m == 0 ? "rebuilt lists + stock AnalyticalPropagator" : "rebuilt lists + device-style helix",
          emuCmp_[m][0],
          emuCmp_[m][1],
          emuCmp_[m][2],
          emuCmp_[m][3],
          emuCmp_[m][1] - emuCmp_[m][2] - emuCmp_[m][3]);
    printEmu("NAVSTUDY-EMU-ALL (every static neighbour of every visited layer)", emuAll_);
    std::printf(
        "NAVSTUDY-EMU returned layers %lld (self-crossing skipped %lld) | stock propagator: invalid %lld, crossing-side "
        "rejected %lld, NOT pushed %lld, well inside %lld | device helix: invalid %lld, NOT pushed %lld, well inside %lld | "
        "decision flips helix vs stock: pushed %lld, well-inside %lld | crossing |dz| max %.3g cm (>1e-4: %lld), |dr| "
        "max %.3g cm (>1e-4: %lld)\n",
        emu_.n,
        emu_.self,
        emu_.aInvalid,
        emu_.aSide,
        emu_.aNotPushed,
        emu_.aWell,
        emu_.bInvalid,
        emu_.bNotPushed,
        emu_.bWell,
        emu_.flipPushed,
        emu_.flipWell,
        emu_.maxDz,
        emu_.nDz,
        emu_.maxDr,
        emu_.nDr);
    // per key: are ALL observed results ordered subsequences of ONE static list? <=> the precedence relation "a before
    // b in some result" is acyclic; then a topological order is a candidate list consistent with every result
    // (cand = that order; its size = the number of distinct layers ever returned for the key)
    long long keys = 0, badKeys = 0, badCalls = 0, calls = 0, overflow = 0;
    size_t maxCand = 0, maxDistinct = 0;
    for (auto& [key, e] : table_) {
      ++keys;
      calls += e.calls;
      overflow += e.overflow;
      maxDistinct = std::max(maxDistinct, e.distinct.size());
      std::map<int, std::set<int>> after;  // a -> layers seen after a
      std::set<int> nodes;
      for (auto const& [l, c] : e.distinct)
        for (size_t i = 0; i < l.size(); ++i) {
          nodes.insert(l[i]);
          for (size_t j = i + 1; j < l.size(); ++j)
            after[l[i]].insert(l[j]);
        }
      // Kahn's algorithm; leftover nodes = a cycle
      std::map<int, int> indeg;
      for (int nd : nodes)
        indeg[nd] = 0;
      for (auto const& [a, bs] : after)
        for (int b : bs)
          ++indeg[b];
      std::vector<int> order, ready;
      for (auto const& [nd, d] : indeg)
        if (d == 0)
          ready.push_back(nd);
      while (!ready.empty()) {
        const int nd = ready.back();
        ready.pop_back();
        order.push_back(nd);
        for (int b : after[nd])
          if (--indeg[b] == 0)
            ready.push_back(b);
      }
      e.cand = order;
      if (order.size() != nodes.size()) {
        for (auto const& [l, c] : e.distinct) {
          e.inconsistent += c;
          if (e.examples.size() < 3)
            e.examples.push_back(l);
        }
      }
      badKeys += e.inconsistent > 0;
      badCalls += e.inconsistent;
      maxCand = std::max(maxCand, e.cand.size());
    }
    std::printf(
        "NAVSTUDY tracks %lld | closure (own BFS over nextLayers) != compatibleLayers: %lld, closure overflows %lld | "
        "keys (layer, dir, bits) %lld, nextLayers calls %lld, calls in keys whose results admit NO common static list "
        "(precedence cycle) %lld (keys %lld) | max candidate list %zu, max distinct results per key %zu, distinct-table overflow %lld\n",
        nTrk_,
        closureMismatch_,
        closureOverflow_,
        keys,
        calls,
        badCalls,
        badKeys,
        maxCand,
        maxDistinct,
        overflow);
    for (int d = 0; d < 2; ++d) {
      std::printf("NAVSTUDY compatible layers per track (%s):", d == 0 ? "inner/opposite" : "outer/along");
      for (int k = 0; k < 32; ++k)
        if (hist_[d][k])
          std::printf(" %d:%lld", k, hist_[d][k]);
      std::printf("\n");
    }
    int shown = 0;
    for (auto const& [key, e] : table_)
      if (e.inconsistent > 0 && shown++ < 6) {
        std::printf("NAVSTUDY-BAD layer %d dir %d bits %d calls %lld bad %lld cand:",
                    std::get<0>(key),
                    std::get<1>(key),
                    std::get<2>(key),
                    e.calls,
                    e.inconsistent);
        for (int s : e.cand)
          std::printf(" %d", s);
        for (auto const& x : e.examples) {
          std::printf(" | example:");
          for (int s : x)
            std::printf(" %d", s);
        }
        std::printf("\n");
      }
    std::fflush(stdout);
  }

private:
  struct Emu {
    long long n = 0, self = 0, aInvalid = 0, aSide = 0, aNotPushed = 0, aWell = 0, bInvalid = 0, bNotPushed = 0,
              bWell = 0, flipPushed = 0, flipWell = 0, nDz = 0, nDr = 0;
    double maxDz = 0, maxDr = 0;
    void add(Emu const& o) {
      n += o.n;
      self += o.self;
      aInvalid += o.aInvalid;
      aSide += o.aSide;
      aNotPushed += o.aNotPushed;
      aWell += o.aWell;
      bInvalid += o.bInvalid;
      bNotPushed += o.bNotPushed;
      bWell += o.bWell;
      flipPushed += o.flipPushed;
      flipWell += o.flipWell;
      nDz += o.nDz;
      nDr += o.nDr;
      maxDz = std::max(maxDz, o.maxDz);
      maxDr = std::max(maxDr, o.maxDr);
    }
  };
  // device-style helix (double, field z at the start point, as AnalyticalPropagator's helix): crossing with the
  // cylinder r = R (barrel) or the plane z = Z (disk) in direction dir; returns false if not reachable.
  static bool helixTo(GlobalPoint const& x0, GlobalVector const& p0, int q, double bz, bool barrel, double RZ,
                      PropagationDirection dir, double out[6]) {
    const double pt = std::hypot(double(p0.x()), double(p0.y()));
    const double kappa = -2.99792458e-3 * q * bz / pt;  // signed transverse curvature (1/cm)
    const double phi0 = std::atan2(double(p0.y()), double(p0.x()));
    const double sgn = dir == alongMomentum ? 1. : -1.;
    double s = 0;  // transverse path length
    if (!barrel) {
      if (p0.z() == 0)
        return false;
      s = (RZ - x0.z()) * pt / p0.z();
      if (s * sgn <= 0)
        return false;
    } else {
      const double xc = x0.x() - std::sin(phi0) / kappa, yc = x0.y() + std::cos(phi0) / kappa;
      const double rho = 1. / std::abs(kappa), d = std::hypot(xc, yc);
      if (d > RZ + rho || d < std::abs(RZ - rho))
        return false;
      // the two crossings of the circles; phases relative to the start point around the helix centre
      const double a = (RZ * RZ - rho * rho + d * d) / (2 * d), h = std::sqrt(std::max(0., RZ * RZ - a * a));
      const double ux = xc / d, uy = yc / d;
      const double best0 = 1e30;
      double best = best0;
      for (int k = 0; k < 2; ++k) {
        const double px = a * ux + (k ? -h : h) * (-uy), py = a * uy + (k ? -h : h) * ux;
        const double a0 = std::atan2(x0.y() - yc, x0.x() - xc), a1 = std::atan2(py - yc, px - xc);
        double dphi = a1 - a0;  // rotation of the radius vector = kappa * s
        // s = dphi / kappa, folded into the propagation direction
        double sk = dphi / kappa;
        const double period = 2 * M_PI / std::abs(kappa);
        while (sk * sgn <= 0)
          sk += sgn * period;
        while (sk * sgn > period)
          sk -= sgn * period;
        if (std::abs(sk) < std::abs(best))
          best = sk;
      }
      if (best == best0)
        return false;
      s = best;
    }
    if (std::abs(kappa * s) > 1.6)  // AnalyticalPropagator's maxDPhi (default 1.6 rad of transverse turning)
      return false;
    const double phi = phi0 + kappa * s;
    out[0] = x0.x() + (std::sin(phi) - std::sin(phi0)) / kappa;
    out[1] = x0.y() - (std::cos(phi) - std::cos(phi0)) / kappa;
    out[2] = x0.z() + s * p0.z() / pt;
    out[3] = std::cos(phi);
    out[4] = std::sin(phi);
    out[5] = p0.z() / pt;
    return true;
  }
  // re-evaluate SimpleNavigableLayer::wellInside for a layer the call returned (error-free state, crossing side on)
  using DLV = std::vector<const DetLayer*>;
  // one wellInside decision (pushed, well inside) with the stock propagator (helix = false) or the device helix
  static void decideOne(DetLayer const& to, FreeTrajectoryState const& fts, PropagationDirection dir,
                        MagneticField const& mf, bool helix, bool& pushed, bool& well) {
    pushed = well = false;
    const bool barrel = to.isBarrel();
    double x, y, z, dx, dy, dz;
    if (!helix) {
      const FreeTrajectoryState f0(fts.parameters());
      AnalyticalPropagator prop(&mf);
      prop.setPropagationDirection(dir);
      const auto ts = barrel ? prop.propagate(f0, static_cast<BarrelDetLayer const&>(to).specificSurface())
                             : prop.propagate(f0, static_cast<ForwardDetLayer const&>(to).specificSurface());
      if (!ts.isValid())
        return;
      const auto gp = ts.globalPosition();
      const auto gd = ts.globalDirection();
      x = gp.x(), y = gp.y(), z = gp.z(), dx = gd.x(), dy = gd.y(), dz = gd.z();
    } else {
      double b[6];
      const double rz = barrel ? static_cast<BarrelDetLayer const&>(to).specificSurface().radius()
                               : static_cast<ForwardDetLayer const&>(to).specificSurface().position().z();
      if (!helixTo(fts.position(), fts.momentum(), fts.charge(), mf.inTesla(fts.position()).z(), barrel, rz, dir, b))
        return;
      x = b[0], y = b[1], z = b[2], dx = b[3], dy = b[4], dz = b[5];
    }
    const auto& p0 = fts.position();
    if ((p0.x() * x + p0.y() * y) < 0 || (p0.x() * x + p0.y() * y + p0.z() * z) < 0)
      return;  // crossing side
    const auto& bounds = to.surface().bounds();
    if (barrel) {
      const float length = bounds.length() * 0.5f;
      const float deltaZ = 0.5f * bounds.thickness() * std::abs(dz) / std::hypot(dx, dy);
      pushed = std::abs(z) < length + deltaZ;
      well = std::abs(z) < length - deltaZ;
    } else {
      auto const& disk = static_cast<ForwardDetLayer const&>(to).specificSurface();
      const float rpos = std::hypot(x, y);
      const float deltaR = 0.5f * bounds.thickness() * std::hypot(dx, dy) / std::abs(dz);
      pushed = disk.innerRadius() - deltaR < rpos && rpos < disk.outerRadius() + deltaR;
      well = disk.innerRadius() + deltaR < rpos && rpos < disk.outerRadius() - deltaR;
    }
  }
  // SimpleNavigableLayer::wellInside over a list: push the inside ones, stop at the first well inside
  static bool wellInsideList(DLV const& l, FreeTrajectoryState const& fts, PropagationDirection dir,
                             MagneticField const& mf, bool helix, DLV& result) {
    for (auto c : l) {
      bool pushed, well;
      decideOne(*c, fts, dir, mf, helix, pushed, well);
      if (pushed)
        result.push_back(c);
      if (well)
        return true;
    }
    return false;
  }
  static DLV pick(DLV const& l, int what) {  // 0 barrel, 1 forward z < 0, 2 forward z > 0, 3 forward
    DLV r;
    for (auto c : l) {
      const bool b = c->isBarrel();
      const double z = c->position().z();
      if ((what == 0 && b) || (what == 1 && !b && z < 0) || (what == 2 && !b && z > 0) || (what == 3 && !b))
        r.push_back(c);
    }
    return r;
  }
  static DLV sorted(DLV l, TkLayerLess const& less) {
    std::stable_sort(l.begin(), l.end(), less);  // the school's own std::sort with the same comparator (validation)
    return l;
  }
  // SimpleBarrel/ForwardNavigableLayer::nextLayers(fts, dir) with lists rebuilt from the static ones
  static DLV nextLayersEmu(NavigationSchool const& nav, DetLayer const& l, FreeTrajectoryState const& fts,
                           PropagationDirection dir, MagneticField const& mf, bool helix) {
    const DLV sOut = nav.nextLayers(l, insideOut), sIn = nav.nextLayers(l, outsideIn);
    const auto pos = fts.position();
    const auto mom = fts.momentum();
    const bool inOutB = pos.x() * mom.x() + pos.y() * mom.y() > 0;
    const bool inOutF = mom.z() * pos.z() > 0;
    const bool along = dir != oppositeToMomentum;
    const bool xb = (along && inOutB) || (!along && !inOutB);
    const bool xf = (along && inOutF) || (!along && !inOutF);
    DLV result;
    if (l.isBarrel()) {
      const DLV oB = pick(sOut, 0), oL = pick(sOut, 1), oR = pick(sOut, 2);
      const DLV iB = pick(sIn, 0), iL = pick(sIn, 1), iR = pick(sIn, 2);
      const bool signZ = ((mom.z() > 0) && dir != alongMomentum) || (!(mom.z() > 0) && dir == alongMomentum);
      auto cat = [](DLV a, DLV const& b) {
        a.insert(a.end(), b.begin(), b.end());
        return a;
      };
      if (xb && xf) {
        wellInsideList(signZ ? sorted(cat(oB, oL), TkLayerLess()) : sorted(cat(oB, oR), TkLayerLess()), fts, dir, mf,
                       helix, result);
      } else if (!xb && !xf) {
        wellInsideList(signZ ? sorted(cat(iB, iR), TkLayerLess(outsideIn)) : sorted(cat(iB, iL), TkLayerLess(outsideIn)),
                       fts, dir, mf, helix, result);
      } else if (!xb && xf) {
        wellInsideList(iB, fts, dir, mf, helix, result);
        if (signZ) {
          wellInsideList(iL, fts, dir, mf, helix, result);
          wellInsideList(oL, fts, dir, mf, helix, result);
        } else {
          wellInsideList(iR, fts, dir, mf, helix, result);
          wellInsideList(oR, fts, dir, mf, helix, result);
        }
      } else {
        wellInsideList(signZ ? iL : iR, fts, dir, mf, helix, result);
        wellInsideList(oB, fts, dir, mf, helix, result);
      }
    } else {
      if (xf && xb)
        wellInsideList(sOut, fts, dir, mf, helix, result);
      else if (!xf && !xb)
        wellInsideList(sIn, fts, dir, mf, helix, result);
      else if (!xf && xb) {
        wellInsideList(pick(sIn, 3), fts, dir, mf, helix, result);
        wellInsideList(pick(sOut, 0), fts, dir, mf, helix, result);
      } else {
        wellInsideList(pick(sIn, 0), fts, dir, mf, helix, result);
        wellInsideList(pick(sOut, 3), fts, dir, mf, helix, result);
      }
    }
    return result;
  }
  static std::set<const DetLayer*> emulateCompatible(NavigationSchool const& nav, DetLayer const& start,
                                                     FreeTrajectoryState const& fts, PropagationDirection dir,
                                                     MagneticField const& mf, bool helix) {
    std::set<const DetLayer*> collect, toTry, nextToTry;
    const DLV first = nextLayersEmu(nav, start, fts, dir, mf, helix);
    if (first.empty())
      return collect;
    toTry.insert(first.begin(), first.end());
    int counter = 0;
    while (!toTry.empty() && (counter++) <= 150) {
      nextToTry.clear();
      for (auto l : toTry) {
        if (!collect.insert(l).second)
          continue;
        const DLV nl = nextLayersEmu(nav, *l, fts, dir, mf, helix);
        nextToTry.insert(nl.begin(), nl.end());
      }
      toTry.swap(nextToTry);
    }
    return collect;
  }
  static void printEmu(const char* tag, Emu const& e) {
    std::printf(
        "%s evaluations %lld | stock: invalid %lld, crossing-side %lld, pushed %lld, well inside %lld | helix: invalid "
        "%lld, pushed %lld, well inside %lld | flips helix vs stock: pushed %lld, well-inside %lld | crossing |dz| max "
        "%.3g cm, |dr| max %.3g cm\n",
        tag,
        e.n,
        e.aInvalid,
        e.aSide,
        e.n - e.self - e.aInvalid - e.aNotPushed,
        e.aWell,
        e.bInvalid,
        e.n - e.self - e.bInvalid - e.bNotPushed,
        e.bWell,
        e.flipPushed,
        e.flipWell,
        e.maxDz,
        e.maxDr);
  }
  static void emulate(DetLayer const& from, DetLayer const& to, FreeTrajectoryState const& fts, PropagationDirection dir,
                      MagneticField const& mf, Emu& e) {
    ++e.n;
    if (&from == &to) {  // crossingState (self search): not emulated
      ++e.self;
      return;
    }
    const FreeTrajectoryState f0(fts.parameters());
    AnalyticalPropagator prop(&mf);
    prop.setPropagationDirection(dir);
    const bool barrel = to.isBarrel();
    TrajectoryStateOnSurface ts = barrel ? prop.propagate(f0, static_cast<BarrelDetLayer const&>(to).specificSurface())
                                         : prop.propagate(f0, static_cast<ForwardDetLayer const&>(to).specificSurface());
    double b[6];
    const double rz = barrel ? static_cast<BarrelDetLayer const&>(to).specificSurface().radius()
                             : static_cast<ForwardDetLayer const&>(to).specificSurface().position().z();
    const bool bOk = helixTo(fts.position(), fts.momentum(), fts.charge(), mf.inTesla(fts.position()).z(), barrel, rz,
                             dir, b);
    auto decide = [&](double x, double y, double z, double dx, double dy, double dz, bool& pushed, bool& well) {
      const auto& bounds = to.surface().bounds();
      if (barrel) {
        const float length = bounds.length() * 0.5f;
        const float deltaZ = 0.5f * bounds.thickness() * std::abs(dz) / std::hypot(dx, dy);
        pushed = std::abs(z) < length + deltaZ;
        well = std::abs(z) < length - deltaZ;
      } else {
        auto const& disk = static_cast<ForwardDetLayer const&>(to).specificSurface();
        const float rpos = std::hypot(x, y);
        const float deltaR = 0.5f * bounds.thickness() * std::hypot(dx, dy) / std::abs(dz);
        pushed = disk.innerRadius() - deltaR < rpos && rpos < disk.outerRadius() + deltaR;
        well = disk.innerRadius() + deltaR < rpos && rpos < disk.outerRadius() - deltaR;
      }
    };
    bool aPushed = false, aWell = false, bPushed = false, bWell = false;
    if (!ts.isValid())
      ++e.aInvalid;
    else {
      const auto gp = ts.globalPosition();
      const bool side = (fts.position().x() * gp.x() + fts.position().y() * gp.y()) < 0 ||
                        gp.basicVector().dot(fts.position().basicVector()) < 0;
      if (side)
        ++e.aSide;
      const auto gd = ts.globalDirection();
      decide(gp.x(), gp.y(), gp.z(), gd.x(), gd.y(), gd.z(), aPushed, aWell);
      aPushed = aPushed && !side;
      e.aNotPushed += !aPushed;
      e.aWell += aWell;
      if (bOk) {
        const double dzc = std::abs(b[2] - gp.z()), drc = std::abs(std::hypot(b[0], b[1]) - gp.perp());
        e.maxDz = std::max(e.maxDz, dzc);
        e.maxDr = std::max(e.maxDr, barrel ? std::hypot(b[0] - gp.x(), b[1] - gp.y()) : drc);
        e.nDz += dzc > 1e-4;
        e.nDr += (barrel ? std::hypot(b[0] - gp.x(), b[1] - gp.y()) : drc) > 1e-4;
      }
    }
    if (!bOk)
      ++e.bInvalid;
    else {
      decide(b[0], b[1], b[2], b[3], b[4], b[5], bPushed, bWell);
      const bool side = (fts.position().x() * b[0] + fts.position().y() * b[1]) < 0 ||
                        (fts.position().x() * b[0] + fts.position().y() * b[1] + fts.position().z() * b[2]) < 0;
      bPushed = bPushed && !side;
      e.bNotPushed += !bPushed;
      e.bWell += bWell;
    }
    e.flipPushed += aPushed != bPushed;
    e.flipWell += aWell != bWell;
  }
  // every static neighbour of a layer (NavigableLayer::nextLayers(insideOut) + nextLayers(outsideIn), i.e. all
  // candidate lists of all branches), evaluated with both propagators: also the candidates a call did NOT return
  static void allNeighbours(NavigationSchool const& nav, DetLayer const& l, FreeTrajectoryState const& fts,
                            PropagationDirection dir, MagneticField const& mf, Emu& e) {
    std::set<const DetLayer*> all;
    for (auto nd : {insideOut, outsideIn})
      for (auto m : nav.nextLayers(l, nd))
        all.insert(m);
    for (auto m : all)
      emulate(l, *m, fts, dir, mf, e);
  }
  static std::vector<int> seqs(std::vector<const DetLayer*> const& v) {
    std::vector<int> r;
    r.reserve(v.size());
    for (auto l : v)
      r.push_back(l->seqNum());
    return r;
  }
  struct Entry {
    std::vector<int> cand;
    std::map<std::vector<int>, long long> distinct;
    long long calls = 0, inconsistent = 0, overflow = 0;
    std::vector<std::vector<int>> examples;
  };
  const edm::EDGetTokenT<reco::TrackCollection> tracks_;
  const edm::ESGetToken<MagneticField, IdealMagneticFieldRecord> mf_;
  const edm::ESGetToken<NavigationSchool, NavigationSchoolRecord> nav_;
  const edm::ESGetToken<GeometricSearchTracker, TrackerRecoGeometryRecord> gst_;
  mutable std::mutex mtx_;
  mutable long long nTrk_ = 0, closureMismatch_ = 0, closureOverflow_ = 0;
  mutable std::array<std::array<long long, 32>, 2> hist_{};
  mutable Emu emu_, emuAll_;
  mutable long long refNs_ = 0, refCalls_ = 0;
  mutable std::array<std::array<long long, 4>, 2> emuCmp_{};
  mutable std::map<std::tuple<int, int, int>, Entry> table_;
};

DEFINE_FWK_MODULE(MkFitAlpakaNavStudy);
