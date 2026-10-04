// Stage C (lane outconv, round 7): ONE host module from the device fit's TrackSoA (host copy) to reco::Track +
// TrackExtra + TrackingRecHitCollection + SeedStopInfo, replacing MkFitAlpakaOutputWrapperFromTrackSoA + the stock
// MkFitOutputTrackConverter (RecoTracker/MkFit/plugins/MkFitOutputTrackConverter.cc) in the device-fit menu.
// Same operations as the stock converter's no-hit-state branch (identical products, checked in-job by
// TrackCollectionCompare), without its waste:
//   - reads the TrackSoA rows directly (no mkfit::TrackVec / MkFitOutputWrapper copy, no MkFitEventOfHits: is_pixel
//     per layer comes from the MkFitGeometry TrackerInfo, which is what LayerOfHits::is_pixel() returns);
//   - the on-track legacy rechits are referenced, not cloned, while the track is built; each is cloned ONCE, into the
//     output collection (stock: three clones per hit: candidate OwnVector, hitsVecs copy, output copy);
//   - the stock 3D-radius hit ordering uses keys precomputed once per hit (the stock comparator recomputes
//     det()->subDetector(), globalPosition() and the TOB side per comparison); same comparator, same result;
//   - optional per-section timers (untracked "timing") and the field study (untracked "fieldStudy": the PCA state with
//     mkFit's own parabolic field instead of the CMSSW MagneticField, pulls vs the stock state, endJob summary).
// Unchanged physics paths (CMSSW code, same calls in the same order): CCS -> global curvilinear conversion and the
// state quality / Sylvester checks, TSCBLBuilderNoMaterial to the beam line with the CMSSW MagneticField, the hit
// pattern (appendHitPattern per hit) and the inner/outer missing-hit navigation (NavigationSchool + compatibleDets
// with PropagatorWithMaterial / Opposite, MeasurementTrackerEvent activity).
// Supported: Phase-2 geometry, no per-hit states (the device fit has none: TrajectoryInEvent is refused).

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <limits>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "DataFormats/BeamSpot/interface/BeamSpot.h"
#include "DataFormats/GeometrySurface/interface/BoundDisk.h"
#include "DataFormats/GeometrySurface/interface/BoundingBox.h"
#include "DataFormats/GeometrySurface/interface/Plane.h"
#include "DataFormats/GeometryVector/interface/VectorUtil.h"
#include "DataFormats/Math/interface/deltaPhi.h"
#include "DataFormats/SiPixelDetId/interface/PixelSubdetector.h"
#include "DataFormats/SiStripDetId/interface/StripSubdetector.h"
#include "DataFormats/TrackReco/interface/SeedStopInfo.h"
#include "DataFormats/TrackReco/interface/Track.h"
#include "DataFormats/TrackReco/interface/TrackExtra.h"
#include "DataFormats/TrackReco/interface/TrackFwd.h"
#include "DataFormats/TrackerCommon/interface/TrackerTopology.h"
#include "DataFormats/TrackerRecHit2D/interface/BaseTrackerRecHit.h"
#include "DataFormats/TrackerRecHit2D/interface/Phase2TrackerRecHit1D.h"
#include "DataFormats/TrackingRecHit/interface/InvalidTrackingRecHit.h"
#include "DataFormats/TrackingRecHit/interface/TrackingRecHitFwd.h"
#include "DataFormats/TrajectorySeed/interface/TrajectorySeed.h"
#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/Framework/interface/global/EDProducer.h"
#include "FWCore/MessageLogger/interface/MessageLogger.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include "FWCore/Utilities/interface/isFinite.h"
#include "Geometry/CommonTopologies/interface/GeomDetEnumerators.h"
#include "MagneticField/Engine/interface/MagneticField.h"
#include "MagneticField/Records/interface/IdealMagneticFieldRecord.h"
#include "RecoTracker/MeasurementDet/interface/MeasurementTrackerEvent.h"
#include "RecoTracker/MkFit/interface/MkFitClusterIndexToHit.h"
// stage D (round 8, lane otdev -> lane stagec patch): OT hits of output tracks made on demand from their clusters
#include "Geometry/Records/interface/TrackerDigiGeometryRecord.h"
#include <optional>
#include "RecoLocalTracker/Phase2TrackerRecHits/interface/Phase2TrackerRecHitOnDemand.h"
#include "RecoLocalTracker/Records/interface/TkPhase2OTCPERecord.h"
#include "RecoTracker/MkFit/interface/MkFitGeometry.h"
#include "RecoTracker/MkFitAlpaka/interface/FitOuterStateProduct.h"
#include "RecoTracker/MkFitAlpaka/interface/OutConvProduct.h"
#include "RecoTracker/MkFitAlpaka/interface/math/PcaToBeamLine.h"
#include "RecoTracker/MkFitAlpaka/interface/StatusProduct.h"
#include "RecoTracker/MkFitAlpaka/interface/StatusReport.h"
#include "RecoTracker/MkFitAlpaka/interface/TrackProduct.h"
#include "RecoTracker/MkFitAlpaka/plugins/OutConvNavEmulation.h"
#include "RecoTracker/MkFitAlpaka/interface/tracks/TrackSoAMkFitConversion.h"
#include "RecoTracker/MkFitCore/interface/Config.h"
#include "RecoTracker/MkFitCore/interface/Track.h"
#include "RecoTracker/MkFitCore/interface/TrackerInfo.h"
#include "RecoTracker/Record/interface/NavigationSchoolRecord.h"
#include "RecoTracker/Record/interface/TrackerRecoGeometryRecord.h"
#include "TrackingTools/DetLayers/interface/DetLayer.h"
#include "TrackingTools/DetLayers/interface/GeometricSearchDet.h"
#include "TrackingTools/DetLayers/interface/NavigationSchool.h"
#include "DataFormats/GeometrySurface/interface/BoundCylinder.h"
#include "TrackingTools/DetLayers/interface/CylinderBuilderFromDet.h"
#include "TrackingTools/DetLayers/interface/ForwardRingDiskBuilderFromDet.h"
#include "TrackingTools/DetLayers/interface/PeriodicBinFinderInZ.h"
#include "TrackingTools/DetLayers/interface/RodPlaneBuilderFromDet.h"
#include "TrackingTools/DetLayers/interface/rangesIntersect.h"
#include "TrackingTools/GeomPropagators/interface/HelixBarrelCylinderCrossing.h"
#include "TrackingTools/GeomPropagators/interface/HelixBarrelPlaneCrossingByCircle.h"
#include "Utilities/BinningTools/interface/GenericBinFinderInZ.h"
#include "Utilities/BinningTools/interface/PeriodicBinFinderInPhi.h"
#include "TrackingTools/GeomPropagators/interface/AnalyticalPropagator.h"
#include "TrackingTools/GeomPropagators/interface/HelixForwardPlaneCrossing.h"
#include "TrackingTools/GeomPropagators/interface/StraightLinePlaneCrossing.h"
#include "TrackingTools/GeomPropagators/interface/Propagator.h"
#include "TrackingTools/KalmanUpdators/interface/Chi2MeasurementEstimator.h"
#include "TrackingTools/MeasurementDet/interface/MeasurementDet.h"
#include "TrackingTools/PatternTools/interface/TSCBLBuilderNoMaterial.h"
#include "TrackingTools/Records/interface/TrackingComponentsRecord.h"
#include "TrackingTools/TrajectoryState/interface/FreeTrajectoryState.h"
#include "TrackingTools/TrajectoryState/interface/TrajectoryStateOnSurface.h"

using mkfitdev::outconv::EmuEl;
using mkfitdev::outconv::EmuGroups;
using mkfitdev::outconv::NavEmuCache;

namespace {
  using Clock = std::chrono::steady_clock;

  // field study: mkFit's parabolic Bz(z, r) (MkFitCore Config::bFieldFromZR, constants set by MkFitGeometryESProducer
  // from bFieldParams, i.e. #186's), no radial component (as mkFit's helix); a stand-in for the field a device PCA
  // computation would use.
  class MkFitParabolicField final : public MagneticField {
  public:
    MkFitParabolicField() { setNominalValue(); }
    GlobalVector inTesla(const GlobalPoint& gp) const override {
      return GlobalVector(0.f, 0.f, mkfit::Config::bFieldFromZR(gp.z(), gp.perp()));
    }
    bool isDefined(const GlobalPoint&) const override { return true; }
  };

  // the stock converter's hit ordering (MkFitOutputTrackConverter.cc, Phase-2 branch of the recHits.sort lambda),
  // evaluated on keys computed once per hit
  struct HitKey {
    GlobalPoint pos;
    bool barrel;  // GeomDetEnumerators::isBarrel(det()->subDetector()) == DetId subdet PXB / TIB / TOB (checked in-job)
    bool tiltedOrNotBarrel;  // (subdetId == TOB && tobSide < 3): the stock "barrel tilted" clause, per hit
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

  enum Section { kInput, kState, kHits, kKeys, kPCA, kPattern, kNavInner, kNavOuter, kOutput, kNSections };
  constexpr const char* kSectionNames[kNSections] = {"input/setup",
                                                     "state+checks",
                                                     "hits",
                                                     "keys+order",
                                                     "PCA (TSCBL)",
                                                     "hit pattern",
                                                     "nav inner",
                                                     "nav outer",
                                                     "output"};
}  // namespace

class MkFitAlpakaOutputTrackConverter : public edm::global::EDProducer<> {
public:
  explicit MkFitAlpakaOutputTrackConverter(edm::ParameterSet const& iConfig);

  static void fillDescriptions(edm::ConfigurationDescriptions& descriptions);

private:
  void produce(edm::StreamID, edm::Event& iEvent, const edm::EventSetup& iSetup) const override;
  void endJob() override;

  const edm::EDGetTokenT<::mkfitdev::TrackSoAHostCollection> tracksToken_;
  edm::EDGetTokenT<::mkfitdev::MkFitStatusHostObject> statusToken_;
  edm::EDGetTokenT<::mkfitdev::FitOuterStateHostCollection> outerToken_;  // DEVIATION D6 (empty tag: off)
  edm::EDGetTokenT<::mkfitdev::OutConvHostCollection> pcaToken_;  // stage C device PCA (empty tag: host TSCBL)
  std::string statusLabel_;
  const edm::EDGetTokenT<MkFitClusterIndexToHit> pixelClusterIndexToHitToken_;
  const edm::EDGetTokenT<MkFitClusterIndexToHit> stripClusterIndexToHitToken_;
  const edm::EDGetTokenT<edm::View<TrajectorySeed>> seedToken_;
  const edm::EDGetTokenT<MeasurementTrackerEvent> measurementTrackerEventToken_;
  const edm::EDGetTokenT<reco::BeamSpot> bsToken_;
  const edm::ESGetToken<Propagator, TrackingComponentsRecord> propagatorAlongToken_;
  const edm::ESGetToken<Propagator, TrackingComponentsRecord> propagatorOppositeToken_;
  const edm::ESGetToken<MagneticField, IdealMagneticFieldRecord> mfToken_;
  const edm::ESGetToken<MkFitGeometry, TrackerRecoGeometryRecord> mkFitGeomToken_;
  const edm::ESGetToken<TrackerTopology, TrackerTopologyRcd> tTopoToken_;
  const edm::ESGetToken<NavigationSchool, NavigationSchoolRecord> navToken_;
  const edm::EDPutTokenT<reco::TrackCollection> putTrackToken_;
  const edm::EDPutTokenT<TrackingRecHitCollection> putHitsToken_;
  const edm::EDPutTokenT<reco::TrackExtraCollection> putExtraToken_;
  const edm::EDPutTokenT<std::vector<SeedStopInfo>> putSeedStopInfoToken_;

