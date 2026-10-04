// Stage C (round 8, lane stagec): step (c) of the device missing-hit navigation - what does compatibleDets need?
// For the converter's calls (compatible layers of the innermost / outermost hit layer, the state at the first hit
// WITH errors, the converter's Chi2MeasurementEstimator(30, -3 sigma bounds)), the first det of compatibleDets
//   stock: PropagatorWithMaterial (along / opposite), as the converter;
//   V1   : AnalyticalPropagator (no material in the errors);
//   V2   : the same state WITHOUT errors (the -3 sigma bound shrink disappears; AnalyticalPropagator);
// and the differences in "a det found" / "which det" vs stock, plus the cost per stock call. First-hit states as in
// MkFitAlpakaPcaCheck. NAVDETS lines at endJob.
#include <array>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <type_traits>

#include "DataFormats/TrackReco/interface/Track.h"
#include "DataFormats/TrackReco/interface/TrackFwd.h"
#include "DataFormats/TrackingRecHit/interface/TrackingRecHit.h"
#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/Framework/interface/global/EDAnalyzer.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include "FWCore/Utilities/interface/Exception.h"
#include "MagneticField/Engine/interface/MagneticField.h"
#include "MagneticField/Records/interface/IdealMagneticFieldRecord.h"
#include "RecoTracker/Record/interface/NavigationSchoolRecord.h"
#include "RecoTracker/Record/interface/TrackerRecoGeometryRecord.h"
#include "RecoTracker/TkDetLayers/interface/GeometricSearchTracker.h"
#include "TrackingTools/DetLayers/interface/DetLayer.h"
#include "TrackingTools/DetLayers/interface/NavigationSchool.h"
#include "TrackingTools/GeomPropagators/interface/AnalyticalPropagator.h"
#include "TrackingTools/GeomPropagators/interface/Propagator.h"
#include "TrackingTools/KalmanUpdators/interface/Chi2MeasurementEstimator.h"
#include "TrackingTools/Records/interface/TrackingComponentsRecord.h"
#include "TrackingTools/TrajectoryState/interface/FreeTrajectoryState.h"
#include "TrackingTools/TrajectoryState/interface/TrajectoryStateOnSurface.h"
#include "TrackingTools/TrajectoryState/interface/TrajectoryStateTransform.h"

class MkFitAlpakaNavDetsStudy : public edm::global::EDAnalyzer<> {
public:
  explicit MkFitAlpakaNavDetsStudy(edm::ParameterSet const& p)
      : tracks_{consumes(p.getParameter<edm::InputTag>("tracks"))},
        mf_{esConsumes()},
        nav_{esConsumes(edm::ESInputTag{"", "SimpleNavigationSchool"})},
        gst_{esConsumes()},
        propAlong_{esConsumes(edm::ESInputTag{"", "PropagatorWithMaterial"})},
        propOpp_{esConsumes(edm::ESInputTag{"", "PropagatorWithMaterialOpposite"})} {}

