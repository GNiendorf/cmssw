// Portable EventOfHits: takes the host MkFitHitWrapper products of the step (pixel + Phase-2 OT hits) and the stock
// host MkFitGeometry, builds the device EventOfHits (HitSoA + per-layer binning reproducing stock LayerOfHits).
// Dead modules: the pixel-quality dead regions are collected on the host exactly as MkFitEventOfHitsProducer does
// (the strip quality DB is off in the HLT LST step and is not supported here), the dead-bin table is filled on device.
//
// Round 3 (seeds lane): the device EventOfHits is put into the Event as the integ product
// mkfitdev::EventOfHitsDeviceCollection (blocks hits, layers, binnedHits, bins). With compareTo set, the module copies
// it back and checks it per event and per layer against the stock MkFitEventOfHits (internal order, hit infos, bin
// table, dead bins), and prints one line per event. Events beyond the device build limits are skipped with a warning
// (M7: no exception at event time). Skip contract (review R4-M1): a skipped event gets an EMPTY EventOfHits (all four
// blocks of size 0; nLayers == 0 marks it, readable on the host from the metadata without a sync). The build module
// then sets the status counter eventSkipped and puts empty TrackSoAs.
//
// Round 5 (lane inputs): deviceHits = True makes the HitSoA on the device from the pixel rechit SoA
// (hltPhase2SiPixelRecHitsSoA) and the OT rechits, with a per-module table (rotation, position, layer, uniqueIdInLayer;
// built per IOV), bitwise equal to the stock hit converters (doc/inputs.txt). The stock MkFitHitWrapper products are
// then not consumed (the stock hit converters leave the menu; the converter's MkFitClusterIndexToHit comes from the
// index-only MkFitAlpaka{Pixel,Phase2}ClusterIndexToHit). What stays on the host: one pass over the legacy pixel
// clusters (row permutation moduleStart + originalId and the legacy sizes; the legacy cluster order is not the SoA
// order) and one pass over the OT rechits (local position / error, module, size). compareHostHits = True also
// fills the host-input HitSoA from the wrappers and compares the two bitwise (DEVICE_HITS_EOH lines; waits).
//
// Round 7 (lane cpu): R6-M3 guard - a pixel row found through originalId is accepted only if the SoA local position is
// bitwise the legacy rechit's; otherwise (and for clusters without originalId) the row is found by position. Both
// cases are counted (LogWarning on the first, DEVICE_HITS_GUARD at endJob) and throw under compareHostHits. D7-c: on
// CPU backends the raw host rows are per-stream scratch (grow-only capacity, reused every event; scratchReuse).
//
// Round 8 (lane mem, D7-f): instance "queue" = a host product with the native handle of the queue the EventOfHits
// buffer was allocated on (0 on CPU backends). With the product deleted early (canDeleteEarly, after its last
// consumer, MkFitAlpakaFitDeviceProducer), the caching allocator orders the block's reuse after the work of that queue
// only; the fit checks that it ran on this queue and waits otherwise (doc/mem.txt).
//
// Round 9 (lane mem, D9-f): the device OT rechit SoA (stage D) is deleted right after this module (canDeleteEarly).
// otSoAQueue = the SoA's allocation queue: when this module runs on another queue, that queue is made to wait (on the
// device) for the kernel that reads the SoA, so the caching allocator cannot hand the block out while it is read.
// otSoAHost = the framework's host copy of the SoA (read by hltInputLST): consumed only so that the copy is made
// before this module runs, i.e. before the early deletion (doc/mem.txt).
#include <algorithm>
#include <array>
#include <optional>
#include <atomic>
#include <cstddef>
#include <type_traits>
#include <cstring>
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
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/global/EDProducer.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/Event.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/EventSetup.h"
#include "HeterogeneousCore/AlpakaInterface/interface/config.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/MakerMacros.h"
#include "RecoTracker/MkFit/interface/MkFitEventOfHits.h"
#include "RecoTracker/MkFit/interface/MkFitGeometry.h"
#include "RecoTracker/MkFitAlpaka/interface/hits/HitModuleTableESData.h"
#include "RecoTracker/MkFitAlpaka/plugins/alpaka/MkFitAlpakaEventOfHitsModuleTable.h"
#include "RecoTracker/MkFit/interface/MkFitHitWrapper.h"
#include "RecoTracker/MkFitCore/interface/HitStructures.h"
#include "RecoTracker/Record/interface/TrackerRecoGeometryRecord.h"

#include "HeterogeneousCore/AlpakaCore/interface/alpaka/EDPutToken.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/ESGetToken.h"
#include "RecoTracker/MkFitAlpaka/interface/EventOfHitsProduct.h"
#include "RecoTracker/MkFitAlpaka/interface/alpaka/EventOfHitsProduct.h"
#include "RecoTracker/MkFitAlpaka/interface/es/ESData.h"
#include "RecoTracker/MkFitAlpaka/interface/hits/EventOfHitsHostSetup.h"
#include "RecoTracker/MkFitAlpaka/interface/hits/alpaka/EventOfHitsBuild.h"
#include "RecoTracker/MkFitAlpaka/interface/hits/alpaka/DeviceHitsBuild.h"
#include "RecoTracker/MkFitAlpaka/interface/othits/alpaka/OTRecHitDeviceCollection.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/EDGetToken.h"
#include "DataFormats/SiPixelCluster/interface/SiPixelCluster.h"
#include "DataFormats/Phase2TrackerCluster/interface/Phase2TrackerCluster1D.h"
#include "DataFormats/TrackerRecHit2D/interface/Phase2TrackerRecHit1D.h"
#include "DataFormats/TrackerRecHit2D/interface/SiPixelRecHitCollection.h"
#include "DataFormats/TrackingRecHitSoA/interface/TrackingRecHitsHost.h"
#include "DataFormats/TrackingRecHitSoA/interface/alpaka/TrackingRecHitsSoACollection.h"
#include "HeterogeneousCore/AlpakaInterface/interface/memory.h"
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>

namespace ALPAKA_ACCELERATOR_NAMESPACE {

  namespace {
    // D7-c: per-stream raw host rows of the device hit input (CPU backends); rows past the event's count are unused
    struct EohScratch {
      int32_t cap = 0;
      uint64_t nAllocs = 0;
      std::optional<::mkfitdev::DeviceHitRawHostCollection> raw;
      // R7-H1 (round 8, lane mem): CPU backends, the EventOfHits product's memory (grow-only, 25% headroom)
      std::optional<cms::alpakatools::host_buffer<std::byte[]>> product;
      std::size_t productCap = 0;
      uint64_t nProductAllocs = 0;
    };
    // R6-M3: per-event counts of the pixel cluster key -> SoA row guard
    struct RawGuard {
      uint32_t keyMismatch = 0;     // originalId row whose local position is not the legacy one (row found by position)
      uint32_t positionSearch = 0;  // clusters without originalId (row found by position)
    };
  }  // namespace