  const float qualityMaxInvPt_;
  const float qualityMinTheta_;
  const float qualityMaxRsq_;
  const float qualityMaxZ_;
  const float qualityMaxPosErrSq_;
  const bool qualitySignPt_;
  const int algo_;
  const bool timing_;
  const bool fieldStudy_;
  const bool stockOrder_;
  const bool orderStudy_;
  // stage D: otClustersOnDemand set = OT hits made on demand (mkFitStripHits may then be a size-only map)
  edm::EDGetTokenT<Phase2TrackerCluster1DCollectionNew> otClustersToken_;
  edm::ESGetToken<TrackerGeometry, TrackerDigiGeometryRecord> otGeomToken_;
  edm::ESGetToken<ClusterParameterEstimator<Phase2TrackerCluster1D>, TkPhase2OTCPERecord> otCpeToken_;
  const bool dropBadChi2_;  // DEVIATION D4 switch (mkfitfit 0005), default off = stock
  const bool pcaCheck_;     // device PCA validation: the host TSCBL too, pulls at endJob (output = device PCA)
  const bool navStudy_;     // stage C (c) requirement: does the ORDER of compatibleDets matter for the hit pattern?
  const bool navEmuStudy_;  // stage C (c) design test: brute-force compatible set + the front() rule vs compatibleDets
  // stage C (c), round 10: the missing-hit entries from the transliterated compatibleDets (analytic propagator, no
  // material, layer tables per stream) instead of DetLayer::compatibleDets; a layer not laid out as expected falls back
  const bool navEmulated_;
  mutable std::array<std::unique_ptr<NavEmuCache>, 256> navCache_;
  mutable std::atomic<long long> nvCalls_{0}, nvFallback_{0};

  // instrumentation (untracked switches; off in the menu)
  mutable std::array<std::atomic<long long>, kNSections> secNs_{};
  mutable std::atomic<long long> nEvents_{0}, nCands_{0}, nTracks_{0}, nHits_{0}, nLong_{0};
  mutable std::atomic<long long> nOuterOk_{0}, nOuterMissing_{0};  // DEVIATION D6: TrackExtras with / without states
  mutable std::array<std::atomic<long long>, 2> navCalls_{}, navFound_{};
  mutable std::array<std::atomic<long long>, 8> navByKind_{}, navFoundByKind_{};
  mutable std::mutex fieldMutex_;
  mutable long long fsN_ = 0;
  mutable std::array<double, 5> fsSumPull_{}, fsMaxPull_{}, fsSumPull2_{};
  mutable std::array<std::array<long long, 3>, 5> fsOver_{};  // |pull| >= 1e-2, 1e-1, 1 (the D-M4 floor edges)
  mutable double fsSumRelB_ = 0, fsMaxRelB_ = 0;
  // missing-hit study: navigation lost inner+outer (rows) vs mkFit's own bookkeeping, count of kHitMissIdx (-1) +
  // kHitInGapIdx (-7) entries in the hit list (columns), both capped at 7; and the inner / outer split separately
  mutable std::array<std::array<long long, 8>, 8> msTot_{}, msInner_{}, msOuter_{};
  // R7-M4 (orderStudy): tracks whose insertion order differs from the stock std::sort on the same (mkFit) input order:
  // anywhere / at hit 0 (TrackListMerger allowFirstHitShare reads recHit(0)) / at the last hit
  mutable std::atomic<long long> osTracks_{0}, osAny_{0}, osFirst_{0}, osLast_{0};
  // pcaCheck: device PCA vs the host TSCBL on the same first-hit state (pulls = (device - host) / host error for
  // qoverp, lambda, phi, dxy, dsz; the D7-a isolated floor edges 1e-2 / 1e-1 / 1)
  mutable std::mutex pcaMutex_;
  mutable long long pcN_ = 0, pcBitwise_ = 0, pcDevOnly_ = 0, pcHostOnly_ = 0, pcBothFail_ = 0, pcFallback_ = 0;
  mutable std::array<double, 5> pcMaxPull_{};
  mutable std::array<std::array<long long, 3>, 5> pcOver_{};
  mutable double pcMaxRelCov_ = 0;
  mutable long long pcRelCov1e4_ = 0;
  // navStudy, [inner|outer]: non-empty compatibleDets calls, calls with >= 2 dets, of those: another det would give a
  // different pattern word (subdet/layer/side/stereo + type) / a different missing-vs-inactive type; max dets per call
  mutable std::array<std::atomic<long long>, 2> nsNonEmpty_{}, nsMulti_{}, nsWordDiffer_{}, nsTypeDiffer_{};
  // of the >= 2 det calls: all dets in ONE module (Phase-2 stack: its two sensors) / front() is the stack's lower
  // sensor / >= 2 modules and then another MODULE's det (not front()'s stack partner) gives a different word
  mutable std::array<std::atomic<long long>, 2> nsOneModule_{}, nsFrontLower_{}, nsMultiModule_{}, nsModuleWordDiffer_{};
  // front() is the det whose state is nearest to the start state (the first one crossed), all calls with >= 2 dets
  mutable std::array<std::atomic<long long>, 2> nsFrontNearest_{};
  // front() is the det with the smallest surface radius (barrel layer) / smallest |z| (endcap layer)
  mutable std::array<std::atomic<long long>, 2> nsFrontInnermost_{};
  // the same two rules restricted to the one-module (stack) calls, and to barrel / endcap layers
  mutable std::array<std::atomic<long long>, 2> nsStackNearest_{}, nsStackInnermost_{};
  mutable std::array<std::array<std::atomic<long long>, 2>, 2> nsStackByKind_{}, nsStackInnermostByKind_{};
  // candidate device rule: the module of the det nearest to the start state, in it the innermost sensor; calls where
  // that det's pattern entry differs from front()'s (all >= 2 det calls)
  mutable std::array<std::atomic<long long>, 2> nsRuleWordDiffer_{};
  // navEmuStudy, per compatibleDets call: both empty / stock only / emulation only / same pattern entry / different
  mutable std::array<std::atomic<long long>, 2> neCalls_{}, neBothEmpty_{}, neStockOnly_{}, neEmuOnly_{}, neSame_{},
      neDiffer_{};
  // variant 2: Phase2OTtiltedBarrelLayer tests only the tilted rings on the z side of the START state
  // (Phase2OTtiltedBarrelLayer.cc: tsos.globalPosition().z() < 0 ? negative rings : positive rings)
  mutable std::array<std::atomic<long long>, 2> ne2BothEmpty_{}, ne2StockOnly_{}, ne2EmuOnly_{}, ne2Same_{}, ne2Differ_{};
  // per layer kind [inner|outer][barrel|endcap][pixel|OT] (navKind order): calls / emulation-only / different entry
  mutable std::array<std::atomic<long long>, 8> neKindCalls_{}, neKindEmuOnly_{}, neKindDiffer_{};
  // variant 3 (analytic propagator) / 4 (the stock PropagatorWithMaterial), round 10: the OT endcap layers emulated
  // structurally (rings by crossing proximity, closest sub-layer det + brother, phi neighbours, DetGroup merging,
  // Phase2EndcapRing's |z| sort), the other layers as variant 2. Per kind: calls, both empty, stock only, emulation
  // only, same, different. Fallback = an OT endcap layer not laid out as expected (variant 2 used); ring layout misses.
  struct NavEmuCounts {
    std::array<std::atomic<long long>, 2> both{}, so{}, eo{}, same{}, diff{};
    std::array<std::array<std::atomic<long long>, 6>, 8> kind{};
  };
  mutable std::array<NavEmuCounts, 2> ne3_;
  mutable std::array<std::atomic<long long>, 2> ne3Fallback_{};
  mutable std::atomic<long long> ne3RingBad_{0}, ne3NoCross_{0}, ne3Tracks_{0}, ne3TracksDiffer_{0};
  // ordering keys (ii) design check, once per job (navEmuStudy): is the stock comparator's TOB "tilted" clause
  // (tobSide < 3, TrackerTopology) the same as a tilted module plane (|normal z| > 1e-3)? Then the device can take it
  // from the ES module table's plane normal (zdir) instead of a new topology bit.
  mutable std::once_flag tiltOnce_;
  // (d) requirement (navEmuStudy): missing-hit entries [inner|outer], of which the det is not active / has bad components
  mutable std::array<std::atomic<long long>, 2> ndEntries_{}, ndInactive_{}, ndBad_{};
  mutable std::atomic<long long> nsMaxDets_{0};
};

MkFitAlpakaOutputTrackConverter::MkFitAlpakaOutputTrackConverter(edm::ParameterSet const& iConfig)
    : tracksToken_{consumes(iConfig.getParameter<edm::InputTag>("tracks"))},
      pixelClusterIndexToHitToken_{consumes(iConfig.getParameter<edm::InputTag>("mkFitPixelHits"))},
      stripClusterIndexToHitToken_{consumes(iConfig.getParameter<edm::InputTag>("mkFitStripHits"))},
      seedToken_{consumes(iConfig.getParameter<edm::InputTag>("seeds"))},
      measurementTrackerEventToken_{consumes(iConfig.getParameter<edm::InputTag>("measurementTrackerEvent"))},
      bsToken_{consumes(iConfig.getParameter<edm::InputTag>("beamSpot"))},
      propagatorAlongToken_{esConsumes(iConfig.getParameter<edm::ESInputTag>("propagatorAlong"))},
      propagatorOppositeToken_{esConsumes(iConfig.getParameter<edm::ESInputTag>("propagatorOpposite"))},
      mfToken_{esConsumes()},
      mkFitGeomToken_{esConsumes()},
      tTopoToken_{esConsumes()},
      navToken_{esConsumes(iConfig.getParameter<edm::ESInputTag>("NavigationSchool"))},
      putTrackToken_{produces()},
      putHitsToken_{produces()},
      putExtraToken_{produces()},
      putSeedStopInfoToken_{produces()},
      qualityMaxInvPt_{float(iConfig.getParameter<double>("qualityMaxInvPt"))},
      qualityMinTheta_{float(iConfig.getParameter<double>("qualityMinTheta"))},
      qualityMaxRsq_{float(std::pow(iConfig.getParameter<double>("qualityMaxR"), 2))},
      qualityMaxZ_{float(iConfig.getParameter<double>("qualityMaxZ"))},
      qualityMaxPosErrSq_{float(std::pow(iConfig.getParameter<double>("qualityMaxPosErr"), 2))},
      qualitySignPt_{iConfig.getParameter<bool>("qualitySignPt")},
      // DEVIATION D8: an explicit track algorithm; stock derives it from the seeds label, and the HLT label
      // hltInitialStepTrajectorySeedsLST maps to undefAlgorithm, which PFAlgo treats as a 1e9-error track
      algo_{reco::TrackBase::algoByName(
          iConfig.getParameter<std::string>("algorithm").empty()
              ? std::string(TString(iConfig.getParameter<edm::InputTag>("seeds").label()).ReplaceAll("Seeds", "").Data())
              : iConfig.getParameter<std::string>("algorithm"))},
      timing_{iConfig.getUntrackedParameter<bool>("timing")},
      fieldStudy_{iConfig.getUntrackedParameter<bool>("fieldStudy")},
      stockOrder_{iConfig.getUntrackedParameter<bool>("validateStockOrder")},
      orderStudy_{iConfig.getUntrackedParameter<bool>("orderStudy")},
      dropBadChi2_{iConfig.getParameter<bool>("dropNegativeChi2")},
      pcaCheck_{iConfig.getUntrackedParameter<bool>("pcaCheck")},
      navStudy_{iConfig.getUntrackedParameter<bool>("navStudy")},
      navEmuStudy_{iConfig.getUntrackedParameter<bool>("navEmuStudy")},
      navEmulated_{iConfig.getParameter<bool>("navEmulated")} {
  if (const auto pca = iConfig.getParameter<edm::InputTag>("pcaStates"); !pca.label().empty())
    pcaToken_ = consumes(pca);
  if (pcaCheck_ && pcaToken_.isUninitialized())
    throw cms::Exception("Configuration") << "MkFitAlpakaOutputTrackConverter: pcaCheck needs pcaStates";
  if (auto const ot = iConfig.getParameter<edm::InputTag>("otClustersOnDemand"); !ot.label().empty()) {
    otClustersToken_ = consumes(ot);
    otGeomToken_ = esConsumes();
    otCpeToken_ = esConsumes(iConfig.getParameter<edm::ESInputTag>("Phase2StripCPE"));
  }
  if (const auto outer = iConfig.getParameter<edm::InputTag>("outerStates"); !outer.label().empty())
    outerToken_ = consumes(outer);
  const auto status = iConfig.getParameter<edm::InputTag>("status");
  if (!status.label().empty()) {
    statusToken_ = consumes(status);
    statusLabel_ = status.encode();
  }
  if (iConfig.getParameter<bool>("TrajectoryInEvent"))
    throw cms::Exception("Configuration") << "MkFitAlpakaOutputTrackConverter: TrajectoryInEvent needs per-hit states, "
                                             "which the device fit does not export; use the stock converter";
  for (auto& s : secNs_)
    s = 0;
}

void MkFitAlpakaOutputTrackConverter::fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
  edm::ParameterSetDescription desc;
  desc.add("tracks", edm::InputTag{"hltInitialStepTrackCandidatesMkFitFitDevice"})
      ->setComment("mkfitdev TrackSoA of the device final fit (host copy)");
  desc.add("status", edm::InputTag())->setComment("MkFitStatusHostObject; empty = the TrackSoA overflow counters");
  desc.add("mkFitPixelHits", edm::InputTag{"mkFitSiPixelHits"});
  desc.add("mkFitStripHits", edm::InputTag{"mkFitSiStripHits"});
  desc.add<edm::InputTag>("otClustersOnDemand", edm::InputTag(""))
      ->setComment("stage D: Phase-2 OT hits of output tracks made on demand from these clusters with Phase2StripCPE "
                   "(= the legacy rechits); empty = read them through mkFitStripHits");
  desc.add<edm::ESInputTag>("Phase2StripCPE", edm::ESInputTag("phase2StripCPEESProducer", "Phase2StripCPE"));
  desc.add("seeds", edm::InputTag{"initialStepSeeds"});
  desc.add<std::string>("algorithm", "")
      ->setComment("DEVIATION D8 (switch, stock = empty): track algorithm name; empty = the stock rule (seeds label "
                   "without 'Seeds', undefAlgorithm for the HLT label)");
  desc.add("beamSpot", edm::InputTag{"offlineBeamSpot"})
      ->setComment("beam line of the PCA; offlineBeamSpot as the stock converter (hard-coded there; R7-L6)");
  desc.add("propagatorAlong", edm::ESInputTag{"", "PropagatorWithMaterial"});
  desc.add("propagatorOpposite", edm::ESInputTag{"", "PropagatorWithMaterialOpposite"});
  desc.add<double>("qualityMaxInvPt", 100)->setComment("max(1/pt) for converted tracks");
  desc.add<double>("qualityMinTheta", 0.01)->setComment("lower bound on theta (or pi-theta) for converted tracks");
  desc.add<double>("qualityMaxR", 120)->setComment("max(R) for the state position for converted tracks");
  desc.add<double>("qualityMaxZ", 280)->setComment("max(|Z|) for the state position for converted tracks");
  desc.add<double>("qualityMaxPosErr", 100)->setComment("max position error for converted tracks");
  desc.add<bool>("qualitySignPt", true)->setComment("check sign of 1/pt for converted tracks");
  desc.add<edm::ESInputTag>("NavigationSchool", edm::ESInputTag{"", "SimpleNavigationSchool"});
  desc.add<edm::InputTag>("measurementTrackerEvent", edm::InputTag("MeasurementTrackerEvent"));
  desc.add("outerStates", edm::InputTag())
      ->setComment("DEVIATION D6 (switch, stock = empty): the device fit's outer states (MkFitAlpakaFitDeviceProducer "
                   "storeOuterState); the TrackExtras then carry the inner (first-hit) and outer states as TrackProducer's");
  desc.add<bool>("dropNegativeChi2", false)
      ->setComment("DEVIATION D4 (switch, stock = false): drop candidates whose fit chi2 is negative or not finite, as "
                   "the KF final fit rejects trajectories with a non-positive-definite covariance");
  desc.add("pcaStates", edm::InputTag())
      ->setComment("stage C: the device PCA (MkFitAlpakaOutConvStateProducer, OutConvHostCollection) used instead of "
                   "TSCBLBuilderNoMaterial; empty = the host TSCBL");
  desc.addUntracked<bool>("pcaCheck", false)
      ->setComment("device PCA validation: also run the host TSCBL, pulls and status agreement at endJob");
  desc.addUntracked<bool>("navStudy", false)
      ->setComment("study: for compatibleDets results with >= 2 dets, would another det change the hit-pattern entry");
  desc.add<bool>("navEmulated", false)
      ->setComment("stage C (c): missing-hit navigation by the transliterated compatibleDets (TkDetLayers search order, "
                  "AnalyticalPropagator with errors, no material); identical entries on ttbar/QCD (navEmuStudy)");
  desc.addUntracked<bool>("navEmuStudy", false)
      ->setComment("study: compatibleDets emulated by brute force (AnalyticalPropagator with errors to every det plane "
                   "near the crossing, the estimator's -3 sigma bounds test, the front() rule) vs the stock call");
  desc.add<bool>("TrajectoryInEvent", false)->setComment("must be False (no per-hit states from the device fit)");
  desc.addUntracked<bool>("timing", false)->setComment("per-section timers, printed at endJob");
  desc.addUntracked<bool>("fieldStudy", false)
      ->setComment("PCA with mkFit's parabolic field vs the CMSSW MagneticField: pulls at endJob (study only)");
  desc.addUntracked<bool>("validateStockOrder", false)
      ->setComment("order the hits with std::sort as the stock converter (exact stock order; validation only)");
  desc.addUntracked<bool>("orderStudy", false)
      ->setComment("R7-M4: count tracks whose insertion order differs from the stock std::sort (study)");
  descriptions.addWithDefaultLabel(desc);
}

