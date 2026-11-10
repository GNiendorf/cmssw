#include <alpaka/alpaka.hpp>

#include "RecoTracker/LSTCore/interface/alpaka/LST.h"
#include "RecoTracker/LSTCore/interface/OTPull.h"
#include "RecoTracker/LSTCore/interface/PixelObjectOptions.h"
#include "RecoTracker/LSTCore/interface/alpaka/OTPullCellsDeviceCollection.h"

#include <algorithm>
#include <optional>
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
#include "HeterogeneousCore/AlpakaInterface/interface/memory.h"

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
      pixelObjectOptions_.pT5Chi2CutsAllPt = config.getParameter<bool>("pT5Chi2CutsAllPt");
      pixelObjectOptions_.pT5ScoreMode = config.getParameter<int>("pT5ScoreMode");
      pixelObjectOptions_.pT5RPhiInHiScale = config.getParameter<double>("pT5RPhiInHiScale");
      pixelObjectOptions_.pT5WpHigh = config.getParameter<double>("pT5WpHigh");
      pixelObjectOptions_.pT5RPhiOutHiPow = config.getParameter<double>("pT5RPhiOutHiPow");
      pixelObjectOptions_.tripletpLSFillHoles = config.getParameter<bool>("tripletpLSFillHoles");
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
        otpullParams_.attach = config.getParameter<int>("otpullAttach");
        otpullParams_.attachChi2Cut = config.getParameter<double>("otpullAttachChi2Cut");
        otpullParams_.attachPLSVeto = config.getParameter<int>("otpullAttachPLSVeto");
        otpullParams_.attachHiPt = config.getParameter<double>("otpullAttachHiPt");
        otpullParams_.attachChi2CutHi = config.getParameter<double>("otpullAttachChi2CutHi");
        otpullParams_.scoreMode = config.getParameter<int>("otpullScoreMode");
        otpullParams_.scoreWindow = config.getParameter<double>("otpullScoreWindow");
        otpullParams_.scoreJrmsCut = config.getParameter<double>("otpullScoreJrmsCut");
        auto const wpPt = config.getParameter<std::vector<double>>("otpullScoreWPPtEdges");
        auto const wpEta = config.getParameter<std::vector<double>>("otpullScoreWPEtaEdges");
        auto const wp = config.getParameter<std::vector<double>>("otpullScoreWP");
        if (wpPt.size() != lst::OTPullParams::kNWpPt - 1 || wpEta.size() != lst::OTPullParams::kNWpEta - 1 ||
            wp.size() != lst::OTPullParams::kNWpPt * lst::OTPullParams::kNWpEta)
          throw cms::Exception("Configuration") << "otpullScoreWPPtEdges / otpullScoreWPEtaEdges / otpullScoreWP size";
        for (int i = 0; i < lst::OTPullParams::kNWpPt - 1; ++i)
          otpullParams_.wpPtEdges[i] = wpPt[i];
        for (int i = 0; i < lst::OTPullParams::kNWpEta - 1; ++i)
          otpullParams_.wpEtaEdges[i] = wpEta[i];
        for (int i = 0; i < lst::OTPullParams::kNWpPt; ++i)
          for (int j = 0; j < lst::OTPullParams::kNWpEta; ++j)
            otpullParams_.wp[i][j] = wp[i * lst::OTPullParams::kNWpEta + j];
        if (otpullParams_.attach) {
          auto const t = config.getParameter<edm::InputTag>("otpullPixelSrcRows");
          if (t.label().empty())
            throw cms::Exception("Configuration") << "otpullAttach needs otpullPixelSrcRows (pixel key -> CA row)";
          srcRowsToken_ = consumes(t);
        }
      }
    }

    void produce(edm::StreamID sid, device::Event& iEvent, const device::EventSetup& iSetup) const override {
      lst::LST lst;
      // Inputs
      auto const& lstInputDC = iEvent.get(lstInputToken_);
      auto const& lstESDeviceData = iSetup.getData(lstESToken_);

      // OT-seeded cell lookup: CA terminal cells (device product)
      lst::OTPullCellsDeviceCollection const* otpullCells = otpullOn_ ? &iEvent.get(otpullToken_) : nullptr;

      // pixel cluster key of each CA pixel row, from a key -> row list (empty list: no stub this event)
      std::optional<cms::alpakatools::device_buffer<Device, uint32_t[]>> pixKey_d;
      unsigned int nPixKey = 0;
      if (otpullOn_ && otpullParams_.attach) {
        auto const& rows = iEvent.get(srcRowsToken_);
        for (uint32_t const r : rows)
          if (r != lst::kOTPullNoRow)
            nPixKey = std::max<unsigned int>(nPixKey, r + 1);
        if (nPixKey > 0) {
          auto h = cms::alpakatools::make_host_buffer<uint32_t[]>(iEvent.queue(), nPixKey);
          std::fill(h.data(), h.data() + nPixKey, 0xffffffffu);
          for (uint32_t k = 0; k < rows.size(); ++k)
            if (rows[k] != lst::kOTPullNoRow)
              h[rows[k]] = k;
          pixKey_d.emplace(cms::alpakatools::make_device_buffer<uint32_t[]>(iEvent.queue(), nPixKey));
          alpaka::memcpy(iEvent.queue(), *pixKey_d, h);
        }
      }

      lst.run(iEvent.queue(),
              verbose_,
              ptCut_,
              clustSizeCut_,
              &lstESDeviceData,
              &lstInputDC,
              nopLSDupClean_,
              tcpLSTriplets_,
              pixelObjectOptions_,
              otpullOn_ ? &otpullParams_ : nullptr,
              otpullCells,
              pixKey_d ? pixKey_d->data() : nullptr,
              nPixKey);

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
      desc.add<bool>("pT5Chi2CutsAllPt", false)
          ->setComment("pT5 rz and outward rPhi chi2 cuts at every pLS pT, not only below the high-pT threshold");
      desc.add<int>("pT5ScoreMode", 0)
          ->setComment(
              "pT5 ranking score: 0 the rPhi chi2, 1 the log of rz x rPhi x inward rPhi chi2, 2 the log of rz x rPhi "
              "chi2");
      desc.add<double>("pT5RPhiInHiScale", 0.)
          ->setComment(
              "> 0: pT5 inward rPhi cut at every T5 radius, its threshold scaled by this factor above the high-pT "
              "threshold");
      desc.add<double>("pT5WpHigh", -1.)->setComment(">= 0: pT5 DNN working point above the high-pT threshold");
      desc.add<double>("pT5RPhiOutHiPow", 0.)
          ->setComment(
              "pT5 outward rPhi cut for high-pT pLS: 0 unchanged, > 0 threshold scaled by (pT / threshold)^p, < 0 "
              "off");
      desc.add<bool>("tripletpLSFillHoles", false)
          ->setComment(
              "3-hit pLSs become track candidates after all other candidates, only if none of their hits is in a track "
              "candidate (independent of tcpLSTriplets, which promotes them unconditionally)");
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
      desc.add<int>("otpullAttach", 0)
          ->setComment(
              "pixel stub from the cell lookup: bit 0 pixel-less T5 TCs, bit 1 T4 TCs, bit 2 the new OT-seeded TCs "
              "carry "
              "their cell; 0 = off");
      desc.add<double>("otpullAttachChi2Cut", 1.0);
      desc.add<double>("otpullAttachHiPt", 1e9)
          ->setComment("GeV, anchor-circle pT above which otpullAttachChi2CutHi applies");
      desc.add<double>("otpullAttachChi2CutHi", 1.0);
      desc.add<int>("otpullScoreMode", 0)
          ->setComment("T3 lookup decision: 0 chi2 below otpullChi2Cut, 1 the pair network with the working points");
      desc.add<double>("otpullScoreWindow", 3.0)->setComment("sigma, outer-hit residual windows of the pair network");
      desc.add<double>("otpullScoreJrmsCut", 5.0)->setComment("sigma, joint circle rms prefilter of the pair network");
      desc.add<std::vector<double>>("otpullScoreWPPtEdges", {2., 5., 10., 25., 50.})->setComment("GeV, T3 pT");
      desc.add<std::vector<double>>("otpullScoreWPEtaEdges", {0.9, 1.5})->setComment("T3 |eta|");
      desc.add<std::vector<double>>("otpullScoreWP",
                                    std::vector<double>(lst::OTPullParams::kNWpPt * lst::OTPullParams::kNWpEta, 0.5))
          ->setComment("pair-network working points, row-major (pT bins x |eta| bins)");
      desc.add<int>("otpullAttachPLSVeto", 0)
          ->setComment("1: cells sharing a hit with the pLS of a pLS/pT3/pT5 TC are not offered to TCs");
      desc.add<edm::InputTag>("otpullPixelSrcRows", edm::InputTag(""))
          ->setComment(
              "pixel cluster key -> CA pixel row (LSTPixelClusterRowsProducer or the EventOfHits pixelSrcRows), "
              "needed with otpullAttach");
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
    lst::PixelObjectOptions pixelObjectOptions_;
    device::EDGetToken<lst::OTPullCellsDeviceCollection> otpullToken_;
    lst::OTPullParams otpullParams_{};
    edm::EDGetTokenT<std::vector<uint32_t>> srcRowsToken_;
  };

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE

#include "HeterogeneousCore/AlpakaCore/interface/alpaka/MakerMacros.h"
DEFINE_FWK_ALPAKA_MODULE(LSTProducer);