  class MkFitAlpakaEventOfHitsProducer : public global::EDProducer<edm::StreamCache<EohScratch>> {
  public:
    explicit MkFitAlpakaEventOfHitsProducer(edm::ParameterSet const& iConfig)
        : EDProducer<edm::StreamCache<EohScratch>>(iConfig),
          mkFitGeomToken_{esConsumes()},
          productToken_{produces()},
          queueToken_{produces("queue")},
          usePixelQualityDB_{iConfig.getParameter<bool>("usePixelQualityDB")},
          compare_{!iConfig.getParameter<edm::InputTag>("compareTo").label().empty()},
          testSkipModulo_{iConfig.getUntrackedParameter<unsigned int>("testSkipModulo")},
          useESLayers_{iConfig.getParameter<bool>("useESLayers")},
          deviceHits_{iConfig.getParameter<bool>("deviceHits")},
          compareHostHits_{iConfig.getParameter<bool>("compareHostHits")},
          pixelSoAOnDevice_{iConfig.getParameter<bool>("pixelSoAOnDevice") && !std::is_same_v<Device, alpaka::DevCpu>},
          scratchReuse_{iConfig.getParameter<int>("scratchReuse") >= 1 && std::is_same_v<Device, alpaka::DevCpu>} {
      // the stock MkFitHitWrapper products are consumed only on the host-input path (or to compare with it), so with
      // deviceHits the stock hit converters are not run for this module
      if (!deviceHits_ || compareHostHits_ || compare_) {
        pixelHitsToken_ = consumes(iConfig.getParameter<edm::InputTag>("pixelHits"));
        stripHitsToken_ = consumes(iConfig.getParameter<edm::InputTag>("stripHits"));
        pixelLayerToken_ = consumes(iConfig.getParameter<edm::InputTag>("pixelHits"));
        stripLayerToken_ = consumes(iConfig.getParameter<edm::InputTag>("stripHits"));
      }
      if (deviceHits_) {
        pixelSoAToken_ = consumes(iConfig.getParameter<edm::InputTag>("pixelSoA"));
        pixelRecHitsToken_ = consumes(iConfig.getParameter<edm::InputTag>("pixelRecHits"));
        // stage D (round 8, lane otdev): OT rows from the device OT rechit SoA (no legacy OT rechits read)
        if (auto const ot = iConfig.getParameter<edm::InputTag>("otSoA"); !ot.label().empty()) {
          otSoAToken_ = consumes(ot);
          useOTSoA_ = true;
          if (auto const q = iConfig.getParameter<edm::InputTag>("otSoAQueue"); !q.label().empty())
            otSoAQueueToken_ = consumes(q);
          if (auto const h = iConfig.getParameter<edm::InputTag>("otSoAHost"); !h.label().empty())
            otSoAHostToken_ = consumes(h);
        } else
          otRecHitsToken_ = consumes(iConfig.getParameter<edm::InputTag>("otRecHits"));
        pixelClustersToken_ = consumes(iConfig.getParameter<edm::InputTag>("pixelClusters"));
        otClustersToken_ = consumes(iConfig.getParameter<edm::InputTag>("otClusters"));
        hitGeomToken_ = esConsumes();
        if (auto const mt = iConfig.getParameter<std::string>("moduleTable"); !mt.empty()) {
          moduleTableToken_ = esConsumes(edm::ESInputTag("", mt));
          moduleTableES_ = true;
        }
        if (pixelSoAOnDevice_)
          pixelSoADevToken_ = consumes(iConfig.getParameter<edm::InputTag>("pixelSoA"));
      }
      if (useESLayers_)
        esHostToken_ = esConsumes(iConfig.getParameter<edm::ESInputTag>("esData"));
      if (usePixelQualityDB_) {
        pixelQualityToken_ = esConsumes();
        geomToken_ = esConsumes();
      }
      if (compare_)
        stockToken_ = consumes(iConfig.getParameter<edm::InputTag>("compareTo"));
    }

    static void fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
      edm::ParameterSetDescription desc;
      desc.add("pixelHits", edm::InputTag{"hltMkFitSiPixelHits"});
      desc.add("stripHits", edm::InputTag{"hltMkFitSiPhase2Hits"});
      desc.add("usePixelQualityDB", true)->setComment("Use SiPixelQuality DB information (as the stock producer)");
      desc.add("useESLayers", false)
          ->setComment("static layer table (q axes) from the host ESData of MkFitAlpakaESProducer instead of TrackerInfo");
      desc.add("esData", edm::ESInputTag{"", ""})->setComment("MkFitAlpakaESProducer ComponentName");
      desc.add("deviceHits", false)
          ->setComment("make the HitSoA on the device from pixelSoA + otRecHits (bitwise = the stock hit converters); "
                       "default false: host input from the stock MkFitHitWrapper products");
      desc.add("pixelSoA", edm::InputTag{"hltPhase2SiPixelRecHitsSoA"})->setComment("deviceHits: pixel rechit SoA (host)");
      desc.add("pixelRecHits", edm::InputTag{"hltSiPixelRecHits"})
          ->setComment("deviceHits: legacy pixel rechits (cluster key -> SoA row, legacy cluster sizes)");
      desc.add("otRecHits", edm::InputTag{"hltSiPhase2RecHits"})->setComment("deviceHits: Phase-2 OT rechits");
      desc.add("otSoA", edm::InputTag{""})
          ->setComment("deviceHits: device OT rechit SoA (MkFitAlpakaOTRecHitsProducer); set = otRecHits is not read "
                       "(stage D, lane otdev)");
      desc.add("otSoAQueue", edm::InputTag{""})
          ->setComment("D9-f early deletion of otSoA: its producer's \"queue\" product; on another queue this module "
                       "orders that queue after its read of the SoA (empty = off)");
      desc.add("otSoAHost", edm::InputTag{""})
          ->setComment("D9-f early deletion of otSoA: the host copy of otSoA, consumed (not read) so that the "
                       "framework copy is made before this module and the early deletion (empty = off)");
      desc.add("pixelClusters", edm::InputTag{"hltSiPixelClusters"})
          ->setComment("deviceHits: legacy pixel clusters of the rechits (indexed by key, as stock convertHits)");
      desc.add("otClusters", edm::InputTag{"hltSiPhase2Clusters"})->setComment("deviceHits: OT clusters of the rechits");
      desc.add("pixelSoAOnDevice", false)
          ->setComment("deviceHits on GPU backends: read the pixel columns from the DEVICE pixel rechit SoA (no copy); "
                       "the host SoA is still read for moduleStart (it exists for the legacy rechits)");
      desc.add<std::string>("moduleTable", "")
          ->setComment("deviceHits: ComponentName of the MkFitAlpakaEventOfHitsModuleTableESProducer product (per-module "
                       "table as an ES product); empty = the module's own per-(device, IOV) table");
      desc.add("scratchReuse", 1)
          ->setComment(
              "D7-c: deviceHits raw host rows and (R7-H1) the EventOfHits product's memory as per-stream scratch on "
              "CPU backends (>= 1; GPU backends use the caching allocator); 0 = per-event allocation");
      desc.add("compareHostHits", false)
          ->setComment("validation (deviceHits): also fill the host-input HitSoA and compare bitwise (waits)");
      desc.addUntracked<unsigned int>("testSkipModulo", 0)
          ->setComment("validation only (R4-M1 skip contract test): treat events with id % N == 0 as unbuildable; 0 = off");
      desc.add("compareTo", edm::InputTag{""})
          ->setComment("stock MkFitEventOfHits to validate against (empty = no validation; validation waits on the queue)");
      descriptions.addWithDefaultLabel(desc);
    }

