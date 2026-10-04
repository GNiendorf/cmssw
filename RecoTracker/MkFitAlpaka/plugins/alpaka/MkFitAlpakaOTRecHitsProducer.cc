// Stage D (round 8, lane otdev; doc/otdev.txt): the Phase-2 OT rechits on the device. Replaces the host
// Phase2TrackerRecHits producer (hltSiPhase2RecHits) for every reader that can take the SoA: one host pass over the
// OT clusters writes (GeomDet index, DetId, size, first strip | column) per cluster key into the SoA's prefix columns,
// one kernel (MkFitAlpakaOTRecHitsKernels.dev.cc) applies Phase2StripCPE (pitch, centre, the per-module Lorentz shift
// and pitch^2/12 errors from a per-IOV table) and Surface::toGlobal. The per-IOV table is built from the stock objects:
// coveredStrips through Phase2StripCPE::driftDirection (SiPhase2OuterTrackerLorentzAngle x the local B field of the
// module, exactly the stock fillParam), errors through Phase2StripCPE::localParameters, topology constants from the
// RectangularPixelPhase2Topology accessors.
// produceCAHits (stage D, replaces Phase2OTRecHitsSoAConverter = hltPhase2OtRecHitsSoA): also puts the CA OT layers'
// hit SoA (P-module hits of the OT barrel, reco::TrackingRecHitsSoACollection) and the host hitModuleStart vector, made
// by a selection kernel from the OT rechit SoA; compareCATo compares them bitwise with the stock converter's products.
// compareTo (validation): copies the SoA back and compares every row bitwise with the legacy Phase2TrackerRecHits
// (cluster key, module, local position, local error, global position; per contraction variant), OTDEV_CMP lines.
// Round 9 (lane mem, D9-f): instance "queue" = a host product with the native handle of the queue the OT rechit SoA was
// allocated on (0 on CPU backends). With the SoA deleted early (after hltMkFitEventOfHits), its last device reader
// orders that queue after its own reads (doc/mem.txt).
#include <array>
#include <atomic>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <type_traits>
#include <vector>

#include "DataFormats/Common/interface/DetSetVectorNew.h"
#include "DataFormats/DetId/interface/DetId.h"
#include "DataFormats/Phase2TrackerCluster/interface/Phase2TrackerCluster1D.h"
#include "DataFormats/TrackerRecHit2D/interface/Phase2TrackerRecHit1D.h"
#include "FWCore/MessageLogger/interface/MessageLogger.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include "FWCore/Utilities/interface/InputTag.h"
#include "FWCore/Utilities/interface/ESInputTag.h"
#include "Geometry/CommonTopologies/interface/GeomDetEnumerators.h"
#include "Geometry/CommonTopologies/interface/PixelGeomDetUnit.h"
#include "Geometry/CommonTopologies/interface/ProxyPixelTopology.h"
#include "Geometry/Records/interface/TrackerDigiGeometryRecord.h"
#include "Geometry/TrackerGeometryBuilder/interface/RectangularPixelPhase2Topology.h"
#include "Geometry/TrackerGeometryBuilder/interface/TrackerGeometry.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/EDPutToken.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/Event.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/EventSetup.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/MakerMacros.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/global/EDProducer.h"
#include "HeterogeneousCore/AlpakaInterface/interface/config.h"
#include "HeterogeneousCore/AlpakaInterface/interface/memory.h"
#include "RecoLocalTracker/Phase2TrackerRecHits/interface/Phase2StripCPE.h"
#include "RecoLocalTracker/Records/interface/TkPhase2OTCPERecord.h"
#include "RecoTracker/MkFitAlpaka/interface/othits/OTCpe.h"
#include "RecoTracker/MkFitAlpaka/interface/othits/OTRecHitSoA.h"
#include "RecoTracker/MkFitAlpaka/interface/othits/alpaka/OTRecHitDeviceCollection.h"

#include "DataFormats/BeamSpot/interface/BeamSpot.h"
#include "DataFormats/SiStripDetId/interface/StripSubdetector.h"
#include "DataFormats/SiPixelDetId/interface/PixelSubdetector.h"
#include "DataFormats/TrackingRecHitSoA/interface/TrackingRecHitsHost.h"
#include "DataFormats/TrackingRecHitSoA/interface/alpaka/TrackingRecHitsSoACollection.h"
#include "MkFitAlpakaOTCAHitsKernels.h"
#include "MkFitAlpakaOTRecHitsKernels.h"

namespace ALPAKA_ACCELERATOR_NAMESPACE {

