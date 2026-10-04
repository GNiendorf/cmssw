#ifndef RecoTracker_MkFitAlpaka_plugins_OutConvNavEmulation_h
#define RecoTracker_MkFitAlpaka_plugins_OutConvNavEmulation_h
// Stage C (c), lane stagec round 10: DetLayer::compatibleDets of the Phase-2 tracker layers the output converter
// navigates, transliterated from RecoTracker/TkDetLayers (TBLayer/TBPLayer, Phase2OTBarrelRod, PixelRod,
// Phase2OTtiltedBarrelLayer, tkDetUtil::groupedCompatibleDetsV, Phase2EndcapRing, Phase2EndcapLayerDoubleDisk,
// Phase2EndcapSingleRing, DetGroupMerger, GeomDetCompatibilityChecker) onto layer tables rebuilt from the public
// GeometricSearchDet interface. Used by MkFitAlpakaOutputTrackConverter (study navEmuStudy, switch navEmulated); the
// reference for the device kernel (doc/stagec.txt section 8). Same result as the stock search for every call measured
// (ttbar + QCD, CPU + GPU menus), with the analytic propagator (no material).

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "DataFormats/GeometrySurface/interface/BoundCylinder.h"
#include "DataFormats/GeometrySurface/interface/BoundDisk.h"
#include "DataFormats/GeometrySurface/interface/BoundingBox.h"
#include "DataFormats/GeometrySurface/interface/Plane.h"
#include "DataFormats/GeometrySurface/interface/RectangularPlaneBounds.h"
#include "DataFormats/GeometryVector/interface/VectorUtil.h"
#include "DataFormats/SiStripDetId/interface/StripSubdetector.h"
#include "DataFormats/TrackerCommon/interface/TrackerTopology.h"
#include "FWCore/Utilities/interface/Exception.h"
#include "Geometry/CommonTopologies/interface/GeomDetEnumerators.h"
#include "TrackingTools/DetLayers/interface/CylinderBuilderFromDet.h"
#include "TrackingTools/DetLayers/interface/DetLayer.h"
#include "TrackingTools/DetLayers/interface/ForwardRingDiskBuilderFromDet.h"
#include "TrackingTools/DetLayers/interface/GeometricSearchDet.h"
#include "TrackingTools/DetLayers/interface/MeasurementEstimator.h"
#include "TrackingTools/DetLayers/interface/PeriodicBinFinderInZ.h"
#include "TrackingTools/DetLayers/interface/RodPlaneBuilderFromDet.h"
#include "TrackingTools/DetLayers/interface/rangesIntersect.h"
#include "TrackingTools/GeomPropagators/interface/AnalyticalPropagator.h"
#include "TrackingTools/GeomPropagators/interface/HelixBarrelCylinderCrossing.h"
#include "TrackingTools/GeomPropagators/interface/HelixBarrelPlaneCrossingByCircle.h"
#include "TrackingTools/GeomPropagators/interface/HelixForwardPlaneCrossing.h"
#include "TrackingTools/GeomPropagators/interface/Propagator.h"
#include "TrackingTools/GeomPropagators/interface/StraightLinePlaneCrossing.h"
#include "TrackingTools/KalmanUpdators/interface/Chi2MeasurementEstimator.h"
#include "TrackingTools/TrajectoryState/interface/TrajectoryStateOnSurface.h"
#include "Utilities/BinningTools/interface/GenericBinFinderInZ.h"
#include "Utilities/BinningTools/interface/PeriodicBinFinderInPhi.h"
#include "RecoTracker/MkFitAlpaka/interface/math/DetPlaneTest.h"
#include "RecoTracker/MkFitAlpaka/interface/math/PcaToBeamLine.h"

namespace mkfitdev::outconv {
  // navEmuStudy variant 3 (round 10): the OT endcap search transliterated (tkDetUtil::groupedCompatibleDetsV of
  // TkDetUtil.h + Phase2EndcapRing::groupedCompatibleDetsV + DetGroupMerger), on host types, as a device kernel would
  // do it from ES tables. EmuGroup = DetGroup (index, indexSize, elements).
  struct EmuEl {
    const GeomDet* det;
    TrajectoryStateOnSurface ts;
  };
  struct EmuGroup {
    int index = 0, indexSize = 1;
    std::vector<EmuEl> el;
  };
  using EmuGroups = std::vector<EmuGroup>;
  // DetGroupMerger::mergeTwoLevels / orderAndMergeTwoLevels / addSameLevel
  inline void emuMergeTwoLevels(EmuGroups&& one, EmuGroups&& two, EmuGroups& result) {
    const int s1 = one.front().indexSize, s2 = two.front().indexSize;
    for (auto&& g : one) {
      result.push_back(std::move(g));
      result.back().indexSize = s1 + s2;
    }
    for (auto&& g : two) {
      result.push_back(std::move(g));
      result.back().index += s1;
      result.back().indexSize += s1;
    }
  }
  inline void emuOrderAndMerge(EmuGroups&& one, EmuGroups&& two, EmuGroups& result, int firstIndex, int firstCrossed) {
    if (one.empty() && two.empty())
      return;
    if (one.empty()) {
      result = std::move(two);
      const int s = result.front().indexSize;
      for (auto& g : result) {
        if (firstIndex == firstCrossed) {  // incrementAndDoubleSize
          g.index += s;
          g.indexSize += s;
        } else
          g.indexSize = 2 * s;  // doubleIndexSize
      }
    } else if (two.empty()) {
      result = std::move(one);
      const int s = result.front().indexSize;
      for (auto& g : result) {
        if (firstIndex == firstCrossed)
          g.indexSize = 2 * s;
        else {
          g.index += s;
          g.indexSize += s;
        }
      }
    } else if (firstIndex == firstCrossed)
      emuMergeTwoLevels(std::move(one), std::move(two), result);
    else
      emuMergeTwoLevels(std::move(two), std::move(one), result);
  }
  inline void emuAddSameLevel(EmuGroups&& gvec, EmuGroups& result) {
    for (auto&& ig : gvec) {
      bool found = false;
      for (auto ires = result.begin(); ires != result.end(); ++ires) {
        if (ig.index == ires->index) {
          ires->el.insert(ires->el.end(), ig.el.begin(), ig.el.end());
          found = true;
          break;
        } else if (ig.index < ires->index) {
          result.insert(ires, ig);
          found = true;
          break;
        }
      }
      if (!found)
        result.insert(result.end(), ig);
    }
  }
  // one Phase2EndcapRing as Phase2EndcapRingBuilder (useBrothers) lays it out: basicComponents() = front lower
  // sensors, back lower sensors, front brothers, back brothers; front/back split at the mean z; sub-layer disks at
  // ForwardRingDiskBuilderFromDet's z (middle of the corner z range); PeriodicBinFinderInPhi per sub-layer
  struct EmuRing {
    bool ok = false;
    bool single = false;              // Phase2EndcapSingleRing (pixel double disks): sub[0] only, no brothers
    const BoundDisk* disk = nullptr;  // the ring surface (tkDetUtil crossing + ring order)
    ReferenceCountingPointer<BoundDisk> ownDisk;  // a ring rebuilt from its dets (ForwardRingDiskBuilderFromDet)
    std::array<std::vector<const GeomDet*>, 2> sub, bro;
    std::array<Plane::PlanePointer, 2> plane;
    std::array<float, 2> phiOffset{}, phiStep{}, invPhiStep{};
    float ringR = 0, thetaMin = 0, thetaMax = 0;
  };

  // barrel (round 10): a Phase2OTBarrelRod (inner/outer sub-rods split at the mean r, brothers = upper sensors, bin
  // finders in z, sub-rod planes from RodPlaneBuilderFromDet) or a PixelRod (dets in z, PeriodicBinFinderInZ)
  struct EmuRod {
    bool ok = false, stacked = false;
    std::array<std::vector<const GeomDet*>, 2> sub, bro;
    std::array<Plane::PlanePointer, 2> plane;
    std::array<GenericBinFinderInZ<float, GeomDet>, 2> bf;
    std::vector<const GeomDet*> dets;  // PixelRod
    PeriodicBinFinderInZ<float> pbf;
  };
  // a TBPLayer (inner/outer rods split at the mean r, cylinders from CylinderBuilderFromDet, phi bin finders) and, for
  // a Phase2OTtiltedBarrelLayer, its tilted rings per z side in construction order
  struct EmuBarrel {
    bool ok = false;
    std::array<std::vector<const GeometricSearchDet*>, 2> rods;
    std::array<ReferenceCountingPointer<BoundCylinder>, 2> cyl;
    std::array<PeriodicBinFinderInPhi<float>, 2> bf;
    std::array<std::vector<EmuRing>, 2> rings;  // [negative z | positive z]
  };