    std::unique_ptr<EohScratch> beginStream(edm::StreamID) const override { return std::make_unique<EohScratch>(); }

    void endStream(edm::StreamID sid) const override { productAllocs_ += streamCache(sid)->nProductAllocs; }

    void endJob() override {
      if (!otSoAQueueToken_.isUninitialized())
        edm::LogPrint("MkFitAlpakaEventOfHits")
            << "OT_RELEASE sameQueue " << nOtSameQueue_ << " ordered " << nOtOrdered_;
      if (productAllocs_ > 0)
        edm::LogPrint("MkFitAlpakaEventOfHits") << "EOH_POOL product block allocations " << productAllocs_
                                                << " largest product " << 1e-6 * productMaxBytes_ << " MB";
      if (deviceHits_)
        edm::LogPrint("MkFitAlpakaEventOfHits")
            << "DEVICE_HITS_GUARD keyMismatch " << nKeyMismatch_ << " positionSearch " << nPositionSearch_;
    }

    void produce(edm::StreamID sid, device::Event& iEvent, device::EventSetup const& iSetup) const override {
      if (testSkipModulo_ != 0 && iEvent.id().event() % testSkipModulo_ == 0) {
        skipEvent(iEvent, 0, 0);
        return;
      }
      const auto& mkFitGeom = iSetup.getData(mkFitGeomToken_);
      const auto& ti = mkFitGeom.trackerInfo();

      // dead regions as MkFitEventOfHitsProducer (pixel quality part)
      std::vector<::mkfitdev::DeadRegionDev> deads;
      if (usePixelQualityDB_) {
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

      // HitSoA (pixel rows, then strip rows) and the static layer table: filled in place on CPU backends, staged in
      // pinned host memory and copied on GPU backends
      static const mkfit::HitVec kNoHits;
      static const std::vector<int> kNoLayers;
      const bool hostHits = !deviceHits_ || compareHostHits_;
      const auto& pixelHits = hostHits ? iEvent.get(pixelHitsToken_).hits() : kNoHits;
      const auto& stripHits = hostHits ? iEvent.get(stripHitsToken_).hits() : kNoHits;
      const auto& pixLay = hostHits ? iEvent.get(pixelLayerToken_) : kNoLayers;
      const auto& strLay = hostHits ? iEvent.get(stripLayerToken_) : kNoLayers;
      uint32_t nPix = pixelHits.size(), nStr = stripHits.size();
      std::optional<::mkfitdev::DeviceHitRawHostCollection> raw;
      const ::mkfitdev::DeviceHitRawHostCollection* rawRef = nullptr;  // raw or the per-stream scratch
      if (deviceHits_) {
        const auto& pixRH = iEvent.get(pixelRecHitsToken_);
        const auto* otRH = useOTSoA_ ? nullptr : &iEvent.get(otRecHitsToken_);
        nPix = stockRows(pixRH);
        // one rechit per OT cluster in cluster-key order (Phase2TrackerRecHits): stock rows = OT clusters
        nStr = useOTSoA_ ? iEvent.get(otClustersToken_).dataSize() : stockRows(*otRH);
        if (compareHostHits_ && (nPix != pixelHits.size() || nStr != stripHits.size()))
          throw cms::Exception("MkFitAlpakaEventOfHits") << "device hit rows " << nPix << "+" << nStr
                                                         << " != stock HitVec sizes " << pixelHits.size() << "+"
                                                         << stripHits.size();
        const int32_t nRaw = int32_t(useOTSoA_ ? nPix : nPix + nStr);  // OT rows come from the SoA
        ::mkfitdev::DeviceHitRawHostCollection* rawP = nullptr;
        if (scratchReuse_) {  // D7-c: grow-only per-stream rows (25% headroom), reused every event
          EohScratch& sc = *streamCache(sid);
          if (!sc.raw || nRaw > sc.cap) {
            sc.cap = ((nRaw + nRaw / 4 + 4095) / 4096) * 4096;
            sc.raw.reset();
            sc.raw.emplace(iEvent.queue(), sc.cap);
            ++sc.nAllocs;
          }
          rawP = &*sc.raw;
        } else {
          raw.emplace(iEvent.queue(), nRaw);
          rawP = &*raw;
        }
        RawGuard guard;
        fillRaw(iEvent.get(pixelSoAToken_),
                pixRH,
                otRH,
                iEvent.getHandle(pixelClustersToken_),
                iEvent.getHandle(otClustersToken_),
                nPix,
                uint32_t(nRaw),
                rawP->view(),
                guard);
        checkGuard(iEvent.id().event(), guard);
        rawRef = rawP;
      }
      const auto axes = useESLayers_ ? layerAxisInputsES(iSetup.getData(esHostToken_)) : ::mkfitdev::layerAxisInputs(ti);
      auto fill = [&](::mkfitdev::HitSoA::View hv, ::mkfitdev::LayerSoA::View lv) {
        hv.nPixel() = nPix;
        hv.nStrip() = nStr;
        ::mkfitdev::fillHits(pixelHits, pixLay, 0, hv);
        ::mkfitdev::fillHits(stripHits, strLay, nPix, hv);
        ::mkfitdev::fillLayers(axes, nPix, lv);
      };
      // the EventOfHits event product (one PortableCollection over the four blocks)
      const uint32_t nHits = nPix + nStr, nLayers = axes.size(), nBins = ::mkfitdev::totalBins(axes);
      const std::array<int32_t, 4> sizes{{int32_t(nHits), int32_t(nLayers), int32_t(nHits), int32_t(nBins)}};
      mkfitdev::EventOfHitsDeviceCollection product = makeProduct(sid, iEvent.queue(), sizes);
      if (deviceHits_) {
        fillFromDeviceInputs(iEvent, iSetup, product, *rawRef, nPix, nStr, axes);
        otReleaseGuard(iEvent);
        if (compareHostHits_)
          compareHits(iEvent, product, [&](::mkfitdev::HitSoA::View hv, ::mkfitdev::LayerSoA::View lv) { fill(hv, lv); });
      } else if constexpr (std::is_same_v<Device, alpaka::DevCpu>) {
        fill(product.view().hits(), product.view().layers());
      } else {
        // stage the hits and layers blocks in a host product of the same shape; copy only the bytes up to the
        // binnedHits block (blocks are laid out in declaration order: hits, layers, binnedHits, bins)
        ::mkfitdev::EventOfHitsHostCollection staging(iEvent.queue(), sizes);
        fill(staging.view().hits(), staging.view().layers());
        const auto* base = reinterpret_cast<const std::byte*>(staging.buffer().data());
        const auto* bh = reinterpret_cast<const std::byte*>(staging.view().binnedHits().metadata().addressOf_rank());
        const auto nBytes = static_cast<uint32_t>(bh - base);
        alpaka::memcpy(iEvent.queue(), product.buffer(), staging.buffer(), alpaka::Vec<alpaka::DimInt<1u>, uint32_t>{nBytes});
      }
      if (!mkfitdev::hits::runBuildEventOfHits(iEvent.queue(), product, deads)) {
        skipEvent(iEvent, nHits, nLayers);
        return;
      }
      if (compare_)
        compare(iEvent, product, axes, iEvent.get(stockToken_).get());
      iEvent.emplace(productToken_, std::move(product));
      putQueue(iEvent);
    }

  private:
    // stock convertHits row count: max(last hit's cluster key + 1, dataSize()), 0 for an empty collection
    template <typename C>
    static uint32_t stockRows(C const& hits) {
      if (hits.empty())
        return 0;
      return std::max<uint32_t>(hits.data().back().firstClusterRef().index() + 1, hits.dataSize());
    }

    // host part of the device hit input: pixel rows = SoA row of each legacy cluster key (moduleStart[det] +
    // originalId; persisted clusters have no originalId, then the SoA row of the module with the bitwise-equal local
    // position, as the legacy rechit position is a copy of the SoA one) + legacy sizes; OT rows = raw rechit values
    static void fillRaw(::reco::TrackingRecHitHost const& soa,
                        SiPixelRecHitCollection const& pixRH,
                        Phase2TrackerRecHit1DCollectionNew const* otRH,  // nullptr: OT rows from the device SoA
                        edm::Handle<SiPixelClusterCollectionNew> const& pixCluH,
                        edm::Handle<Phase2TrackerCluster1DCollectionNew> const& otCluH,
                        uint32_t nPix,
                        uint32_t n,  // rows in use (nPix + nStr); the collection may be larger (per-stream scratch)
                        ::mkfitdev::DeviceHitRawSoA::View rv,
                        RawGuard& guard) {
      const auto& pixClu = *pixCluH;
      const auto& otClu = *otCluH;
      uint32_t* src = rv.metadata().addressOf_srcRow();
      int32_t* mod = rv.metadata().addressOf_module();
      for (uint32_t i = 0; i < nPix; ++i)
        src[i] = ::mkfitdev::kNoHit;
      for (uint32_t i = nPix; i < n; ++i)
        mod[i] = -1;
      const auto hv = soa.const_view().trackingHits();
      const auto mv = soa.const_view().hitModules();
      const uint32_t nSoa = soa.nHits();
      uint32_t* spans = rv.metadata().addressOf_spans();
      for (const auto& ds : pixRH) {
        if (ds.empty())
          continue;
        const uint32_t gind = ds.begin()->det()->index();
        const uint32_t b = mv[gind].moduleStart(), e = mv[gind + 1].moduleStart();
        for (const auto& h : ds) {
          const auto ref = h.firstClusterRef();
          if (ref.id() != pixCluH.id())  // stock reads the cluster by key from the collection of the hits' refs
            throw cms::Exception("MkFitAlpakaEventOfHits") << "pixelClusters is not the collection of the rechits";
          const uint32_t k = ref.index();
          const auto& clu = pixClu.data()[k];
          uint32_t j = ::mkfitdev::kNoHit;
          const float lx = h.localPosition().x(), ly = h.localPosition().y();
          if (clu.originalId() != SiPixelCluster::invalidClusterId) {
            j = b + clu.originalId();
            // R6-M3: the SoA row of originalId must carry the legacy local position (bitwise: the legacy one is a copy)
            if (!(j < e && j < nSoa && hv[j].xLocal() == lx && hv[j].yLocal() == ly)) {
              ++guard.keyMismatch;
              j = ::mkfitdev::kNoHit;
            }
          } else {
            ++guard.positionSearch;
          }
          if (j == ::mkfitdev::kNoHit) {
            for (uint32_t r = b; r < e && r < nSoa; ++r)
              if (hv[r].xLocal() == lx && hv[r].yLocal() == ly) {
                j = r;
                break;
              }
          }
          if (j >= nSoa || hv[j].detectorIndex() != gind)
            throw cms::Exception("MkFitAlpakaEventOfHits")
                << "pixel cluster key " << k << " (det index " << gind << ") has no row in the pixel rechit SoA";
          src[k] = j;
          spans[k] = uint32_t(clu.sizeX()) | (uint32_t(clu.sizeY()) << 16);
        }
      }
      float* lx = rv.metadata().addressOf_lx();
      float* ly = rv.metadata().addressOf_ly();
      float* exx = rv.metadata().addressOf_exx();
      float* exy = rv.metadata().addressOf_exy();
      float* eyy = rv.metadata().addressOf_eyy();
      if (otRH == nullptr)
        return;
      for (const auto& ds : *otRH) {
        if (ds.empty())
          continue;
        const int32_t gind = ds.begin()->det()->index();
        for (const auto& h : ds) {
          const auto ref = h.firstClusterRef();
          if (ref.id() != otCluH.id())
            throw cms::Exception("MkFitAlpakaEventOfHits") << "otClusters is not the collection of the rechits";
          const uint32_t i = nPix + ref.index();
          const auto lp = h.localPosition();
          const auto le = h.localPositionError();
          mod[i] = gind;
          lx[i] = lp.x();
          ly[i] = lp.y();
          exx[i] = le.xx();
          exy[i] = le.xy();
          eyy[i] = le.yy();
          spans[i] = otClu.data()[ref.index()].size();
        }
      }
    }

    // per-IOV module table (rotation, position, mkFit layer, uniqueIdInLayer per GeomDet index); immutable once built,
    // shared by the streams through a shared_ptr (no data race on an IOV change)
    std::shared_ptr<const std::vector<::mkfitdev::HitModuleDev>> hitTable(device::EventSetup const& iSetup) const {
      edm::EventSetup const& es = iSetup;
      const auto id = es.get<TrackerRecoGeometryRecord>().cacheIdentifier();
      std::lock_guard<std::mutex> lock(tableMutex_);
      if (table_ && tableId_ == id)
        return table_;
      auto t = std::make_shared<const std::vector<::mkfitdev::HitModuleDev>>(
          ::mkfitdev::buildHitModuleTable(iSetup.getData(hitGeomToken_), iSetup.getData(mkFitGeomToken_)));
      table_ = std::move(t);
      tableId_ = id;
      return table_;
    }

    // Device copy of the per-IOV module table, one per (device, host table): made once (one copy + wait per IOV and
    // device), then shared by every event and stream on that device (as the build module's per-IOV engine tables).
    // Replaces the per-event 1.7 MB host-to-device copy of round 5 (round 6, lane gpu).
    const ::mkfitdev::HitModuleDev* deviceTable(Queue& queue,
                                                std::shared_ptr<const std::vector<::mkfitdev::HitModuleDev>> const& t) const {
      const auto key = std::make_pair(static_cast<long>(alpaka::getNativeHandle(alpaka::getDev(queue))),
                                      reinterpret_cast<uintptr_t>(t.get()));
      std::lock_guard<std::mutex> lock(tableMutex_);
      auto it = devTables_.find(key);
      if (it == devTables_.end()) {
        const uint32_t n = std::max<size_t>(t->size(), 1);
        auto e = std::make_unique<DeviceTable>(
            DeviceTable{t, cms::alpakatools::make_device_buffer<::mkfitdev::HitModuleDev[]>(alpaka::getDev(queue), n)});
        if (!t->empty())
          alpaka::memcpy(queue, e->buf, cms::alpakatools::make_host_view(const_cast<::mkfitdev::HitModuleDev*>(t->data()), t->size()));
        alpaka::wait(queue);  // once per IOV and device: other queues use the table afterwards
        // tables of earlier IOVs stay alive until the end of the job (events of other streams may still use them)
        it = devTables_.emplace(key, std::move(e)).first;
      }
      return it->second->buf.data();
    }

    // R6-M3: counters per job; the first event with a guard hit warns; compareHostHits (validation) throws
    void checkGuard(unsigned long long evt, RawGuard const& g) const {
      if (g.keyMismatch == 0 && g.positionSearch == 0)
        return;
      if (compareHostHits_ && g.keyMismatch != 0)
        throw cms::Exception("MkFitAlpakaEventOfHits")
            << "event " << evt << ": " << g.keyMismatch
            << " pixel clusters whose originalId row has another local position in the pixel rechit SoA";
      nKeyMismatch_ += g.keyMismatch;
      nPositionSearch_ += g.positionSearch;
      if (!warned_.exchange(true))
        edm::LogWarning("MkFitAlpakaEventOfHits")
            << "event " << evt << ": device hit input found " << g.keyMismatch + g.positionSearch
            << " pixel rows by position (" << g.keyMismatch << " originalId mismatches, " << g.positionSearch
            << " clusters without originalId); totals at endJob (DEVICE_HITS_GUARD)";
    }

    static void setRaw(mkfitdev::hits::DeviceHitInputs& in, ::mkfitdev::DeviceHitRawSoA::ConstView v) {
      const auto m = v.metadata();
      in.srcRow = m.addressOf_srcRow();
      in.module = m.addressOf_module();
      in.spans = m.addressOf_spans();
      in.lx = m.addressOf_lx();
      in.ly = m.addressOf_ly();
      in.exx = m.addressOf_exx();
      in.exy = m.addressOf_exy();
      in.eyy = m.addressOf_eyy();
    }

    // stage D: OT rows straight from the device OT rechit SoA (device memory; row = OT cluster key)
    void setOT(device::Event& iEvent, mkfitdev::hits::DeviceHitInputs& in) const {
      if (!useOTSoA_)
        return;
      const auto m = iEvent.get(otSoAToken_).const_view().metadata();
      if (static_cast<uint32_t>(m.size()) < in.nStrip)
        throw cms::Exception("MkFitAlpakaEventOfHits") << "OT rechit SoA has fewer rows than OT clusters";
      in.otModule = m.addressOf_module();
      in.otSize = m.addressOf_clustSize();
      in.otLx = m.addressOf_lx();
      in.otLy = m.addressOf_ly();
      in.otExx = m.addressOf_exx();
      in.otEyy = m.addressOf_eyy();
    }

    template <typename T>
    static auto hostView(const T* p, uint32_t n) {
      return cms::alpakatools::make_host_view(const_cast<T*>(p), n);
    }

    void fillFromDeviceInputs(device::Event& iEvent,
                              device::EventSetup const& iSetup,
                              mkfitdev::EventOfHitsDeviceCollection& product,
                              ::mkfitdev::DeviceHitRawHostCollection const& raw,
                              uint32_t nPix,
                              uint32_t nStr,
                              std::vector<::mkfitdev::LayerAxisInput> const& axes) const {
      auto& queue = iEvent.queue();
      // per-module table: ES product (moduleTable set) or the module's own per-IOV table (+ per-device copy)
      std::shared_ptr<const std::vector<::mkfitdev::HitModuleDev>> table;
      const ::mkfitdev::HitModuleDev* esTable = nullptr;
      if (moduleTableES_)
        esTable = iSetup.getData(moduleTableToken_).data();
      else
        table = hitTable(iSetup);
      const auto& soa = iEvent.get(pixelSoAToken_);
      const auto soaView = soa.const_view();
      const auto hitsView = soaView.trackingHits();
      const auto hm = hitsView.metadata();  // refers to hitsView: keep the view alive
      const uint32_t nSoa = soa.nHits();
      mkfitdev::hits::DeviceHitInputs in{};
      in.nPixel = nPix;
      in.nStrip = nStr;
      if constexpr (std::is_same_v<Device, alpaka::DevCpu>) {
        in.xLocal = hm.addressOf_xLocal();
        in.yLocal = hm.addressOf_yLocal();
        in.xerrLocal = hm.addressOf_xerrLocal();
        in.yerrLocal = hm.addressOf_yerrLocal();
        in.detectorIndex = hm.addressOf_detectorIndex();
        in.modules = esTable ? esTable : table->data();
        setRaw(in, raw.const_view());
        setOT(iEvent, in);
        ::mkfitdev::fillLayers(axes, nPix, product.view().layers());
        mkfitdev::hits::runFillHitsFromDeviceInputs(queue, in, product.view().hits());
      } else if (pixelSoAOnDevice_) {
        const auto& soaD = iEvent.get(pixelSoADevToken_);
        const auto dView = soaD.const_view();
        const auto dHits = dView.trackingHits();
        const auto dm = dHits.metadata();
        if (static_cast<uint32_t>(dm.size()) != nSoa)
          throw cms::Exception("MkFitAlpakaEventOfHits") << "device / host pixel rechit SoA sizes differ";
        mkfitdev::hits::DeviceHitRawDeviceCollection rawD(queue, raw.view().metadata().size());
        alpaka::memcpy(queue, rawD.buffer(), raw.buffer());
        in.xLocal = dm.addressOf_xLocal();
        in.yLocal = dm.addressOf_yLocal();
        in.xerrLocal = dm.addressOf_xerrLocal();
        in.yerrLocal = dm.addressOf_yerrLocal();
        in.detectorIndex = dm.addressOf_detectorIndex();
        in.modules = esTable ? esTable : deviceTable(queue, table);  // no per-event copy (round 6, lane gpu)
        setRaw(in, rawD.const_view());
        setOT(iEvent, in);
        copyLayers(queue, product, nPix, axes);
        mkfitdev::hits::runFillHitsFromDeviceInputs(queue, in, product.view().hits());
      } else {
        // pixel SoA columns and raw rows to the device; the module table is copied once per (device, IOV)
        auto dX = cms::alpakatools::make_device_buffer<float[]>(queue, nSoa);
        auto dY = cms::alpakatools::make_device_buffer<float[]>(queue, nSoa);
        auto dEX = cms::alpakatools::make_device_buffer<float[]>(queue, nSoa);
        auto dEY = cms::alpakatools::make_device_buffer<float[]>(queue, nSoa);
        auto dDet = cms::alpakatools::make_device_buffer<uint16_t[]>(queue, nSoa);
        alpaka::memcpy(queue, dX, hostView(hm.addressOf_xLocal(), nSoa));
        alpaka::memcpy(queue, dY, hostView(hm.addressOf_yLocal(), nSoa));
        alpaka::memcpy(queue, dEX, hostView(hm.addressOf_xerrLocal(), nSoa));
        alpaka::memcpy(queue, dEY, hostView(hm.addressOf_yerrLocal(), nSoa));
        alpaka::memcpy(queue, dDet, hostView(hm.addressOf_detectorIndex(), nSoa));
        mkfitdev::hits::DeviceHitRawDeviceCollection rawD(queue, raw.view().metadata().size());
        alpaka::memcpy(queue, rawD.buffer(), raw.buffer());
        in.xLocal = dX.data();
        in.yLocal = dY.data();
        in.xerrLocal = dEX.data();
        in.yerrLocal = dEY.data();
        in.detectorIndex = dDet.data();
        in.modules = esTable ? esTable : deviceTable(queue, table);
        setRaw(in, rawD.const_view());
        setOT(iEvent, in);
        copyLayers(queue, product, nPix, axes);
        mkfitdev::hits::runFillHitsFromDeviceInputs(queue, in, product.view().hits());
        // the temporaries above are queue-ordered (caching allocator); pageable sources are staged by the copy call
      }
    }

    // layers block: filled on the host, copied alone (blocks in declaration order: hits, layers, binnedHits, bins)
    static void copyLayers(Queue& queue,
                           mkfitdev::EventOfHitsDeviceCollection& product,
                           uint32_t nPix,
                           std::vector<::mkfitdev::LayerAxisInput> const& axes) {
      auto pView = product.view();
      const std::array<int32_t, 4> sizes{{pView.hits().metadata().size(),
                                          pView.layers().metadata().size(),
                                          pView.binnedHits().metadata().size(),
                                          pView.bins().metadata().size()}};
      ::mkfitdev::EventOfHitsHostCollection staging(queue, sizes);
      auto sView = staging.view();
      auto sLayers = sView.layers();
      ::mkfitdev::fillLayers(axes, nPix, sLayers);
      auto sBinned = sView.binnedHits();
      auto pLayers = pView.layers();
      auto* sl = reinterpret_cast<std::byte*>(sLayers.metadata().addressOf_qRMin());
      auto* sb = reinterpret_cast<std::byte*>(sBinned.metadata().addressOf_rank());
      auto* dl = reinterpret_cast<std::byte*>(pLayers.metadata().addressOf_qRMin());
      const uint32_t nBytes = static_cast<uint32_t>(sb - sl);
      alpaka::memcpy(queue,
                     cms::alpakatools::make_device_view(alpaka::getDev(queue), dl, nBytes),
                     cms::alpakatools::make_host_view(sl, nBytes));
    }

    // validation: device-made HitSoA vs the host-input HitSoA (stock wrappers + fillHits), column by column, bitwise
    template <typename F>
    void compareHits(device::Event& iEvent, mkfitdev::EventOfHitsDeviceCollection const& product, F&& fill) const {
      auto& queue = iEvent.queue();
      const auto dv = product.const_view();
      const std::array<int32_t, 4> sizes{{dv.hits().metadata().size(),
                                          dv.layers().metadata().size(),
                                          dv.binnedHits().metadata().size(),
                                          dv.bins().metadata().size()}};
      ::mkfitdev::EventOfHitsHostCollection dev(queue, sizes), host(queue, sizes);
      alpaka::memcpy(queue, dev.buffer(), product.buffer());
      fill(host.view().hits(), host.view().layers());
      alpaka::wait(queue);
      const auto a = dev.const_view().hits(), b = host.const_view().hits();
      const uint32_t n = sizes[0];
      uint64_t mis[12] = {};
      auto neq = [](float x, float y) { return std::memcmp(&x, &y, sizeof(float)) != 0; };
      for (uint32_t i = 0; i < n; ++i) {
        mis[0] += neq(a[i].x(), b[i].x());
        mis[1] += neq(a[i].y(), b[i].y());
        mis[2] += neq(a[i].z(), b[i].z());
        mis[3] += neq(a[i].e00(), b[i].e00());
        mis[4] += neq(a[i].e10(), b[i].e10());
        mis[5] += neq(a[i].e11(), b[i].e11());
        mis[6] += neq(a[i].e20(), b[i].e20());
        mis[7] += neq(a[i].e21(), b[i].e21());
        mis[8] += neq(a[i].e22(), b[i].e22());
        mis[9] += a[i].packed() != b[i].packed();
        mis[10] += a[i].layer() != b[i].layer();
        mis[11] += a[i].origIdx() != b[i].origIdx();
      }
      const bool scal = a.nPixel() == b.nPixel() && a.nStrip() == b.nStrip();
      uint64_t tot = 0;
      for (auto m : mis)
        tot += m;
      edm::LogPrint("MkFitAlpakaEventOfHits")
          << "DEVICE_HITS_EOH " << EDM_STRINGIZE(ALPAKA_ACCELERATOR_NAMESPACE) << " event " << iEvent.id().event()
          << " rows " << n << " nPixel " << a.nPixel() << " nStrip " << a.nStrip() << " scalarsEqual " << scal
          << " mismatches x,y,z " << mis[0] << "," << mis[1] << "," << mis[2] << " err " << mis[3] << "," << mis[4]
          << "," << mis[5] << "," << mis[6] << "," << mis[7] << "," << mis[8] << " packed " << mis[9] << " layer "
          << mis[10] << " origIdx " << mis[11] << (tot == 0 && scal ? " IDENTICAL" : " DIFFERENT");
    }

    // Static layer table inputs from the ES product (filled from the same stock LayerOfHits::Initializator per layer,
    // esFill.cc), so the per-event TrackerInfo walk goes away. Identical to layerAxisInputs(TrackerInfo).
    static std::vector<::mkfitdev::LayerAxisInput> layerAxisInputsES(::mkfitdev::ESDataHost const& es) {
      const auto lv = es.layers->const_view();
      std::vector<::mkfitdev::LayerAxisInput> v;
      v.reserve(lv.metadata().size());
      for (int l = 0; l < lv.metadata().size(); ++l)
        v.push_back({lv[l].q_min(),
                     lv[l].q_max(),
                     lv[l].n_q(),
                     lv[l].layer_type() == static_cast<int>(::mkfitdev::LayerType::Barrel),
                     lv[l].is_pixel()});
      return v;
    }

    // M7: an event beyond the device build limits is not built (no exception at event time); downstream device
    // modules must treat a missing/empty EventOfHits as "skip mkFit for this event".
    // R4-M1: the product is always put; a skipped event gets the empty one (nLayers == 0).
    void skipEvent(device::Event& iEvent, uint32_t nHits, uint32_t nLayers) const {
      edm::LogWarning("MkFitAlpakaEventOfHits")
          << "event " << iEvent.id().event() << ": device EventOfHits not built (" << nHits << " hits, " << nLayers
          << " layers exceed the device build limits); mkFit is skipped for this event";
      const std::array<int32_t, 4> empty{{0, 0, 0, 0}};
      iEvent.emplace(productToken_, mkfitdev::EventOfHitsDeviceCollection(iEvent.queue(), empty));
      putQueue(iEvent);
    }

    // R7-H1: CPU backends take the product's memory from the per-stream pool (scratchReuse >= 1); GPU backends (and
    // scratchReuse 0) allocate per event (GPU: caching allocator)
    mkfitdev::EventOfHitsDeviceCollection makeProduct(edm::StreamID sid,
                                                      Queue& queue,
                                                      std::array<int32_t, 4> const& sizes) const {
#if defined(ALPAKA_ACC_CPU_B_SEQ_T_SEQ_ENABLED) || defined(ALPAKA_ACC_CPU_B_TBB_T_SEQ_ENABLED)
      if (scratchReuse_) {
        EohScratch& sc = *streamCache(sid);
        const std::size_t n = ::mkfitdev::EventOfHitsPooledHostCollection::bytes(sizes);
        for (uint64_t m = productMaxBytes_.load(); n > m && !productMaxBytes_.compare_exchange_weak(m, n);) {
        }
        if (!sc.product || n > sc.productCap) {
          sc.productCap = ((n + n / 4 + 65535) / 65536) * 65536;
          sc.product.reset();
          sc.product.emplace(
              cms::alpakatools::make_host_buffer<std::byte[]>(queue, static_cast<alpaka_common::Idx>(sc.productCap)));
          ++sc.nProductAllocs;
        }
        return mkfitdev::EventOfHitsDeviceCollection(*sc.product, sizes);
      }
#endif
      return mkfitdev::EventOfHitsDeviceCollection(queue, sizes);
    }

    // D9-f: the OT rechit SoA is deleted early, after this module. Its block goes back to the caching allocator with a
    // marker on the queue it was allocated on (otSoAQueue). If that is not this module's queue, make it wait on the
    // device for this module's work so far (the last read of the SoA is the hit fill kernel enqueued just before).
    void otReleaseGuard(device::Event& iEvent) const {
      if (otSoAQueueToken_.isUninitialized())
        return;
#if defined(ALPAKA_ACC_GPU_CUDA_ENABLED) || defined(ALPAKA_ACC_GPU_HIP_ENABLED)
      auto& queue = iEvent.queue();
      const auto otQueue = iEvent.get(otSoAQueueToken_);
      if (otQueue == reinterpret_cast<unsigned long long>(alpaka::getNativeHandle(queue))) {
        ++nOtSameQueue_;
        return;
      }
      alpaka::Event<Queue> done(alpaka::getDev(queue));
      alpaka::enqueue(queue, done);
#if defined(ALPAKA_ACC_GPU_CUDA_ENABLED)
      const auto rc = cudaStreamWaitEvent(reinterpret_cast<cudaStream_t>(otQueue), alpaka::getNativeHandle(done), 0);
      if (rc != cudaSuccess)
        throw cms::Exception("MkFitAlpakaEventOfHits") << "cudaStreamWaitEvent: " << cudaGetErrorString(rc);
#else
      const auto rc = hipStreamWaitEvent(reinterpret_cast<hipStream_t>(otQueue), alpaka::getNativeHandle(done), 0);
      if (rc != hipSuccess)
        throw cms::Exception("MkFitAlpakaEventOfHits") << "hipStreamWaitEvent: " << hipGetErrorString(rc);
#endif
      ++nOtOrdered_;
#else
      ++nOtSameQueue_;  // CPU backends: blocking queue, the read is done
      (void)iEvent;
#endif
    }

    // D7-f: the native handle of the queue of the EventOfHits allocation (0 on CPU backends: blocking queue)
    void putQueue(device::Event& iEvent) const {
      unsigned long long h = 0;
#if !(defined(ALPAKA_ACC_CPU_B_SEQ_T_SEQ_ENABLED) || defined(ALPAKA_ACC_CPU_B_TBB_T_SEQ_ENABLED))
      h = reinterpret_cast<unsigned long long>(alpaka::getNativeHandle(iEvent.queue()));
#endif
      iEvent.emplace(queueToken_, h);
    }

    void compare(device::Event& iEvent,
                 mkfitdev::EventOfHitsDeviceCollection const& d,
                 std::vector<::mkfitdev::LayerAxisInput> const& axes,
                 mkfit::EventOfHits const& eoh) const {
      auto& queue = iEvent.queue();
      const uint32_t nL = axes.size();
      const auto dv = d.const_view();
      const std::array<int32_t, 4> sizes{{dv.hits().metadata().size(),
                                          dv.layers().metadata().size(),
                                          dv.binnedHits().metadata().size(),
                                          dv.bins().metadata().size()}};
      ::mkfitdev::EventOfHitsHostCollection h(queue, sizes);
      alpaka::memcpy(queue, h.buffer(), d.buffer());
      alpaka::wait(queue);
      const auto lo = h.const_view().layers();
      const auto bo = h.const_view().binnedHits();
      const auto bn = h.const_view().bins();
      uint64_t nHits = 0, orderMis = 0, infoMis = 0, binMis = 0, deadMis = 0, countMis = 0;
      for (uint32_t il = 0; il < nL; ++il) {
        const auto& loh = eoh[il];
        const uint32_t n = loh.nHits();
        nHits += n;
        if (lo[il].nHits() != n) {
          ++countMis;
          continue;
        }
        const uint32_t hb = lo[il].hitBegin(), bb = lo[il].binBegin();
        for (uint32_t i = 0; i < n; ++i) {
          auto r = bo[hb + i];
          if (r.rank() != loh.getOriginalHitIndex(i)) {
            ++orderMis;
            continue;
          }
          const auto& hi = loh.hit_info(i);
          const float dv[4] = {r.phi(), r.q(), r.qHalfLength(), r.qbar()};
          const float sv[4] = {hi.phi, hi.q, hi.q_half_length, hi.qbar};
          infoMis += std::memcmp(dv, sv, sizeof(dv)) != 0;
        }
        for (uint32_t qb = 0; qb < axes[il].nq; ++qb)
          for (uint32_t pb = 0; pb < ::mkfitdev::kNPhiBins; ++pb) {
            auto c = loh.phiQBinContent(pb, qb);
            auto row = bn[bb + qb * ::mkfitdev::kNPhiBins + pb];
            binMis += row.content() != (uint32_t(c.first) | (uint32_t(c.count) << ::mkfitdev::kBinFirstBits));
            deadMis += (row.dead() != 0) != loh.isBinDead(pb, qb);
          }
      }
      edm::LogPrint("MkFitAlpakaEventOfHits")
          << "EOH_COMPARE " << EDM_STRINGIZE(ALPAKA_ACCELERATOR_NAMESPACE) << " event " << iEvent.id().event()
          << " hits " << nHits << " layerCountMismatch " << countMis << " orderMismatch " << orderMis
          << " hitInfoNotIdentical " << infoMis << " binMismatch " << binMis << " deadMismatch " << deadMis
          << " overflow " << lo.nOverflowFirst() + lo.nOverflowCount();
    }

    edm::EDGetTokenT<MkFitHitWrapper> pixelHitsToken_;
    edm::EDGetTokenT<MkFitHitWrapper> stripHitsToken_;
    edm::EDGetTokenT<std::vector<int>> pixelLayerToken_;
    edm::EDGetTokenT<std::vector<int>> stripLayerToken_;
    edm::EDGetTokenT<::reco::TrackingRecHitHost> pixelSoAToken_;
    edm::EDGetTokenT<SiPixelRecHitCollection> pixelRecHitsToken_;
    edm::EDGetTokenT<Phase2TrackerRecHit1DCollectionNew> otRecHitsToken_;
    device::EDGetToken<mkfitdev::OTRecHitDeviceCollection> otSoAToken_;
    edm::EDGetTokenT<unsigned long long> otSoAQueueToken_;               // D9-f
    edm::EDGetTokenT<::mkfitdev::OTRecHitHostCollection> otSoAHostToken_;  // D9-f, consumed only
    mutable std::atomic<uint64_t> nOtSameQueue_{0}, nOtOrdered_{0};
    bool useOTSoA_ = false;
    edm::EDGetTokenT<SiPixelClusterCollectionNew> pixelClustersToken_;
    edm::EDGetTokenT<Phase2TrackerCluster1DCollectionNew> otClustersToken_;
    edm::ESGetToken<TrackerGeometry, TrackerDigiGeometryRecord> hitGeomToken_;
    device::EDGetToken<ALPAKA_ACCELERATOR_NAMESPACE::reco::TrackingRecHitsSoACollection> pixelSoADevToken_;
    const edm::ESGetToken<MkFitGeometry, TrackerRecoGeometryRecord> mkFitGeomToken_;
    const device::EDPutToken<mkfitdev::EventOfHitsDeviceCollection> productToken_;
    const edm::EDPutTokenT<unsigned long long> queueToken_;
    edm::ESGetToken<SiPixelQuality, SiPixelQualityRcd> pixelQualityToken_;
    edm::ESGetToken<TrackerGeometry, TrackerDigiGeometryRecord> geomToken_;
    edm::EDGetTokenT<MkFitEventOfHits> stockToken_;
    const bool usePixelQualityDB_;
    const bool compare_;
    const unsigned int testSkipModulo_;
    const bool useESLayers_;
    const bool deviceHits_;
    const bool compareHostHits_;
    const bool pixelSoAOnDevice_;
    const bool scratchReuse_;
    mutable std::atomic<uint64_t> nKeyMismatch_{0}, nPositionSearch_{0};
    mutable std::atomic<bool> warned_{false};
    mutable std::atomic<uint64_t> productAllocs_{0}, productMaxBytes_{0};
    edm::ESGetToken<::mkfitdev::ESDataHost, TrackerRecoGeometryRecord> esHostToken_;
    mutable std::mutex tableMutex_;
    mutable std::shared_ptr<const std::vector<::mkfitdev::HitModuleDev>> table_;
    mutable unsigned long long tableId_ = 0;
    struct DeviceTable {
      std::shared_ptr<const std::vector<::mkfitdev::HitModuleDev>> host;  // keeps the key pointer unique
      cms::alpakatools::device_buffer<Device, ::mkfitdev::HitModuleDev[]> buf;
    };
    mutable std::map<std::pair<long, uintptr_t>, std::unique_ptr<DeviceTable>> devTables_;
    device::ESGetToken<::mkfitdev::HitModuleTableESData<Device>, TrackerRecoGeometryRecord> moduleTableToken_;
    bool moduleTableES_ = false;
  };

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE

DEFINE_FWK_ALPAKA_MODULE(MkFitAlpakaEventOfHitsProducer);