  namespace {
    struct OTCpeTable {
      std::vector<::mkfitdev::OTCpeModule> modules;  // index = GeomDet index - firstIndex
      int32_t firstIndex = 0;
      // CA OT layers (Phase2OTRecHitsSoAConverter::beginRun): P modules of the OT barrel in detUnits order
      std::vector<int32_t> pOffset;  // index = GeomDet index - firstIndex; -1: not a P module of the OT barrel
      uint32_t nP = 0;
      uint16_t modulesInPixel = 0;
    };

    // validation counters (compareTo)
    // per non-empty cluster detset: first cluster key, size, table index (GeomDet index - firstIndex)
    struct DetSetSpan {
      uint32_t first, size;
      int32_t mi;
    };

    struct CmpCounts {
      uint64_t rows = 0, keyMis = 0, moduleMis = 0, detIdMis = 0, sizeMis = 0, lxMis = 0, lyMis = 0, errMis = 0,
               gMis = 0;
      std::array<uint64_t, 3> localVar{{0, 0, 0}}, globalVar{{0, 0, 0}};  // per Contract variant, host recompute
    };
  }  // namespace

  class MkFitAlpakaOTRecHitsProducer : public global::EDProducer<> {
  public:
    explicit MkFitAlpakaOTRecHitsProducer(edm::ParameterSet const& iConfig)
        : EDProducer<>(iConfig),
          clustersToken_{consumes(iConfig.getParameter<edm::InputTag>("src"))},
          geomToken_{esConsumes()},
          cpeToken_{esConsumes(iConfig.getParameter<edm::ESInputTag>("Phase2StripCPE"))},
          putToken_{produces()},
          queuePutToken_{produces("queue")},
          contractLocal_{iConfig.getParameter<int>("contractLocal")},
          contractGlobal_{iConfig.getParameter<int>("contractGlobal")},
          compare_{!iConfig.getParameter<edm::InputTag>("compareTo").label().empty()},
          caHits_{iConfig.getParameter<bool>("produceCAHits")},
          contractCA_{iConfig.getParameter<int>("contractCA")},
          compareCA_{!iConfig.getParameter<edm::InputTag>("compareCATo").label().empty()} {
      if (compare_)
        legacyToken_ = consumes(iConfig.getParameter<edm::InputTag>("compareTo"));
      if (caHits_) {
        beamSpotToken_ = consumes(iConfig.getParameter<edm::InputTag>("beamSpot"));
        pixelSoAToken_ = consumes(iConfig.getParameter<edm::InputTag>("pixelRecHitSoASource"));
        caPutToken_ = produces();
        hmsPutToken_ = produces();
        if (compareCA_) {
          stockCAToken_ = consumes(iConfig.getParameter<edm::InputTag>("compareCATo"));
          stockHMSToken_ = consumes(iConfig.getParameter<edm::InputTag>("compareCATo"));
        }
      }
    }

    static void fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
      edm::ParameterSetDescription desc;
      desc.add<edm::InputTag>("src", edm::InputTag("hltSiPhase2Clusters"))->setComment("Phase-2 OT clusters");
      desc.add<edm::ESInputTag>("Phase2StripCPE", edm::ESInputTag("phase2StripCPEESProducer", "Phase2StripCPE"))
          ->setComment("the stock Phase2StripCPE (as hltSiPhase2RecHits); its per-module numbers fill the table");
      desc.add<int>("contractLocal", 1)
          ->setComment("mkfitdev::Contract of float(binoff * pitch) + fraction * local_pitch in localX/localY "
                       "(0 fuse second, 1 fuse first, 2 no fuse); the gate picks the stock one");
      desc.add<int>("contractGlobal", 1)
          ->setComment("mkfitdev::Contract of Surface::toGlobal for gx/gy/gz (1 = the mkFit hit converter's)");
      desc.add<bool>("produceCAHits", false)
          ->setComment("also put the CA OT layers' hit SoA + hitModuleStart (= Phase2OTRecHitsSoAConverter products)");
      desc.add<edm::InputTag>("beamSpot", edm::InputTag("hltOnlineBeamSpot"))->setComment("produceCAHits");
      desc.add<edm::InputTag>("pixelRecHitSoASource", edm::InputTag("hltPhase2SiPixelRecHitsSoA"))
          ->setComment("produceCAHits: pixel rechit SoA (host), for the pixel hit count");
      desc.add<int>("contractCA", 1)->setComment("produceCAHits: mkfitdev::Contract of the stock converter's toGlobal");
      desc.add<edm::InputTag>("compareCATo", edm::InputTag(""))
          ->setComment("validation: stock Phase2OTRecHitsSoAConverter products to compare with bitwise (waits)");
      desc.add<edm::InputTag>("compareTo", edm::InputTag(""))
          ->setComment("validation: legacy Phase2TrackerRecHits to compare with bitwise (waits); empty = off");
      descriptions.addWithDefaultLabel(desc);
    }

