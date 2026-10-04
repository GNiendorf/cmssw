// MkFitAlpakaHpFeaturesToDevice (round 7, lane hpsel): stage C, device high-purity selection, step 1.
// The initial step's HP Torch classifier runs as alpaka_serial_sync::TrackTorchClassifierAlpaka in the release menu
// (4.2-4.3 ms host time per input event on the GPU menu), because its input, the feature SoA of
// alpaka_serial_sync::TrackFeatureExtractor, is a HOST product (PortableHostCollection), which only the serial
// backend's classifier can consume. This module copies that host feature SoA to the device (one async memcpy of
// 15 float columns), so the SAME classifier module (TrackTorchClassifierAlpaka@alpaka, same model.pt, same
// PyTorchAlpaka inference path) runs on the GPU. The scores come back through the framework's automatic
// device->host copy, so TrackTorchClassifierFromSoA and everything downstream are unchanged.
// - global::EDProducer: produce() only enqueues (no alpaka::wait).
// - Bitwise copy: the device classifier sees exactly the host features; any score difference is the inference
//   backend's floating point (libtorch CUDA vs CPU), measured by MkFitAlpakaHpCompare.
// - On CPU backends the "device" collection is a host collection: a 15 x 4 B per track copy (negligible).

#include <alpaka/alpaka.hpp>

#include "DataFormats/Portable/interface/PortableHostCollection.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include "FWCore/Utilities/interface/InputTag.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/EDPutToken.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/Event.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/EventSetup.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/MakerMacros.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/global/EDProducer.h"
#include "HeterogeneousCore/AlpakaInterface/interface/config.h"
#include "RecoTracker/FinalTrackSelectors/interface/TrackTorchClassifierFeaturesSoA.h"
#include "RecoTracker/FinalTrackSelectors/interface/alpaka/TrackFeaturesDeviceCollection.h"

namespace ALPAKA_ACCELERATOR_NAMESPACE {

  class MkFitAlpakaHpFeaturesToDevice : public global::EDProducer<> {
  public:
    using HostFeatures = PortableHostCollection<TrackTorchClassifierFeaturesSoA>;

    explicit MkFitAlpakaHpFeaturesToDevice(edm::ParameterSet const& iConfig)
        : EDProducer<>(iConfig),
          srcToken_{consumes(iConfig.getParameter<edm::InputTag>("src"))},
          putToken_{produces()} {}

    static void fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
      edm::ParameterSetDescription desc;
      desc.add<edm::InputTag>("src", edm::InputTag("hltInitialStepTrackFeatureExtractor"));
      descriptions.addWithDefaultLabel(desc);
    }

    void produce(edm::StreamID, device::Event& iEvent, device::EventSetup const&) const override {
      auto const& host = iEvent.get(srcToken_);
      int32_t const n = host.const_view().metadata().size();
      TrackFeaturesDeviceCollection dev(iEvent.queue(), n);
      if (n > 0)
        alpaka::memcpy(iEvent.queue(), dev.buffer(), host.const_buffer());
      iEvent.emplace(putToken_, std::move(dev));
    }

  private:
    const edm::EDGetTokenT<HostFeatures> srcToken_;
    const device::EDPutToken<TrackFeaturesDeviceCollection> putToken_;
  };

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE

DEFINE_FWK_ALPAKA_MODULE(MkFitAlpakaHpFeaturesToDevice);