  static void fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
    edm::ParameterSetDescription desc;
    desc.add<edm::InputTag>("tracks", edm::InputTag("hltInitialStepTracks"));
    descriptions.addWithDefaultLabel(desc);
  }

  void analyze(edm::StreamID, edm::Event const& ev, edm::EventSetup const& es) const override {
    auto const& trks = ev.get(tracks_);
    auto const& mf = es.getData(mf_);
    auto const& nav = es.getData(nav_);
    auto const& gst = es.getData(gst_);
    const Propagator* pm[2] = {&es.getData(propOpp_), &es.getData(propAlong_)};
    const AnalyticalPropagator prop(&mf, anyDirection);
    AnalyticalPropagator pa[2] = {AnalyticalPropagator(&mf, oppositeToMomentum), AnalyticalPropagator(&mf, alongMomentum)};
    const Chi2MeasurementEstimator estimator(30., -3.0, 0.5, 2.0, 0.5, 1.e12);  // as the converter
    std::array<long long, 10> c{};  // calls, stock found, V1 found-flip, V1 det-diff, V2 found-flip, V2 det-diff, ns
    for (auto const& trk : trks) {
      if (trk.recHitsSize() < 2)
        continue;
      const auto first = trk.recHit(0), last = trk.recHit(trk.recHitsSize() - 1);
      if (!first->isValid() || !last->isValid() || first->det() == nullptr)
        continue;
      const FreeTrajectoryState pcaFts = trajectoryStateTransform::initialFreeState(trk, &mf);
      const TrajectoryStateOnSurface t1 = prop.propagate(pcaFts, first->det()->surface());
      if (!t1.isValid() || !t1.hasError())
        continue;
      const FreeTrajectoryState fts = *t1.freeState();
      const TrajectoryStateOnSurface tsos(fts, first->det()->surface());
      const TrajectoryStateOnSurface tsosNoErr(FreeTrajectoryState(fts.parameters()), first->det()->surface());
      const DetLayer* layers[2] = {gst.idToLayer(first->geographicalId()), gst.idToLayer(last->geographicalId())};
      for (int d = 0; d < 2; ++d) {
        if (layers[d] == nullptr)
          continue;
        for (auto it : nav.compatibleLayers(*layers[d], fts, d == 0 ? oppositeToMomentum : alongMomentum)) {
          if (it->basicComponents().empty())
            continue;
          const auto c0 = std::chrono::steady_clock::now();
          auto const s = it->compatibleDets(tsos, *pm[d], estimator);
          c[6] += std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - c0).count();
          ++c[0];
          c[1] += !s.empty();
          auto const v1 = it->compatibleDets(tsos, pa[d], estimator);
          std::remove_const_t<decltype(v1)> v2;
          try {
            if (tsosNoErr.isValid())
              v2 = it->compatibleDets(tsosNoErr, pa[d], estimator);
          } catch (cms::Exception const&) {
            ++c[9];  // the error-free state is not accepted by the estimator path
          }
          c[2] += s.empty() != v1.empty();
          c[3] += !s.empty() && !v1.empty() && s.front().first != v1.front().first;
          c[4] += s.empty() != v2.empty();
          c[5] += !s.empty() && !v2.empty() && s.front().first != v2.front().first;
          c[7] += !s.empty() && v2.empty();  // stock finds a det, the error-free state none
          c[8] += s.empty() && !v2.empty();  // the error-free state finds one, stock none
        }
      }
    }
    std::lock_guard<std::mutex> lk(mtx_);
    for (int k = 0; k < 10; ++k)
      tot_[k] += c[k];
  }

  void endJob() override {
    std::printf(
        "NAVDETS compatibleDets calls %lld, stock first det found %lld, %.2f us per stock call | V1 no material: found "
        "flips %lld, other first det %lld | V2 no errors: found flips %lld (stock only %lld, V2 only %lld), other first "
        "det %lld | V2 exceptions %lld\n",
        tot_[0],
        tot_[1],
        tot_[0] ? tot_[6] * 1e-3 / tot_[0] : 0.,
        tot_[2],
        tot_[3],
        tot_[4],
        tot_[7],
        tot_[8],
        tot_[5],
        tot_[9]);
    std::fflush(stdout);
  }

private:
  const edm::EDGetTokenT<reco::TrackCollection> tracks_;
  const edm::ESGetToken<MagneticField, IdealMagneticFieldRecord> mf_;
  const edm::ESGetToken<NavigationSchool, NavigationSchoolRecord> nav_;
  const edm::ESGetToken<GeometricSearchTracker, TrackerRecoGeometryRecord> gst_;
  const edm::ESGetToken<Propagator, TrackingComponentsRecord> propAlong_, propOpp_;
  mutable std::mutex mtx_;
  mutable std::array<long long, 10> tot_{};
};

DEFINE_FWK_MODULE(MkFitAlpakaNavDetsStudy);