    void endJob() override {
      if (!compare_)
        return;
      std::lock_guard<std::mutex> lock(cmpMutex_);
      const auto& c = tot_;
      edm::LogPrint("MkFitAlpakaOTRecHits")
          << "OTDEV_CMP_TOTAL rows " << c.rows << " keyMis " << c.keyMis << " moduleMis " << c.moduleMis
          << " detIdMis " << c.detIdMis << " sizeMis " << c.sizeMis << " lxMis " << c.lxMis << " lyMis " << c.lyMis
          << " errMis " << c.errMis << " globalMis " << c.gMis << " | host recompute local mismatches per variant "
          << "(fuseSecond fuseFirst noFuse) " << c.localVar[0] << " " << c.localVar[1] << " " << c.localVar[2]
          << " | global vs BaseTrackerRecHit::globalPosition per variant " << c.globalVar[0] << " " << c.globalVar[1]
          << " " << c.globalVar[2] << " | contractLocal " << contractLocal_ << " contractGlobal " << contractGlobal_;
      if (compareCA_)
        edm::LogPrint("MkFitAlpakaOTRecHits")
            << "OTDEV_CA_TOTAL hits " << caTot_[0] << " sizeMis " << caTot_[1] << " hmsMis " << caTot_[2] << " localMis "
            << caTot_[3] << " globalMis " << caTot_[4] << " rMis " << caTot_[5] << " iphiMis " << caTot_[6]
            << " otherMis " << caTot_[7] << " | contractCA " << contractCA_;
    }

    void produce(edm::StreamID, device::Event& iEvent, device::EventSetup const& iSetup) const override {
      const auto& clusters = iEvent.get(clustersToken_);
      const auto& geom = iSetup.getData(geomToken_);
      edm::EventSetup const& es = iSetup;
      const auto t = table(iSetup.getData(cpeToken_), geom, es.get<TkPhase2OTCPERecord>().cacheIdentifier());
      const auto* devTable = deviceTable(iEvent.queue(), t);

      const uint32_t n = clusters.dataSize();
      std::vector<DetSetSpan> spans;
      mkfitdev::OTRecHitDeviceCollection product(iEvent.queue(), std::max<int32_t>(int32_t(n), 1));
      if constexpr (std::is_same_v<Device, alpaka::DevCpu>) {
        fillHost(clusters, geom, *t, n, product.view(), spans);
      } else {
        ::mkfitdev::OTRecHitHostCollection staging(iEvent.queue(), std::max<int32_t>(int32_t(n), 1));
        fillHost(clusters, geom, *t, n, staging.view(), spans);
        // prefix copy: the scalar and the host-filled columns (nHits, module, detId, clustSize, strip) precede lx
        const auto* base = reinterpret_cast<const std::byte*>(staging.buffer().data());
        const auto* lxp = reinterpret_cast<const std::byte*>(staging.view().metadata().addressOf_lx());
        const auto nBytes = static_cast<uint32_t>(lxp - base);
        alpaka::memcpy(iEvent.queue(), product.buffer(), staging.buffer(), alpaka::Vec<alpaka::DimInt<1u>, uint32_t>{nBytes});
      }
      mkfitdev::otdev::runOTCpe(
          iEvent.queue(), product.view(), devTable, t->firstIndex, n, contractLocal_, contractGlobal_);
      if (compare_)
        compare(iEvent, product, *t, n);
      if (caHits_)
        produceCA(iEvent, spans, t, devTable, n, product);
      iEvent.emplace(putToken_, std::move(product));
      unsigned long long h = 0;  // D9-f: allocation queue of the SoA (0 on CPU backends: blocking queue)
#if !(defined(ALPAKA_ACC_CPU_B_SEQ_T_SEQ_ENABLED) || defined(ALPAKA_ACC_CPU_B_TBB_T_SEQ_ENABLED))
      h = reinterpret_cast<unsigned long long>(alpaka::getNativeHandle(iEvent.queue()));
#endif
      iEvent.emplace(queuePutToken_, h);
    }

