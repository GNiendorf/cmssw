/**
 * PixelTrackTorchHighPuritySelector
 *
 * GPU/Accelerator module performing HighPurity pixel-track selection composed of:
 *   1. CA-based quality preselection
 *   2. Feature extraction
 *   3. TorchScript DNN inference
 *   4. Score-based filtering
 *   5. Track/hit compaction and output production
 *
 * Input: TracksSoA (pixel tracks + hit associations), TrackingRecHitsSoA + OTRecHitsSoA
 * (the latter only when useHitFeatures = true).
 *
 * Torch inference: track tensor [maxPreselectedTracks, N_track_features], padding slots 0-filled.
 */

#include <algorithm>
#include <optional>
#include <vector>

#include "HeterogeneousCore/AlpakaInterface/interface/config.h"
#include "HeterogeneousCore/AlpakaInterface/interface/workdivision.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/EDGetToken.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/EDPutToken.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/Event.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/EventSetup.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/stream/FixedQueueEDProducer.h"

#include "FWCore/Framework/interface/Frameworkfwd.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include "FWCore/Utilities/interface/InputTag.h"

#include "DataFormats/TrackSoA/interface/TrackDefinitions.h"
#include "DataFormats/TrackSoA/interface/TracksDevice.h"
#include "DataFormats/TrackSoA/interface/TracksHost.h"
#include "DataFormats/TrackSoA/interface/alpaka/TracksSoACollection.h"
#include "DataFormats/TrackingRecHitSoA/interface/alpaka/OTRecHitsSoACollection.h"
#include "DataFormats/TrackingRecHitSoA/interface/alpaka/TrackingRecHitsSoACollection.h"
#include "DataFormats/TrackingRecHitSoA/interface/alpaka/VectorHitsSoACollection.h"
#include "RecoTracker/PixelSeeding/interface/CAHitsView.h"

#include "RecoTracker/FinalTrackSelectors/interface/PixelTrackFeaturesSoA.h"
#include "RecoTracker/FinalTrackSelectors/plugins/alpaka/PixelTrackFeaturesDeviceCollection.h"
#include "RecoTracker/FinalTrackSelectors/plugins/alpaka/PixelTrackTorchHighPuritySelectorKernels.h"

#include "PhysicsTools/PyTorchAlpaka/interface/BatchedTensorCollection.h"
#include "PhysicsTools/PyTorchAlpaka/interface/alpaka/AlpakaModel.h"

namespace ALPAKA_ACCELERATOR_NAMESPACE {

  class PixelTrackTorchHighPuritySelector : public stream::FixedQueueEDProducer<> {
    using TkSoADevice = reco::TracksSoACollection;
    using HitsOnDevice = reco::TrackingRecHitsSoACollection;
    using VectorHitsOnDevice = reco::VectorHitsSoACollection;
    using OTHitsOnDevice = reco::OTRecHitsSoACollection;

  public:
    explicit PixelTrackTorchHighPuritySelector(const edm::ParameterSet&);
    static void fillDescriptions(edm::ConfigurationDescriptions&);

  private:
    void produce(device::Event&, const device::EventSetup&) override;
    void beginStream(edm::StreamID /*sid*/, Queue queue) override;

