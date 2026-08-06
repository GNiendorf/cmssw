#include <alpaka/alpaka.hpp>

#include "RecoTracker/LSTCore/interface/alpaka/LST.h"
#include "RecoTracker/LSTGeometry/interface/Common.h"

#include "FWCore/MessageLogger/interface/MessageLogger.h"
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

  // Translate the grouped chainTracking PSets into the flat frozen-config struct the kernels take
  // by value. Anything absent from the parameter surface stays at its ChainConfig.h default, which
  // is the FREEZE_RECORD value, so a configuration that names nothing reproduces the freeze.
  inline lst::ChainConfig makeChainConfig(edm::ParameterSet const& ps) {
    lst::ChainConfig c;
    auto const& g = ps.getParameter<edm::ParameterSet>("gate");
    c.thetaEdge = g.getParameter<double>("thetaEdge");
    c.lambdaLen = g.getParameter<double>("lambdaLen");
    c.terminalTrim = g.getParameter<bool>("terminalTrim");
    c.trimFactor = g.getParameter<double>("trimFactor");
    c.trimAbsChi2 = g.getParameter<double>("trimAbsChi2");
    c.dcaSplit = g.getParameter<double>("dcaSplit");
    auto const m4 = g.getParameter<std::vector<double>>("marginT4");
    c.m3Theta4 = m4.at(0);
    c.m3Theta4D = m4.at(1);
    auto const rc = g.getParameter<std::vector<double>>("rescue");
    c.m3ThetaRI = rc.at(0);
    c.m3ThetaR = rc.at(1);
    auto const rb = g.getParameter<std::vector<double>>("rescueBand");
    c.m3ThetaRB = rb.at(0);
    c.m3ThetaRT = rb.at(1);
    auto const te = g.getParameter<std::vector<double>>("thetaExempt");
    c.thetaExempt4 = te.at(0);
    c.thetaExempt5 = te.at(1);
    c.thetaExempt6 = te.at(2);
    auto const c25 = g.getParameter<std::vector<double>>("cell25");
    c.c25Theta = c25.at(0);
    c.c25ThetaD = c25.at(1);
    auto const eb = g.getParameter<std::vector<double>>("etaBand");
    c.zEta1 = eb.at(0);
    c.zEta2 = eb.at(1);
    auto const ed = g.getParameter<std::vector<double>>("etaBandDelta");
    c.zdM4 = ed.at(0);
    c.zdM4D = ed.at(1);

    auto const& k = ps.getParameter<edm::ParameterSet>("claim");
    c.orderAlpha = k.getParameter<double>("orderAlpha");
    c.orderHinge = k.getParameter<double>("orderHinge");
    c.maxClaimedFrac = k.getParameter<double>("maxClaimedFrac");
    c.maxClaimedMDs = k.getParameter<int32_t>("maxClaimedMDs");
    c.braidFrac = k.getParameter<double>("braidFrac");
    c.braidFracAlt = k.getParameter<double>("braidFracAlt");
    c.braidAltEta = k.getParameter<double>("braidAltEta");
    c.preClaim = k.getParameter<bool>("preClaim");

    auto const& a = ps.getParameter<edm::ParameterSet>("attach");
    auto const th = a.getParameter<std::vector<double>>("theta");
    c.attachTheta = th.at(0);
    c.attachThetaT = th.at(1);
    c.attachThetaE = th.at(2);
    c.attachThetaT3 = a.getParameter<double>("thetaT3");
    c.rpsThetaChain = a.getParameter<double>("rpsThetaChain");
    c.t3FakeMax = a.getParameter<double>("t3FakeMax");
    c.ccMinShared = a.getParameter<int32_t>("ccMinShared");
    auto const cs = a.getParameter<std::vector<double>>("ccsTheta");
    c.ccsTheta = cs.at(0);
    c.ccsThetaT = cs.at(1);
    c.ccsThetaE = cs.at(2);
    auto const xt = a.getParameter<std::vector<double>>("xcTheta");
    c.xcTheta = xt.at(0);
    c.xcThetaT = xt.at(1);
    c.xcThetaE = xt.at(2);
    c.xcDR2Pix = a.getParameter<double>("xcDR2Pix");
    c.xcDR2Chain = a.getParameter<double>("xcDR2Chain");
    c.replacePT5 = a.getParameter<bool>("replacePT5");
    c.replacePT3 = a.getParameter<bool>("replacePT3");
    c.attachSuppressBarePLS = a.getParameter<bool>("suppressBarePLS");
    c.attachSeedDedup = a.getParameter<bool>("seedDedup");
    c.attachDcaMax = a.getParameter<double>("dcaMax");
    return c;
  }

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
          reduceMemByFullPrecompute_(config.getParameter<bool>("reduceMemByFullPrecompute")),
          chainConfig_(makeChainConfig(config.getParameter<edm::ParameterSet>("chainTracking"))),
          lstInputToken_{consumes(config.getParameter<edm::InputTag>("lstInput"))},
          lstESToken_{esConsumes(edm::ESInputTag("", ptCutStr_))},
          lstOutputToken_{produces()} {}

    void produce(edm::StreamID sid, device::Event& iEvent, const device::EventSetup& iSetup) const override {
      lst::LST lst;
      // Inputs
      auto const& lstInputDC = iEvent.get(lstInputToken_);
      auto const& lstESDeviceData = iSetup.getData(lstESToken_);

      lst.run(iEvent.queue(),
              verbose_,
              ptCut_,
              clustSizeCut_,
              &lstESDeviceData,
              &lstInputDC,
              nopLSDupClean_,
              tcpLSTriplets_,
              reduceMemByFullPrecompute_,
              chainConfig_);

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
      desc.add<bool>("reduceMemByFullPrecompute", false)
          ->setComment(
              "If true, run extra counting kernels that exactly size the MD/LS/T3/T5/T4 "
              "buffers, reducing average per-event memory at a small CPU/GPU runtime cost. "
              "If false (default), buffers use cheaper, looser occupancy estimates.");

      // Grouped chain-tracking configuration. Every value is the M19 FREEZE_RECORD default (see
      // RecoTracker/LSTCore/interface/ChainConfig.h, which documents the prototype flag each one
      // corresponds to). The grouping is deliberate: the prototype exposes ~40 command-line flags,
      // most of which the M14-M18 scans measured as no-ops, bad trades or dead experiments, and
      // those are compile-time constants here rather than producer parameters.
      edm::ParameterSetDescription chainDesc;
      {
        edm::ParameterSetDescription gateDesc;
        gateDesc.add<double>("thetaEdge", 0.0)->setComment("-e: edge-logit floor for K6 welding.");
        gateDesc.add<double>("lambdaLen", 3.0)->setComment("-L: chain-score length weight.");
        gateDesc.add<bool>("terminalTrim", true)->setComment("-TR: K6f terminal trim on.");
        gateDesc.add<double>("trimFactor", 1.2)->setComment("-TT: chi2/hit improvement factor a trim must beat.");
        gateDesc.add<double>("trimAbsChi2", 1.0)->setComment("-TA: absolute chi2/hit floor for trim eligibility.");
        gateDesc.add<double>("dcaSplit", 0.5)->setComment("-X: IP-compatibility boundary on the chain dcaXY, cm.");
        gateDesc.add<std::vector<double>>("marginT4", {4.0, -1.2})
            ->setComment("-M4 / -M4D: T4-class kills on the 3-class mX and mD margins.");
        gateDesc.add<std::vector<double>>("rescue", {-0.5, -1.8})
            ->setComment("-MRI / -MR: mX OR-rescue floors for the IP and exempt 5+ branches.");
        gateDesc.add<std::vector<double>>("rescueBand", {-1.8, -1.8})
            ->setComment(
                "-MRB / -MRT: barrel / transition band split of the exempt-5+ -MR floor. Both sit at "
                "the global -MR: the tightened -1.2 bars spent ~45 of the 77 displaced sims the "
                "barrel-dup round cost, and bought fake rate only.");
        gateDesc.add<std::vector<double>>("thetaExempt", {0.0, 0.0, 0.0})
            ->setComment("-U4 / -U5 / -U6: per-length acceptance thresholds of the exempt branch.");
        gateDesc.add<std::vector<double>>("cell25", {0.0, -2.0})
            ->setComment("-C25 / -C25D: the (nNodes == 2, nLayers == 5) cell rule.");
        gateDesc.add<std::vector<double>>("etaBand", {1.1, 1.7})->setComment("-ZE1 / -ZE2: transition |eta| band.");
        gateDesc.add<std::vector<double>>("etaBandDelta", {-0.5, 1.2})
            ->setComment("-ZM4 / -ZM4D: in-band additive deltas to marginT4.");
        chainDesc.add<edm::ParameterSetDescription>("gate", gateDesc);

        edm::ParameterSetDescription claimDesc;
        claimDesc.add<double>("orderAlpha", 10.0)->setComment("-B: weight of the mX hinge in the K9 order key.");
        claimDesc.add<double>("orderHinge", 5.0)->setComment("-BT: hinge point of the same key.");
        claimDesc.add<double>("maxClaimedFrac", 0.20)->setComment("-F: candidate-relative claim tolerance.");
        claimDesc.add<int32_t>("maxClaimedMDs", 1)->setComment("-FC: absolute claim tolerance, MiniDoublet units.");
        claimDesc.add<double>("braidFrac", 0.50)->setComment("-W: owner-relative braid kill fraction.");
        claimDesc.add<double>("braidFracAlt", 0.20)->setComment("-WE: the same fraction inside the |eta| band.");
        claimDesc.add<double>("braidAltEta", 1.5)->setComment("-WZ: |eta| where the band braid takes over.");
        claimDesc.add<bool>("preClaim", true)
            ->setComment("-PU: pre-claim the surviving carried pixel rows' outer-tracker hits.");
        chainDesc.add<edm::ParameterSetDescription>("claim", claimDesc);

        edm::ParameterSetDescription attachDesc;
        attachDesc.add<std::vector<double>>("theta", {6.0, 6.0, 6.0})
            ->setComment(
                "-a / -a2 / -a3: pair-head delivery margins, banded on |seed eta| at 1.1 / 1.7. All "
                "three at 6.0: the lowered barrel/transition margins spent ~34 displaced sims on "
                "wrong-seed conversions and are unwound.");
        attachDesc.add<double>("thetaT3", 6.0)->setComment("-AT3: the bare-T3 (stage B) delivery margin, global.");
        attachDesc.add<double>("rpsThetaChain", 5.5)
            ->setComment("-RPSA: chain-side seed-retirement bar (global; the T3 bar is thetaT3).");
        attachDesc.add<double>("t3FakeMax", 0.10)
            ->setComment("-T3F: stage-B target admission on the T3 fake score (NaN rejected).");
        attachDesc.add<int32_t>("ccMinShared", 1)
            ->setComment("-CCN: revoke a stage-B delivery when >= this many of its 3 MDs are already claimed.");
        attachDesc.add<std::vector<double>>("ccsTheta", {1e9, 1e9, 1e9})
            ->setComment(
                "-CCS / -CCS2 / -CCS3: chain-loser suppression bars per TC-|eta| band; >= 1e8 = band "
                "OFF. All OFF: unwound with the -a lowering it was tuned alongside.");
        attachDesc.add<std::vector<double>>("xcTheta", {3.75, 3.5, 3.75})
            ->setComment("-XCT / -XCT2 / -XCT3: seed-crossclean bare-chain-arm logit bars per |seed eta| band.");
        attachDesc.add<double>("xcDR2Pix", 1e-6)->setComment("-XCR2: pixel-anchored crossclean dR^2 window.");
        attachDesc.add<double>("xcDR2Chain", 0.02)->setComment("-XCW2: bare-chain crossclean dR^2 window.");
        attachDesc.add<bool>("replacePT5", true)->setComment("-RT5: retire every carried pT5 row.");
        attachDesc.add<bool>("replacePT3", true)->setComment("-RT3: retire every carried pT3 row.");
        attachDesc.add<bool>("suppressBarePLS", true)
            ->setComment("-RPS: retire a carried bare-pLS row whose seed lost an above-margin contention.");
        attachDesc.add<bool>("seedDedup", true)
            ->setComment("-RD: seed-family dedup of the attach owners (>= 2 shared pixel hits).");
        attachDesc.add<double>("dcaMax", 1e9)
            ->setComment("-D4: attach-eligibility dcaXY gate, cm. 1e9 = off, which is the freeze.");
        chainDesc.add<edm::ParameterSetDescription>("attach", attachDesc);
      }
      desc.add<edm::ParameterSetDescription>("chainTracking", chainDesc)
          ->setComment("Chain-tracking configuration (the only track-building path).");

      descriptions.addWithDefaultLabel(desc);
    }

  private:
    const bool verbose_;
    const double ptCut_;
    const std::string ptCutStr_;
    const uint16_t clustSizeCut_;
    const bool nopLSDupClean_;
    const bool tcpLSTriplets_;
    const bool reduceMemByFullPrecompute_;
    const lst::ChainConfig chainConfig_;
    const device::EDGetToken<lst::LSTInputDeviceCollection> lstInputToken_;
    const device::ESGetToken<lst::LSTESData<Device>, TrackerRecoGeometryRecord> lstESToken_;
    const device::EDPutToken<lst::TrackCandidatesBaseDeviceCollection> lstOutputToken_;
  };

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE

#include "HeterogeneousCore/AlpakaCore/interface/alpaka/MakerMacros.h"
DEFINE_FWK_ALPAKA_MODULE(LSTProducer);