  private:
    // per-IOV table (TkPhase2OTCPERecord depends on the geometry, the field and the Lorentz angle records)
    std::shared_ptr<const OTCpeTable> table(ClusterParameterEstimator<Phase2TrackerCluster1D> const& cpeBase,
                                            TrackerGeometry const& geom,
                                            unsigned long long id) const {
      std::lock_guard<std::mutex> lock(tableMutex_);
      if (table_ && tableId_ == id)
        return table_;
      auto const* cpe = dynamic_cast<Phase2StripCPE const*>(&cpeBase);
      if (cpe == nullptr)
        throw cms::Exception("MkFitAlpakaOTRecHits") << "the configured OT CPE is not a Phase2StripCPE";
      auto t = std::make_shared<OTCpeTable>();
      // first OT GeomDetUnit index, as Phase2StripCPE::fillParam
      auto const& dus = geom.detUnits();
      uint32_t off = dus.size();
      for (unsigned int i = 3; i < 7; ++i) {
        const auto o = geom.offsetDU(GeomDetEnumerators::tkDetEnum[i]);
        if (o != dus.size() && o < off)
          off = o;
      }
      t->firstIndex = int32_t(off);
      t->modules.resize(dus.size() - off);
      for (auto i = off; i != dus.size(); ++i) {
        auto& m = t->modules[i - off];
        std::memset(&m, 0, sizeof(m));
        auto const* det = dynamic_cast<PixelGeomDetUnit const*>(dus[i]);
        if (det == nullptr || det->index() != int(i))
          continue;
        // PixelGeomDetUnit holds a ProxyPixelTopology (surface deformations); localX/localY forward to the wrapped one
        PixelTopology const* tp = &det->specificTopology();
        if (auto const* proxy = dynamic_cast<ProxyPixelTopology const*>(tp))
          tp = &proxy->specificTopology();
        auto const* topo = dynamic_cast<RectangularPixelPhase2Topology const*>(tp);
        if (topo == nullptr)
          continue;
        // Phase2StripCPE::fillParam, verbatim
        auto pitch_x = topo->pitch().first;
        auto thickness = det->specificSurface().bounds().thickness();
        auto drift = cpe->driftDirection(*det) * thickness;
        auto lvec = drift + LocalVector(0, 0, -thickness);
        float coveredStrips = lvec.x() / pitch_x;
        m.halfCovered = 0.5f * coveredStrips;
        // errors exactly as the CPE returns them
        const auto lv = cpe->localParameters(Phase2TrackerCluster1D(0, 0, 1), *det);
        m.exx = lv.second.xx();
        m.eyy = lv.second.yy();
        if (lv.second.xy() != 0.f)
          throw cms::Exception("MkFitAlpakaOTRecHits") << "Phase2StripCPE local error xy != 0 for det index " << i;
        // RectangularPixelPhase2Topology::localX/localY constants (same integer and float expressions)
        const int nrows = topo->nrows(), ncols = topo->ncolumns(), rpr = topo->rowsperroc(), cpr = topo->colsperroc();
        m.pitchX = topo->pitch().first;
        m.pitchY = topo->pitch().second;
        m.bigPitchX = topo->pitchbigpixelX();
        m.bigPitchY = topo->pitchbigpixelY();
        m.xOffset = topo->xoffset();
        m.yOffset = topo->yoffset();
        const int one = 1;
        m.xHalfTerm = one * 2 * m.bigPitchX * nrows / rpr;
        m.yHalfTerm = one * m.bigPitchY * ncols / cpr;
        m.xA = nrows / 2 - 2;
        m.xShift = 2 * nrows / rpr;
        m.xB = nrows / 2 - 2 + 2 * nrows / rpr;
        m.yA = ncols / 2 - 1;
        m.yShift = ncols / cpr;
        m.yB = ncols / 2 - 1 + ncols / cpr;
        // Surface rotation rows and position (GloballyPositioned<float>)
        auto const& s = det->surface();
        auto const& r = s.rotation();
        const float rr[9] = {r.xx(), r.xy(), r.xz(), r.yx(), r.yy(), r.yz(), r.zx(), r.zy(), r.zz()};
        for (int j = 0; j < 9; ++j)
          m.r[j] = rr[j];
        m.p[0] = s.position().x();
        m.p[1] = s.position().y();
        m.p[2] = s.position().z();
        m.detId = det->geographicalId().rawId();
        m.valid = 1;
      }
      // CA OT layers: P modules of the OT barrel in detUnits order (Phase2OTRecHitsSoAConverter::beginRun)
      t->pOffset.assign(t->modules.size(), -1);
      uint32_t nPix = 0, nP = 0;
      for (auto const* du : dus) {
        const DetId d = du->geographicalId();
        if (d.subdetId() == PixelSubdetector::PixelBarrel || d.subdetId() == PixelSubdetector::PixelEndcap)
          ++nPix;
        if (geom.getDetectorType(d) == TrackerGeometry::ModuleType::Ph2PSP && d.subdetId() == StripSubdetector::TOB) {
          const int32_t mi = du->index() - t->firstIndex;
          if (mi < 0 || mi >= int32_t(t->modules.size()))
            throw cms::Exception("MkFitAlpakaOTRecHits") << "P module outside the OT index range";
          t->pOffset[mi] = int32_t(nP++);
        }
      }
      t->nP = nP;
      t->modulesInPixel = uint16_t(nPix);
      table_ = std::move(t);
      tableId_ = id;
      return table_;
    }