    /// Registers the "track_features" tensor, sliced in batches of `batchSize` rows: the 17 fit/cov columns,
    /// plus the first 14 hit/vector hit columns when useHitFeatures, in the model's input order. The tensor may
    /// span the two blocks: they are back to back and the fit block is a whole number of padded columns.
    template <typename TFitRecord, typename THitRecord>
    static void addTrackFeatures(cms::torch::alpakatools::BatchedTensorCollection<Queue>& tc,
                                 uint32_t batchSize,
                                 TFitRecord& r,
                                 THitRecord& h,
                                 bool useHitFeatures) {
      if (useHitFeatures) {
        tc.addBatched<PixelTrackFitFeaturesSoA>("track_features",
                                                batchSize,
                                                r.chi2(),
                                                r.dzError(),
                                                r.dxyError(),
                                                r.eta(),
                                                r.nHits(),
                                                r.phi(),
                                                r.phiError(),
                                                r.pt(),
                                                r.qOverPtError(),
                                                r.dzBS(),
                                                r.dxyBS(),
                                                r.nLayers(),
                                                r.cotThetaError(),
                                                r.covCotThetaDz(),
                                                r.covDxyQOverPt(),
                                                r.covPhiDxy(),
                                                r.covPhiQOverPt(),
                                                h.caFitChi2(),
                                                h.psFrac(),
                                                h.r0(),
                                                h.nPS(),
                                                h.spanZ(),
                                                h.nVectorHits(),
                                                h.logChi2VectorHit(),
                                                h.kErr(),
                                                h.dcaEst(),
                                                h.nBarrel(),
                                                h.rzChi2(),
                                                h.meanVectorHitKappa(),
                                                h.leverArm(),
                                                h.rMax());
      } else {
        tc.addBatched<PixelTrackFitFeaturesSoA>("track_features",
                                                batchSize,
                                                r.chi2(),
                                                r.dzError(),
                                                r.dxyError(),
                                                r.eta(),
                                                r.nHits(),
                                                r.phi(),
                                                r.phiError(),
                                                r.pt(),
                                                r.qOverPtError(),
                                                r.dzBS(),
                                                r.dxyBS(),
                                                r.nLayers(),
                                                r.cotThetaError(),
                                                r.covCotThetaDz(),
                                                r.covDxyQOverPt(),
                                                r.covPhiDxy(),
                                                r.covPhiQOverPt());
      }
    }

    /// Inference dtype, shared by the warm-up and the per-event forward passes: the first forward moves
    /// and casts the model to the device, so the two must agree.
    std::optional<::torch::Dtype> inferenceDtype() const {
      return inferenceHalf_ ? std::optional<::torch::Dtype>{::torch::kHalf} : std::nullopt;
    }

    const device::EDGetToken<TkSoADevice> pixelTrackToken_;
    const int maxNumberOfTracks_;
    const int maxPreselectedTracks_;
    const int minNumberOfHits_;
    const int avgHitsPerTrack_;
    const pixelTrack::Quality minimumTrackQuality_;
    const double scoreThreshold_;
    const bool passThreeHitTracks_;
    // dxy-aware threshold ramp: threshold goes from scoreThresholdLowDxy at |dxyBS|=0 down to
    // scoreThreshold at |dxyBS|>=dxyRampKnee. scoreThresholdLowDxy < 0 disables it (flat threshold).
    const double scoreThresholdLowDxy_;
    const double dxyRampKnee_;
    // When true, the 14 CA hit/vector hit features are appended to the 17 fit/cov features, for the enriched
    // 31-feature model; the merged-hits product is consumed only then.
    const bool useHitFeatures_;
    // forward(dtype) casts the whole model: kHalf is fine for the MLP but breaks a tree model, whose
    // int64 index buffers must not be cast. inferenceHalf = false leaves the model in fp32.
    const bool inferenceHalf_;
    device::EDGetToken<HitsOnDevice> pixelHitsToken_;
    device::EDGetToken<VectorHitsOnDevice> vectorHitsToken_;
    // Raw OT-rechit SoA, so the hit-feature walk can resolve the bit30-tagged raw-OT ids a track may
    // carry. Consumed only when useHitFeatures_.
    device::EDGetToken<OTHitsOnDevice> otRecHitsSoAToken_;
    torch::AlpakaModel model_;
    const int batchSize_;
    const int warmupIterations_ = 3;
    // score working points per (pT, |eta|) cell, separately for three-hit tracks, and the high-purity marking;
    // the table is copied to the device once per stream and read there by the score filter
    const std::vector<float> wpPtEdges_;
    const std::vector<float> wpAbsEtaEdges_;
    const std::vector<float> wpThresholds_;
    const std::vector<float> wpThresholdsThreeHit_;
    const double highPurityThreshold_;
    const int highPurityMinNumberOfHits_;
    const std::vector<float> highPurityThresholds_;
    const std::vector<float> highPurityThresholdsThreeHit_;
    std::optional<cms::alpakatools::device_buffer<Device, float[]>> wpDevice_;
    // wait for the selector's device work at the end of produce, so that its input tracks may be deleted early
    const bool waitBeforeReturn_;
    const device::EDPutToken<TkSoADevice> tokenTrackOut_;
  };

