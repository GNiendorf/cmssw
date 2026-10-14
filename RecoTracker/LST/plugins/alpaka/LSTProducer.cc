#include <alpaka/alpaka.hpp>

#include "RecoTracker/LSTCore/interface/alpaka/LST.h"
#include "RecoTracker/LSTCore/interface/OTPull.h"
#include "RecoTracker/LSTCore/interface/alpaka/OTPullCellsDeviceCollection.h"

#include <vector>
#include "RecoTracker/LSTGeometry/interface/Common.h"

#include "FWCore/MessageLogger/interface/MessageLogger.h"
#include "FWCore/Utilities/interface/Exception.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include "FWCore/Utilities/interface/InputTag.h"

#include "HeterogeneousCore/AlpakaCore/interface/alpaka/EDGetToken.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/EDPutToken.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/Event.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/EventSetup.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/global/EDProducer.h"
#include "HeterogeneousCore/AlpakaInterface/interface/config.h"

#include "RecoTracker/LSTCore/interface/alpaka/TrackCandidatesDeviceCollection.h"

#include "RecoTracker/Record/interface/TrackerRecoGeometryRecord.h"

namespace ALPAKA_ACCELERATOR_NAMESPACE {

  class LSTProducer : public global::EDProducer<> {
  public:
    LSTProducer(edm::ParameterSet const& config)
        : EDProducer(config),
          verbose_(config.getParameter<bool>("verbose")),
          ptCut_(config.getParameter<double>("ptCut")),
          ptCutStr_(lst::floatToStr(ptCut_, 1)),
          clustSizeCut_(static_cast<uint16_t>(config.getParameter<uint32_t>("clustSizeCut"))),
          nopLSDupClean_(config.getParameter<bool>("nopLSDupClean")),
          tcpLSTriplets_(config.getParameter<bool>("tcpLSTriplets")),
          lstInputToken_{consumes(config.getParameter<edm::InputTag>("lstInput"))},
          lstESToken_{esConsumes(edm::ESInputTag("", ptCutStr_))},
          lstOutputToken_{produces()},
          otpullOn_(!config.getParameter<edm::InputTag>("otpullCells").label().empty()) {
      if (otpullOn_) {
        otpullToken_ = consumes(config.getParameter<edm::InputTag>("otpullCells"));
        auto const sig = config.getParameter<std::vector<double>>("otpullSigma");
        auto const edges = config.getParameter<std::vector<double>>("otpullPtEdges");
        if (sig.size() != lst::OTPullParams::kNVars * lst::OTPullParams::kNPtBins ||
            edges.size() != lst::OTPullParams::kNPtBins - 1)
          throw cms::Exception("Configuration") << "otpullSigma / otpullPtEdges size";
        for (int v = 0; v < lst::OTPullParams::kNVars; ++v)
          for (int b = 0; b < lst::OTPullParams::kNPtBins; ++b)
            otpullParams_.sigma[v][b] = sig[v * lst::OTPullParams::kNPtBins + b];
        for (int b = 0; b < lst::OTPullParams::kNPtBins - 1; ++b)
          otpullParams_.ptEdges[b] = edges[b];
        otpullParams_.chi2Cut = config.getParameter<double>("otpullChi2Cut");
        otpullParams_.phiWindow = config.getParameter<double>("otpullPhiWindow");
        otpullParams_.nPhiBins = config.getParameter<int>("otpullNPhiBins");
        otpullParams_.nShells = config.getParameter<int>("otpullNShells");
        otpullParams_.shellWidth = config.getParameter<double>("otpullShellWidth");
        otpullParams_.plsMatch = config.getParameter<int>("otpullPLSMatch");
        otpullParams_.capacity = config.getParameter<uint32_t>("otpullCapacity");
      }
    }

    void produce(edm::StreamID sid, device::Event& iEvent, const device::EventSetup& iSetup) const override {
      lst::LST lst;
      // Inputs
      auto const& lstInputDC = iEvent.get(lstInputToken_);
      auto const& lstESDeviceData = iSetup.getData(lstESToken_);

      // OT-seeded cell lookup: CA terminal cells (device product)
      lst::OTPullCellsDeviceCollection const* otpullCells = otpullOn_ ? &iEvent.get(otpullToken_) : nullptr;

      lst.run(iEvent.queue(),
              verbose_,
              ptCut_,
              clustSizeCut_,
              &lstESDeviceData,
              &lstInputDC,
              nopLSDupClean_,
              tcpLSTriplets_,
              otpullOn_ ? &otpullParams_ : nullptr,
              otpullCells);

      // Output
      auto lstTrackCandidates = lst.getTrackCandidates();
      iEvent.emplace(lstOutputToken_, std::move(*lstTrackCandidates.release()));
    }

    static void fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
      edm::ParameterSetDescription desc;
      desc.add<edm::InputTag>("lstInput", edm::InputTag{"lstInputProducer"});
      desc.add<bool>("verbose", false);
      desc.add<double>("ptCut", 0.8);
      desc.add<uint32_t>("clustSizeCut", 16);
      desc.add<bool>("nopLSDupClean", false);
      desc.add<bool>("tcpLSTriplets", false);
      desc.add<edm::InputTag>("otpullCells", edm::InputTag(""))
          ->setComment("CA terminal cells for the OT-seeded lookup (6 floats per cell); empty = off");
      desc.add<double>("otpullChi2Cut", 0.3);
      desc.add<std::vector<double>>("otpullSigma", std::vector<double>(25, 1.0))
          ->setComment("resolution per variable (outer rphi, outer rz, direction, rz slope, joint rms) x T3 pT bin");
      desc.add<std::vector<double>>("otpullPtEdges", {1.5, 3., 6., 15.});
      desc.add<double>("otpullPhiWindow", 0.15);
      desc.add<int>("otpullNPhiBins", 512);
      desc.add<int>("otpullNShells", 15)->setComment("radial shells of the cell outer hit (lookup only, no physics)");
      desc.add<double>("otpullShellWidth", 2.0);
      desc.add<int>("otpullPLSMatch", 0)
          ->setComment(
              "cells sharing a pixel hit with a pLS are not offered: 0 none, 1 the pLS last hit, 2 all stored pLS "
              "hits");
      desc.add<uint32_t>("otpullCapacity", 1024);
      descriptions.addWithDefaultLabel(desc);
    }

  private:
    const bool verbose_;
    const double ptCut_;
    const std::string ptCutStr_;
    const uint16_t clustSizeCut_;
    const bool nopLSDupClean_;
    const bool tcpLSTriplets_;
    const device::EDGetToken<lst::LSTInputDeviceCollection> lstInputToken_;
    const device::ESGetToken<lst::LSTESData<Device>, TrackerRecoGeometryRecord> lstESToken_;
    const device::EDPutToken<lst::TrackCandidatesBaseDeviceCollection> lstOutputToken_;
    const bool otpullOn_;
    device::EDGetToken<lst::OTPullCellsDeviceCollection> otpullToken_;
    lst::OTPullParams otpullParams_{};
  };

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE

#include "HeterogeneousCore/AlpakaCore/interface/alpaka/MakerMacros.h"
DEFINE_FWK_ALPAKA_MODULE(LSTProducer);