  // pixel endcap double disk: sub-disks of single rings
  struct EmuSubDisk {
    float z = 0;
    Plane::PlanePointer plane;
    std::vector<EmuRing> rings;
  };
  struct EmuPixEndcap {
    bool ok = false, doubleDisk = false;
    std::vector<EmuSubDisk> subs;
  };
  // the layer tables of the transliterated navigation, built at first use per stream (the tracker geometry is fixed in
  // a job; a device version builds them at ES time)
  struct NavEmuCache {
    std::unordered_map<const GeometricSearchDet*, EmuRing> rings;
    std::unordered_map<const GeometricSearchDet*, EmuRod> rods;
    std::unordered_map<const DetLayer*, EmuBarrel> barrels;
    std::unordered_map<const DetLayer*, EmuPixEndcap> pixEndcaps;
    // sizing for the device kernel (per groups() call): calls, det tests (compatible evaluations), maxima; histogram
    // of the det tests per call (last bin = 15 and more)
    long long calls = 0, detTests = 0, maxDetTests = 0, maxElements = 0, maxGroups = 0;
    unsigned long long iov = 0;  // TrackerRecoGeometryRecord cache identifier the tables were built for
    // device per-det test check (DetPlaneTest.h): tests, non-rectangular bounds (not checked), sagitta / validity /
    // decision flips, compared crossings, max |dpos| (cm) and > 1 um, max relative local-error difference and > 1e-3
    long long dtTests = 0, dtNonRect = 0, dtSagFlip = 0, dtValidFlip = 0, dtDecFlip = 0, dtCompared = 0, dtDpos1um = 0,
              dtRelErr1e3 = 0;
    double dtMaxDpos = 0, dtMaxRelErr = 0, dtDevOnlyMaxDphi = 0;
    long long dtHostOnly = 0, dtHostOnlyOk = 0, dtDevOnly = 0, dtDevOnlyIn = 0;
    long long dtFullRel1e3 = 0, dtFullDecFlip = 0;
    double dtFullMaxRel = 0;
    void clearTables() {
      rings.clear();
      rods.clear();
      barrels.clear();
      pixEndcaps.clear();
    }
    std::array<long long, 16> detTestsHist{};
  };

  // the transliterated search on the tables of one stream (built at first use)
  class NavEmulation {
  public:
    NavEmulation(TrackerTopology const& tTopo,
                 Chi2MeasurementEstimator const& est,
                 NavEmuCache& cache,
                 std::atomic<long long>& ringBad,
                 std::atomic<long long>& noCross,
                 bool checkDevice = false)
        : tTopo_(tTopo), est_(est), cache_(cache), ringBad_(ringBad), noCross_(noCross), checkDevice_(checkDevice) {}
    // DetLayer::groupedCompatibleDets of one layer, in the stock order; false = the layer is not laid out as expected
    bool groups(const DetLayer* layer,
                TrajectoryStateOnSurface const& start,
                Propagator const& prop,
                EmuGroups& result) const;

  private:
    // first device piece (interface/math/DetPlaneTest.h) vs this det test: sagitta pre-check, helix crossing, local
    // errors from the END curvilinear covariance of the host propagation, the -3 sigma decision
    void checkDetTest(TrajectoryStateOnSurface const& tsos,
                      Plane const& plane,
                      Propagator const& prop,
                      TrajectoryStateOnSurface const& out,
                      bool hostOk,
                      bool sagRejected = false) const {
      if (dynamic_cast<const AnalyticalPropagator*>(&prop) == nullptr)
        return;  // the device test is material-free: compare with the analytic host propagation only
      auto const* rb = dynamic_cast<const RectangularPlaneBounds*>(&plane.bounds());
      ++cache_.dtTests;
      if (rb == nullptr) {
        ++cache_.dtNonRect;
        return;
      }
      namespace nd = mkfitdev::navdev;
      nd::HelixStart st{{tsos.globalPosition().x(), tsos.globalPosition().y(), tsos.globalPosition().z()},
                        {tsos.globalMomentum().x(), tsos.globalMomentum().y(), tsos.globalMomentum().z()},
                        tsos.transverseCurvature()};
      nd::DetPlane pl;
      const auto gx = plane.toGlobal(LocalVector(1, 0, 0)), gy = plane.toGlobal(LocalVector(0, 1, 0)),
                 gz = plane.toGlobal(LocalVector(0, 0, 1));
      for (int i = 0; i < 3; ++i) {
        pl.pos[i] = i == 0 ? plane.position().x() : (i == 1 ? plane.position().y() : plane.position().z());
        pl.ax[i] = i == 0 ? gx.x() : (i == 1 ? gx.y() : gx.z());
        pl.ay[i] = i == 0 ? gy.x() : (i == 1 ? gy.y() : gy.z());
        pl.az[i] = i == 0 ? gz.x() : (i == 1 ? gz.y() : gz.z());
      }
      pl.halfWidth = rb->width() / 2;
      pl.halfLength = rb->length() / 2;
      pl.halfThickness = rb->thickness() / 2;
      const bool along = prop.propagationDirection() == alongMomentum;
      const bool sagOk = nd::sagittaOk(st, pl, along, est_.maxSagitta(), est_.minTolerance2());
      cache_.dtSagFlip += sagOk == sagRejected;
      if (sagRejected || !sagOk)
        return;
      const auto h = nd::helixToPlane(st, pl, along, 1.6);
      if (h.valid != out.isValid()) {
        ++cache_.dtValidFlip;
        if (out.isValid()) {  // host crossing only: a decision flip if the host found the det compatible
          ++cache_.dtHostOnly;
          cache_.dtHostOnlyOk += hostOk;
        } else {  // device crossing only: a possible flip only if the crossing is inside the unshrunk bounds
          ++cache_.dtDevOnly;
          cache_.dtDevOnlyIn += std::abs(h.lx) < pl.halfWidth && std::abs(h.ly) < pl.halfLength;
          if (std::abs(h.lx) < pl.halfWidth && std::abs(h.ly) < pl.halfLength)
            cache_.dtDevOnlyMaxDphi = std::max(cache_.dtDevOnlyMaxDphi, std::abs(st.rho * h.s));
        }
        return;
      }
      if (!h.valid)
        return;
      ++cache_.dtCompared;
      const auto gp = out.globalPosition();
      const double dpos = std::sqrt((h.x[0] - gp.x()) * (h.x[0] - gp.x()) + (h.x[1] - gp.y()) * (h.x[1] - gp.y()) +
                                    (h.x[2] - gp.z()) * (h.x[2] - gp.z()));
      cache_.dtMaxDpos = std::max(cache_.dtMaxDpos, dpos);
      cache_.dtDpos1um += dpos > 1e-4;
      auto const& cc = out.curvilinearError().matrix();
      const double cTT[3] = {cc(3, 3), cc(3, 4), cc(4, 4)};
      double sxx, syy;
      nd::localPositionErrors(pl, h.t, cTT, sxx, syy);
      const auto le = out.localError().positionError();
      const double rel = std::max(std::abs(sxx - le.xx()) / le.xx(), std::abs(syy - le.yy()) / le.yy());
      cache_.dtMaxRelErr = std::max(cache_.dtMaxRelErr, rel);
      cache_.dtRelErr1e3 += rel > 1e-3;
      cache_.dtDecFlip += nd::insideShrunk(pl, h, sxx, syy, -3.) != hostOk;
      // the full device chain: the start covariance transported with pca::curvilinearJacobian over the device crossing
      namespace pc = mkfitdev::pca;
      const auto bf = tsos.magneticField()->inTesla(tsos.globalPosition());
      const auto pm = tsos.globalMomentum().mag();
      const pc::F3 x1{float(st.x[0]), float(st.x[1]), float(st.x[2])}, p1{float(st.p[0]), float(st.p[1]), float(st.p[2])};
      const pc::F3 x2{float(h.x[0]), float(h.x[1]), float(h.x[2])};
      const pc::F3 p2{float(h.t[0] * pm), float(h.t[1] * pm), float(h.t[2] * pm)};
      double J[5][5], C0[5][5], C1[5][5];
      auto const& c0 = tsos.curvilinearError().matrix();
      for (int i = 0; i < 5; ++i)
        for (int j = 0; j < 5; ++j)
          C0[i][j] = c0(i, j);
      pc::curvilinearJacobian(x1, p1, tsos.charge(), x2, p2, h.s, pc::F3{bf.x(), bf.y(), bf.z()}, J);
      pc::similarity5(J, C0, C1);
      const double cT1[3] = {C1[3][3], C1[3][4], C1[4][4]};
      double fxx, fyy;
      nd::localPositionErrors(pl, h.t, cT1, fxx, fyy);
      const double frel = std::max(std::abs(fxx - le.xx()) / le.xx(), std::abs(fyy - le.yy()) / le.yy());
      cache_.dtFullMaxRel = std::max(cache_.dtFullMaxRel, frel);
      cache_.dtFullRel1e3 += frel > 1e-3;
      cache_.dtFullDecFlip += nd::insideShrunk(pl, h, fxx, fyy, -3.) != hostOk;
    }
    TrackerTopology const& tTopo_;
    Chi2MeasurementEstimator const& est_;
    NavEmuCache& cache_;
    std::atomic<long long>& ringBad_;
    std::atomic<long long>& noCross_;
    const bool checkDevice_;
  };