  namespace {
    std::vector<float> toFloats(std::vector<double> const& v) { return std::vector<float>(v.begin(), v.end()); }

    void checkEdges(std::vector<float> const& edges, char const* name) {
      for (size_t k = 1; k < edges.size(); ++k) {
        if (!(edges[k] > edges[k - 1])) {
          throw cms::Exception("PixelTrackConfiguration") << name << " must be strictly increasing";
        }
      }
    }
  }  // namespace

  PixelTrackTorchHighPuritySelector::PixelTrackTorchHighPuritySelector(const edm::ParameterSet& iConfig)
      : FixedQueueEDProducer(iConfig),
        pixelTrackToken_(consumes(iConfig.getParameter<edm::InputTag>("pixelTrackSrc"))),
        maxNumberOfTracks_(iConfig.getParameter<int>("maxNumberOfTracks")),
        maxPreselectedTracks_(iConfig.getParameter<int>("maxPreselectedTracks")),
        minNumberOfHits_(iConfig.getParameter<int>("minNumberOfHits")),
        avgHitsPerTrack_(iConfig.getParameter<int>("avgHitsPerTrack")),
        minimumTrackQuality_(pixelTrack::qualityByName(iConfig.getParameter<std::string>("minimumTrackQuality"))),
        scoreThreshold_(iConfig.getParameter<double>("scoreThreshold")),
        passThreeHitTracks_(iConfig.getParameter<bool>("passThreeHitTracks")),
        scoreThresholdLowDxy_(iConfig.getParameter<double>("scoreThresholdLowDxy")),
        dxyRampKnee_(iConfig.getParameter<double>("dxyRampKnee")),
        useHitFeatures_(iConfig.getParameter<bool>("useHitFeatures")),
        inferenceHalf_(iConfig.getParameter<bool>("inferenceHalf")),
        model_(iConfig.getParameter<edm::FileInPath>("model").fullPath()),
        batchSize_(iConfig.getParameter<int>("batchSize")),
        wpPtEdges_(toFloats(iConfig.getParameter<std::vector<double>>("wpPtEdges"))),
        wpAbsEtaEdges_(toFloats(iConfig.getParameter<std::vector<double>>("wpAbsEtaEdges"))),
        wpThresholds_(toFloats(iConfig.getParameter<std::vector<double>>("wpThresholds"))),
        wpThresholdsThreeHit_(toFloats(iConfig.getParameter<std::vector<double>>("wpThresholdsThreeHit"))),
        highPurityThreshold_(iConfig.getParameter<double>("highPurityThreshold")),
        highPurityMinNumberOfHits_(iConfig.getParameter<int>("highPurityMinNumberOfHits")),
        highPurityThresholds_(toFloats(iConfig.getParameter<std::vector<double>>("highPurityThresholds"))),
        highPurityThresholdsThreeHit_(
            toFloats(iConfig.getParameter<std::vector<double>>("highPurityThresholdsThreeHit"))),
        waitBeforeReturn_(iConfig.getParameter<bool>("waitBeforeReturn")),
        tokenTrackOut_(produces()) {
    checkEdges(wpPtEdges_, "wpPtEdges");
    checkEdges(wpAbsEtaEdges_, "wpAbsEtaEdges");
    const size_t nCells = (wpPtEdges_.size() + 1) * (wpAbsEtaEdges_.size() + 1);
    if (!wpThresholds_.empty() && wpThresholds_.size() != nCells) {
      throw cms::Exception("PixelTrackConfiguration")
          << "wpThresholds has " << wpThresholds_.size() << " entries, the (pT, |eta|) table needs " << nCells;
    }
    if (!wpThresholdsThreeHit_.empty() && (wpThresholds_.empty() || wpThresholdsThreeHit_.size() != nCells)) {
      throw cms::Exception("PixelTrackConfiguration")
          << "wpThresholdsThreeHit needs wpThresholds and the same number of entries (" << nCells << ")";
    }
    for (auto const* hp : {&highPurityThresholds_, &highPurityThresholdsThreeHit_}) {
      if (!hp->empty() && (wpThresholds_.empty() || hp->size() != nCells || highPurityThreshold_ < 0.)) {
        throw cms::Exception("PixelTrackConfiguration")
            << "highPurityThresholds(ThreeHit) need wpThresholds, a non-negative highPurityThreshold and " << nCells
            << " entries";
      }
    }
    if (useHitFeatures_) {
      pixelHitsToken_ = consumes(iConfig.getParameter<edm::InputTag>("pixelRecHitSrc"));
      vectorHitsToken_ = consumes(iConfig.getParameter<edm::InputTag>("vectorHitsSrc"));
      otRecHitsSoAToken_ = consumes(iConfig.getParameter<edm::InputTag>("otRecHitsSoASrc"));
    }
    if (minimumTrackQuality_ == pixelTrack::Quality::notQuality) {
      throw cms::Exception("PixelTrackConfiguration")
          << iConfig.getParameter<std::string>("minimumTrackQuality") + " is not a pixelTrack::Quality";
    }
    if (minimumTrackQuality_ < pixelTrack::Quality::dup) {
      throw cms::Exception("PixelTrackConfiguration")
          << iConfig.getParameter<std::string>("minimumTrackQuality") + " not supported";
    }
    if (maxPreselectedTracks_ > maxNumberOfTracks_) {
      throw cms::Exception("PixelTrackConfiguration") << "maxPreselectedTracks must be <= maxNumberOfTracks";
    }
    if (batchSize_ <= 0) {
      throw cms::Exception("PixelTrackConfiguration") << "batchSize must be positive";
    }
  }