void MkFitAlpakaOutputTrackConverter::produce(edm::StreamID sid, edm::Event& iEvent, const edm::EventSetup& iSetup) const {
  if (sid.value() >= navCache_.size())
    throw cms::Exception("Configuration") << "MkFitAlpakaOutputTrackConverter: more than " << navCache_.size() << " streams";
  auto t0 = Clock::now();
  std::array<long long, kNSections> sec{};
  auto lap = [&](Section s) {
    if (timing_) {
      const auto t1 = Clock::now();
      sec[s] += std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
      t0 = t1;
    }
  };

  if (!statusLabel_.empty())
    ::mkfitdev::warnIfNotClean(iEvent.get(statusToken_).value(), statusLabel_);
  const auto& soa = iEvent.get(tracksToken_);
  const auto v = soa.const_view();
  if (statusLabel_.empty() && v.metadata().size() != 0 && (v.nOverflowTracks() != 0 || v.nOverflowHits() != 0))
    edm::LogWarning("MkFitAlpakaOutputTrackConverter")
        << "event " << iEvent.id().event() << ": TrackSoA overflow, " << v.nOverflowTracks() << " tracks dropped, "
        << v.nOverflowHits() << " hit lists truncated";
  // R4-M1: a skipped or seedless event carries a zero-capacity TrackSoA; its scalars are not read
  const int nCand = v.metadata().size() == 0 ? 0 : v.nTracks();
  const ::mkfitdev::FitOuterStateHostCollection* outerStates =
      outerToken_.isUninitialized() ? nullptr : &iEvent.get(outerToken_);
  if (outerStates && outerStates->const_view().metadata().size() < nCand)
    throw cms::Exception("LogicError") << "MkFitAlpakaOutputTrackConverter: outer-state rows "
                                       << outerStates->const_view().metadata().size() << " < tracks " << nCand;
  long long nOuterOk = 0, nOuterMissing = 0;
  const ::mkfitdev::OutConvHostCollection* pcaStates = pcaToken_.isUninitialized() ? nullptr : &iEvent.get(pcaToken_);
  if (pcaStates && pcaStates->const_view().metadata().size() < nCand)
    throw cms::Exception("LogicError") << "MkFitAlpakaOutputTrackConverter: PCA rows "
                                       << pcaStates->const_view().metadata().size() << " < tracks " << nCand;
  // pcaCheck, per event (merged at the end of the event)
  long long pcN = 0, pcBitwise = 0, pcDevOnly = 0, pcHostOnly = 0, pcBothFail = 0, pcFallback = 0, pcRelCov1e4 = 0;
  std::array<double, 5> pcMaxPull{};
  std::array<std::array<long long, 3>, 5> pcOver{};
  double pcMaxRelCov = 0;

  edm::Handle<edm::View<TrajectorySeed>> hseeds;
  iEvent.getByToken(seedToken_, hseeds);
  const auto& pixelHits = iEvent.get(pixelClusterIndexToHitToken_).hits();
  const auto& stripHits = iEvent.get(stripClusterIndexToHitToken_).hits();
  const auto& measTk = iEvent.get(measurementTrackerEventToken_);
  const auto& bs = iEvent.get(bsToken_);
  const auto& mf = iSetup.getData(mfToken_);
  const auto& propagatorAlong = iSetup.getData(propagatorAlongToken_);
  const auto& propagatorOpposite = iSetup.getData(propagatorOppositeToken_);
  const auto& mkFitGeom = iSetup.getData(mkFitGeomToken_);
  const auto& tTopo = iSetup.getData(tTopoToken_);
  const auto& navSchool = iSetup.getData(navToken_);
  const auto& detLayers = mkFitGeom.detLayers();
  if (navEmuStudy_)
    std::call_once(tiltOnce_, [&]() {
      long long nTob = 0, nTiltTopo = 0, nTiltNormal = 0, nMismatch = 0;
      float minTiltNz = 1.f, maxFlatNz = 0.f;
      std::unordered_set<const GeomDet*> seen;
      for (auto const* l : detLayers) {
        if (l == nullptr)
          continue;
        for (auto const* g : l->basicComponents()) {
          const DetId id = g->geographicalId();
          if (id.subdetId() != StripSubdetector::TOB || !seen.insert(g).second)
            continue;
          ++nTob;
          const bool topo = tTopo.tobSide(id) < 3;
          const float nz = std::abs(g->surface().normalVector().z());
          const bool normal = nz > 1e-3f;
          nTiltTopo += topo;
          nTiltNormal += normal;
          nMismatch += topo != normal;
          if (topo)
            minTiltNz = std::min(minTiltNz, nz);
          else
            maxFlatNz = std::max(maxFlatNz, nz);
        }
      }
      edm::LogPrint("MkFitAlpakaOutputTrackConverter")
          << "[outconv TILTCHECK] TOB sensors " << nTob << ": tilted by topology (tobSide < 3) " << nTiltTopo
          << ", by plane normal (|n_z| > 1e-3) " << nTiltNormal << ", mismatches " << nMismatch
          << "; min |n_z| tilted " << minTiltNz << ", max |n_z| flat " << maxFlatNz;
    });
  const auto& trackerInfo = mkFitGeom.trackerInfo();
  if (mkFitGeom.isPhase1())
    throw cms::Exception("Configuration") << "MkFitAlpakaOutputTrackConverter supports the Phase-2 geometry only";

  reco::TrackCollection trks;
  trks.reserve(nCand);
  TrackingRecHitCollection outHits;
  reco::TrackExtraCollection extras;
  extras.reserve(nCand);
  const reco::TrackExtraRefProd refExtras = iEvent.getRefBeforePut<reco::TrackExtraCollection>();
  const TrackingRecHitRefProd refHits = iEvent.getRefBeforePut<TrackingRecHitCollection>();

  // stage D: on-demand OT hits (per-candidate store, reserved: stable addresses)
  std::optional<Phase2TrackerRecHitOnDemand> otOnDemand;
  if (!otClustersToken_.isUninitialized())
    otOnDemand.emplace(iEvent.getHandle(otClustersToken_), iSetup.getData(otGeomToken_), iSetup.getData(otCpeToken_));
  std::vector<Phase2TrackerRecHit1D> otStore;
  otStore.reserve(::mkfitdev::kMaxTrkHits);
  // per-candidate scratch (fixed capacity = the TrackSoA hit-list capacity)
  std::array<const BaseTrackerRecHit*, ::mkfitdev::kMaxTrkHits> hitPtr;
  std::array<HitKey, ::mkfitdev::kMaxTrkHits> keys;
  std::array<int, ::mkfitdev::kMaxTrkHits> order;
  //use negative sigma=-3.0 in order to use a more conservative definition of isInside() for Bounds classes.
  const Chi2MeasurementEstimator estimator(30., -3.0, 0.5, 2.0, 0.5, 1.e12);  // as the stock converter
  const TSCBLBuilderNoMaterial tscblBuilder;
  MkFitParabolicField mkfField;
  long long nHitsOut = 0, nLong = 0;
  std::array<long long, 2> navCalls{}, navFound{};  // compatibleDets calls / non-empty results, inner and outer
  std::array<long long, 8> navByKind{}, navFoundByKind{};  // [inner|outer][barrel|endcap][pixel|OT]
  auto navKind = [](int d, const DetLayer* l) {
    const auto sub = l->subDetector();
    return d * 4 + (GeomDetEnumerators::isBarrel(sub) ? 0 : 2) + (GeomDetEnumerators::isInnerTracker(sub) ? 0 : 1);
  };
  // navStudy: the hit-pattern entry every returned det would give vs the one of front() (what the converter appends)
  auto studyDets = [&](int d,
                       std::vector<GeometricSearchDet::DetWithState> const& dws,
                       GlobalPoint const& start,
                       bool barrelLayer) {
    const auto cat = d == 0 ? reco::HitPattern::MISSING_INNER_HITS : reco::HitPattern::MISSING_OUTER_HITS;
    const auto miss = d == 0 ? TrackingRecHit::missing_inner : TrackingRecHit::missing_outer;
    const auto inact = d == 0 ? TrackingRecHit::inactive_inner : TrackingRecHit::inactive_outer;
    auto entry = [&](GeometricSearchDet::DetWithState const& x, TrackingRecHit::Type& type) {
      MeasurementDetWithData const& md = measTk.idToDet(x.first->geographicalId());
      type = md.isActive() && !md.hasBadComponents(x.second) ? miss : inact;
      reco::HitPattern hp;
      hp.appendHit(x.first->geographicalId(), type, tTopo);
      return hp.getHitPattern(cat, 0);
    };
    ++nsNonEmpty_[d];
    long long mx = nsMaxDets_.load();
    while (static_cast<long long>(dws.size()) > mx && !nsMaxDets_.compare_exchange_weak(mx, dws.size())) {
    }
    if (dws.size() < 2)
      return;
    ++nsMulti_[d];
    // module key: the Phase-2 stack for stacked sensors, the det itself otherwise
    auto moduleOf = [&](DetId id) -> uint32_t {
      return (tTopo.isLower(id) || tTopo.isUpper(id)) ? tTopo.stack(id) : id.rawId();
    };
    const DetId id0 = dws.front().first->geographicalId();
    const uint32_t m0 = moduleOf(id0);
    TrackingRecHit::Type t0, ti;
    const auto w0 = entry(dws.front(), t0);
    bool wordDiff = false, typeDiff = false, oneModule = true, moduleWordDiff = false;
    for (size_t i = 1; i < dws.size(); ++i) {
      const auto wi = entry(dws[i], ti);
      const bool sameModule = moduleOf(dws[i].first->geographicalId()) == m0;
      wordDiff = wordDiff || wi != w0;
      typeDiff = typeDiff || ti != t0;
      oneModule = oneModule && sameModule;
      moduleWordDiff = moduleWordDiff || (!sameModule && wi != w0);
    }
    nsWordDiffer_[d] += wordDiff;
    nsTypeDiffer_[d] += typeDiff;
    nsOneModule_[d] += oneModule;
    nsFrontLower_[d] += oneModule && tTopo.isLower(id0);
    nsMultiModule_[d] += !oneModule;
    nsModuleWordDiffer_[d] += moduleWordDiff;
    bool nearest = true;
    const float d0 = (dws.front().second.globalPosition() - start).mag2();
    for (size_t i = 1; i < dws.size(); ++i)
      nearest = nearest && d0 <= (dws[i].second.globalPosition() - start).mag2();
    nsFrontNearest_[d] += nearest;
    auto rank = [&](GeometricSearchDet::DetWithState const& x) {
      const auto p = x.first->surface().position();
      return barrelLayer ? p.perp() : std::abs(p.z());
    };
    bool innermost = true;
    for (size_t i = 1; i < dws.size(); ++i)
      innermost = innermost && rank(dws.front()) <= rank(dws[i]);
    nsFrontInnermost_[d] += innermost;
    {
      size_t iNear = 0;
      for (size_t i = 1; i < dws.size(); ++i)
        if ((dws[i].second.globalPosition() - start).mag2() < (dws[iNear].second.globalPosition() - start).mag2())
          iNear = i;
      const uint32_t mNear = moduleOf(dws[iNear].first->geographicalId());
      size_t iRule = iNear;
      for (size_t i = 0; i < dws.size(); ++i)
        if (moduleOf(dws[i].first->geographicalId()) == mNear && rank(dws[i]) < rank(dws[iRule]))
          iRule = i;
      TrackingRecHit::Type tr;
      nsRuleWordDiffer_[d] += entry(dws[iRule], tr) != w0;
    }
    if (oneModule) {
      nsStackNearest_[d] += nearest;
      nsStackInnermost_[d] += innermost;
      ++nsStackByKind_[d][barrelLayer ? 0 : 1];
      nsStackInnermostByKind_[d][barrelLayer ? 0 : 1] += innermost;
    }
  };
  // navEmuStudy: what a device compatibleDets would do (no GeometricSearchDet search, no material)
  std::optional<AnalyticalPropagator> anaAlong, anaOpp;
  if (navEmuStudy_ || navEmulated_) {
    anaAlong.emplace(&mf, alongMomentum);
    anaOpp.emplace(&mf, oppositeToMomentum);
  }
  auto patternEntry = [&](int d, const GeomDet* det, TrajectoryStateOnSurface const& ts) {
    MeasurementDetWithData const& md = measTk.idToDet(det->geographicalId());
    const auto type = md.isActive() && !md.hasBadComponents(ts)
                          ? (d == 0 ? TrackingRecHit::missing_inner : TrackingRecHit::missing_outer)
                          : (d == 0 ? TrackingRecHit::inactive_inner : TrackingRecHit::inactive_outer);
    reco::HitPattern hp;
    hp.appendHit(det->geographicalId(), type, tTopo);
    return hp.getHitPattern(d == 0 ? reco::HitPattern::MISSING_INNER_HITS : reco::HitPattern::MISSING_OUTER_HITS, 0);
  };
  if (!navCache_[sid.value()])
    navCache_[sid.value()] = std::make_unique<NavEmuCache>();
  if (navEmulated_ || navEmuStudy_) {  // the tables point into the tracker search geometry: rebuild them on a new IOV
    const unsigned long long iov = iSetup.get<TrackerRecoGeometryRecord>().cacheIdentifier();
    if (navCache_[sid.value()]->iov != iov) {
      navCache_[sid.value()]->clearTables();
      navCache_[sid.value()]->iov = iov;
    }
  }
  const mkfitdev::outconv::NavEmulation navEmu(
      tTopo, estimator, *navCache_[sid.value()], ne3RingBad_, ne3NoCross_, navEmuStudy_);
  // the transliterated compatibleDets' front(): 1 found, 0 none, -1 layer not laid out as expected
  auto emuFront = [&](int d, const DetLayer* layer, TrajectoryStateOnSurface const& start, EmuEl& out) {
    const Propagator& ana = d == 0 ? *anaOpp : *anaAlong;
    EmuGroups g;
    const bool done = navEmu.groups(layer, start, ana, g);
    if (!done)
      return -1;
    if (g.empty() || g.front().el.empty())
      return 0;
    out = g.front().el.front();
    return 1;
  };
  bool emuTrackSame = true;  // variant 3: every missing-hit entry of this track equals the stock one
  auto emuDets = [&](int d,
                     const DetLayer* layer,
                     TrajectoryStateOnSurface const& start,
                     std::vector<GeometricSearchDet::DetWithState> const& stock) {
    const Propagator& ana = d == 0 ? *anaOpp : *anaAlong;
    const Propagator& mat = d == 0 ? static_cast<const Propagator&>(propagatorOpposite)
                                   : static_cast<const Propagator&>(propagatorAlong);
    const bool barrelLayer = GeomDetEnumerators::isBarrel(layer->subDetector());
    const GlobalPoint sp = start.globalPosition();
    using Cands = std::vector<std::pair<const GeomDet*, TrajectoryStateOnSurface>>;
    // brute force: every det of the layer near the layer crossing, propagated with errors, -3 sigma bounds
    auto brute = [&](Propagator const& prop, Cands& cands) {
      const auto cross = prop.propagate(start, layer->surface());
      for (auto const* det : layer->basicComponents()) {
        const auto dp = det->surface().position();
        if (cross.isValid()) {  // prefilter near the layer crossing
          const auto cp = cross.globalPosition();
          if (std::abs(reco::deltaPhi(dp.barePhi(), cp.barePhi())) > 0.35f ||
              (barrelLayer ? std::abs(dp.z() - cp.z()) > 25.f : std::abs(dp.perp() - cp.perp()) > 25.f))
            continue;
        }
        const auto ts = prop.propagate(start, det->surface());
        if (!ts.isValid() || !static_cast<const MeasurementEstimator&>(estimator).estimate(ts, det->surface()))
          continue;
        cands.emplace_back(det, ts);
      }
    };
    auto moduleOf = [&](DetId id) -> uint32_t {
      return (tTopo.isLower(id) || tTopo.isUpper(id)) ? tTopo.stack(id) : id.rawId();
    };
    auto rank = [&](const GeomDet* g) {
      return barrelLayer ? g->surface().position().perp() : std::abs(g->surface().position().z());
    };
    // the rule on a candidate list: nearest det's module, its innermost sensor
    auto pick = [&](Cands const& cs, TrajectoryStateOnSurface& pickTs) -> const GeomDet* {
      const GeomDet* b = nullptr;
      float bd2 = std::numeric_limits<float>::max();
      for (auto const& [g, ts] : cs) {
        const float d2 = (ts.globalPosition() - sp).mag2();
        if (d2 < bd2)
          bd2 = d2, b = g, pickTs = ts;
      }
      if (b == nullptr)
        return b;
      const uint32_t m = moduleOf(b->geographicalId());
      for (auto const& [g, ts] : cs)
        if (moduleOf(g->geographicalId()) == m && rank(g) < rank(b))
          b = g, pickTs = ts;
      return b;
    };
    // the tilted-ring side rule (Phase2OTtiltedBarrelLayer.cc:88)
    auto sideRule = [&](Cands const& cands) {
      Cands kept;
      const bool startNeg = sp.z() < 0;
      for (auto const& [g, ts] : cands) {
        const DetId id = g->geographicalId();
        if (id.subdetId() == StripSubdetector::TOB && tTopo.tobSide(id) < 3 && ((tTopo.tobSide(id) == 1) != startNeg))
          continue;  // a tilted ring on the other z side than the start state: never tested by the stock layer
        kept.emplace_back(g, ts);
      }
      return kept;
    };
    auto count = [&](const GeomDet* b, TrajectoryStateOnSurface const& bts, auto& both, auto& so, auto& eo, auto& same,
                     auto& diff) -> int {
      if (stock.empty() && b == nullptr)
        return ++both[d], 0;
      if (b == nullptr)
        return ++so[d], 1;
      if (stock.empty())
        return ++eo[d], 2;
      if (patternEntry(d, stock.front().first, stock.front().second) == patternEntry(d, b, bts))
        return ++same[d], 3;
      return ++diff[d], 4;
    };
    Cands cands;
    brute(ana, cands);
    TrajectoryStateOnSurface bestTs, ts2;
    const GeomDet* best = pick(cands, bestTs);
    const GeomDet* b2 = pick(sideRule(cands), ts2);
    count(b2, ts2, ne2BothEmpty_, ne2StockOnly_, ne2EmuOnly_, ne2Same_, ne2Differ_);
    ++neCalls_[d];
    const int kind = navKind(d, layer);
    ++neKindCalls_[kind];
    const int r1 = count(best, bestTs, neBothEmpty_, neStockOnly_, neEmuOnly_, neSame_, neDiffer_);
    neKindEmuOnly_[kind] += r1 == 2;
    neKindDiffer_[kind] += r1 == 4;
    // variants 3 (analytic) and 4 (the stock PropagatorWithMaterial): the transliterated search (OutConvNavEmulation.h);
    // variant 2 only where a layer is not laid out as expected (counted as fallback)
    for (int v = 0; v < 2; ++v) {
      Propagator const& prop = v == 0 ? ana : mat;
      const GeomDet* b = nullptr;
      TrajectoryStateOnSurface bts;
      bool done = false;
      {
        EmuGroups g;
        done = navEmu.groups(layer, start, prop, g);
        if (done && !g.empty() && !g.front().el.empty()) {
          b = g.front().el.front().det;
          bts = g.front().el.front().ts;
        }
        ne3Fallback_[v] += !done;
      }
      if (!done) {
        if (v == 0) {
          b = b2;
          bts = ts2;
        } else {
          Cands cm;
          brute(mat, cm);
          b = pick(sideRule(cm), bts);
        }
      }
      auto& c = ne3_[v];
      const int r = count(b, bts, c.both, c.so, c.eo, c.same, c.diff);
      ++c.kind[kind][0];
      ++c.kind[kind][1 + r];
      if (v == 0 && (r == 1 || r == 2 || r == 4))
        emuTrackSame = false;
    }
  };
  lap(kInput);

  for (int c = 0; c < nCand; ++c) {
    const auto row = v[c];
    mkfit::TrackState state;
    for (int k = 0; k < 6; ++k)
      state.parameters[k] = row.params().v[k];
    std::memcpy(state.errors.Array(), row.errors().v, sizeof(float) * 21);
    state.charge = row.charge();

    // state: basic quality first (stock order)
    if (state.invpT() > qualityMaxInvPt_ || (qualitySignPt_ && state.invpT() < 0) || state.theta() < qualityMinTheta_ ||
        (M_PI - state.theta()) < qualityMinTheta_ || state.posRsq() > qualityMaxRsq_ ||
        std::abs(state.z()) > qualityMaxZ_ ||
        (state.errors.At(0, 0) + state.errors.At(1, 1) + state.errors.At(2, 2)) > qualityMaxPosErrSq_) {
      lap(kState);
      continue;
    }
    state.convertFromCCSToGlbCurvilinear();
    const auto& param = state.parameters;
    const auto& err = state.errors;
    AlgebraicSymMatrix55 cov;
    for (int i = 0; i < 5; ++i)
      for (int j = i; j < 5; ++j)
        cov[i][j] = err.At(i, j);
    const FreeTrajectoryState fts(
        GlobalTrajectoryParameters(
            GlobalPoint(param[0], param[1], param[2]), GlobalVector(param[3], param[4], param[5]), state.charge, &mf),
        CurvilinearTrajectoryError(cov));
    if (!fts.curvilinearError().posDef()) {
      lap(kState);
      continue;
    }
    // DEVIATION D4: negative / non-finite fit chi2 = non-positive-definite covariance in the fit (bit test: -Ofast safe)
    if (dropBadChi2_ && (edm::isNotFinite(row.chi2()) || row.chi2() < 0.f)) {
      lap(kState);
      continue;
    }
    //Sylvester's criterion, start from the smaller submatrix size (stock)
    double det = 0;
    const auto& cm = fts.curvilinearError().matrix();
    if ((!cm.Sub<AlgebraicSymMatrix22>(0, 0).Det(det)) || det < 0 || (!cm.Sub<AlgebraicSymMatrix33>(0, 0).Det(det)) ||
        det < 0 || (!cm.Sub<AlgebraicSymMatrix44>(0, 0).Det(det)) || det < 0 || (!cm.Det2(det)) || det < 0) {
      lap(kState);
      continue;
    }
    lap(kState);

    // on-track hits: referenced, keyed once
    const int nTot = row.nTotalHits();
    int n = 0;
    otStore.clear();
    for (int i = 0; i < nTot; ++i) {
      const auto hot = row.hits().hot[i];
      if (hot.index < 0) {
        if (detLayers.at(hot.layer) == nullptr)
          throw cms::Exception("LogicError") << "DetLayer for layer index " << hot.layer << " is null!";
        continue;
      }
      const bool isPixel = trackerInfo.layer(hot.layer).is_pixel();
      const auto& hits = isPixel ? pixelHits : stripHits;
      if (!isPixel && otOnDemand)
        otStore.push_back(otOnDemand->make(hot.index));
      const auto& thit = (!isPixel && otOnDemand) ? static_cast<BaseTrackerRecHit const&>(otStore.back())
                                                 : static_cast<BaseTrackerRecHit const&>(*hits[hot.index]);
      if (!isPixel && !thit.firstClusterRef().isPhase2())
        throw cms::Exception("LogicError") << "MkFitAlpakaOutputTrackConverter: non-Phase-2 outer-tracker hit";
      hitPtr[n] = &thit;
      order[n] = n;
      ++n;
    }
    lap(kHits);
    // the stock comparator's keys, once per hit (stage C (ii): what a device permutation would replace)
    for (int k = 0; k < n; ++k) {
      const auto& thit = *hitPtr[k];
      const auto id = thit.geographicalId();
      keys[k].pos = thit.globalPosition();
      // the comparator only asks isBarrel(subDetector()): from the DetId, no GeomDet dereference
      keys[k].barrel = id.subdetId() == PixelSubdetector::PixelBarrel || id.subdetId() == StripSubdetector::TIB ||
                       id.subdetId() == StripSubdetector::TOB;
      if (stockOrder_ && keys[k].barrel != GeomDetEnumerators::isBarrel(thit.det()->subDetector()))
        throw cms::Exception("LogicError") << "MkFitAlpakaOutputTrackConverter: barrel flag from the DetId differs from "
                                              "the GeomDet subdetector for "
                                           << id.rawId();
      keys[k].tiltedOrNotBarrel = (id.subdetId() == StripSubdetector::TOB && tTopo.tobSide(id) < 3);
    }
    // hit order: the stock comparator on the precomputed keys, applied by a stable insertion (n <= kMaxTrkHits, almost
    // sorted input). The stock OwnVector::sort is std::sort (introsort, not stable): where hits are equivalent or the
    // comparator is not a strict weak order (mixed mag2 / perp2 pairs), the two orders can differ - only among such
    // hits (same pattern words, so the hit pattern, parameters and everything else are unchanged; see
    // doc/SIMPLIFICATIONS.txt). validateStockOrder = True uses std::sort to reproduce the stock order exactly
    // (validation only; no general sort in production).
    if (stockOrder_) {
      std::sort(order.begin(), order.begin() + n, [&](int a, int b) { return hitLess(keys[a], keys[b]); });
    } else {
      std::array<int, ::mkfitdev::kMaxTrkHits> ref;  // R7-M4: the stock std::sort on the same input order
      if (orderStudy_) {
        for (int i = 0; i < n; ++i)
          ref[i] = i;
        std::sort(ref.begin(), ref.begin() + n, [&](int a, int b) { return hitLess(keys[a], keys[b]); });
      }
      for (int i = 1; i < n; ++i) {
        const int x = order[i];
        int j = i;
        while (j > 0 && hitLess(keys[x], keys[order[j - 1]])) {
          order[j] = order[j - 1];
          --j;
        }
        order[j] = x;
      }
      if (orderStudy_ && n > 0) {
        ++osTracks_;
        osAny_ += !std::equal(ref.begin(), ref.begin() + n, order.begin());
        osFirst_ += ref[0] != order[0];
        osLast_ += ref[n - 1] != order[n - 1];
      }
    }
    if (n > 16)
      ++nLong;
    lap(kKeys);
    if (n == 0)
      continue;  // stock: recHits[0] on an empty OwnVector; cannot happen for a fitted track

    const GeomDet* detH0 = hitPtr[order[0]]->det();
    if (detH0 == nullptr)
      continue;
    const TrajectoryStateOnSurface tsosState(fts, detH0->surface());
    if (!tsosState.isValid())
      continue;
    // the PCA: the device state (pcaStates; status ok) or TSCBLBuilderNoMaterial on the host (no pcaStates, or the
    // device's host-fallback / failure rows: the host decides validity there, as stock)
    const int8_t devStatus =
        pcaStates ? pcaStates->const_view()[c].pcaStatus() : int8_t(::mkfitdev::pca::kPcaHostFallback);
    const bool useDevice = devStatus == ::mkfitdev::pca::kPcaOk;
    TrajectoryStateClosestToBeamLine tsAtPCA;
    if (!useDevice || pcaCheck_)
      tsAtPCA = tscblBuilder(*tsosState.freeState(), bs);
    if (pcaCheck_) {
      ++pcN;
      pcFallback += devStatus == ::mkfitdev::pca::kPcaHostFallback;
      if (devStatus == ::mkfitdev::pca::kPcaFailed)
        ++(tsAtPCA.isValid() ? pcHostOnly : pcBothFail);
      else if (useDevice && !tsAtPCA.isValid())
        ++pcDevOnly;
    }
    if (!useDevice && !tsAtPCA.isValid()) {
      lap(kPCA);
      continue;
    }
    GlobalPoint v0;
    GlobalVector p;
    AlgebraicSymMatrix55 pcaCov;
    if (useDevice) {
      const ::mkfitdev::PcaState ds = pcaStates->const_view()[c].pcaState();
      const ::mkfitdev::PcaCov dc = pcaStates->const_view()[c].pcaCov();
      v0 = GlobalPoint(ds.v[0], ds.v[1], ds.v[2]);
      p = GlobalVector(ds.v[3], ds.v[4], ds.v[5]);
      for (int i = 0, k = 0; i < 5; ++i)
        for (int j = 0; j <= i; ++j)
          pcaCov(i, j) = dc.v[k++];  // float, as reco::TrackBase stores it
    } else {
      const auto& stateAtPCA = tsAtPCA.trackStateAtPCA();
      v0 = stateAtPCA.position();
      p = stateAtPCA.momentum();
      pcaCov = stateAtPCA.curvilinearError().matrix();
    }
    int ndof = -5;
    for (int k = 0; k < n; ++k)
      ndof += hitPtr[order[k]]->dimension();  // the stock OT clone is the same class (Phase2TrackerRecHit1D)
    reco::Track trk(row.chi2(),
                    ndof,
                    math::XYZPoint(v0.x(), v0.y(), v0.z()),
                    math::XYZVector(p.x(), p.y(), p.z()),
                    fts.charge(),  // = TrajectoryStateClosestToBeamLine::trackStateAtPCA().charge()
                    pcaCov,
                    static_cast<reco::TrackBase::TrackAlgorithm>(algo_));
    if (pcaCheck_ && useDevice && tsAtPCA.isValid()) {
      const auto& h = tsAtPCA.trackStateAtPCA();
      const reco::Track tH(row.chi2(),
                           ndof,
                           math::XYZPoint(h.position().x(), h.position().y(), h.position().z()),
                           math::XYZVector(h.momentum().x(), h.momentum().y(), h.momentum().z()),
                           h.charge(),
                           h.curvilinearError().matrix(),
                           static_cast<reco::TrackBase::TrackAlgorithm>(algo_));
      bool same = h.position().x() == v0.x() && h.position().y() == v0.y() && h.position().z() == v0.z() &&
                  h.momentum().x() == p.x() && h.momentum().y() == p.y() && h.momentum().z() == p.z();
      for (int k = 0; k < 5; ++k) {
        const double pull = std::abs(trk.parameter(k) - tH.parameter(k)) / tH.error(k);
        pcMaxPull[k] = std::max(pcMaxPull[k], pull);
        pcOver[k][0] += pull >= 1e-2;
        pcOver[k][1] += pull >= 1e-1;
        pcOver[k][2] += pull >= 1.;
      }
      double relCov = 0;
      for (int i = 0; i < 5; ++i)
        for (int j = 0; j <= i; ++j) {
          same = same && trk.covariance(i, j) == tH.covariance(i, j);
          relCov = std::max(relCov,
                            std::abs(trk.covariance(i, j) - tH.covariance(i, j)) /
                                std::sqrt(tH.covariance(i, i) * tH.covariance(j, j)));
        }
      pcMaxRelCov = std::max(pcMaxRelCov, relCov);
      pcRelCov1e4 += relCov > 1e-4;
      pcBitwise += same;
    }
    if (fieldStudy_) {
      const FreeTrajectoryState ftsM(
          GlobalTrajectoryParameters(fts.position(), fts.momentum(), fts.charge(), &mkfField), fts.curvilinearError());
      const auto tsM = tscblBuilder(ftsM, bs);
      if (tsM.isValid()) {
        const auto& sM = tsM.trackStateAtPCA();
        const reco::Track tM(row.chi2(),
                             ndof,
                             math::XYZPoint(sM.position().x(), sM.position().y(), sM.position().z()),
                             math::XYZVector(sM.momentum().x(), sM.momentum().y(), sM.momentum().z()),
                             sM.charge(),
                             sM.curvilinearError(),
                             static_cast<reco::TrackBase::TrackAlgorithm>(algo_));
        const auto pos0 = fts.position();
        const double bC = mf.inTesla(pos0).z(), bM = mkfField.inTesla(pos0).z();
        std::lock_guard<std::mutex> lk(fieldMutex_);
        ++fsN_;
        const double relB = std::abs(bM / bC - 1.);
        fsSumRelB_ += relB;
        fsMaxRelB_ = std::max(fsMaxRelB_, relB);
        for (int k = 0; k < 5; ++k) {
          const double pull = (tM.parameter(k) - trk.parameter(k)) / trk.error(k);
          fsSumPull_[k] += std::abs(pull);
          fsSumPull2_[k] += pull * pull;
          fsMaxPull_[k] = std::max(fsMaxPull_[k], std::abs(pull));
          fsOver_[k][0] += std::abs(pull) >= 1e-2;
          fsOver_[k][1] += std::abs(pull) >= 1e-1;
          fsOver_[k][2] += std::abs(pull) >= 1.;
        }
      }
    }
    lap(kPCA);

    for (int k = 0; k < n; ++k)
      trk.appendHitPattern(*hitPtr[order[k]], tTopo);
    lap(kPattern);

    //extra hits (taken from TrackProducerBase<T>::setSecondHitPattern), as the stock converter
    const auto* outerLayer = detLayers.at(mkFitGeom.mkFitLayerNumber(hitPtr[order[n - 1]]->geographicalId()));
    const auto* innerLayer = detLayers.at(mkFitGeom.mkFitLayerNumber(hitPtr[order[0]]->geographicalId()));
    emuTrackSame = true;
    {
      auto const& innerCompLayers = navSchool.compatibleLayers(*innerLayer, fts, oppositeToMomentum);
      for (auto it : innerCompLayers) {
        if (it->basicComponents().empty())
          continue;
        if (navEmulated_) {
          ++nvCalls_;
          EmuEl e{nullptr, TrajectoryStateOnSurface()};
          const int r = emuFront(0, it, tsosState, e);
          if (r >= 0) {
            ++navCalls[0];
            if (r == 0)
              continue;
            ++navFound[0];
            MeasurementDetWithData const& md = measTk.idToDet(e.det->geographicalId());
            const InvalidTrackingRecHit tmpHit(
                *e.det, md.isActive() && !md.hasBadComponents(e.ts) ? TrackingRecHit::missing_inner : TrackingRecHit::inactive_inner);
            trk.appendHitPattern(tmpHit, tTopo);
            continue;
          }
          ++nvFallback_;
        }
        auto const& detWithState = it->compatibleDets(tsosState, propagatorOpposite, estimator);
        if (navEmuStudy_)
          emuDets(0, it, tsosState, detWithState);
        ++navCalls[0];
        ++navByKind[navKind(0, it)];
        if (detWithState.empty())
          continue;
        ++navFound[0];
        ++navFoundByKind[navKind(0, it)];
        if (navStudy_)
          studyDets(0, detWithState, tsosState.globalPosition(), GeomDetEnumerators::isBarrel(it->subDetector()));
        const DetId id = detWithState.front().first->geographicalId();
        MeasurementDetWithData const& measDet = measTk.idToDet(id);
        if (navEmuStudy_) {  // (d) requirement: how often is the entry inactive, and why
          const bool active = measDet.isActive();
          ++ndEntries_[0];
          ndInactive_[0] += !active;
          ndBad_[0] += active && measDet.hasBadComponents(detWithState.front().second);
        }
        const InvalidTrackingRecHit tmpHit(*detWithState.front().first,
                                           measDet.isActive() && !measDet.hasBadComponents(detWithState.front().second)
                                               ? TrackingRecHit::missing_inner
                                               : TrackingRecHit::inactive_inner);
        trk.appendHitPattern(tmpHit, tTopo);
      }
    }
    lap(kNavInner);
    {
      auto const& outerCompLayers = navSchool.compatibleLayers(*outerLayer, fts, alongMomentum);
      for (auto it : outerCompLayers) {
        if (it->basicComponents().empty())
          continue;
        if (navEmulated_) {
          ++nvCalls_;
          EmuEl e{nullptr, TrajectoryStateOnSurface()};
          const int r = emuFront(1, it, tsosState, e);
          if (r >= 0) {
            ++navCalls[1];
            if (r == 0)
              continue;
            ++navFound[1];
            MeasurementDetWithData const& md = measTk.idToDet(e.det->geographicalId());
            const InvalidTrackingRecHit tmpHit(
                *e.det, md.isActive() && !md.hasBadComponents(e.ts) ? TrackingRecHit::missing_outer : TrackingRecHit::inactive_outer);
            trk.appendHitPattern(tmpHit, tTopo);
            continue;
          }
          ++nvFallback_;
        }
        auto const& detWithState = it->compatibleDets(tsosState, propagatorAlong, estimator);
        if (navEmuStudy_)
          emuDets(1, it, tsosState, detWithState);
        ++navCalls[1];
        ++navByKind[navKind(1, it)];
        if (detWithState.empty())
          continue;
        ++navFound[1];
        ++navFoundByKind[navKind(1, it)];
        if (navStudy_)
          studyDets(1, detWithState, tsosState.globalPosition(), GeomDetEnumerators::isBarrel(it->subDetector()));
        const DetId id = detWithState.front().first->geographicalId();
        MeasurementDetWithData const& measDet = measTk.idToDet(id);
        if (navEmuStudy_) {  // (d) requirement: how often is the entry inactive, and why
          const bool active = measDet.isActive();
          ++ndEntries_[1];
          ndInactive_[1] += !active;
          ndBad_[1] += active && measDet.hasBadComponents(detWithState.front().second);
        }
        const InvalidTrackingRecHit tmpHit(*detWithState.front().first,
                                           measDet.isActive() && !measDet.hasBadComponents(detWithState.front().second)
                                               ? TrackingRecHit::missing_outer
                                               : TrackingRecHit::inactive_outer);
        trk.appendHitPattern(tmpHit, tTopo);
      }
    }
    lap(kNavOuter);
    if (navEmuStudy_) {
      ++ne3Tracks_;
      ne3TracksDiffer_ += !emuTrackSame;
    }
    if (fieldStudy_) {
      const auto& hp = trk.hitPattern();
      const int nIn = std::min(7, hp.numberOfAllHits(reco::HitPattern::MISSING_INNER_HITS));
      const int nOut = std::min(7, hp.numberOfAllHits(reco::HitPattern::MISSING_OUTER_HITS));
      // mkFit: misses before the first / after the last valid entry of the hit list, and in total
      int first = -1, last = -1, nMiss = 0, nMissIn = 0, nMissOut = 0;
      for (int i = 0; i < nTot; ++i)
        if (row.hits().hot[i].index >= 0) {
          if (first < 0)
            first = i;
          last = i;
        }
      for (int i = 0; i < nTot; ++i) {
        const int idx = row.hits().hot[i].index;
        if (idx == -1 || idx == -7) {
          ++nMiss;
          if (i < first)
            ++nMissIn;
          if (i > last)
            ++nMissOut;
        }
      }
      std::lock_guard<std::mutex> lk(fieldMutex_);
      ++msTot_[std::min(7, nIn + nOut)][std::min(7, nMiss)];
      ++msInner_[nIn][std::min(7, nMissIn)];
      ++msOuter_[nOut][std::min(7, nMissOut)];
    }

    // output: one clone per hit (pixel: as is; OT: the stock 1D re-creation with the yy error at float max)
    const auto hidx = outHits.size();
    for (int k = 0; k < n; ++k) {
      const auto& thit = *hitPtr[order[k]];
      if (thit.firstClusterRef().isPixel())
        outHits.push_back(thit.clone());
      else
        outHits.push_back(std::make_unique<Phase2TrackerRecHit1D>(
            thit.localPosition(),
            LocalError(thit.localPositionError().xx(), 0.f, std::numeric_limits<float>::max()),
            *thit.det(),
            thit.firstClusterRef().cluster_phase2OT()));
    }
    nHitsOut += n;
    reco::TrackExtra extra;
    if (outerStates) {
      // DEVIATION D6: inner = the fitted state on the first hit's surface (tsosState, as the PCA input), outer = the
      // forward pass's updated state on its outermost hit's module; as TrackProducer / the stock converter with states
      const auto o = outerStates->const_view()[c];
      TrajectoryStateOnSurface outerTsos;
      const int pos = o.pos();
      if (pos >= 0 && pos < nTot && row.hits().hot[pos].index >= 0) {
        mkfit::TrackState os;
        for (int k = 0; k < 6; ++k)
          os.parameters[k] = o.params().v[k];
        std::memcpy(os.errors.Array(), o.errors().v, sizeof(float) * 21);
        os.charge = row.charge();
        os.convertFromCCSToGlbCurvilinear();
        AlgebraicSymMatrix55 ocov;
        for (int i = 0; i < 5; ++i)
          for (int j = i; j < 5; ++j)
            ocov[i][j] = os.errors.At(i, j);
        const auto& op = os.parameters;
        const FreeTrajectoryState ofts(GlobalTrajectoryParameters(GlobalPoint(op[0], op[1], op[2]),
                                                                  GlobalVector(op[3], op[4], op[5]),
                                                                  os.charge,
                                                                  &mf),
                                       CurvilinearTrajectoryError(ocov));
        const auto hotO = row.hits().hot[pos];
        const bool isPixelO = trackerInfo.layer(hotO.layer).is_pixel();
        // stage D: with otClustersOnDemand the strip index map is size-only, so the OT hit is made on demand here too
        std::optional<Phase2TrackerRecHit1D> hitOD;
        if (!isPixelO && otOnDemand)
          hitOD.emplace(otOnDemand->make(hotO.index));
        const auto& hitO = hitOD ? static_cast<BaseTrackerRecHit const&>(*hitOD)
                                 : static_cast<BaseTrackerRecHit const&>(*(isPixelO ? pixelHits : stripHits)[hotO.index]);
        if (hitO.det() != nullptr && ofts.curvilinearError().posDef())
          outerTsos = TrajectoryStateOnSurface(ofts, hitO.det()->surface());
        if (outerTsos.isValid()) {
          const auto ip = tsosState.globalPosition(), op2 = outerTsos.globalPosition();
          const auto im = tsosState.globalMomentum(), om = outerTsos.globalMomentum();
          extra = reco::TrackExtra(math::XYZPoint(op2.x(), op2.y(), op2.z()),
                                   math::XYZVector(om.x(), om.y(), om.z()),
                                   true,
                                   math::XYZPoint(ip.x(), ip.y(), ip.z()),
                                   math::XYZVector(im.x(), im.y(), im.z()),
                                   true,
                                   outerTsos.curvilinearError().matrix(),
                                   hitO.geographicalId().rawId(),
                                   tsosState.curvilinearError().matrix(),
                                   hitPtr[order[0]]->geographicalId().rawId(),
                                   alongMomentum);
        }
      }
      ++(outerTsos.isValid() ? nOuterOk : nOuterMissing);
    }
    extra.setHits(refHits, hidx, trk.numberOfValidHits());
    extra.setSeedRef(edm::RefToBase<TrajectorySeed>(hseeds, row.label()));
    const AlgebraicVector5 zero(0, 0, 0, 0, 0);
    extra.setTrajParams(reco::TrackExtra::TrajParams(trk.numberOfValidHits(), LocalTrajectoryParameters(zero, 1.)),
                        reco::TrackExtra::Chi2sFive(trk.numberOfValidHits(), 0));
    extras.push_back(std::move(extra));
    trk.setExtra(reco::TrackExtraRef(refExtras, extras.size() - 1));
    trks.push_back(std::move(trk));
    lap(kOutput);
  }

  const auto nSeeds = hseeds->size();
  if (pcaCheck_) {
    std::lock_guard<std::mutex> lk(pcaMutex_);
    pcN_ += pcN;
    pcBitwise_ += pcBitwise;
    pcDevOnly_ += pcDevOnly;
    pcHostOnly_ += pcHostOnly;
    pcBothFail_ += pcBothFail;
    pcFallback_ += pcFallback;
    pcRelCov1e4_ += pcRelCov1e4;
    pcMaxRelCov_ = std::max(pcMaxRelCov_, pcMaxRelCov);
    for (int k = 0; k < 5; ++k) {
      pcMaxPull_[k] = std::max(pcMaxPull_[k], pcMaxPull[k]);
      for (int b = 0; b < 3; ++b)
        pcOver_[k][b] += pcOver[k][b];
    }
  }
  if (outerStates) {
    nOuterOk_ += nOuterOk;
    nOuterMissing_ += nOuterMissing;
  }
  if (timing_) {
    for (int s = 0; s < kNSections; ++s)
      secNs_[s] += sec[s];
    ++nEvents_;
    nCands_ += nCand;
    nTracks_ += trks.size();
    nHits_ += nHitsOut;
    nLong_ += nLong;
    for (int d = 0; d < 2; ++d) {
      navCalls_[d] += navCalls[d];
      navFound_[d] += navFound[d];
    }
    for (int k = 0; k < 8; ++k) {
      navByKind_[k] += navByKind[k];
      navFoundByKind_[k] += navFoundByKind[k];
    }
  }
  iEvent.emplace(putTrackToken_, std::move(trks));
  iEvent.emplace(putExtraToken_, std::move(extras));
  iEvent.emplace(putHitsToken_, std::move(outHits));
  // as the stock converter: SeedStopInfo unfilled, one per seed
  iEvent.emplace(putSeedStopInfoToken_, nSeeds);
}

