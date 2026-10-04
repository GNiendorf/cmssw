// Stage C (lane stagec): MkFitAlpakaOutConvStateProducer - the device part of the output conversion. Reads the device
// fit's TrackSoA (its state is the fitted state at the first hit, the one the converter attaches to the first hit's
// surface) and writes per row the state at the point of closest approach to the beam line (OutConvSoA: the device
// TSCBLBuilderNoMaterial, interface/math/PcaToBeamLine.h). MkFitAlpakaOutputTrackConverter reads the host copy
// (pcaStates) instead of calling TSCBLBuilderNoMaterial. No synchronisation: the output has the TrackSoA's capacity.
// Beam spot: the same product as the host converter (offlineBeamSpot, R7-L6), passed to the kernel by value.

#include <atomic>
#include <cmath>

#include <alpaka/alpaka.hpp>

#include "DataFormats/BeamSpot/interface/BeamSpot.h"
#include "FWCore/MessageLogger/interface/MessageLogger.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include "FWCore/Utilities/interface/InputTag.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/EDGetToken.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/EDPutToken.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/Event.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/EventSetup.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/MakerMacros.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/global/EDProducer.h"
#include "HeterogeneousCore/AlpakaInterface/interface/config.h"
#include "RecoTracker/MkFitAlpaka/interface/alpaka/OutConvProduct.h"
#include "RecoTracker/MkFitAlpaka/interface/alpaka/tracks/TrackSoADeviceCollection.h"

#include "MkFitAlpakaOutConvKernels.h"

namespace ALPAKA_ACCELERATOR_NAMESPACE {

  class MkFitAlpakaOutConvStateProducer : public global::EDProducer<> {
  public:
    explicit MkFitAlpakaOutConvStateProducer(edm::ParameterSet const& iConfig)
        : EDProducer<>(iConfig),
          tracksToken_{consumes(iConfig.getParameter<edm::InputTag>("tracks"))},
          bsToken_{consumes(iConfig.getParameter<edm::InputTag>("beamSpot"))},
          putToken_{produces()} {
      if (auto const cmp = iConfig.getUntrackedParameter<edm::InputTag>("compareBeamSpot"); !cmp.label().empty())
        bsCmpToken_ = consumes(cmp);
    }

    ~MkFitAlpakaOutConvStateProducer() override {
      if (!bsCmpToken_.isUninitialized())
        edm::LogPrint("MkFitAlpakaOutConvStateProducer")
            << "[outconv BSCMP] events " << bsEvents_ << " with a different beam line (x, y, z, dxdz, dydz) "
            << bsDiffer_;
    }

    static void fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
      edm::ParameterSetDescription desc;
      desc.add<edm::InputTag>("tracks", edm::InputTag("hltInitialStepTrackCandidatesMkFitFitDevice"))
          ->setComment("device TrackSoA of the device final fit (MkFitAlpakaFitDeviceProducer)");
      desc.add<edm::InputTag>("beamSpot", edm::InputTag("offlineBeamSpot"))
          ->setComment("must be the beam spot of the output converter (it uses offlineBeamSpot)");
      desc.addUntracked<edm::InputTag>("compareBeamSpot", edm::InputTag(""))
          ->setComment("study (R7-L6): count events where this beam spot's beam line differs from beamSpot's");
      descriptions.addWithDefaultLabel(desc);
    }

    void produce(edm::StreamID, device::Event& iEvent, device::EventSetup const&) const override {
      auto& queue = iEvent.queue();
      auto const& trk = iEvent.get(tracksToken_);
      auto const& bs = iEvent.get(bsToken_);
      if (!bsCmpToken_.isUninitialized()) {
        auto const& o = iEvent.get(bsCmpToken_);
        const bool differ = o.position().x() != bs.position().x() || o.position().y() != bs.position().y() ||
                            o.position().z() != bs.position().z() || o.dxdz() != bs.dxdz() || o.dydz() != bs.dydz();
        ++bsEvents_;
        if (differ && bsDiffer_++ < 5)
          edm::LogPrint("MkFitAlpakaOutConvStateProducer")
              << "[outconv BSCMP] event " << iEvent.id().event() << " beamSpot (" << bs.position().x() << ", "
              << bs.position().y() << ", " << bs.position().z() << ", " << bs.dxdz() << ", " << bs.dydz()
              << ") vs compareBeamSpot (" << o.position().x() << ", " << o.position().y() << ", " << o.position().z()
              << ", " << o.dxdz() << ", " << o.dydz() << ")";
      }
      const int capacity = trk.const_view().metadata().size();  // R4-M1: 0 for a skipped / seedless event
      mkfitdev::OutConvDeviceCollection out(queue, capacity);
      if (capacity > 0) {
        // TSCBLBuilderNoMaterial: GlobalPoint(beamSpot.position()), GlobalVector(dxdz, dydz, 1.) (float)
        ::mkfitdev::pca::BeamIn bl;
        bl.pos = ::mkfitdev::pca::F3{float(bs.position().x()), float(bs.position().y()), float(bs.position().z())};
        bl.dir = ::mkfitdev::pca::F3{float(bs.dxdz()), float(bs.dydz()), 1.f};
        mkfitdev::outconv::launchOutConvStates(queue, trk.const_view(), capacity, bl, out.view());
      }
      iEvent.emplace(putToken_, std::move(out));
    }

  private:
    const device::EDGetToken<mkfitdev::TrackSoADeviceCollection> tracksToken_;
    const edm::EDGetTokenT<reco::BeamSpot> bsToken_;
    const device::EDPutToken<mkfitdev::OutConvDeviceCollection> putToken_;
    edm::EDGetTokenT<reco::BeamSpot> bsCmpToken_;
    mutable std::atomic<long long> bsEvents_{0}, bsDiffer_{0};
  };

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE

DEFINE_FWK_ALPAKA_MODULE(MkFitAlpakaOutConvStateProducer);
