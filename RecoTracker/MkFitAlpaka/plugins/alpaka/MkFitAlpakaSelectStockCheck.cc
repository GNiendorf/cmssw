// Lane select test module: runs K2 (inter-layer propagation + selectHitIndicesV2 + handle_missed_layers) on the
// per-candidate inputs that a PRIVATE instrumented stock MkFitCore dumped in the same job (test/select_dump_format.h,
// one file sel_<seq>.bin per event, seq = 0-based event count; run with ONE stream), with the device EventOfHits built
// from the same hit wrappers and the device ES product, and compares every output with stock:
// propagated state + fail flag, Bins, WSR/in_gap, the selected hit list (order included) and the extras flags.
// Config: test/select_stock_cfg.py.
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <type_traits>
#include <vector>

#include "CondFormats/DataRecord/interface/SiPixelQualityRcd.h"
#include "CondFormats/SiPixelObjects/interface/SiPixelQuality.h"
#include "DataFormats/TrackerCommon/interface/TrackerDetSide.h"
#include "DataFormats/TrackerCommon/interface/TrackerTopology.h"
#include "FWCore/MessageLogger/interface/MessageLogger.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include "FWCore/Utilities/interface/InputTag.h"
#include "Geometry/Records/interface/TrackerDigiGeometryRecord.h"
#include "Geometry/TrackerGeometryBuilder/interface/TrackerGeometry.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/ESGetToken.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/Event.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/EventSetup.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/MakerMacros.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/global/EDProducer.h"
#include "HeterogeneousCore/AlpakaInterface/interface/config.h"
#include "HeterogeneousCore/AlpakaInterface/interface/memory.h"
#include "DataFormats/Portable/interface/PortableHostCollection.h"
#include "DataFormats/Portable/interface/alpaka/PortableCollection.h"
#include "RecoTracker/MkFit/interface/MkFitGeometry.h"
#include "RecoTracker/MkFit/interface/MkFitHitWrapper.h"
#include "RecoTracker/MkFit/interface/MkFitOutputWrapper.h"
#include "RecoTracker/Record/interface/TrackerRecoGeometryRecord.h"

#include "RecoTracker/MkFitAlpaka/interface/es/ESData.h"
#include "RecoTracker/MkFitAlpaka/interface/hits/EventOfHitsHostSetup.h"
#include "RecoTracker/MkFitAlpaka/interface/hits/alpaka/EventOfHitsBuild.h"
#include "RecoTracker/MkFitAlpaka/src/alpaka/select/SelectEntry.h"
#include "RecoTracker/MkFitAlpaka/src/alpaka/select/SelectSoA.h"
#include "RecoTracker/MkFitAlpaka/test/select_dump_format.h"

namespace ALPAKA_ACCELERATOR_NAMESPACE {

  namespace {
    struct SelDiagHost {  // layout of mkfitdev::select::SelDiag
      float qc, dqTrack, dphiTrack;
      int32_t q0, q1, q2, p1, p2;
      float sp1[3], sp2[3];
      float qmin, qmax;
    };

    struct Totals {
      std::mutex mtx;
      long events = 0, records = 0, missingFile = 0, maskSet = 0;
      long failMismatch = 0, failed = 0;
      long parBitExact = 0, errBitExact = 0;
      double maxParRel = 0, maxErrRel = 0;
      double maxParPull = 0, maxErrCorr = 0;  // |dpar|/sigma_stock, |derr_ij|/sqrt(err_ii err_jj)
      long parPullHist[5] = {0, 0, 0, 0, 0};  // max |pull| per cand: ==0, <1e-6, <1e-4, <1e-2, >=1e-2
      long errCorrHist[5] = {0, 0, 0, 0, 0};  // max normalized cov diff per cand: ==0, <1e-6, <1e-4, <1e-2, >=1e-2
      long binsMismatch = 0, binsQMismatch = 0, binsPMismatch = 0;
      double maxQcRel = 0, maxDqRel = 0, maxDphiRel = 0;
      long qcExact = 0, dqExact = 0, dphiExact = 0, spExact = 0, qminmaxExact = 0;
      double maxSpRel = 0;
      long wsrMismatch = 0, gapMismatch = 0, extraMismatch = 0;
      long withHits = 0, hitsIdentical = 0, hitsSameSet = 0, hitsDiffer = 0, nHitsDiffer = 0;
      long stockHits = 0, portHits = 0;
      long wsrCount[4] = {0, 0, 0, 0};
      long extraHeld = 0, extraStop = 0;
      int printed = 0, printedQ = 0;
    };