  inline bool NavEmulation::groups(const DetLayer* layer,
                                   TrajectoryStateOnSurface const& start,
                                   Propagator const& prop,
                                   EmuGroups& result) const {
    // the names the transliteration was written with
    long long nDetTests = 0;
    auto const& tTopo = tTopo_;
    auto const& estimator = est_;
    auto& emuRings = cache_.rings;
    auto& emuRods = cache_.rods;
    auto& emuBarrels = cache_.barrels;
    auto& emuPixEndcaps = cache_.pixEndcaps;
    auto& ne3RingBad_ = ringBad_;
    auto& ne3NoCross_ = noCross_;
    // ---- variant 3: the OT endcap search, structurally (see EmuRing) ----
    // a ring from its basicComponents (front lowers, back lowers, front brothers, back brothers); false = not that layout
    auto buildRing = [&](std::vector<const GeomDet*> const& bc, EmuRing& e) {
      if (bc.size() >= 2 && !tTopo.isLower(bc.front()->geographicalId()) &&
          !tTopo.isUpper(bc.front()->geographicalId())) {
        // no brothers (pixel Phase2EndcapRing): front / back split at the mean z
        double mz = 0;
        for (auto const* g : bc)
          mz += g->position().z();
        mz /= bc.size();
        for (auto const* g : bc)
          e.sub[std::abs(g->position().z()) < std::abs(mz) ? 0 : 1].push_back(g);
        if (e.sub[0].empty() || e.sub[1].empty())
          return false;
      } else {
        const size_t half = bc.size() / 2;
        if (bc.size() < 4 || bc.size() % 2 != 0)
          return false;
        double mz = 0, mzb = 0;
        for (size_t i = 0; i < half; ++i) {
          mz += bc[i]->position().z();
          mzb += bc[half + i]->position().z();
        }
        mz /= half;
        mzb /= half;
        for (size_t i = 0; i < half; ++i) {
          e.sub[std::abs(bc[i]->position().z()) < std::abs(mz) ? 0 : 1].push_back(bc[i]);
          e.bro[std::abs(bc[half + i]->position().z()) < std::abs(mzb) ? 0 : 1].push_back(bc[half + i]);
        }
        bool good = !e.sub[0].empty() && !e.sub[1].empty();
        for (int s = 0; s < 2 && good; ++s) {
          good = e.sub[s].size() == e.bro[s].size();
          for (size_t i = 0; good && i < e.sub[s].size(); ++i)
            good = tTopo.stack(e.sub[s][i]->geographicalId()) == tTopo.stack(e.bro[s][i]->geographicalId());
        }
        if (!good)
          return false;
      }
      for (int s = 0; s < 2; ++s) {
        // ForwardRingDiskBuilderFromDet::computeBounds: z range of all corners (start from the first det's centre)
        float zmin = e.sub[s].front()->surface().position().z(), zmax = zmin;
        for (auto const* g : e.sub[s])
          for (auto const& c : BoundingBox().corners(g->specificSurface())) {
            zmin = std::min(zmin, c.z());
            zmax = std::max(zmax, c.z());
          }
        e.plane[s] = Plane::build(Surface::PositionType(0., 0., (zmax + zmin) / 2.), Surface::RotationType());
        constexpr float kTwoPi = 2 * float(3.141592653589793238);
        e.phiStep[s] = kTwoPi / float(e.sub[s].size());
        e.invPhiStep[s] = 1.f / e.phiStep[s];
        e.phiOffset[s] = float(e.sub[s].front()->surface().position().phi()) - 0.5f * e.phiStep[s];
      }
      e.ok = true;
      return true;
    };
    auto ringOf = [&](const GeometricSearchDet* r) -> EmuRing const& {
      auto [it, isNew] = emuRings.try_emplace(r);
      EmuRing& e = it->second;
      if (!isNew)
        return e;
      if (!buildRing(r->basicComponents(), e)) {
        e.ok = false;
        ++ne3RingBad_;
        return e;
      }
      // tkDetUtil::fillRingParametersFromDisk
      auto const& rd = static_cast<const BoundDisk&>(r->surface());
      e.disk = &rd;
      const float ringMinZ = std::abs(rd.position().z()) - rd.bounds().thickness() / 2.;
      const float ringMaxZ = std::abs(rd.position().z()) + rd.bounds().thickness() / 2.;
      e.thetaMin = rd.innerRadius() / ringMaxZ;
      e.thetaMax = rd.outerRadius() / ringMinZ;
      e.ringR = (rd.innerRadius() + rd.outerRadius()) / 2.;
      e.ok = true;
      return e;
    };
    // PeriodicBinFinderInPhi<float>::binIndex
    auto binIndex = [](EmuRing const& R, int s, float phi) {
      constexpr float kTwoPi = 2 * float(3.141592653589793238);
      const int n = R.sub[s].size();
      float tmp = std::fmod((phi - R.phiOffset[s]), kTwoPi) * R.invPhiStep[s];
      if (tmp < 0)
        tmp += n;
      return std::min(int(tmp), n - 1);
    };
    // GeomDetCompatibilityChecker::isCompatible (straight-line sagitta pre-check, then the propagation + estimate)
    auto compatible = [&](const GeomDet* det,
                          TrajectoryStateOnSurface const& tsos,
                          Propagator const& prop,
                          TrajectoryStateOnSurface& out) {
      ++nDetTests;
      const MeasurementEstimator& est = estimator;
      auto const& plane = det->specificSurface();
      if (est.maxSagitta() > 0) {
        StraightLinePlaneCrossing crossing(
            tsos.globalPosition().basicVector(), tsos.globalMomentum().basicVector(), prop.propagationDirection());
        auto path = crossing.pathLength(plane);
        if (path.first) {
          auto gpos = GlobalPoint(crossing.position(path.second));
          auto tpath2 = (gpos - tsos.globalPosition()).perp2();
          const float sagitta = 0.5f * std::abs(tpath2 * tsos.globalParameters().transverseCurvature());
          if (sagitta < est.maxSagitta()) {
            const float tollL2 = std::max(sagitta * sagitta, est.minTolerance2());
            if (!plane.bounds().inside(plane.toLocal(gpos), LocalError(tollL2, 0, tollL2))) {
              if (checkDevice_)
                checkDetTest(tsos, plane, prop, TrajectoryStateOnSurface(), false, true);
              return false;
            }
          }
        }
      }
      out = prop.propagate(tsos, plane);
      const bool ok = out.isValid() && est.estimate(out, plane);
      if (checkDevice_)
        checkDetTest(tsos, plane, prop, out, ok);
      return ok;
    };
    // tkDetUtil::calculatePhiWindow (the generic and the z-normal branches)
    auto phiWindowOf = [&](const GeomDet* det, TrajectoryStateOnSurface const& ts) {
      const MeasurementEstimator& est = estimator;
      const Plane& plane = det->surface();
      const auto imax = est.maximalLocalDisplacement(ts, plane);
      const MeasurementEstimator::Local2DVector maxD(std::abs(imax.x()), std::abs(imax.y()));
      constexpr float tolerance = 1.e-6;
      const LocalPoint start = ts.localPosition();
      float w = 0;
      if (std::abs(1.f - std::abs(plane.normalVector().z())) < tolerance) {
        auto ori = plane.toLocal(GlobalPoint(0., 0., 0.));
        auto xc = std::abs(start.x() - ori.x());
        auto yc = std::abs(start.y() - ori.y());
        if (yc < maxD.y() && xc < maxD.x())
          w = M_PI;
        else {
          auto hori = yc > maxD.y();
          auto y0 = hori ? yc + std::copysign(maxD.y(), xc - maxD.x()) : xc - maxD.x();
          auto x0 = hori ? xc - maxD.x() : -yc - maxD.y();
          auto y1 = hori ? yc - maxD.y() : xc - maxD.x();
          auto x1 = hori ? xc + maxD.x() : -yc + maxD.y();
          auto sp = (x0 * x1 + y0 * y1) / std::sqrt((x0 * x0 + y0 * y0) * (x1 * x1 + y1 * y1));
          sp = std::min(std::max(sp, -1.f), 1.f);
          w = std::acos(sp);
        }
      } else {
        float corners[] = {plane.toGlobal(LocalPoint(start.x() + maxD.x(), start.y() + maxD.y())).barePhi(),
                           plane.toGlobal(LocalPoint(start.x() - maxD.x(), start.y() + maxD.y())).barePhi(),
                           plane.toGlobal(LocalPoint(start.x() - maxD.x(), start.y() - maxD.y())).barePhi(),
                           plane.toGlobal(LocalPoint(start.x() + maxD.x(), start.y() - maxD.y())).barePhi()};
        float phimin = corners[0], phimax = phimin;
        for (int i = 1; i < 4; i++) {
          if (Geom::phiLess(corners[i], phimin))
            phimin = corners[i];
          if (Geom::phiLess(phimax, corners[i]))
            phimax = corners[i];
        }
        w = phimax - phimin;
        if (w < 0.)
          w += 2. * Geom::pi();
      }
      return std::copysign(w, imax.x());  // tkDetUtil::computeWindowSize
    };
    // Phase2EndcapRing::groupedCompatibleDetsV (brothers = the upper sensors of the stacks)
    auto ringGroups =
        [&](EmuRing const& R, TrajectoryStateOnSurface const& tsos, Propagator const& prop, EmuGroups& result) {
          HelixForwardPlaneCrossing xing(HelixPlaneCrossing::PositionType(tsos.globalPosition()),
                                         HelixPlaneCrossing::DirectionType(tsos.globalMomentum()),
                                         tsos.transverseCurvature(),
                                         prop.propagationDirection());
          auto fp = xing.pathLength(*R.plane[0]);
          if (!fp.first)
            return;
          auto bp = xing.pathLength(*R.plane[1]);
          if (!bp.first)
            return;
          const GlobalPoint pt[2] = {GlobalPoint(xing.position(fp.second)), GlobalPoint(xing.position(bp.second))};
          int idx[2];
          float dist[2];
          for (int s = 0; s < 2; ++s) {
            idx[s] = binIndex(R, s, pt[s].barePhi());
            dist[s] = std::abs(Geom::deltaPhi(pt[s].barePhi(), float(R.sub[s][idx[s]]->surface().phi())));
          }
          const int cs = dist[0] < dist[1] ? 0 : 1, os = 1 - cs;
          const bool hasBro = !R.bro[0].empty() && !R.bro[1].empty();
          auto add = [&](const GeomDet* det, EmuGroups& res) {
            TrajectoryStateOnSurface ts;
            if (!compatible(det, tsos, prop, ts))
              return false;
            if (res.empty())
              res.emplace_back();
            res.front().el.push_back({det, ts});
            return true;
          };
          auto searchNeighbors =
              [&](int s, int closestIndex, float window, EmuGroups& res, EmuGroups& bres, bool checkClosest) {
                auto const& sl = R.sub[s];
                auto const& bl = R.bro[s];
                const GlobalPoint& cp = pt[s];
                const int n = sl.size();
                int negStart = closestIndex - 1, posStart = closestIndex + 1;
                if (checkClosest) {
                  if (Geom::phiLess(cp.barePhi(), float(sl[closestIndex]->surface().phi())))
                    posStart = closestIndex;
                  else
                    negStart = closestIndex;
                }
                auto wrap = [n](int i) {
                  const int ind = i % n;
                  return ind < 0 ? ind + n : ind;
                };
                auto overlapInPhi = [&](const GeomDet* g) {
                  const std::pair<float, float> range(cp.barePhi() - window, cp.barePhi() + window);
                  return rangesIntersect(
                      range, g->surface().phiSpan(), [](auto x, auto y) { return Geom::phiLess(x, y); });
                };
                const int halfN = n / 2;
                for (int i = negStart; i >= negStart - halfN; i--) {
                  if (!overlapInPhi(sl[wrap(i)]) || !add(sl[wrap(i)], res) || !hasBro)
                    break;
                  add(bl[wrap(i)], bres);
                }
                for (int i = posStart; i < posStart + halfN; i++) {
                  if (!overlapInPhi(sl[wrap(i)]) || !add(sl[wrap(i)], res) || !hasBro)
                    break;
                  add(bl[wrap(i)], bres);
                }
              };
          EmuGroups closestRes, closestBro;
          add(R.sub[cs][idx[cs]], closestRes);
          if (hasBro)
            add(R.bro[cs][idx[cs]], closestBro);
          if (closestRes.empty())
            return;
          EmuEl const cg = closestRes.front().el.front();
          // LayerCrossingSide::endcapSide on the closest det's state
          const bool outwards = cg.ts.globalMomentum().z() * cg.ts.globalPosition().z() > 0;
          const int side = prop.propagationDirection() == (outwards ? alongMomentum : oppositeToMomentum) ? 0 : 1;
          const float window = phiWindowOf(cg.det, cg.ts);
          searchNeighbors(cs, idx[cs], window, closestRes, closestBro, false);
          EmuGroups closestComplete, nextRes, nextBro, nextComplete;
          emuOrderAndMerge(std::move(closestRes), std::move(closestBro), closestComplete, 0, side);
          searchNeighbors(os, idx[os], window, nextRes, nextBro, true);
          emuOrderAndMerge(std::move(nextRes), std::move(nextBro), nextComplete, 0, side);
          emuOrderAndMerge(std::move(closestComplete), std::move(nextComplete), result, cs, side);
          // stock: sort(result, DetGroupElementZLess) (|z| of each group's first det; <= 4 groups, distinct sub-layers):
          // the same order by insertion; only with brothers
          for (size_t i = 1; hasBro && i < result.size(); ++i)
            for (size_t j = i; j > 0 && std::abs(result[j].el.front().det->position().z()) <
                                            std::abs(result[j - 1].el.front().det->position().z());
                 --j)
              std::swap(result[j], result[j - 1]);
        };
    // tkDetUtil::groupedCompatibleDetsV over rings (EmuRing with its disk); search(ring, tsos, prop, result) is the ring's
    // groupedCompatibleDetsV (Phase2EndcapRing or Phase2EndcapSingleRing)
    auto tkDetUtilGroups = [&](std::vector<EmuRing const*> const& rings,
                               TrajectoryStateOnSurface const& start,
                               Propagator const& prop,
                               EmuGroups& result,
                               auto&& search) {
      const int nR = rings.size();
      HelixForwardPlaneCrossing xing(HelixPlaneCrossing::PositionType(start.globalPosition()),
                                     HelixPlaneCrossing::DirectionType(start.globalMomentum()),
                                     float(start.transverseCurvature()),
                                     prop.propagationDirection());
      std::vector<GlobalPoint> rc;
      rc.reserve(nR);
      for (int i = 0; i < nR; ++i) {
        auto pl = xing.pathLength(*rings[i]->disk);
        rc.push_back(pl.first ? GlobalPoint(xing.position(pl.second)) : GlobalPoint(0., 0., 0.));
      }
      // tkDetUtil::findThreeClosest
      std::array<int, 3> bins{{0, -1, -1}};
      {
        float r0 = std::abs(rc[0].perp() - rings[0]->ringR), r1 = -1., r2 = -1.;
        for (int i = 1; i < nR; i++) {
          const float t = std::abs(rc[i].perp() - rings[i]->ringR);
          if (t < r0) {
            r2 = r1;
            r1 = r0;
            r0 = t;
            bins[2] = bins[1];
            bins[1] = bins[0];
            bins[0] = i;
          } else if (r1 < 0 || t < r1) {
            r2 = r1;
            r1 = t;
            bins[2] = bins[1];
            bins[1] = i;
          } else if (r2 < 0 || t < r2) {
            r2 = t;
            bins[2] = i;
          }
        }
      }
      if (bins[0] == -1 || bins[1] == -1 || bins[2] == -1)
        return true;  // stock: LogError + empty
      std::vector<int> ringOrder(nR, 1);
      if (nR > 1) {
        const float z0 = std::abs(rings[0]->disk->position().z()), z1 = std::abs(rings[1]->disk->position().z());
        if (z0 < z1) {
          for (int i = 0; i < nR; i++)
            if (i % 2 == 0)
              ringOrder[i] = 0;
        } else if (z0 > z1) {
          std::fill(ringOrder.begin(), ringOrder.end(), 0);
          for (int i = 0; i < nR; i++)
            if (i % 2 == 0)
              ringOrder[i] = 1;
        } else
          return false;
      }
      auto index = [&](int i) { return ringOrder[bins[i]]; };
      EmuGroups closestRes;
      search(*rings[bins[0]], start, prop, closestRes);
      if (closestRes.empty()) {
        search(*rings[bins[1]], start, prop, result);
        return true;
      }
      EmuEl const cg = closestRes.front().el.front();
      const double rWindow =
          static_cast<const MeasurementEstimator&>(estimator).maximalLocalDisplacement(cg.ts, cg.det->surface()).y();
      auto overlapInR = [&](int ri) {
        const float tsRadius = cg.ts.globalPosition().perp();
        const float thetamin = (std::max(0., tsRadius - rWindow)) / (std::abs(cg.ts.globalPosition().z()) + 10.f);
        const float thetamax = (tsRadius + rWindow) / (std::abs(cg.ts.globalPosition().z()) - 10.f);
        return !(thetamin > rings[ri]->thetaMax || rings[ri]->thetaMin > thetamax);
      };
      const bool ring1ok = overlapInR(bins[1]);
      bool ring2ok = overlapInR(bins[2]);
      int direction = 0;
      if (start.globalPosition().z() * start.globalMomentum().z() > 0)
        direction = prop.propagationDirection() == alongMomentum ? 0 : 1;
      else
        direction = prop.propagationDirection() == alongMomentum ? 1 : 0;
      if (index(0) == index(1) && index(0) == index(2))
        ring2ok = false;
      if (index(0) == index(1)) {
        if (ring1ok) {
          EmuGroups r1;
          search(*rings[bins[1]], start, prop, r1);
          emuAddSameLevel(std::move(r1), closestRes);
        }
        if (ring2ok) {
          EmuGroups r2;
          search(*rings[bins[2]], start, prop, r2);
          emuOrderAndMerge(std::move(closestRes), std::move(r2), result, index(0), direction);
        } else
          result.swap(closestRes);
      } else if (index(0) == index(2)) {
        if (ring2ok) {
          EmuGroups r2;
          search(*rings[bins[2]], start, prop, r2);
          emuAddSameLevel(std::move(r2), closestRes);
        }
        if (ring1ok) {
          EmuGroups r1;
          search(*rings[bins[1]], start, prop, r1);
          emuOrderAndMerge(std::move(closestRes), std::move(r1), result, index(0), direction);
        } else
          result.swap(closestRes);
      } else {
        EmuGroups r12;
        if (ring1ok)
          search(*rings[bins[1]], start, prop, r12);
        if (ring2ok) {
          EmuGroups r2;
          search(*rings[bins[2]], start, prop, r2);
          emuAddSameLevel(std::move(r2), r12);
        }
        if (!r12.empty())
          emuOrderAndMerge(std::move(closestRes), std::move(r12), result, index(0), direction);
        else
          result.swap(closestRes);
      }
      return true;
    };
    auto endcapLayerGroups =
        [&](const DetLayer* layer, TrajectoryStateOnSurface const& start, Propagator const& prop, EmuGroups& result) {
          auto const& comps = layer->components();
          if (comps.empty())
            return false;
          std::vector<EmuRing const*> rings(comps.size());
          for (size_t i = 0; i < comps.size(); ++i) {
            rings[i] = &ringOf(comps[i]);
            if (!rings[i]->ok)
              return false;
          }
          return tkDetUtilGroups(rings, start, prop, result, ringGroups);
        };
    // ---- variant 3, pixel endcap (round 10): Phase2EndcapLayerDoubleDisk (sub-disks of Phase2EndcapSingleRing; its
    // components() throws, so the sub-disks and rings are rebuilt from basicComponents: runs of (pxfPanel, pxfBlade)) or
    // a Phase2EndcapLayer of Phase2EndcapRing without brothers ----
    auto singleRing = [&](std::vector<const GeomDet*> const& dets, EmuRing& e) {
      if (dets.size() < 2)
        return false;
      e.single = true;
      e.sub[0] = dets;
      e.ownDisk = ReferenceCountingPointer<BoundDisk>(ForwardRingDiskBuilderFromDet()(dets));
      e.disk = e.ownDisk.get();
      e.plane[0] = Plane::build(Surface::PositionType(0., 0., e.disk->position().z()), Surface::RotationType());
      constexpr float kTwoPi = 2 * float(3.141592653589793238);
      e.phiStep[0] = kTwoPi / float(dets.size());
      e.invPhiStep[0] = 1.f / e.phiStep[0];
      e.phiOffset[0] = float(dets.front()->surface().position().phi()) - 0.5f * e.phiStep[0];
      const float ringMinZ = std::abs(e.disk->position().z()) - e.disk->bounds().thickness() / 2.;
      const float ringMaxZ = std::abs(e.disk->position().z()) + e.disk->bounds().thickness() / 2.;
      e.thetaMin = e.disk->innerRadius() / ringMaxZ;
      e.thetaMax = e.disk->outerRadius() / ringMinZ;
      e.ringR = (e.disk->innerRadius() + e.disk->outerRadius()) / 2.;
      e.ok = true;
      return true;
    };
    auto pixEndcapOf = [&](const DetLayer* layer) -> EmuPixEndcap const& {
      auto [it, isNew] = emuPixEndcaps.try_emplace(layer);
      EmuPixEndcap& e = it->second;
      if (!isNew)
        return e;
      bool hasComps = true;
      try {
        (void)layer->components();
      } catch (cms::Exception const&) {
        hasComps = false;
      }
      if (hasComps)
        return e;  // a Phase2EndcapLayer: handled by endcapLayerGroups
      e.doubleDisk = true;
      std::vector<std::vector<std::vector<const GeomDet*>>> subs;  // [subdisk][ring][det]
      int lastPanel = -1, lastBlade = -1;
      for (auto const* g : layer->basicComponents()) {
        const DetId id = g->geographicalId();
        const int panel = tTopo.pxfPanel(id), blade = tTopo.pxfBlade(id);
        if (panel != lastPanel) {
          subs.emplace_back();
          lastBlade = -1;
        }
        if (blade != lastBlade)
          subs.back().emplace_back();
        subs.back().back().push_back(g);
        lastPanel = panel;
        lastBlade = blade;
      }
      if (subs.size() < 2)
        return e;
      for (auto const& sd : subs) {
        EmuSubDisk d;
        float zmin = std::numeric_limits<float>::max(), zmax = -zmin;
        for (auto const& rd : sd) {
          EmuRing r;
          if (!singleRing(rd, r))
            return e;
          // tkDetUtil::computeDisk: z range of the ring disks
          zmin = std::min(zmin, r.disk->position().z() - r.disk->bounds().thickness() / 2);
          zmax = std::max(zmax, r.disk->position().z() + r.disk->bounds().thickness() / 2);
          d.rings.push_back(std::move(r));
        }
        d.z = (zmax + zmin) / 2;
        d.plane = Plane::build(Surface::PositionType(0., 0., d.z), Surface::RotationType());
        e.subs.push_back(std::move(d));
      }
      e.ok = true;
      return e;
    };
    // Phase2EndcapSingleRing::groupedCompatibleDetsV
    auto singleRingGroups =
        [&](EmuRing const& R, TrajectoryStateOnSurface const& tsos, Propagator const& prop, EmuGroups& result) {
          HelixForwardPlaneCrossing xing(HelixPlaneCrossing::PositionType(tsos.globalPosition()),
                                         HelixPlaneCrossing::DirectionType(tsos.globalMomentum()),
                                         tsos.transverseCurvature(),
                                         prop.propagationDirection());
          auto fp = xing.pathLength(*R.disk);
          if (!fp.first)
            return;
          const GlobalPoint cp(xing.position(fp.second));
          const int idx = binIndex(R, 0, cp.barePhi());
          auto const& sl = R.sub[0];
          const int n = sl.size();
          EmuGroups closestRes;
          auto add = [&](const GeomDet* det) {
            TrajectoryStateOnSurface ts;
            if (!compatible(det, tsos, prop, ts))
              return false;
            if (closestRes.empty())
              closestRes.emplace_back();
            closestRes.front().el.push_back({det, ts});
            return true;
          };
          add(sl[idx]);
          if (closestRes.empty())
            return;
          EmuEl const cg = closestRes.front().el.front();
          const float window = phiWindowOf(cg.det, cg.ts);
          auto overlapInPhi = [&](const GeomDet* g) {
            const std::pair<float, float> range(cp.barePhi() - window, cp.barePhi() + window);
            return rangesIntersect(range, g->surface().phiSpan(), [](auto x, auto y) { return Geom::phiLess(x, y); });
          };
          auto wrap = [n](int i) {
            const int ind = i % n;
            return ind < 0 ? ind + n : ind;
          };
          const int halfN = n / 2;
          for (int i = idx - 1; i >= idx - 1 - halfN; i--)
            if (!overlapInPhi(sl[wrap(i)]) || !add(sl[wrap(i)]))
              break;
          for (int i = idx + 1; i < idx + 1 + halfN; i++)
            if (!overlapInPhi(sl[wrap(i)]) || !add(sl[wrap(i)]))
              break;
          emuAddSameLevel(std::move(closestRes), result);
        };
    // Phase2EndcapLayerDoubleDisk::groupedCompatibleDetsV
    auto pixEndcapGroups =
        [&](const DetLayer* layer, TrajectoryStateOnSurface const& start, Propagator const& prop, EmuGroups& result) {
          EmuPixEndcap const& P = pixEndcapOf(layer);
          if (!P.doubleDisk)
            return endcapLayerGroups(layer, start, prop, result);
          if (!P.ok)
            return false;
          const int nS = P.subs.size();
          HelixForwardPlaneCrossing xing(HelixPlaneCrossing::PositionType(start.globalPosition()),
                                         HelixPlaneCrossing::DirectionType(start.globalMomentum()),
                                         float(start.transverseCurvature()),
                                         prop.propagationDirection());
          std::vector<GlobalPoint> sc;
          for (int i = 0; i < nS; ++i) {
            auto pl = xing.pathLength(*P.subs[i].plane);
            sc.push_back(pl.first ? GlobalPoint(xing.position(pl.second)) : GlobalPoint(0., 0., 0.));
          }
          // findTwoClosest (theSubDiskZ = |z| of the sub-disk surface)
          std::array<int, 2> bins{{0, -1}};
          {
            float z0 = std::abs(sc[0].z() - std::abs(P.subs[0].z)), z1 = -1.;
            for (int i = 1; i < nS; i++) {
              const float t = std::abs(sc[i].z() - std::abs(P.subs[i].z));
              if (t < z0) {
                z1 = z0;
                z0 = t;
                bins[1] = bins[0];
                bins[0] = i;
              } else if (z1 < 0 || t < z1) {
                z1 = t;
                bins[1] = i;
              }
            }
          }
          std::vector<int> order(nS, 1);
          if (nS > 1) {
            const float a = std::abs(P.subs[0].z), b = std::abs(P.subs[1].z);
            if (a < b) {
              for (int i = 0; i < nS; i++)
                if (i % 2 == 0)
                  order[i] = 0;
            } else if (a > b) {
              std::fill(order.begin(), order.end(), 0);
              for (int i = 0; i < nS; i++)
                if (i % 2 == 0)
                  order[i] = 1;
            } else
              return false;
          }
          auto subGroups = [&](int i, EmuGroups& res) {
            std::vector<EmuRing const*> rings;
            for (auto const& r : P.subs[i].rings)
              rings.push_back(&r);
            tkDetUtilGroups(rings, start, prop, res, singleRingGroups);
          };
          EmuGroups closestRes;
          subGroups(bins[0], closestRes);
          if (closestRes.empty()) {
            subGroups(bins[1], result);
            return true;
          }
          const bool sub1ok = bins[1] != -1;
          int direction = 0;
          if (start.globalPosition().z() * start.globalMomentum().z() > 0)
            direction = prop.propagationDirection() == alongMomentum ? 0 : 1;
          else
            direction = prop.propagationDirection() == alongMomentum ? 1 : 0;
          if (order[bins[0]] == order[bins[1]]) {
            if (sub1ok) {
              EmuGroups r1;
              subGroups(bins[1], r1);
              emuAddSameLevel(std::move(r1), closestRes);
              result.swap(closestRes);
            }
          } else {
            EmuGroups r1;
            if (sub1ok)
              subGroups(bins[1], r1);
            if (!r1.empty())
              emuOrderAndMerge(std::move(closestRes), std::move(r1), result, order[bins[0]], direction);
            else
              result.swap(closestRes);
          }
          return true;
        };
    // ---- variant 3, barrel (round 10): TBPLayer + Phase2OTBarrelRod / PixelRod + Phase2OTtiltedBarrelLayer rings ----
    auto rodOf = [&](const GeometricSearchDet* r, bool stacked) -> EmuRod const& {
      auto [it, isNew] = emuRods.try_emplace(r);
      EmuRod& e = it->second;
      if (!isNew)
        return e;
      auto const& bc = r->basicComponents();
      e.stacked = stacked;
      if (!stacked) {  // PixelRod: theDets, sorted in z
        if (bc.size() < 2)
          return e;
        e.dets.assign(bc.begin(), bc.end());
        e.pbf = PeriodicBinFinderInZ<float>(e.dets.begin(), e.dets.end());
        e.ok = true;
        return e;
      }
      const size_t half = bc.size() / 2;
      if (bc.size() < 4 || bc.size() % 2 != 0)
        return e;
      double mr = 0, mrb = 0;
      for (size_t i = 0; i < half; ++i) {
        mr += bc[i]->position().perp();
        mrb += bc[half + i]->position().perp();
      }
      mr /= half;
      mrb /= half;
      for (size_t i = 0; i < half; ++i) {
        e.sub[bc[i]->position().perp() < mr ? 0 : 1].push_back(bc[i]);
        e.bro[bc[half + i]->position().perp() < mrb ? 0 : 1].push_back(bc[half + i]);
      }
      for (int s = 0; s < 2; ++s) {
        if (e.sub[s].size() < 3 || e.sub[s].size() != e.bro[s].size())
          return e;
        for (size_t i = 0; i < e.sub[s].size(); ++i)
          if (tTopo.stack(e.sub[s][i]->geographicalId()) != tTopo.stack(e.bro[s][i]->geographicalId()))
            return e;
        e.plane[s] = Plane::PlanePointer(RodPlaneBuilderFromDet()(e.sub[s]));
        e.bf[s] = GenericBinFinderInZ<float, GeomDet>(e.sub[s].begin(), e.sub[s].end());
      }
      e.ok = true;
      return e;
    };
    auto barrelOf = [&](const DetLayer* layer) -> EmuBarrel const& {
      auto [it, isNew] = emuBarrels.try_emplace(layer);
      EmuBarrel& e = it->second;
      if (!isNew)
        return e;
      auto const& comps = layer->components();  // TBLayer::theComps = inner rods, outer rods
      if (comps.size() < 4)
        return e;
      double mr = 0;
      for (auto const* c : comps)
        mr += c->position().perp();
      mr /= comps.size();
      for (auto const* c : comps)
        e.rods[c->position().perp() < mr ? 0 : 1].push_back(c);
      if (e.rods[0].empty() || e.rods[1].empty() || comps[0] != e.rods[0].front() || comps.back() != e.rods[1].back())
        return e;
      for (int s = 0; s < 2; ++s) {
        std::vector<const GeomDet*> tmp;
        for (auto const* rod : e.rods[s])
          tmp.insert(tmp.end(), rod->basicComponents().begin(), rod->basicComponents().end());
        e.cyl[s] = ReferenceCountingPointer<BoundCylinder>(CylinderBuilderFromDet()(tmp.begin(), tmp.end()));
        e.bf[s] = PeriodicBinFinderInPhi<float>(e.rods[s].front()->position().phi(), e.rods[s].size());
      }
      // tilted rings: the tilted (TOB side 1/2) basic components in order, a ring = a run of lowers then uppers
      std::vector<const GeomDet*> cur;
      bool prevUpper = false;
      auto flush = [&]() {
        if (cur.empty())
          return true;
        EmuRing r;
        if (!buildRing(cur, r))
          return false;
        e.rings[cur.front()->position().z() < 0 ? 0 : 1].push_back(std::move(r));
        cur.clear();
        return true;
      };
      for (auto const* g : layer->basicComponents()) {
        const DetId id = g->geographicalId();
        if (id.subdetId() != StripSubdetector::TOB || tTopo.tobSide(id) >= 3)
          continue;
        const bool upper = tTopo.isUpper(id);
        if (!upper && prevUpper && !flush())
          return e;
        cur.push_back(g);
        prevUpper = upper;
      }
      if (!flush())
        return e;
      e.ok = true;
      return e;
    };
    auto addEl = [](EmuGroups& res, const GeomDet* det, TrajectoryStateOnSurface const& ts) {
      if (res.empty())
        res.emplace_back();
      res.front().el.push_back({det, ts});
    };
    auto addDet =
        [&](const GeomDet* det, TrajectoryStateOnSurface const& tsos, Propagator const& prop, EmuGroups& res) {
          TrajectoryStateOnSurface ts;
          if (!compatible(det, tsos, prop, ts))
            return false;
          addEl(res, det, ts);
          return true;
        };
    // LayerCrossingSide::barrelSide
    auto barrelSide = [](TrajectoryStateOnSurface const& ts, Propagator const& prop) {
      const auto pos = ts.globalPosition();
      const auto dir = ts.globalMomentum();
      const bool outwards = (pos.x() * dir.x() + pos.y() * dir.y()) > 0;
      return prop.propagationDirection() == (outwards ? alongMomentum : oppositeToMomentum) ? 0 : 1;
    };
    // Phase2OTBarrelRod::groupedCompatibleDetsV
    auto stackRodGroups = [&](EmuRod const& R,
                              TrajectoryStateOnSurface const& tsos,
                              Propagator const& prop,
                              EmuGroups& result) {
      HelixBarrelPlaneCrossingByCircle xing(
          tsos.globalPosition(), tsos.globalMomentum(), tsos.transverseCurvature(), prop.propagationDirection());
      auto op = xing.pathLength(*R.plane[1]);
      if (!op.first)
        return;
      const GlobalPoint outerPt(xing.position(op.second));
      auto ip = xing.pathLength(*R.plane[0]);
      if (!ip.first)
        return;
      const GlobalPoint pt[2] = {GlobalPoint(xing.position(ip.second)), outerPt};
      int idx[2];
      float dist[2];
      for (int s = 0; s < 2; ++s) {
        idx[s] = R.bf[s].binIndex(pt[s].z());
        dist[s] = std::abs(R.bf[s].binPosition(idx[s]) - pt[s].z());
      }
      const int cs = dist[0] < dist[1] ? 0 : 1, os = 1 - cs;
      auto overlapZ = [](GlobalPoint const& cp, const GeomDet& det, float window) {
        constexpr float relativeMargin = 1.01;
        const LocalPoint lc(det.surface().toLocal(cp));
        return (std::abs(lc.y()) - window) < relativeMargin * 0.5f * det.surface().bounds().length();
      };
      auto searchNeighbors = [&](int s, float window, EmuGroups& res, EmuGroups& bres, bool checkClosest) {
        auto const& sr = R.sub[s];
        auto const& br = R.bro[s];
        const GlobalPoint& cp = pt[s];
        int negStart = idx[s] - 1, posStart = idx[s] + 1;
        if (checkClosest) {
          if (cp.z() < sr[idx[s]]->surface().position().z())
            posStart = idx[s];
          else
            negStart = idx[s];
        }
        for (int i = negStart; i >= 0; i--) {
          if (!overlapZ(cp, *sr[i], window) || !addDet(sr[i], tsos, prop, res))
            break;
          addDet(br[i], tsos, prop, bres);
        }
        for (int i = posStart; i < static_cast<int>(sr.size()); i++) {
          if (!overlapZ(cp, *sr[i], window) || !addDet(sr[i], tsos, prop, res))
            break;
          addDet(br[i], tsos, prop, bres);
        }
      };
      EmuGroups closestRes, closestBro;
      addDet(R.sub[cs][idx[cs]], tsos, prop, closestRes);
      addDet(R.bro[cs][idx[cs]], tsos, prop, closestBro);
      if (closestRes.empty()) {
        EmuGroups nextRes, nextBro;
        addDet(R.sub[os][idx[os]], tsos, prop, nextRes);
        addDet(R.bro[os][idx[os]], tsos, prop, nextBro);
        if (nextRes.empty())
          return;
        const int side = barrelSide(nextRes.front().el.front().ts, prop);
        EmuGroups cc, nc;
        emuOrderAndMerge(std::move(closestRes), std::move(closestBro), cc, 0, side);
        emuOrderAndMerge(std::move(nextRes), std::move(nextBro), nc, 0, side);
        emuOrderAndMerge(std::move(cc), std::move(nc), result, cs, side);
      } else {
        EmuEl const cg = closestRes.front().el.front();
        const int side = barrelSide(cg.ts, prop);
        const float window =
            static_cast<const MeasurementEstimator&>(estimator).maximalLocalDisplacement(cg.ts, cg.det->surface()).y();
        searchNeighbors(cs, window, closestRes, closestBro, false);
        EmuGroups cc, nextRes, nextBro, nc;
        emuOrderAndMerge(std::move(closestRes), std::move(closestBro), cc, 0, side);
        searchNeighbors(os, window, nextRes, nextBro, true);
        emuOrderAndMerge(std::move(nextRes), std::move(nextBro), nc, 0, side);
        emuOrderAndMerge(std::move(cc), std::move(nc), result, cs, side);
      }
      // stock: sort(result, DetGroupElementPerpLess) (distinct sensors): the same order by insertion
      for (size_t i = 1; i < result.size(); ++i)
        for (size_t j = i;
             j > 0 && result[j].el.front().det->position().perp() < result[j - 1].el.front().det->position().perp();
             --j)
          std::swap(result[j], result[j - 1]);
    };
    // PixelRod::compatibleDetsV (one group)
    auto pixelRodDets = [&](const GeometricSearchDet* rod,
                            EmuRod const& R,
                            TrajectoryStateOnSurface const& tsos,
                            Propagator const& prop,
                            std::vector<EmuEl>& out) {
      const auto ts = prop.propagate(tsos, static_cast<const Plane&>(rod->surface()));
      if (!ts.isValid())
        return;
      const int closest = R.pbf.binIndex(ts.globalPosition().z());
      TrajectoryStateOnSurface cs;
      if (compatible(R.dets[closest], tsos, prop, cs))
        out.push_back({R.dets[closest], cs});
      else if (!cs.isValid())
        return;
      const auto maxD = static_cast<const MeasurementEstimator&>(estimator).maximalLocalDisplacement(
          cs, R.dets[closest]->specificSurface());
      const float detHalfLen = R.dets[closest]->surface().bounds().length() / 2.;
      auto addOne = [&](int i) {
        TrajectoryStateOnSurface t;
        if (!compatible(R.dets[i], tsos, prop, t))
          return false;
        out.push_back({R.dets[i], t});
        return true;
      };
      for (size_t i = closest + 1; i < R.dets.size(); i++) {
        const LocalPoint np(R.dets[i]->surface().toLocal(cs.globalPosition()));
        if (!(std::abs(np.y()) < detHalfLen + maxD.y()) || !addOne(i))
          break;
      }
      for (int i = closest - 1; i >= 0; i--) {
        const LocalPoint np(R.dets[i]->surface().toLocal(cs.globalPosition()));
        if (!(std::abs(np.y()) < detHalfLen + maxD.y()) || !addOne(i))
          break;
      }
    };
    // CompatibleDetToGroupAdder::add for a rod; false = nothing compatible
    bool rodLayoutBad = false;
    auto rodAdd = [&](const GeometricSearchDet* rod,
                      bool stacked,
                      TrajectoryStateOnSurface const& tsos,
                      Propagator const& prop,
                      EmuGroups& result) {
      EmuRod const& R = rodOf(rod, stacked);
      if (!R.ok) {
        rodLayoutBad = true;
        return false;
      }
      if (stacked) {
        EmuGroups tmp;
        stackRodGroups(R, tsos, prop, tmp);
        if (tmp.empty())
          return false;
        if (result.empty())
          result.swap(tmp);
        else
          emuAddSameLevel(std::move(tmp), result);
        return true;
      }
      std::vector<EmuEl> els;
      pixelRodDets(rod, R, tsos, prop, els);
      if (els.empty())
        return false;
      if (result.empty())
        result.emplace_back();
      result.front().el.insert(result.front().el.end(), els.begin(), els.end());
      return true;
    };
    // TBLayer::groupedCompatibleDetsV with TBPLayer's indexes / window / neighbours, then the tilted rings
    // (Phase2OTtiltedBarrelLayer); false = not laid out as expected
    auto barrelLayerGroups = [&](const DetLayer* layer,
                                 TrajectoryStateOnSurface const& start,
                                 Propagator const& prop,
                                 EmuGroups& result) {
      EmuBarrel const& B = barrelOf(layer);
      if (!B.ok)
        return false;
      const bool stacked = !GeomDetEnumerators::isInnerTracker(layer->subDetector());
      rodLayoutBad = false;
      EmuGroups rodsRes;
      [&]() {
        const GlobalPoint startPos(start.globalPosition());
        const GlobalVector startDir(start.globalMomentum());
        const double rho(start.transverseCurvature());
        const bool inBetween = ((B.cyl[1]->position() - startPos).perp() < B.cyl[1]->radius()) &&
                               ((B.cyl[0]->position() - startPos).perp() > B.cyl[0]->radius());
        HelixBarrelCylinderCrossing ix(
            startPos, startDir, rho, prop.propagationDirection(), *B.cyl[0], HelixBarrelCylinderCrossing::onlyPos);
        if (!inBetween && !ix.hasSolution())
          return;
        HelixBarrelCylinderCrossing ox(
            startPos, startDir, rho, prop.propagationDirection(), *B.cyl[1], HelixBarrelCylinderCrossing::onlyPos);
        if (!ix.hasSolution() && !ox.hasSolution()) {
          ++ne3NoCross_;
          return;
        }
        GlobalPoint pt[2] = {GlobalPoint(ix.position()), GlobalPoint(ox.position())};
        if (!ix.hasSolution())
          pt[0] = pt[1];
        else if (!ox.hasSolution())
          pt[1] = pt[0];
        // TBPLayer::computeIndexes
        int idx[2];
        float dist[2];
        for (int s = 0; s < 2; ++s) {
          idx[s] = B.bf[s].binIndex(pt[s].barePhi());
          dist[s] = B.bf[s].binPosition(idx[s]) - pt[s].barePhi();
          dist[s] *= Geom::phiLess(B.bf[s].binPosition(idx[s]), pt[s].barePhi()) ? -1.f : 1.f;
          if (dist[s] < 0.f)
            dist[s] += Geom::ftwoPi();
        }
        const int cs = dist[0] < dist[1] ? 0 : 1, os = 1 - cs;
        EmuGroups closestRes;
        rodAdd(B.rods[cs][idx[cs]], stacked, start, prop, closestRes);
        if (closestRes.empty()) {
          rodAdd(B.rods[os][idx[os]], stacked, start, prop, rodsRes);
          return;
        }
        EmuEl const cg = closestRes.front().el.front();
        // barrelUtil::computeWindowSize
        const float xmax =
            static_cast<const MeasurementEstimator&>(estimator).maximalLocalDisplacement(cg.ts, cg.det->surface()).x();
        float window;
        {
          const LocalPoint sp0 = cg.ts.localPosition();
          const LocalVector shift(xmax, 0., 0.);
          const auto phi1 = cg.det->surface().toGlobal(sp0 + shift).barePhi();
          const auto phi2 = cg.det->surface().toGlobal(sp0 + (-shift)).barePhi();
          const auto phiStart = cg.ts.globalPosition().barePhi();
          window = std::min(std::abs(phiStart - phi1), std::abs(phiStart - phi2));
        }
        auto searchNeighbors = [&](int s, EmuGroups& res, bool checkClosest) {
          const float gphi = pt[s].barePhi();
          auto const& sl = B.rods[s];
          int negStart = idx[s] - 1, posStart = idx[s] + 1;
          if (checkClosest) {
            if (Geom::phiLess(gphi, float(sl[idx[s]]->surface().phi())))
              posStart = idx[s];
            else
              negStart = idx[s];
          }
          auto overlap = [&](const GeometricSearchDet* g) {  // barrelUtil::overlap
            constexpr float phiOffset = 0.00034;
            const float w = window + phiOffset;
            const std::pair<float, float> range(gphi - w, gphi + w);
            return rangesIntersect(range, g->surface().phiSpan(), [](auto x, auto y) { return Geom::phiLess(x, y); });
          };
          const int quarter = sl.size() / 4;
          for (int i = negStart; i >= negStart - quarter; i--) {
            const auto* rod = sl[B.bf[s].binIndex(i)];
            if (!overlap(rod) || !rodAdd(rod, stacked, start, prop, res))
              break;
          }
          for (int i = posStart; i < posStart + quarter; i++) {
            const auto* rod = sl[B.bf[s].binIndex(i)];
            if (!overlap(rod) || !rodAdd(rod, stacked, start, prop, res))
              break;
          }
        };
        searchNeighbors(cs, closestRes, false);
        EmuGroups nextRes;
        searchNeighbors(os, nextRes, true);
        const int side = barrelSide(cg.ts, prop);
        emuOrderAndMerge(std::move(closestRes), std::move(nextRes), rodsRes, cs, side);
      }();
      if (rodLayoutBad)
        return false;
      EmuGroups ringsRes;
      for (auto const& R : B.rings[start.globalPosition().z() < 0 ? 0 : 1])
        ringGroups(R, start, prop, ringsRes);
      result = std::move(rodsRes);
      result.insert(result.end(), ringsRes.begin(), ringsRes.end());
      return true;
    };
    const auto sub = layer->subDetector();
    const bool ok = GeomDetEnumerators::isBarrel(sub)         ? barrelLayerGroups(layer, start, prop, result)
                    : GeomDetEnumerators::isInnerTracker(sub) ? pixEndcapGroups(layer, start, prop, result)
                                                              : endcapLayerGroups(layer, start, prop, result);
    long long nEl = 0;
    for (auto const& g : result)
      nEl += g.el.size();
    ++cache_.calls;
    cache_.detTests += nDetTests;
    cache_.maxDetTests = std::max(cache_.maxDetTests, nDetTests);
    cache_.maxElements = std::max(cache_.maxElements, nEl);
    cache_.maxGroups = std::max(cache_.maxGroups, static_cast<long long>(result.size()));
    ++cache_.detTestsHist[std::min<long long>(nDetTests, 15)];
    return ok;
  }
}  // namespace mkfitdev::outconv

#endif
