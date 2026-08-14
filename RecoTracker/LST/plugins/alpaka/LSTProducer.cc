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

  // Translate the grouped chainTracking PSets into the flat config struct the chain kernels take by
  // value. Only the working points worth exposing appear on the parameter surface; every other
  // ChainConfig field keeps its in-code default, so a configuration that names nothing still gets a
  // complete and consistent configuration. See RecoTracker/LSTCore/interface/ChainConfig.h for what
  // each field means and why it has the value it has.
  inline lst::ChainConfig makeChainConfig(edm::ParameterSet const& ps) {
    lst::ChainConfig cfg;
    auto const& gatePs = ps.getParameter<edm::ParameterSet>("gate");
    cfg.thetaEdge = gatePs.getParameter<double>("thetaEdge");
    cfg.lambdaLen = gatePs.getParameter<double>("lambdaLen");
    cfg.terminalTrim = gatePs.getParameter<bool>("terminalTrim");
    cfg.trimFactor = gatePs.getParameter<double>("trimFactor");
    cfg.trimAbsChi2 = gatePs.getParameter<double>("trimAbsChi2");
    cfg.dcaSplit = gatePs.getParameter<double>("dcaSplit");
    auto const marginT4 = gatePs.getParameter<std::vector<double>>("marginT4");
    cfg.m3Theta4 = marginT4.at(0);
    cfg.m3Theta4D = marginT4.at(1);
    auto const rescue = gatePs.getParameter<std::vector<double>>("rescue");
    cfg.m3ThetaRI = rescue.at(0);
    cfg.m3ThetaR = rescue.at(1);
    auto const rescueBand = gatePs.getParameter<std::vector<double>>("rescueBand");
    cfg.m3ThetaRB = rescueBand.at(0);
    cfg.m3ThetaRT = rescueBand.at(1);
    auto const thetaExempt = gatePs.getParameter<std::vector<double>>("thetaExempt");
    cfg.thetaExempt4 = thetaExempt.at(0);
    cfg.thetaExempt5 = thetaExempt.at(1);
    cfg.thetaExempt6 = thetaExempt.at(2);
    auto const cell25 = gatePs.getParameter<std::vector<double>>("cell25");
    cfg.c25Theta = cell25.at(0);
    cfg.c25ThetaD = cell25.at(1);
    auto const etaBand = gatePs.getParameter<std::vector<double>>("etaBand");
    cfg.zEta1 = etaBand.at(0);
    cfg.zEta2 = etaBand.at(1);
    auto const etaBandDelta = gatePs.getParameter<std::vector<double>>("etaBandDelta");
    cfg.zdM4 = etaBandDelta.at(0);
    cfg.zdM4D = etaBandDelta.at(1);

    auto const& claimPs = ps.getParameter<edm::ParameterSet>("claim");
    cfg.orderAlpha = claimPs.getParameter<double>("orderAlpha");
    cfg.orderHinge = claimPs.getParameter<double>("orderHinge");
    cfg.maxClaimedFrac = claimPs.getParameter<double>("maxClaimedFrac");
    cfg.maxClaimedMDs = claimPs.getParameter<int32_t>("maxClaimedMDs");
    cfg.braidFrac = claimPs.getParameter<double>("braidFrac");
    cfg.braidFracAlt = claimPs.getParameter<double>("braidFracAlt");
    cfg.braidAltEta = claimPs.getParameter<double>("braidAltEta");
    cfg.preClaim = claimPs.getParameter<bool>("preClaim");

    auto const& attachPs = ps.getParameter<edm::ParameterSet>("attach");
    auto const attachTheta = attachPs.getParameter<std::vector<double>>("theta");
    cfg.attachTheta = attachTheta.at(0);
    cfg.attachThetaT = attachTheta.at(1);
    cfg.attachThetaE = attachTheta.at(2);
    cfg.attachThetaT3 = attachPs.getParameter<double>("thetaT3");
    cfg.rpsThetaChain = attachPs.getParameter<double>("rpsThetaChain");
    cfg.t3FakeMax = attachPs.getParameter<double>("t3FakeMax");
    cfg.ccMinShared = attachPs.getParameter<int32_t>("ccMinShared");
    auto const xcTheta = attachPs.getParameter<std::vector<double>>("xcTheta");
    cfg.xcTheta = xcTheta.at(0);
    cfg.xcThetaT = xcTheta.at(1);
    cfg.xcThetaE = xcTheta.at(2);
    cfg.cc9MinShared = attachPs.getParameter<int32_t>("cc9MinShared");
    cfg.replacePT5 = attachPs.getParameter<bool>("replacePT5");
    cfg.replacePT3 = attachPs.getParameter<bool>("replacePT3");
    cfg.attachSuppressBarePLS = attachPs.getParameter<bool>("suppressBarePLS");
    cfg.attachSeedDedup = attachPs.getParameter<bool>("seedDedup");
    cfg.attachDcaMax = attachPs.getParameter<double>("dcaMax");

    // A memory bound on the edge enumeration rather than a physics working point, so it sits at the
    // top of the chain PSet rather than inside one of the three stage groups.
    cfg.degreeCap = ps.getParameter<uint32_t>("degreeCap");
    return cfg;
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

      // Chain-tracking configuration, grouped by the stage each working point belongs to: the gate
      // that judges a chain, the claim that arbitrates chains competing for the same hits, and the
      // pixel-seed attach. Only working points a reconstruction configuration has a reason to move
      // are exposed; the rest of ChainConfig stays at its in-code default, which is documented in
      // RecoTracker/LSTCore/interface/ChainConfig.h.
      edm::ParameterSetDescription chainDesc;
      chainDesc.add<uint32_t>("degreeCap", 256)
          ->setComment(
              "Resource guard, not a working point: a shared MiniDoublet / Segment key enumerates at "
              "most this many in- and out-triplets. The edge count is sum_key degIn * degOut, which on "
              "a collimated (jet) event reaches 400 M edges / 8.4 GB and overruns the 4 GiB alpaka "
              "buffer extent; 1000000000 disables the cap and reproduces the unguarded behaviour. "
              "Measured PU200 cost at 256: 0.074% of the enumerated edges pooled over 150 events.");
      {
        edm::ParameterSetDescription gateDesc;
        gateDesc.add<double>("thetaEdge", 0.0)
            ->setComment("Edge-head logit floor for welding: an edge is a weld candidate only at or above this.");
        gateDesc.add<double>("lambdaLen", 3.0)->setComment("Weight on chain length in the chain score.");
        gateDesc.add<bool>("terminalTrim", true)
            ->setComment(
                "Enable the terminal trim, which drops an end node of a chain when doing so yields a "
                "more credible chain.");
        gateDesc.add<double>("trimFactor", 1.2)
            ->setComment("Chi2-per-hit improvement ratio a drop must beat when the chi2 rule takes the decision.");
        gateDesc.add<double>("trimAbsChi2", 1.0)
            ->setComment("A chain whose combined fit already reaches this chi2 per hit is never trimmed.");
        gateDesc.add<double>("dcaSplit", 0.5)
            ->setComment(
                "IP-compatibility boundary on the chain transverse DCA, cm: below it a chain is judged "
                "as prompt, at or above it by the exempt (displaced) branch.");
        gateDesc.add<std::vector<double>>("marginT4", {4.0, -1.2})
            ->setComment(
                "4-layer gate bars, in chain-head logit margin units: [0] the IP branch, on "
                "mX = max(zPrompt, zDisp) - zFake; [1] the exempt branch, on mD = zDisp - zFake. A "
                "chain below its bar is killed.");
        gateDesc.add<std::vector<double>>("rescue", {-0.5, -1.8})
            ->setComment(
                "Rescue floors on mX for the 5+-layer branches: [0] IP, [1] exempt. A 5+ chain is "
                "killed only when both its per-class bar and this floor fail, so a chain that is "
                "unambiguously real on one class survives whatever the other class says.");
        gateDesc.add<std::vector<double>>("rescueBand", {-1.8, -1.8})
            ->setComment(
                "Band replacements of the exempt 5+ rescue floor, keyed on |eta| of the innermost "
                "member triplet: [0] barrel, below etaBand[0]; [1] transition, in [etaBand[0], "
                "etaBand[1]). Raising either buys fake rate at the cost of displaced efficiency, "
                "because the exempt branch is the branch displaced tracks come through.");
        gateDesc.add<std::vector<double>>("thetaExempt", {0.0, 0.0, 0.0})
            ->setComment(
                "Per-length acceptance thresholds the hit claim applies to exempt (large transverse "
                "DCA) chains, indexed [4-layer, 5-layer, 6+-layer]. They live on the summed edge-logit "
                "chain score, so 0 admits every chain the gate left alive.");
        gateDesc.add<std::vector<double>>("cell25", {0.0, -2.0})
            ->setComment(
                "Extra bars for the (2 nodes, 5 layers) cell, applied on top of whichever branch rule "
                "already ran: [0] on mX, [1] on mD. Kills only when both margins fail, and never "
                "re-kills an already-killed chain.");
        gateDesc.add<std::vector<double>>("etaBand", {1.1, 1.7})
            ->setComment(
                "Transition |eta| band [lo, hi) that the additive deltas below reach, evaluated on the "
                "innermost member triplet.");
        gateDesc.add<std::vector<double>>("etaBandDelta", {-0.5, 1.2})
            ->setComment(
                "In-band additive deltas to marginT4: [0] added to the 4-layer IP bar, [1] to the "
                "4-layer exempt bar. All-zero is a no-op and skips the band computation entirely.");
        chainDesc.add<edm::ParameterSetDescription>("gate", gateDesc);

        edm::ParameterSetDescription claimDesc;
        claimDesc.add<double>("orderAlpha", 10.0)
            ->setComment(
                "Weight of the realness-margin hinge in the best-first order key of the greedy claim "
                "walk, orderKey = score - orderAlpha * max(0, orderHinge - mX). The key is a rank, "
                "never a threshold.");
        claimDesc.add<double>("orderHinge", 5.0)
            ->setComment(
                "Margin at which that penalty vanishes: a chain is pushed down the queue in proportion "
                "to how far its mX falls short of this, and not at all once it clears it.");
        claimDesc.add<double>("maxClaimedFrac", 0.20)
            ->setComment(
                "Candidate-relative claim tolerance: reject a candidate once this fraction of it is already "
                "claimed.");
        claimDesc.add<int32_t>("maxClaimedMDs", 1)
            ->setComment(
                "Absolute claim tolerance, in mini-doublets for readability. The claim universe is hit "
                "rows and every mini-doublet contributes two, so the effective budget is twice this.");
        claimDesc.add<double>("braidFrac", 0.50)
            ->setComment(
                "Owner-relative braid kill: drop a candidate overlapping an already-accepted chain by "
                "more than this fraction of the OWNER, which is the test that catches a short candidate "
                "braided through a long accepted one.");
        claimDesc.add<double>("braidFracAlt", 0.20)
            ->setComment("The same fraction for candidates at or above braidAltEta.");
        claimDesc.add<double>("braidAltEta", 1.5)
            ->setComment(
                "|eta| of the innermost member triplet at or above which braidFracAlt replaces "
                "braidFrac.");
        claimDesc.add<bool>("preClaim", true)
            ->setComment(
                "Pre-claim the outer-tracker hits of the surviving carried pixel tracks before the "
                "greedy walk, so a chain cannot claim hits a pixel track already owns. Pixel owners "
                "supply claimed slots to maxClaimedFrac only; they take no part in the braid test.");
        chainDesc.add<edm::ParameterSetDescription>("claim", claimDesc);

        edm::ParameterSetDescription attachDesc;
        attachDesc.add<std::vector<double>>("theta", {7.3, 7.0, 6.4})
            ->setComment(
                "Pair-head logit an (accepted chain, pixel seed) pair must reach to attach, banded on "
                "|seed eta|: [0] below 1.1, [1] 1.1 to 1.7, [2] at or above 1.7. The head's logit "
                "calibration shifts across eta, so a single global margin would be a different working "
                "point in each band. These are tied to the shipped attach-head weights, and the "
                "direction that hurts is loosening: a chain converted with the wrong seed loses its "
                "match outright.");
        attachDesc.add<double>("thetaT3", 6.450)
            ->setComment(
                "Delivery margin of the bare-triplet attach, global. Doubles as the triplet-side bar of "
                "the carried-pixel-seed retirement predicate.");
        attachDesc.add<double>("rpsThetaChain", 6.084)
            ->setComment(
                "Chain-side bar of that same retirement predicate. Deliberately not eta-banded, and "
                "distinct from the delivery margins above.");
        attachDesc.add<double>("t3FakeMax", 0.10)
            ->setComment(
                "Bare-triplet target admission on the triplet fake score. Applied NaN-rejecting and to "
                "the target mask, so a rejected triplet is never scored and never becomes a seed's best "
                "triplet.");
        attachDesc.add<int32_t>("ccMinShared", 1)
            ->setComment(
                "Hit-overlap contention on the bare-triplet deliveries: two deliveries conflict once "
                "they share this many mini-doublet rows, and the lower-scoring one is revoked.");
        attachDesc.add<std::vector<double>>("xcTheta", {4.3, 3.6, 4.0})
            ->setComment(
                "Cross-clean bars per |seed eta| band (below 1.1 / 1.1 to 1.7 / at or above 1.7): "
                "retire a carried bare pixel seed whose attach-head logit toward some delivered "
                "seedless chain reaches the band's bar. This arm stands in for LST's pixel-seed to T5 "
                "embedding test and carries no geometric window.");
        attachDesc.add<int32_t>("cc9MinShared", 2)
            ->setComment(
                "Drop a delivered seedless chain sharing this many outer-tracker hits with a delivered "
                "pixel-seeded track (2 = one full mini-doublet); 0 disables the test.");
        attachDesc.add<bool>("replacePT5", true)
            ->setComment("Retire every carried pT5 track, because the chains deliver that class themselves.");
        attachDesc.add<bool>("replacePT3", true)
            ->setComment("Retire every carried pT3 track, because the bare-triplet attach delivers that class.");
        attachDesc.add<bool>("suppressBarePLS", true)
            ->setComment(
                "Retire a carried bare pixel-seed track whose seed had a scored pair above its "
                "retirement bar but lost the contention.");
        attachDesc.add<bool>("seedDedup", true)
            ->setComment(
                "Seed-family dedup of the attach owners: two pixel seeds are the same seed when they "
                "share at least two pixel hit rows.");
        attachDesc.add<double>("dcaMax", 1e9)
            ->setComment(
                "Attach-eligibility ceiling on the chain transverse DCA, cm. 1e9 leaves it off: a "
                "displaced chain with a pixel seed is an upside to claim, not a dilution to guard "
                "against.");
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