    inline double relDiff(float a, float b) {
      const double d = std::abs(double(a) - double(b));
      const double s = std::max(std::abs(double(a)), std::abs(double(b)));
      return s > 0 ? d / s : 0.0;
    }
  }  // namespace

  class MkFitAlpakaSelectStockCheck : public global::EDProducer<> {
  public:
    explicit MkFitAlpakaSelectStockCheck(edm::ParameterSet const& iConfig)
        : EDProducer<>(iConfig),
          pixelHitsToken_{consumes(iConfig.getParameter<edm::InputTag>("pixelHits"))},
          stripHitsToken_{consumes(iConfig.getParameter<edm::InputTag>("stripHits"))},
          pixelLayerToken_{consumes(iConfig.getParameter<edm::InputTag>("pixelHits"))},
          stripLayerToken_{consumes(iConfig.getParameter<edm::InputTag>("stripHits"))},
          stockCandsToken_{consumes(iConfig.getParameter<edm::InputTag>("stockCandidates"))},
          mkFitGeomToken_{esConsumes()},
          pixelQualityToken_{esConsumes()},
          geomToken_{esConsumes()},
          esToken_{esConsumes(iConfig.getParameter<edm::ESInputTag>("esData"))},
          dumpDir_{iConfig.getParameter<std::string>("dumpDir")},
          maxPrint_{iConfig.getParameter<int>("maxPrint")} {}

    static void fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
      edm::ParameterSetDescription desc;
      desc.add("pixelHits", edm::InputTag{"hltMkFitSiPixelHits"});
      desc.add("stripHits", edm::InputTag{"hltMkFitSiPhase2Hits"});
      desc.add("stockCandidates", edm::InputTag{"hltInitialStepTrackCandidatesMkFit"})
          ->setComment("consumed only to run after the instrumented stock building (which writes the dump)");
      desc.add("esData", edm::ESInputTag{"", ""});
      desc.add<std::string>("dumpDir", "");
      desc.add<int>("maxPrint", 20);
      descriptions.addWithDefaultLabel(desc);
    }