  void PixelTrackTorchHighPuritySelector::beginStream(edm::StreamID /*sid*/, Queue queue) {
    // The working-point table goes to the device once: edges, then the cut block, the three-hit block and the
    // high-purity blocks
    if (!wpThresholds_.empty()) {
      std::vector<float> packed(wpPtEdges_);
      packed.insert(packed.end(), wpAbsEtaEdges_.begin(), wpAbsEtaEdges_.end());
      packed.insert(packed.end(), wpThresholds_.begin(), wpThresholds_.end());
      packed.insert(packed.end(), wpThresholdsThreeHit_.begin(), wpThresholdsThreeHit_.end());
      packed.insert(packed.end(), highPurityThresholds_.begin(), highPurityThresholds_.end());
      packed.insert(packed.end(), highPurityThresholdsThreeHit_.begin(), highPurityThresholdsThreeHit_.end());
      auto host = cms::alpakatools::make_host_buffer<float[]>(queue, packed.size());
      std::copy(packed.begin(), packed.end(), host.data());
      wpDevice_.emplace(cms::alpakatools::make_device_buffer<float[]>(queue, packed.size()));
      alpaka::memcpy(queue, *wpDevice_, host);
      alpaka::wait(queue);
    }

    // Warmup the model with dummy data

    // Create temporary feature and score buffers used to warm up the model, with the per-event capacity: the input
    // tensors are views of the SoA columns, so the warm-up then sees the same shapes and strides as the events.
    PixelTrackFeaturesOnDevice trackFeatures(queue, maxPreselectedTracks_, useHitFeatures_ ? maxPreselectedTracks_ : 0);
    PixelTrackScoresOnDevice trackScoresOnDevice(queue, maxPreselectedTracks_);
    // a Metarecords keeps a reference to its view, so the views live in named locals
    auto fit_view = trackFeatures.view().fit();
    auto hit_view = trackFeatures.view().hit();
    auto fit_record = fit_view.records();
    auto hit_record = hit_view.records();
    auto score_record = trackScoresOnDevice.view().records();

    for (auto it = 0; it < warmupIterations_; ++it) {
      cms::torch::alpakatools::BatchedTensorCollection<Queue> dummy_inputs;
      cms::torch::alpakatools::BatchedTensorCollection<Queue> dummy_outputs;

      // Same column list (and therefore the same tensor width) and batching as the per-event inference below.
      addTrackFeatures(dummy_inputs, batchSize_, fit_record, hit_record, useHitFeatures_);

      dummy_outputs.addBatched<PixelTrackScoresSoA>("track_scores", batchSize_, score_record.score());

      model_.forward(queue, dummy_inputs, dummy_outputs, inferenceDtype());
    }
  }