    // device copy per (device, table): one copy + wait per IOV and device, shared by all streams
    const ::mkfitdev::OTCpeModule* deviceTable(Queue& queue, std::shared_ptr<const OTCpeTable> const& t) const {
      return deviceTables(queue, t).first;
    }
    std::pair<const ::mkfitdev::OTCpeModule*, const int32_t*> deviceTables(Queue& queue,
                                                                           std::shared_ptr<const OTCpeTable> const& t) const {
      if constexpr (std::is_same_v<Device, alpaka::DevCpu>) {
        return {t->modules.data(), t->pOffset.data()};
      } else {
        const auto key = std::make_pair(static_cast<long>(alpaka::getNativeHandle(alpaka::getDev(queue))),
                                        reinterpret_cast<uintptr_t>(t.get()));
        std::lock_guard<std::mutex> lock(tableMutex_);
        auto it = devTables_.find(key);
        if (it == devTables_.end()) {
          const uint32_t n = std::max<size_t>(t->modules.size(), 1);
          auto e = std::make_unique<DeviceTable>(
              DeviceTable{t,
                          cms::alpakatools::make_device_buffer<::mkfitdev::OTCpeModule[]>(alpaka::getDev(queue), n),
                          cms::alpakatools::make_device_buffer<int32_t[]>(alpaka::getDev(queue), n)});
          if (!t->modules.empty()) {
            alpaka::memcpy(queue,
                           e->buf,
                           cms::alpakatools::make_host_view(const_cast<::mkfitdev::OTCpeModule*>(t->modules.data()),
                                                            t->modules.size()));
            alpaka::memcpy(
                queue, e->pOff, cms::alpakatools::make_host_view(const_cast<int32_t*>(t->pOffset.data()), t->pOffset.size()));
          }
          alpaka::wait(queue);
          it = devTables_.emplace(key, std::move(e)).first;
        }
        return {it->second->buf.data(), it->second->pOff.data()};
      }
    }

    // the one host pass: per cluster key the GeomDet index, DetId, size and first strip | column
    static void fillHost(Phase2TrackerCluster1DCollectionNew const& clusters,
                         TrackerGeometry const& geom,
                         OTCpeTable const& t,
                         uint32_t n,
                         ::mkfitdev::OTRecHitSoA::View v,
                         std::vector<DetSetSpan>& spans) {
      v.nHits() = n;
      spans.reserve(clusters.size());
      int32_t* mod = v.metadata().addressOf_module();
      uint32_t* did = v.metadata().addressOf_detId();
      uint16_t* sz = v.metadata().addressOf_clustSize();
      uint32_t* st = v.metadata().addressOf_strip();
      const auto* data0 = clusters.data().data();
      uint32_t covered = 0;
      for (const auto& ds : clusters) {
        if (ds.empty())
          continue;
        const uint32_t id = ds.detId();
        const auto* gdu = geom.idToDetUnit(DetId(id));
        const int32_t gind = gdu ? gdu->index() : -1;
        if (gind < t.firstIndex || gind - t.firstIndex >= int32_t(t.modules.size()) ||
            !t.modules[gind - t.firstIndex].valid)
          throw cms::Exception("MkFitAlpakaOTRecHits") << "OT cluster on DetId " << id << " without a CPE table entry";
        const uint32_t k0 = &*ds.begin() - data0;
        spans.push_back({k0, uint32_t(ds.size()), gind - t.firstIndex});
        uint32_t k = k0;
        for (const auto& c : ds) {
          mod[k] = gind;
          did[k] = id;
          sz[k] = c.size();
          st[k] = (c.firstStrip() & 0xffffu) | (c.column() << 16);
          ++k;
        }
        covered += k - k0;
      }
      if (covered != n)
        throw cms::Exception("MkFitAlpakaOTRecHits") << "OT clusters: detsets cover " << covered << " of " << n << " keys";
    }