    void produce(edm::StreamID, device::Event& iEvent, device::EventSetup const& iSetup) const override {
      iEvent.get(stockCandsToken_);  // ordering only
      const int seq = seq_++;
      // ---- dump
      std::vector<::mkfitdev::seldump::Record> recs;
      {
        const std::string fn = dumpDir_ + "/sel_" + std::to_string(seq) + ".bin";
        FILE* f = fopen(fn.c_str(), "rb");
        if (!f) {
          std::lock_guard<std::mutex> lk(tot_.mtx);
          ++tot_.missingFile;
          edm::LogPrint("SelectStockCheck") << "SELCHECK missing dump " << fn;
          return;
        }
        uint32_t magic = 0;
        if (fread(&magic, 4, 1, f) != 1 || magic != ::mkfitdev::seldump::kMagic) {
          fclose(f);
          throw cms::Exception("SelectStockCheck") << "bad dump " << fn;
        }
        ::mkfitdev::seldump::Record r;
        while (fread(&r, sizeof(r), 1, f) == 1)
          recs.push_back(r);
        fclose(f);
      }
      const int n = recs.size();

      // ---- device EventOfHits, exactly as MkFitAlpakaEventOfHitsProducer
      const auto& pixelHits = iEvent.get(pixelHitsToken_).hits();
      const auto& stripHits = iEvent.get(stripHitsToken_).hits();
      const auto& pixLay = iEvent.get(pixelLayerToken_);
      const auto& strLay = iEvent.get(stripLayerToken_);
      const auto& mkFitGeom = iSetup.getData(mkFitGeomToken_);
      const auto& ti = mkFitGeom.trackerInfo();
      std::vector<::mkfitdev::DeadRegionDev> deads;
      {
        const auto& trackerGeom = iSetup.getData(geomToken_);
        const auto& pixelQuality = iSetup.getData(pixelQualityToken_);
        for (const auto& bp : pixelQuality.getBadComponentList()) {
          const DetId detid(bp.DetID);
          const auto& surf = trackerGeom.idToDet(detid)->surface();
          bool isBarrel = (mkFitGeom.topology()->side(detid) == static_cast<unsigned>(TrackerDetSide::Barrel));
          const auto ilay = mkFitGeom.mkFitLayerNumber(detid);
          const auto q1 = isBarrel ? surf.zSpan().first : surf.rSpan().first;
          const auto q2 = isBarrel ? surf.zSpan().second : surf.rSpan().second;
          if (bp.errorType == 0)
            deads.push_back({surf.phiSpan().first, surf.phiSpan().second, q1, q2, ilay});
        }
      }
      const uint32_t nPix = pixelHits.size(), nStr = stripHits.size();
      const auto axes = ::mkfitdev::layerAxisInputs(ti);
      ::mkfitdev::HitsHostCollection hitsH(iEvent.queue(), nPix + nStr);
      ::mkfitdev::LayersHostCollection layersH(iEvent.queue(), axes.size());
      hitsH.view().nPixel() = nPix;
      hitsH.view().nStrip() = nStr;
      ::mkfitdev::fillHits(pixelHits, pixLay, 0, hitsH.view());
      ::mkfitdev::fillHits(stripHits, strLay, nPix, hitsH.view());
      ::mkfitdev::fillLayers(axes, nPix, layersH.view());
      auto eoh = mkfitdev::hits::runBuildEventOfHits(iEvent.queue(), hitsH, layersH, deads);

      if (n == 0)
        return;

      // ---- K2 inputs
      PortableHostCollection<::mkfitdev::CandSlotsSoA> slotsH(iEvent.queue(), n);
      PortableHostCollection<::mkfitdev::SelListSoA> listH(iEvent.queue(), n);
      for (int i = 0; i < n; ++i) {
        const auto& r = recs[i];
        ::mkfitdev::CandState& cs = slotsH.view()[i].state();
        std::memset(&cs, 0, sizeof(cs));
        for (int k = 0; k < 6; ++k)
          cs.par[k] = r.parC[k];
        for (int k = 0; k < 21; ++k)
          cs.err[k] = r.errC[k];
        cs.charge = r.chg;
        cs.label = r.label;
        listH.view()[i].row() = i;
        listH.view()[i].layer() = r.layer;
        listH.view()[i].region() = r.region;
      }
      listH.view().n() = n;
      PortableCollection<::mkfitdev::CandSlotsSoA> slotsD(iEvent.queue(), n);
      PortableCollection<::mkfitdev::SelListSoA> listD(iEvent.queue(), n);
      alpaka::memcpy(iEvent.queue(), slotsD.buffer(), slotsH.buffer());
      alpaka::memcpy(iEvent.queue(), listD.buffer(), listH.buffer());
      PortableCollection<::mkfitdev::PropStateSoA> propsD(iEvent.queue(), n);
      PortableCollection<::mkfitdev::SelHitsSoA> selsD(iEvent.queue(), n);
      auto diagD = cms::alpakatools::make_device_buffer<SelDiagHost[]>(iEvent.queue(), n);

      const auto& esd = iSetup.getData(esToken_);
      alpaka::wait(iEvent.queue());
      const auto tk0 = std::chrono::steady_clock::now();
      mkfitdev::select::runSelectHits(iEvent.queue(),
                                      slotsD.const_view(),
                                      listD.const_view(),
                                      n,
                                      esd.view(),
                                      eoh.layers.const_view(),
                                      eoh.binnedHits.const_view(),
                                      eoh.bins.const_view(),
                                      eoh.hits.const_view(),
                                      propsD.view(),
                                      selsD.view(),
                                      reinterpret_cast<mkfitdev::select::SelDiag*>(diagD.data()));
      alpaka::wait(iEvent.queue());
      k2ns_ += std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - tk0).count();

      PortableHostCollection<::mkfitdev::PropStateSoA> propsH(iEvent.queue(), n);
      PortableHostCollection<::mkfitdev::SelHitsSoA> selsH(iEvent.queue(), n);
      auto diagH = cms::alpakatools::make_host_buffer<SelDiagHost[]>(iEvent.queue(), n);
      alpaka::memcpy(iEvent.queue(), propsH.buffer(), propsD.buffer());
      alpaka::memcpy(iEvent.queue(), selsH.buffer(), selsD.buffer());
      alpaka::memcpy(iEvent.queue(), diagH, diagD);
      alpaka::wait(iEvent.queue());