  void PixelTrackTorchHighPuritySelector::produce(device::Event& iEvent, const device::EventSetup&) {
    /*
    Processing steps:
      1. CA-based preselection of tracks
      2. Feature extraction (track SoA)
      3. DNN inference
      4. Score-based filtering
      5. Track compaction and output production
*/
    // Retrieve tokens
    auto& queue = iEvent.queue();
    const auto& tracks = iEvent.get(pixelTrackToken_).view();

    // Instantiate the necessary objects in memory
    //  - Temporary storage for filtering
    auto d_nPreselectedTracks = cms::alpakatools::make_device_buffer<int>(queue);
    auto d_nSelectedTracks = cms::alpakatools::make_device_buffer<int>(queue);
    auto d_preselectedTrackIndices = cms::alpakatools::make_device_buffer<int[]>(queue, maxNumberOfTracks_);
    auto d_selectedTrackIndices = cms::alpakatools::make_device_buffer<int[]>(queue, maxPreselectedTracks_);
    auto d_trackHitCounts = cms::alpakatools::make_device_buffer<int[]>(queue, maxPreselectedTracks_);
    auto d_selectedTrackHitOffsets = cms::alpakatools::make_device_buffer<int[]>(queue, maxPreselectedTracks_);
    auto d_preselectionOffsets = cms::alpakatools::make_device_buffer<int[]>(queue, maxNumberOfTracks_);

    alpaka::memset(queue, d_nPreselectedTracks, 0);
    alpaka::memset(queue, d_nSelectedTracks, 0);
    alpaka::memset(queue, d_trackHitCounts, 0);
    alpaka::memset(queue, d_selectedTrackHitOffsets, 0);
    alpaka::memset(queue, d_preselectedTrackIndices, 0xFF);
    alpaka::memset(queue, d_selectedTrackIndices, 0xFF);
    alpaka::memset(queue, d_preselectionOffsets, 0);

    //  - Features and scores containers; the hit block is sized 0 when the model does not read it
    PixelTrackFeaturesOnDevice trackFeatures(queue, maxPreselectedTracks_, useHitFeatures_ ? maxPreselectedTracks_ : 0);
    PixelTrackScoresOnDevice trackScoresOnDevice(queue, maxPreselectedTracks_);

    // 1. CA-based preselection of tracks
    //  Launch first kernel to look which tracks need to be filtered out
    //  based on quality criteria from the CA

    launchCAPreselection(queue,
                         maxNumberOfTracks_,
                         minNumberOfHits_,
                         minimumTrackQuality_,
                         tracks.tracks(),
                         alpaka::getPtrNative(d_preselectedTrackIndices),
                         alpaka::getPtrNative(d_preselectionOffsets),
                         alpaka::getPtrNative(d_nPreselectedTracks));

    // 2. Feature extraction. The merged TrackingRecHitsSoA is needed only for the hit/vector hit features;
    // otherwise pass a null view and nHitsTot = 0.
    caStructures::CAHitsView hitsView{};
    int nHitsTot = 0;
    // OT-rechit view for resolving tagged OT extras (empty view + 0 when not needed).
    ::reco::OTRecHitsConstView otHitsView{};
    uint32_t nOTHits = 0;
    if (useHitFeatures_) {
      const auto& pixHits = iEvent.get(pixelHitsToken_);
      const auto& vectorHits = iEvent.get(vectorHitsToken_);
      hitsView = caStructures::CAHitsView(pixHits.const_view().trackingHits(),
                                          pixHits.const_view().hitModules(),
                                          vectorHits.const_view().vectorHits(),
                                          vectorHits.const_view().vectorHitModules(),
                                          pixHits.nHits(),
                                          vectorHits.nVectorHits(),
                                          pixHits.nModules());
      nHitsTot = hitsView.size();
      const auto& otHits = iEvent.get(otRecHitsSoAToken_);
      otHitsView = otHits.const_view().otRecHits();
      nOTHits = otHitsView.metadata().size();
    }

    launchFeaturesExtractor(queue,
                            maxPreselectedTracks_,
                            tracks.tracks(),
                            tracks.trackHits(),
                            hitsView,
                            nHitsTot,
                            otHitsView,
                            nOTHits,
                            alpaka::getPtrNative(d_preselectedTrackIndices),
                            alpaka::getPtrNative(d_nPreselectedTracks),
                            trackFeatures.view().fit(),
                            trackFeatures.view().hit(),
                            alpaka::getPtrNative(d_trackHitCounts));

    // 3. DNN inference
    //  Prepare TensorCollection inputs and outputs for the model
    // a Metarecords keeps a reference to its view, so the views live in named locals
    auto fit_view = trackFeatures.view().fit();
    auto hit_view = trackFeatures.view().hit();
    auto fit_record = fit_view.records();
    auto hit_record = hit_view.records();
    auto score_record = trackScoresOnDevice.view().records();

    auto inputs = cms::torch::alpakatools::BatchedTensorCollection<Queue>();
    auto outputs = cms::torch::alpakatools::BatchedTensorCollection<Queue>();

    // Order must match the TorchScript model input schema
    addTrackFeatures(inputs, batchSize_, fit_record, hit_record, useHitFeatures_);

    outputs.addBatched<PixelTrackScoresSoA>("track_scores", batchSize_, score_record.score());

    model_.forward(queue, inputs, outputs, inferenceDtype());

    // 4. Score-based filtering, with the working-point table and the high-purity marking when configured
    ScoreWorkingPoints wp;
    if (wpDevice_) {
      const float* base = wpDevice_->data();
      wp.nPtEdges = wpPtEdges_.size();
      wp.nAbsEtaEdges = wpAbsEtaEdges_.size();
      wp.ptEdges = base;
      wp.absEtaEdges = base + wp.nPtEdges;
      wp.thresholds = wp.absEtaEdges + wp.nAbsEtaEdges;
      const float* next = wp.thresholds + wpThresholds_.size();
      if (!wpThresholdsThreeHit_.empty()) {
        wp.thresholdsThreeHit = next;
        next += wpThresholdsThreeHit_.size();
      }
      if (!highPurityThresholds_.empty()) {
        wp.highPurityThresholds = next;
        next += highPurityThresholds_.size();
      }
      if (!highPurityThresholdsThreeHit_.empty())
        wp.highPurityThresholdsThreeHit = next;
    }
    wp.highPurityThreshold = highPurityThreshold_;
    wp.highPurityMinNumberOfHits = highPurityMinNumberOfHits_;
    std::optional<cms::alpakatools::device_buffer<Device, int[]>> d_selectedHighPurity;
    if (highPurityThreshold_ >= 0.)
      d_selectedHighPurity.emplace(cms::alpakatools::make_device_buffer<int[]>(queue, maxPreselectedTracks_));
    int* selectedHighPurity = d_selectedHighPurity ? d_selectedHighPurity->data() : nullptr;

    launchScoreFilter(queue,
                      maxPreselectedTracks_,
                      scoreThreshold_,
                      scoreThresholdLowDxy_,
                      dxyRampKnee_,
                      passThreeHitTracks_,
                      trackFeatures.const_view().fit(),
                      trackScoresOnDevice.view(),
                      alpaka::getPtrNative(d_preselectedTrackIndices),
                      alpaka::getPtrNative(d_nPreselectedTracks),
                      alpaka::getPtrNative(d_trackHitCounts),
                      alpaka::getPtrNative(d_selectedTrackIndices),
                      alpaka::getPtrNative(d_nSelectedTracks),
                      alpaka::getPtrNative(d_selectedTrackHitOffsets),
                      wp,
                      selectedHighPurity);

    auto tracks_out = launchProduceOutputTracks(queue,
                                                maxPreselectedTracks_,
                                                avgHitsPerTrack_,
                                                tracks.tracks(),
                                                tracks.trackHits(),
                                                alpaka::getPtrNative(d_selectedTrackIndices),
                                                alpaka::getPtrNative(d_nSelectedTracks),
                                                alpaka::getPtrNative(d_selectedTrackHitOffsets),
                                                nullptr,
                                                selectedHighPurity);
    iEvent.emplace(tokenTrackOut_, std::move(tracks_out));
    // the selector runs on its own queue: the input tracks are read by the kernels above until this queue completes
    if (waitBeforeReturn_)
      alpaka::wait(queue);
  }