    // validation: bitwise comparison with the legacy Phase2TrackerRecHits (waits on the queue)
    void compare(device::Event& iEvent,
                 mkfitdev::OTRecHitDeviceCollection const& product,
                 OTCpeTable const& t,
                 uint32_t n) const {
      using ::mkfitdev::Contract;
      ::mkfitdev::OTRecHitHostCollection h(iEvent.queue(), std::max<int32_t>(int32_t(n), 1));
      alpaka::memcpy(iEvent.queue(), h.buffer(), product.const_buffer());
      alpaka::wait(iEvent.queue());
      const auto v = h.const_view();
      const auto& legacy = iEvent.get(legacyToken_);
      CmpCounts c;
      auto bits = [](float x) {
        uint32_t u;
        std::memcpy(&u, &x, 4);
        return u;
      };
      if (legacy.dataSize() != n || v.nHits() != n)
        ++c.keyMis;
      uint32_t idx = 0;
      for (const auto& ds : legacy) {
        for (const auto& rh : ds) {
          const uint32_t k = rh.firstClusterRef().index();
          ++c.rows;
          if (k != idx || k >= n) {
            ++c.keyMis;
            ++idx;
            continue;
          }
          ++idx;
          const auto lp = rh.localPosition();
          const auto le = rh.localPositionError();
          const auto gp = rh.globalPosition();
          const int32_t gind = rh.det()->index();
          if (v[k].module() != gind)
            ++c.moduleMis;
          if (v[k].detId() != rh.rawId())
            ++c.detIdMis;
          if (v[k].clustSize() != rh.cluster()->size())
            ++c.sizeMis;
          if (bits(v[k].lx()) != bits(lp.x()))
            ++c.lxMis;
          if (bits(v[k].ly()) != bits(lp.y()))
            ++c.lyMis;
          if (bits(v[k].exx()) != bits(le.xx()) || bits(v[k].eyy()) != bits(le.yy()) || le.xy() != 0.f)
            ++c.errMis;
          if (bits(v[k].gx()) != bits(gp.x()) || bits(v[k].gy()) != bits(gp.y()) || bits(v[k].gz()) != bits(gp.z()))
            ++c.gMis;
          // host recompute per contraction variant (which variant the stock code matches)
          if (gind >= t.firstIndex && gind - t.firstIndex < int32_t(t.modules.size())) {
            auto const& m = t.modules[gind - t.firstIndex];
            const auto& cl = *rh.cluster();
            auto locMis = [&](auto tag) {
              constexpr Contract M = decltype(tag)::value;
              float x, y;
              ::mkfitdev::otcpe::localPosition<M>(m, cl.firstStrip(), cl.column(), cl.size(), x, y);
              return bits(x) != bits(lp.x()) || bits(y) != bits(lp.y());
            };
            auto gloMis = [&](auto tag) {
              constexpr Contract M = decltype(tag)::value;
              float g[3];
              ::mkfitdev::otcpe::toGlobal<M>(m, lp.x(), lp.y(), g);
              return bits(g[0]) != bits(gp.x()) || bits(g[1]) != bits(gp.y()) || bits(g[2]) != bits(gp.z());
            };
            using C0 = std::integral_constant<Contract, Contract::kFuseSecond>;
            using C1 = std::integral_constant<Contract, Contract::kFuseFirst>;
            using C2 = std::integral_constant<Contract, Contract::kNoFuse>;
            c.localVar[0] += locMis(C0{});
            c.localVar[1] += locMis(C1{});
            c.localVar[2] += locMis(C2{});
            c.globalVar[0] += gloMis(C0{});
            c.globalVar[1] += gloMis(C1{});
            c.globalVar[2] += gloMis(C2{});
          }
        }
      }
      if (idx != n)
        ++c.keyMis;
      edm::LogPrint("MkFitAlpakaOTRecHits")
          << "OTDEV_CMP event " << iEvent.id().event() << " rows " << c.rows << " keyMis " << c.keyMis << " moduleMis "
          << c.moduleMis << " detIdMis " << c.detIdMis << " sizeMis " << c.sizeMis << " lxMis " << c.lxMis << " lyMis "
          << c.lyMis << " errMis " << c.errMis << " globalMis " << c.gMis << " localVar " << c.localVar[0] << " "
          << c.localVar[1] << " " << c.localVar[2] << " globalVar " << c.globalVar[0] << " " << c.globalVar[1] << " "
          << c.globalVar[2];
      std::lock_guard<std::mutex> lock(cmpMutex_);
      tot_.rows += c.rows;
      tot_.keyMis += c.keyMis;
      tot_.moduleMis += c.moduleMis;
      tot_.detIdMis += c.detIdMis;
      tot_.sizeMis += c.sizeMis;
      tot_.lxMis += c.lxMis;
      tot_.lyMis += c.lyMis;
      tot_.errMis += c.errMis;
      tot_.gMis += c.gMis;
      for (int j = 0; j < 3; ++j) {
        tot_.localVar[j] += c.localVar[j];
        tot_.globalVar[j] += c.globalVar[j];
      }
    }