void MkFitAlpakaOutputTrackConverter::endJob() {
  if (timing_ && nEvents_ > 0) {
    edm::LogPrint log("MkFitAlpakaOutputTrackConverter");
    const double ne = nEvents_;
    long long tot = 0;
    for (auto& s : secNs_)
      tot += s;
    log << "[outconv timing] events " << nEvents_ << " cands/ev " << nCands_ / ne << " tracks/ev " << nTracks_ / ne
        << " hits/ev " << nHits_ / ne << " tracks with > 16 hits " << nLong_ << " | ms/event total " << tot / ne * 1e-6 << " :";
    for (int s = 0; s < kNSections; ++s)
      log << " " << kSectionNames[s] << " " << secNs_[s] / ne * 1e-6;
    log << " | compatibleDets calls/ev inner " << navCalls_[0] / ne << " (non-empty " << navFound_[0] / ne << "), outer "
        << navCalls_[1] / ne << " (non-empty " << navFound_[1] / ne << ")";
    {
      const char* kn[8] = {"in/barrel/pixel", "in/barrel/OT", "in/endcap/pixel", "in/endcap/OT",
                           "out/barrel/pixel", "out/barrel/OT", "out/endcap/pixel", "out/endcap/OT"};
      log << " | per layer kind calls(found)/ev:";
      for (int k = 0; k < 8; ++k)
        log << " " << kn[k] << " " << navByKind_[k] / ne << "(" << navFoundByKind_[k] / ne << ")";
    }
  }
  if (orderStudy_ && osTracks_ > 0)
    edm::LogPrint("MkFitAlpakaOutputTrackConverter")
        << "[outconv order] tracks " << osTracks_ << " insertion order != stock std::sort: any " << osAny_
        << " first hit " << osFirst_ << " last hit " << osLast_;
  if (navEmulated_ || navEmuStudy_) {
    long long calls = 0, tests = 0, maxTests = 0, maxEl = 0, maxGroups = 0;
    std::array<long long, 16> hist{};
    for (auto const& cache : navCache_)
      if (cache) {
        calls += cache->calls;
        tests += cache->detTests;
        maxTests = std::max(maxTests, cache->maxDetTests);
        maxEl = std::max(maxEl, cache->maxElements);
        maxGroups = std::max(maxGroups, cache->maxGroups);
        for (int i = 0; i < 16; ++i)
          hist[i] += cache->detTestsHist[i];
      }
    long long dt[8] = {}, dtv[4] = {}, dtf[2] = {};
    double dtMaxDpos = 0, dtMaxRelErr = 0, dtDevMaxDphi = 0, dtFullMaxRel = 0;
    for (auto const& cache : navCache_)
      if (cache) {
        long long const v[8] = {cache->dtTests, cache->dtNonRect, cache->dtSagFlip, cache->dtValidFlip,
                                cache->dtDecFlip, cache->dtCompared, cache->dtDpos1um, cache->dtRelErr1e3};
        for (int i = 0; i < 8; ++i)
          dt[i] += v[i];
        dtMaxDpos = std::max(dtMaxDpos, cache->dtMaxDpos);
        dtMaxRelErr = std::max(dtMaxRelErr, cache->dtMaxRelErr);
        dtv[0] += cache->dtHostOnly;
        dtv[1] += cache->dtHostOnlyOk;
        dtv[2] += cache->dtDevOnly;
        dtv[3] += cache->dtDevOnlyIn;
        dtDevMaxDphi = std::max(dtDevMaxDphi, cache->dtDevOnlyMaxDphi);
        dtf[0] += cache->dtFullRel1e3;
        dtf[1] += cache->dtFullDecFlip;
        dtFullMaxRel = std::max(dtFullMaxRel, cache->dtFullMaxRel);
      }
    if (dt[0] > 0)
      edm::LogPrint("MkFitAlpakaOutputTrackConverter")
          << "[outconv DETTEST] device per-det test (DetPlaneTest.h; end covariance from the host) vs the host: tests "
          << dt[0] << ", non-rectangular " << dt[1] << ", sagitta flips " << dt[2] << ", crossing validity flips "
          << dt[3] << " (host only " << dtv[0] << ", of which host-compatible " << dtv[1] << "; device only " << dtv[2]
          << ", of which inside the unshrunk bounds " << dtv[3] << ", max |rho s| " << dtDevMaxDphi << ")"
          << ", compared " << dt[5] << ", |dpos| > 1 um " << dt[6] << " (max " << dtMaxDpos
          << " cm), local error rel diff > 1e-3 " << dt[7] << " (max " << dtMaxRelErr << "), decision flips " << dt[4]
          << " | full device chain (start covariance transported by pca::curvilinearJacobian over the device crossing): "
             "local error rel diff > 1e-3 "
          << dtf[0] << " (max " << dtFullMaxRel << "), decision flips " << dtf[1];
    edm::LogPrint log("MkFitAlpakaOutputTrackConverter");
    log << "[outconv NAVEMUSIZE] transliterated searches " << calls << ", det tests " << tests << " ("
        << (calls > 0 ? double(tests) / calls : 0.) << " per search, max " << maxTests << "), max dets returned "
        << maxEl << ", max groups " << maxGroups << " | det tests per search 0..15+:";
    for (auto x : hist)
      log << " " << x;
  }
  if (navEmulated_)
    edm::LogPrint("MkFitAlpakaOutputTrackConverter")
        << "[outconv NAVEMULATED] transliterated compatibleDets calls " << nvCalls_ << ", fell back to DetLayer::compatibleDets "
        << nvFallback_;
  if (navEmuStudy_) {
    edm::LogPrint log("MkFitAlpakaOutputTrackConverter");
    log << "[outconv NAVEMU] brute-force compatible set (AnalyticalPropagator with errors, -3 sigma bounds) + front() "
           "rule vs compatibleDets";
    for (int d = 0; d < 2; ++d)
      log << " | " << (d == 0 ? "inner" : "outer") << ": calls " << neCalls_[d] << ", both empty " << neBothEmpty_[d]
          << ", stock only " << neStockOnly_[d] << ", emulation only " << neEmuOnly_[d] << ", same entry " << neSame_[d]
          << ", different entry " << neDiffer_[d] << " (tilted rings on the start state's z side only: both empty "
          << ne2BothEmpty_[d] << ", stock only " << ne2StockOnly_[d] << ", emulation only " << ne2EmuOnly_[d]
          << ", same " << ne2Same_[d] << ", different " << ne2Differ_[d] << ")";
    const char* kn[8] = {"in/barrel/pixel", "in/barrel/OT", "in/endcap/pixel", "in/endcap/OT",
                         "out/barrel/pixel", "out/barrel/OT", "out/endcap/pixel", "out/endcap/OT"};
    log << " | per layer kind calls / emulation-only / different:";
    for (int k = 0; k < 8; ++k)
      log << " " << kn[k] << " " << neKindCalls_[k] << "/" << neKindEmuOnly_[k] << "/" << neKindDiffer_[k];
    log << "\n[outconv NAVACTIVE] stock entries inner " << ndEntries_[0] << " (det not active " << ndInactive_[0]
        << ", bad components at the predicted point " << ndBad_[0] << "), outer " << ndEntries_[1] << " (" << ndInactive_[1]
        << ", " << ndBad_[1] << ")";
    for (int v = 0; v < 2; ++v) {
      auto const& c = ne3_[v];
      log << "\n[outconv NAVEMU3] variant " << (v == 0 ? "3 (analytic)" : "4 (stock PropagatorWithMaterial)")
          << ": every layer kind structural; fallback calls " << ne3Fallback_[v]
          << ", bad rings " << ne3RingBad_ << ", barrel calls without a cylinder crossing " << ne3NoCross_;
      if (v == 0)
        log << "; tracks " << ne3Tracks_ << ", tracks with any missing-hit entry different from stock " << ne3TracksDiffer_;
      for (int d = 0; d < 2; ++d) {
        const long long n = c.both[d] + c.so[d] + c.eo[d] + c.same[d] + c.diff[d];
        log << " | " << (d == 0 ? "inner" : "outer") << ": calls " << n << ", agree " << c.both[d] + c.same[d] << " ("
            << (n > 0 ? 100. * (c.both[d] + c.same[d]) / n : 0.) << "%), both empty " << c.both[d] << ", stock only "
            << c.so[d] << ", emulation only " << c.eo[d] << ", same " << c.same[d] << ", different " << c.diff[d];
      }
      log << " | per kind calls/both/stock-only/emu-only/same/different:";
      for (int k = 0; k < 8; ++k)
        log << " " << kn[k] << " " << c.kind[k][0] << "/" << c.kind[k][1] << "/" << c.kind[k][2] << "/" << c.kind[k][3]
            << "/" << c.kind[k][4] << "/" << c.kind[k][5];
    }
  }
  if (navStudy_) {
    edm::LogPrint log("MkFitAlpakaOutputTrackConverter");
    log << "[outconv NAVORDER] max dets per call " << nsMaxDets_;
    for (int d = 0; d < 2; ++d)
      log << " | " << (d == 0 ? "inner" : "outer") << ": non-empty calls " << nsNonEmpty_[d] << ", >= 2 dets "
          << nsMulti_[d] << ", another det gives a different pattern word " << nsWordDiffer_[d]
          << ", a different missing/inactive type " << nsTypeDiffer_[d] << "; all in one module " << nsOneModule_[d]
          << " (front = the lower sensor " << nsFrontLower_[d] << "), >= 2 modules " << nsMultiModule_[d]
          << " (another module's det gives a different word " << nsModuleWordDiffer_[d] << "), front = the det nearest "
          << "to the start state " << nsFrontNearest_[d] << ", front = the innermost det (barrel r / endcap |z|) "
          << nsFrontInnermost_[d] << "; one-module calls: front nearest " << nsStackNearest_[d] << ", innermost "
          << nsStackInnermost_[d] << " (barrel " << nsStackInnermostByKind_[d][0] << "/" << nsStackByKind_[d][0]
          << ", endcap " << nsStackInnermostByKind_[d][1] << "/" << nsStackByKind_[d][1] << ")"
          << "; rule (nearest module, its innermost sensor) gives another word than front() " << nsRuleWordDiffer_[d];
  }
  if (pcaCheck_) {
    edm::LogPrint log("MkFitAlpakaOutputTrackConverter");
    log << "[outconv PCACHECK] tracks " << pcN_ << " device == host bitwise (x, p, covariance) " << pcBitwise_
        << " | status: host fallback " << pcFallback_ << ", device ok / host invalid " << pcDevOnly_
        << ", device failed / host valid " << pcHostOnly_ << ", both failed " << pcBothFail_
        << " | tracks with |pull| >= 1e-2 / 1e-1 / 1 (qoverp, lambda, phi, dxy, dsz):";
    for (int k = 0; k < 5; ++k)
      log << " " << pcOver_[k][0] << "/" << pcOver_[k][1] << "/" << pcOver_[k][2];
    log << " | max |pull|:";
    for (int k = 0; k < 5; ++k)
      log << " " << pcMaxPull_[k];
    log << " | max |dC|/sqrt(CiiCjj) " << pcMaxRelCov_ << " (tracks > 1e-4: " << pcRelCov1e4_ << ")";
  }
  if (!outerToken_.isUninitialized())
    edm::LogPrint("MkFitAlpakaOutputTrackConverter")
        << "[outconv D6] TrackExtras with inner/outer states " << nOuterOk_ << ", without (no valid outer state) "
        << nOuterMissing_;
  if (fieldStudy_ && fsN_ > 0) {
    edm::LogPrint log("MkFitAlpakaOutputTrackConverter");
    log << "[outconv field] tracks " << fsN_ << " |Bz_mkFit/Bz_CMSSW - 1| at the first-hit state mean "
        << fsSumRelB_ / fsN_ << " max " << fsMaxRelB_ << " | PCA pulls (mkFit field - CMSSW field)/sigma "
        << "(qoverp, lambda, phi, dxy, dsz) mean|.|/rms/max:";
    for (int k = 0; k < 5; ++k)
      log << " " << fsSumPull_[k] / fsN_ << "/" << std::sqrt(fsSumPull2_[k] / fsN_) << "/" << fsMaxPull_[k];
    log << " | tracks with |pull| >= 1e-2 / 1e-1 / 1:";
    for (int k = 0; k < 5; ++k)
      log << " " << fsOver_[k][0] << "/" << fsOver_[k][1] << "/" << fsOver_[k][2];
    auto table = [&](const char* name, const std::array<std::array<long long, 8>, 8>& t) {
      edm::LogPrint log2("MkFitAlpakaOutputTrackConverter");
      log2 << "[outconv miss " << name << "] rows = navigation count 0..7, columns = mkFit -1/-7 count 0..7";
      for (int r = 0; r < 8; ++r) {
        log2 << "\n  " << r << ":";
        for (int c = 0; c < 8; ++c)
          log2 << " " << t[r][c];
      }
    };
    table("total", msTot_);
    table("inner(before first valid)", msInner_);
    table("outer(after last valid)", msOuter_);
  }
}

DEFINE_FWK_MODULE(MkFitAlpakaOutputTrackConverter);