      // ---- compare
      std::lock_guard<std::mutex> lk(tot_.mtx);
      Totals& T = tot_;
      ++T.events;
      long evHitsId = 0, evWithHits = 0, evDiff = 0;
      for (int i = 0; i < n; ++i) {
        const auto& r = recs[i];
        const ::mkfitdev::PropState ps = propsH.view()[i].ps();
        const ::mkfitdev::SelHits sh = selsH.view()[i].sel();
        const SelDiagHost dg = diagH.data()[i];
        ++T.records;
        if (r.maskSet)
          ++T.maskSet;
        bool bad = false;
        if (ps.fail != r.failFlag) {
          ++T.failMismatch;
          bad = true;
        }
        if (r.failFlag)
          ++T.failed;
        bool parEx = true, errEx = true;
        for (int k = 0; k < 6; ++k) {
          parEx &= std::memcmp(&ps.par[k], &r.parP[k], 4) == 0;
          T.maxParRel = std::max(T.maxParRel, relDiff(ps.par[k], r.parP[k]));
        }
        for (int k = 0; k < 21; ++k) {
          errEx &= std::memcmp(&ps.err[k], &r.errP[k], 4) == 0;
          T.maxErrRel = std::max(T.maxErrRel, relDiff(ps.err[k], r.errP[k]));
        }
        {
          // packed lower-triangle index of (i,j), i >= j
          auto idx = [](int i, int j) { return i * (i + 1) / 2 + j; };
          double mp = 0;
          for (int k = 0; k < 6; ++k) {
            const double sig = std::sqrt(std::max(0.f, r.errP[idx(k, k)]));
            if (sig > 0)
              mp = std::max(mp, std::abs(double(ps.par[k]) - double(r.parP[k])) / sig);
          }
          T.maxParPull = std::max(T.maxParPull, mp);
          T.parPullHist[mp == 0 ? 0 : mp < 1e-6 ? 1 : mp < 1e-4 ? 2 : mp < 1e-2 ? 3 : 4]++;
          double me = 0;
          for (int a = 0; a < 6; ++a)
            for (int b = 0; b <= a; ++b) {
              const double nrm = std::sqrt(std::max(0.f, r.errP[idx(a, a)]) * std::max(0.f, r.errP[idx(b, b)]));
              if (nrm > 0)
                me = std::max(me, std::abs(double(ps.err[idx(a, b)]) - double(r.errP[idx(a, b)])) / nrm);
            }
          T.maxErrCorr = std::max(T.maxErrCorr, me);
          T.errCorrHist[me == 0 ? 0 : me < 1e-6 ? 1 : me < 1e-4 ? 2 : me < 1e-2 ? 3 : 4]++;
        }
        T.parBitExact += parEx;
        T.errBitExact += errEx;
        const bool qBins = dg.q0 == r.q0 && dg.q1 == r.q1 && dg.q2 == r.q2;
        const bool pBins = dg.p1 == r.p1 && dg.p2 == r.p2;
        if (!qBins)
          ++T.binsQMismatch;
        if (!pBins)
          ++T.binsPMismatch;
        if (!qBins || !pBins)
          ++T.binsMismatch;
        {
          auto bx = [](float a, float b) { return std::memcmp(&a, &b, 4) == 0; };
          T.qcExact += bx(dg.qc, r.qc);
          T.dqExact += bx(dg.dqTrack, r.dqTrack);
          T.dphiExact += bx(dg.dphiTrack, r.dphiTrack);
          bool spx = true;
          for (int k = 0; k < 3; ++k) {
            spx &= bx(dg.sp1[k], r.sp1[k]) && bx(dg.sp2[k], r.sp2[k]);
            T.maxSpRel = std::max(T.maxSpRel, std::max(relDiff(dg.sp1[k], r.sp1[k]), relDiff(dg.sp2[k], r.sp2[k])));
          }
          T.spExact += spx;
          const bool qmm = bx(dg.qmin, r.qmin) && bx(dg.qmax, r.qmax);
          T.qminmaxExact += qmm;
          if (!qmm && spx && T.printedQ < 8) {
            ++T.printedQ;
            edm::LogPrint("SelectStockCheck")
                << "QMMDIFF layer " << r.layer << " barrel " << ti.layer(r.layer).is_barrel() << " fail " << r.failFlag
                << " stock qmin/qmax " << r.qmin << " " << r.qmax << " port " << dg.qmin << " " << dg.qmax << " sp1 "
                << r.sp1[0] << " " << r.sp1[1] << " " << r.sp1[2] << " sp2 " << r.sp2[0] << " " << r.sp2[1] << " "
                << r.sp2[2];
          }
        }
        T.maxQcRel = std::max(T.maxQcRel, relDiff(dg.qc, r.qc));
        T.maxDqRel = std::max(T.maxDqRel, relDiff(dg.dqTrack, r.dqTrack));
        T.maxDphiRel = std::max(T.maxDphiRel, relDiff(dg.dphiTrack, r.dphiTrack));
        if (sh.wsrRaw != r.wsr) {
          ++T.wsrMismatch;
          bad = true;
        }
        if (r.wsr >= 0 && r.wsr < 4)
          ++T.wsrCount[r.wsr];
        // stock m_in_gap is stale (not reset) for WSR_Failed slots, which are never used: compare the others only
        if (r.wsr != ::mkfitdev::WSR_Failed && bool(sh.inGap) != bool(r.inGap)) {
          ++T.gapMismatch;
          bad = true;
        }
        // stock handle_missed_layers from the stock WSR
        {
          int w = r.wsr;
          uint8_t ex = 0;
          if (w == ::mkfitdev::WSR_Failed) {
            if (r.layer >= 0 && ti.layer(r.layer).is_barrel()) {
              ex = ::mkfitdev::kSelExtraHeldBack;
              if (r.region == 2)
                ex |= ::mkfitdev::kSelExtraStopNode;
            }
          } else if (w == ::mkfitdev::WSR_Outside)
            ex = ::mkfitdev::kSelExtraHeldBack;
          if (ex != sh.extra) {
            ++T.extraMismatch;
            bad = true;
          }
          if (ex & ::mkfitdev::kSelExtraHeldBack)
            ++T.extraHeld;
          if (ex & ::mkfitdev::kSelExtraStopNode)
            ++T.extraStop;
        }
        T.stockHits += r.nHits;
        T.portHits += sh.nHits;
        if (r.nHits > 0 || sh.nHits > 0) {
          ++T.withHits;
          ++evWithHits;
          bool same = r.nHits == sh.nHits;
          for (int k = 0; same && k < r.nHits; ++k)
            same = r.hits[k] == sh.hits[k];
          if (same) {
            ++T.hitsIdentical;
            ++evHitsId;
          } else {
            ++evDiff;
            bad = true;
            if (r.nHits != sh.nHits)
              ++T.nHitsDiffer;
            bool sameSet = r.nHits == sh.nHits;
            for (int k = 0; sameSet && k < r.nHits; ++k) {
              bool found = false;
              for (int m = 0; m < sh.nHits; ++m)
                found |= sh.hits[m] == r.hits[k];
              sameSet = found;
            }
            if (sameSet)
              ++T.hitsSameSet;
            else
              ++T.hitsDiffer;
          }
        }
        if (bad && T.printed < maxPrint_) {
          ++T.printed;
          char buf[1024];
          snprintf(buf,
                   sizeof buf,
                   "SELDIFF ev %d rec %d layer %d region %d label %d: fail %d/%d wsr %d/%d gap %d/%d nh %d/%d "
                   "bins stock q %d %d %d p %d %d port q %d %d %d p %d %d | stock hits %d %d %d %d %d %d port %d %d %d "
                   "%d %d %d",
                   seq, i, r.layer, r.region, r.label, r.failFlag, ps.fail, r.wsr, sh.wsrRaw, r.inGap, sh.inGap,
                   r.nHits, sh.nHits, r.q0, r.q1, r.q2, r.p1, r.p2, dg.q0, dg.q1, dg.q2, dg.p1, dg.p2, r.hits[0],
                   r.hits[1], r.hits[2], r.hits[3], r.hits[4], r.hits[5], sh.hits[0], sh.hits[1], sh.hits[2],
                   sh.hits[3], sh.hits[4], sh.hits[5]);
          edm::LogPrint("SelectStockCheck") << buf;
        }
      }
      edm::LogPrint("SelectStockCheck") << "SELCHECK ev " << seq << " records " << n << " withHits " << evWithHits
                                        << " hitListIdentical " << evHitsId << " differ " << evDiff;
    }

    void endJob() override {
      const Totals& T = tot_;
      char buf[4096];
      snprintf(buf,
               sizeof buf,
               "SELCHECK SUMMARY events %ld records %ld (missing dumps %ld, mask set %ld)\n"
               "  prop fail flag mismatches %ld (failed %ld); propagated par bit-exact %ld/%ld (max rel %.3g), "
               "err bit-exact %ld/%ld (max rel %.3g)\n"
               "  propagated state: max |dpar|/sigma %.3g (per-cand max pull ==0 %ld, <1e-6 %ld, <1e-4 %ld, <1e-2 %ld, "
               ">=1e-2 %ld), max |derr_ij|/sqrt(err_ii err_jj) %.3g (per cand ==0 %ld, <1e-6 %ld, <1e-4 %ld, <1e-2 %ld, "
               ">=1e-2 %ld)\n"
               "  bins mismatches %ld (q %ld, phi %ld); max rel q_c %.3g dq_track %.3g dphi_track %.3g\n"
               "  bit-exact: q_c %ld dq_track %ld dphi_track %ld limit-states sp1+sp2 %ld (max rel %.3g) qmin+qmax %ld\n"
               "  wsr mismatches %ld, in_gap mismatches %ld, extras-flag mismatches %ld; stock wsr Inside %ld Edge "
               "%ld Outside %ld Failed %ld; extras held-back %ld stop-node %ld\n"
               "  candidates with hits %ld: identical lists (order incl.) %ld, same set other order %ld, "
               "different %ld (n differs %ld); total hits stock %ld port %ld",
               T.events, T.records, T.missingFile, T.maskSet, T.failMismatch, T.failed, T.parBitExact, T.records,
               T.maxParRel, T.errBitExact, T.records, T.maxErrRel, T.maxParPull, T.parPullHist[0], T.parPullHist[1],
               T.parPullHist[2], T.parPullHist[3], T.parPullHist[4], T.maxErrCorr, T.errCorrHist[0], T.errCorrHist[1],
               T.errCorrHist[2], T.errCorrHist[3], T.errCorrHist[4], T.binsMismatch, T.binsQMismatch, T.binsPMismatch,
               T.maxQcRel, T.maxDqRel, T.maxDphiRel, T.qcExact, T.dqExact, T.dphiExact, T.spExact, T.maxSpRel,
               T.qminmaxExact, T.wsrMismatch, T.gapMismatch, T.extraMismatch, T.wsrCount[0],
               T.wsrCount[1], T.wsrCount[2], T.wsrCount[3], T.extraHeld, T.extraStop, T.withHits, T.hitsIdentical,
               T.hitsSameSet, T.hitsDiffer, T.nHitsDiffer, T.stockHits, T.portHits);
      edm::LogPrint("SelectStockCheck") << buf;
      edm::LogPrint("SelectStockCheck") << "SELCHECK K2 kernel time (incl. launch + wait, diagnostics on): total "
                                        << k2ns_.load() * 1e-6 << " ms, per event "
                                        << (T.events ? k2ns_.load() * 1e-6 / T.events : 0.) << " ms, per candidate "
                                        << (T.records ? double(k2ns_.load()) / T.records : 0.) << " ns";
    }

  private:
    const edm::EDGetTokenT<MkFitHitWrapper> pixelHitsToken_;
    const edm::EDGetTokenT<MkFitHitWrapper> stripHitsToken_;
    const edm::EDGetTokenT<std::vector<int>> pixelLayerToken_;
    const edm::EDGetTokenT<std::vector<int>> stripLayerToken_;
    const edm::EDGetTokenT<MkFitOutputWrapper> stockCandsToken_;
    const edm::ESGetToken<MkFitGeometry, TrackerRecoGeometryRecord> mkFitGeomToken_;
    const edm::ESGetToken<SiPixelQuality, SiPixelQualityRcd> pixelQualityToken_;
    const edm::ESGetToken<TrackerGeometry, TrackerDigiGeometryRecord> geomToken_;
    const device::ESGetToken<::mkfitdev::ESData<Device>, TrackerRecoGeometryRecord> esToken_;
    const std::string dumpDir_;
    const int maxPrint_;
    mutable std::atomic<int> seq_{0};
    mutable std::atomic<long> k2ns_{0};
    mutable Totals tot_;
  };

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE

DEFINE_FWK_ALPAKA_MODULE(MkFitAlpakaSelectStockCheck);