    // CA OT layers: per-event P-module ranges on the host (from the cluster detset sizes), rows on the device
    void produceCA(device::Event& iEvent,
                   std::vector<DetSetSpan> const& spans,
                   std::shared_ptr<const OTCpeTable> const& tsp,
                   const ::mkfitdev::OTCpeModule* devTable,
                   uint32_t n,
                   mkfitdev::OTRecHitDeviceCollection const& ot) const {
      auto& queue = iEvent.queue();
      OTCpeTable const& t = *tsp;
      const auto& bs = iEvent.get(beamSpotToken_);
      const int nPixelHits = iEvent.get(pixelSoAToken_).view().trackingHits().metadata().size();
      const uint32_t nP = t.nP;
      if (nP == 0)
        throw cms::Exception("MkFitAlpakaOTRecHits") << "no P module in the OT barrel";
      // hitStart[0..nP] (exclusive prefix of the P-module hit counts), keyStart[0..nP)
      auto hostSK = cms::alpakatools::make_host_buffer<uint32_t[]>(queue, 2 * nP + 1);
      uint32_t* hitStart = hostSK.data();
      uint32_t* keyStart = hostSK.data() + nP + 1;
      std::fill(hitStart, hitStart + 2 * nP + 1, 0u);
      for (const auto& sp : spans) {
        const int32_t off = t.pOffset[sp.mi];
        if (off < 0)
          continue;
        hitStart[off + 1] = sp.size;
        keyStart[off] = sp.first;
      }
      for (uint32_t i = 0; i < nP; ++i)
        hitStart[i + 1] += hitStart[i];
      const uint32_t nPHits = hitStart[nP];
      std::vector<uint32_t> hms(nP + 1);
      for (uint32_t i = 0; i <= nP; ++i)
        hms[i] = hitStart[i] + uint32_t(nPixelHits);
      reco::TrackingRecHitsSoACollection ca(queue, nPHits, nP);
      // moduleStart (nP + 1 entries) to the hitModules block
      auto mv = ca.view().hitModules();
      alpaka::memcpy(queue,
                     cms::alpakatools::make_device_view(alpaka::getDev(queue), mv.metadata().addressOf_moduleStart(), nP + 1),
                     cms::alpakatools::make_host_view(hms.data(), nP + 1));
      // hitStart / keyStart to the device (CPU backends: used in place)
      const auto tabs = deviceTables(queue, tsp);
      const uint32_t* dSK = hostSK.data();
      std::optional<cms::alpakatools::device_buffer<Device, uint32_t[]>> devSK;
      if constexpr (!std::is_same_v<Device, alpaka::DevCpu>) {
        devSK.emplace(cms::alpakatools::make_device_buffer<uint32_t[]>(queue, 2 * nP + 1));
        alpaka::memcpy(queue, *devSK, hostSK);
        dSK = devSK->data();
      }
      mkfitdev::otdev::CAHitsParams p{bs.x0(), bs.y0(), bs.z0(), t.firstIndex, n, t.modulesInPixel, contractCA_};
      mkfitdev::otdev::runOTCAHits(
          queue, ot.const_view(), devTable, tabs.second, dSK, dSK + nP + 1, p, ca.view().trackingHits());
      if (compareCA_)
        compareCA(iEvent, ca, hms, nPHits, nP);
      iEvent.emplace(caPutToken_, std::move(ca));
      iEvent.emplace(hmsPutToken_, std::move(hms));
    }