  void PixelTrackTorchHighPuritySelector::fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
    edm::ParameterSetDescription desc;
    desc.add<edm::InputTag>("pixelTrackSrc", {"hltPhase2PixelTracksSoA"});
    desc.add<int>("maxNumberOfTracks", 2 * 60 * 1024);
    desc.add<int>("maxPreselectedTracks", 9984);
    desc.add<int>("minNumberOfHits", 0);
    desc.add<int>("avgHitsPerTrack", 8);
    desc.add<std::string>("minimumTrackQuality", "tight");
    desc.add<edm::FileInPath>("model");
    desc.add<double>("scoreThreshold", 0.4);
    desc.add<bool>("passThreeHitTracks", false)->setComment("3-hit tracks are kept whatever their score");
    desc.add<int>("batchSize", 4992);
    // dxy-aware threshold ramp: the cut goes from scoreThresholdLowDxy at |dxyBS| = 0 to
    // scoreThreshold at |dxyBS| >= dxyRampKnee [cm]. scoreThresholdLowDxy < 0 disables it.
    desc.add<double>("scoreThresholdLowDxy", -1.0);
    desc.add<double>("dxyRampKnee", 2.0);
    // Hit/vector hit features (enriched model); the hit features read the pixel rechits + vector hits the CA indexed,
    // consumed only when useHitFeatures is true.
    desc.add<bool>("useHitFeatures", false);
    desc.add<edm::InputTag>("pixelRecHitSrc", {"siPixelRecHitsPreSplittingAlpaka"});
    desc.add<edm::InputTag>("vectorHitsSrc", {"phase2OTVectorHitsSoA"});
    // Raw OT-rechit SoA, consumed only when useHitFeatures is true: resolves the bit30-tagged raw-OT hit
    // ids so OT-extended tracks are scored on their full hit content.
    desc.add<edm::InputTag>("otRecHitsSoASrc", {"caOTRecHitsSoA"});
    // inferenceHalf = false keeps the model in fp32, required for a tree model whose int index buffers
    // must not be cast to half; true selects the fp16 path used by the MLP models.
    desc.add<bool>("inferenceHalf", true);
    // Working points per (pT, |eta|) cell: the bin of a value is the number of edges at or below it; the cut block
    // is row-major in pT. wpThresholdsThreeHit (same layout) holds the cuts of three-hit tracks. An empty
    // wpThresholds keeps the single scoreThreshold.
    desc.add<std::vector<double>>("wpPtEdges", {});
    desc.add<std::vector<double>>("wpAbsEtaEdges", {});
    desc.add<std::vector<double>>("wpThresholds", {});
    desc.add<std::vector<double>>("wpThresholdsThreeHit", {});
    // highPurityThreshold >= 0: the selected tracks with at least highPurityMinNumberOfHits hits and a score at or
    // above it get quality highPurity, the other selected tracks at most tight; a negative value leaves the CA
    // qualities as they are.
    desc.add<double>("highPurityThreshold", -1.0);
    desc.add<int>("highPurityMinNumberOfHits", 4);
    // High-purity cuts per (pT, |eta|) cell, in the layout of wpThresholds (need wpThresholds and highPurityThreshold
    // >= 0): highPurityThresholds replaces highPurityThreshold for the tracks with at least highPurityMinNumberOfHits
    // hits; highPurityThresholdsThreeHit puts the selected three-hit tracks at or above it in the subset. Empty: unused.
    desc.add<std::vector<double>>("highPurityThresholds", {});
    desc.add<std::vector<double>>("highPurityThresholdsThreeHit", {});
    desc.add<bool>("waitBeforeReturn", false)
        ->setComment(
            "Wait for the selector's device work before produce returns, so that the input tracks can be listed in "
            "process.options.canDeleteEarly (the selector uses its own queue, not the producer's)");
    descriptions.addWithDefaultLabel(desc);
  }
}  // namespace ALPAKA_ACCELERATOR_NAMESPACE

#include "HeterogeneousCore/AlpakaCore/interface/alpaka/MakerMacros.h"
DEFINE_FWK_ALPAKA_MODULE(PixelTrackTorchHighPuritySelector);