    void compareCA(device::Event& iEvent,
                   reco::TrackingRecHitsSoACollection const& ca,
                   std::vector<uint32_t> const& hms,
                   uint32_t nPHits,
                   uint32_t nP) const {
      ::reco::TrackingRecHitHost h(iEvent.queue(), nPHits, nP);
      alpaka::memcpy(iEvent.queue(), h.buffer(), ca.const_buffer());
      alpaka::wait(iEvent.queue());
      const auto& stock = iEvent.get(stockCAToken_);
      const auto& stockHMS = iEvent.get(stockHMSToken_);
      std::array<uint64_t, 8> c{};
      auto bits = [](float x) {
        uint32_t u;
        std::memcpy(&u, &x, 4);
        return u;
      };
      const auto a = h.const_view().trackingHits();
      const auto b = stock.const_view().trackingHits();
      if (uint32_t(b.metadata().size()) != nPHits || stockHMS.size() != hms.size())
        ++c[1];
      for (size_t i = 0; i < std::min(hms.size(), stockHMS.size()); ++i)
        c[2] += hms[i] != stockHMS[i];
      const uint32_t m = std::min<uint32_t>(nPHits, b.metadata().size());
      c[0] = m;
      for (uint32_t i = 0; i < m; ++i) {
        c[3] += bits(a[i].xLocal()) != bits(b[i].xLocal()) || bits(a[i].yLocal()) != bits(b[i].yLocal()) ||
                bits(a[i].xerrLocal()) != bits(b[i].xerrLocal()) || bits(a[i].yerrLocal()) != bits(b[i].yerrLocal());
        c[4] += bits(a[i].xGlobal()) != bits(b[i].xGlobal()) || bits(a[i].yGlobal()) != bits(b[i].yGlobal()) ||
                bits(a[i].zGlobal()) != bits(b[i].zGlobal());
        c[5] += bits(a[i].rGlobal()) != bits(b[i].rGlobal());
        c[6] += a[i].iphi() != b[i].iphi();
        c[7] += a[i].clusterSizeX() != b[i].clusterSizeX() || a[i].clusterSizeY() != b[i].clusterSizeY() ||
                a[i].detectorIndex() != b[i].detectorIndex() ||
                a[i].chargeAndStatus().charge != b[i].chargeAndStatus().charge;
      }
      edm::LogPrint("MkFitAlpakaOTRecHits")
          << "OTDEV_CA event " << iEvent.id().event() << " hits " << c[0] << " sizeMis " << c[1] << " hmsMis " << c[2]
          << " localMis " << c[3] << " globalMis " << c[4] << " rMis " << c[5] << " iphiMis " << c[6] << " otherMis "
          << c[7];
      std::lock_guard<std::mutex> lock(cmpMutex_);
      for (int j = 0; j < 8; ++j)
        caTot_[j] += c[j];
    }

    struct DeviceTable {
      std::shared_ptr<const OTCpeTable> owner;
      cms::alpakatools::device_buffer<Device, ::mkfitdev::OTCpeModule[]> buf;
      cms::alpakatools::device_buffer<Device, int32_t[]> pOff;
    };

    const edm::EDGetTokenT<Phase2TrackerCluster1DCollectionNew> clustersToken_;
    const edm::ESGetToken<TrackerGeometry, TrackerDigiGeometryRecord> geomToken_;
    const edm::ESGetToken<ClusterParameterEstimator<Phase2TrackerCluster1D>, TkPhase2OTCPERecord> cpeToken_;
    const device::EDPutToken<mkfitdev::OTRecHitDeviceCollection> putToken_;
    const edm::EDPutTokenT<unsigned long long> queuePutToken_;
    edm::EDGetTokenT<Phase2TrackerRecHit1DCollectionNew> legacyToken_;
    const int contractLocal_;
    const int contractGlobal_;
    const bool compare_;
    const bool caHits_;
    const int contractCA_;
    const bool compareCA_;
    edm::EDGetTokenT<::reco::BeamSpot> beamSpotToken_;
    edm::EDGetTokenT<::reco::TrackingRecHitHost> pixelSoAToken_;
    device::EDPutToken<reco::TrackingRecHitsSoACollection> caPutToken_;
    edm::EDPutTokenT<std::vector<uint32_t>> hmsPutToken_;
    edm::EDGetTokenT<::reco::TrackingRecHitHost> stockCAToken_;
    edm::EDGetTokenT<std::vector<uint32_t>> stockHMSToken_;
    mutable std::array<uint64_t, 8> caTot_{};

    mutable std::mutex tableMutex_;
    mutable std::shared_ptr<const OTCpeTable> table_;
    mutable unsigned long long tableId_ = 0;
    mutable std::map<std::pair<long, uintptr_t>, std::unique_ptr<DeviceTable>> devTables_;
    mutable std::mutex cmpMutex_;
    mutable CmpCounts tot_;
  };

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE

DEFINE_FWK_ALPAKA_MODULE(MkFitAlpakaOTRecHitsProducer);
