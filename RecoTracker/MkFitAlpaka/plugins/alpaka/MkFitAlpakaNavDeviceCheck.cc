// Stage C (round 8, lane stagec): the device navigation prototype (steps (a) + (b), MkFitAlpakaNavDevice.h) ON THE
// DEVICE vs the host NavigationSchool::compatibleLayers, per output track and direction, with the converter's calls
// (innermost-hit layer + oppositeToMomentum, outermost-hit layer + alongMomentum) on the same first-hit states as
// MkFitAlpakaPcaCheck. The table is built on the host from the school's public static lists + TkLayerLess (first
// event of each stream; the geometry is fixed in these jobs). NAVDEV-SUMMARY at the end. Validation only.
#include <algorithm>
#include <array>
#include <cstdio>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <vector>

#include <alpaka/alpaka.hpp>

#include "DataFormats/GeometrySurface/interface/BoundCylinder.h"
#include "DataFormats/GeometrySurface/interface/BoundDisk.h"
#include "DataFormats/TrackReco/interface/Track.h"
#include "DataFormats/TrackReco/interface/TrackFwd.h"
#include "DataFormats/TrackingRecHit/interface/TrackingRecHit.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include "FWCore/Utilities/interface/Exception.h"
#include "FWCore/Utilities/interface/stringize.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/Event.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/EventSetup.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/MakerMacros.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/stream/EDProducer.h"
#include "HeterogeneousCore/AlpakaInterface/interface/config.h"
#include "HeterogeneousCore/AlpakaInterface/interface/memory.h"
#include "MagneticField/Engine/interface/MagneticField.h"
#include "MagneticField/Records/interface/IdealMagneticFieldRecord.h"
#include "RecoTracker/Record/interface/NavigationSchoolRecord.h"
#include "RecoTracker/Record/interface/TrackerRecoGeometryRecord.h"
#include "RecoTracker/TkDetLayers/interface/GeometricSearchTracker.h"
#include "TrackingTools/DetLayers/interface/BarrelDetLayer.h"
#include "TrackingTools/DetLayers/interface/DetLayer.h"
#include "TrackingTools/DetLayers/interface/ForwardDetLayer.h"
#include "TrackingTools/DetLayers/interface/NavigationSchool.h"
#include "TrackingTools/DetLayers/interface/TkLayerLess.h"
#include "TrackingTools/GeomPropagators/interface/AnalyticalPropagator.h"
#include "TrackingTools/TrajectoryState/interface/FreeTrajectoryState.h"
#include "TrackingTools/TrajectoryState/interface/TrajectoryStateOnSurface.h"
#include "TrackingTools/TrajectoryState/interface/TrajectoryStateTransform.h"

#include "MkFitAlpakaNavKernels.h"

namespace ALPAKA_ACCELERATOR_NAMESPACE {

  class MkFitAlpakaNavDeviceCheck : public stream::EDProducer<> {
  public:
    using DLV = std::vector<const DetLayer*>;
    explicit MkFitAlpakaNavDeviceCheck(edm::ParameterSet const& p)
        : EDProducer<>(p),
          tracks_{consumes(p.getParameter<edm::InputTag>("tracks"))},
          mf_{esConsumes()},
          nav_{esConsumes(p.getParameter<edm::ESInputTag>("NavigationSchool"))},
          gst_{esConsumes()},
          dummy_{produces()} {}

    static void fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
      edm::ParameterSetDescription desc;
      desc.add<edm::InputTag>("tracks", edm::InputTag("hltInitialStepTracks"));
      desc.add<edm::ESInputTag>("NavigationSchool", edm::ESInputTag{"", "SimpleNavigationSchool"});
      descriptions.addWithDefaultLabel(desc);
    }

    void produce(device::Event& ev, device::EventSetup const& es) override {
      namespace nav = ::mkfitdev::nav;
      auto const& trks = ev.get(tracks_);
      auto const& mf = es.getData(mf_);
      auto const& school = es.getData(nav_);
      auto const& gst = es.getData(gst_);
      auto& queue = ev.queue();
      if (!table_)
        buildTable(school, gst, queue);
      const AnalyticalPropagator prop(&mf, anyDirection);
      std::vector<nav::TrackIn> in;
      std::vector<std::array<uint64_t, 2>> ref;
      long long unmapped = 0;
      for (auto const& trk : trks) {
        if (trk.recHitsSize() < 2)
          continue;
        const auto first = trk.recHit(0), last = trk.recHit(trk.recHitsSize() - 1);
        if (!first->isValid() || !last->isValid() || first->det() == nullptr)
          continue;
        const FreeTrajectoryState pcaFts = trajectoryStateTransform::initialFreeState(trk, &mf);
        const TrajectoryStateOnSurface t1 = prop.propagate(pcaFts, first->det()->surface());
        if (!t1.isValid())
          continue;
        const FreeTrajectoryState fts = *t1.freeState();
        const DetLayer* inner = gst.idToLayer(first->geographicalId());
        const DetLayer* outer = gst.idToLayer(last->geographicalId());
        if (inner == nullptr || outer == nullptr)
          continue;
        nav::TrackIn t;
        t.x = fts.position().x(), t.y = fts.position().y(), t.z = fts.position().z();
        t.px = fts.momentum().x(), t.py = fts.momentum().y(), t.pz = fts.momentum().z();
        t.charge = fts.charge();
        t.bz = mf.inTesla(fts.position()).z();
        t.start[0] = index(inner);
        t.start[1] = index(outer);
        std::array<uint64_t, 2> r{};
        for (int d = 0; d < 2; ++d) {
          for (auto l : school.compatibleLayers(d == 0 ? *inner : *outer, fts, d == 0 ? oppositeToMomentum : alongMomentum)) {
            const int k = index(l);
            if (k < 0)
              ++unmapped;
            else
              r[d] |= uint64_t(1) << k;
          }
        }
        in.push_back(t);
        ref.push_back(r);
      }
      const int n = in.size();
      std::array<long long, 4> loc{};  // compared, differ, device misses layers, device has extra layers
      if (n > 0) {
        auto hIn = cms::alpakatools::make_host_buffer<nav::TrackIn[]>(queue, n);
        std::copy(in.begin(), in.end(), hIn.data());
        auto dIn = cms::alpakatools::make_device_buffer<nav::TrackIn[]>(queue, n);
        alpaka::memcpy(queue, dIn, hIn);
        auto dOut = cms::alpakatools::make_device_buffer<uint64_t[]>(queue, 2 * n);
        ALPAKA_ACCELERATOR_NAMESPACE::mkfitdev::navdev::launchNav(queue, table_->data(), dIn.data(), dOut.data(), n);
        auto hOut = cms::alpakatools::make_host_buffer<uint64_t[]>(queue, 2 * n);
        alpaka::memcpy(queue, hOut, dOut);
        alpaka::wait(queue);
        for (int i = 0; i < n; ++i)
          for (int d = 0; d < 2; ++d) {
            const uint64_t a = ref[i][d], b = hOut[2 * i + d];
            ++loc[0];
            if (a != b) {
              ++loc[1];
              loc[2] += (a & ~b) != 0;
              loc[3] += (b & ~a) != 0;
            }
          }
      }
      {
        std::lock_guard<std::mutex> lk(mtx_);
        for (int k = 0; k < 4; ++k)
          tot_[k] += loc[k];
        unmapped_ += unmapped;
      }
      ev.emplace(dummy_, n);
    }

    ~MkFitAlpakaNavDeviceCheck() override {
      std::lock_guard<std::mutex> lk(mtx_);
      if (reported_)
        return;
      reported_ = true;
      std::printf(
          "NAVDEV-SUMMARY backend %s layers %d list entries %d | track x direction %lld: device compatible-layer set "
          "differs from the host NavigationSchool %lld (device misses layers %lld, has extra %lld) | unmapped host "
          "layers %lld\n",
          EDM_STRINGIZE(ALPAKA_ACCELERATOR_NAMESPACE),
          nLayers_,
          nIdx_,
          tot_[0],
          tot_[1],
          tot_[2],
          tot_[3],
          unmapped_);
      std::fflush(stdout);
    }

  private:
    int index(const DetLayer* l) const {
      auto it = map_.find(l);
      return it == map_.end() ? -1 : it->second;
    }
    static DLV pick(DLV const& l, int what) {  // 0 barrel, 1 forward z < 0, 2 forward z > 0, 3 forward
      DLV r;
      for (auto c : l) {
        const bool b = c->isBarrel();
        const double z = c->position().z();
        if ((what == 0 && b) || (what == 1 && !b && z < 0) || (what == 2 && !b && z > 0) || (what == 3 && !b))
          r.push_back(c);
      }
      return r;
    }
    static DLV sorted(DLV a, DLV const& b, TkLayerLess const& less) {
      a.insert(a.end(), b.begin(), b.end());
      std::stable_sort(a.begin(), a.end(), less);
      return a;
    }
    // the table: per layer the candidate lists of SimpleBarrel/ForwardNavigableLayer, rebuilt from the static lists
    void buildTable(NavigationSchool const& school, GeometricSearchTracker const& gst, Queue& queue) {
      namespace nav = ::mkfitdev::nav;
      auto host = cms::alpakatools::make_host_buffer<nav::Table>(queue);
      nav::Table& T = *host.data();
      auto const& all = gst.allLayers();
      if (all.size() > size_t(nav::kMaxLayers))
        throw cms::Exception("Configuration") << "MkFitAlpakaNavDeviceCheck: " << all.size() << " layers > 64";
      for (size_t i = 0; i < all.size(); ++i)
        map_[all[i]] = i;
      T.nLayers = all.size();
      T.nIdx = 0;
      for (size_t i = 0; i < all.size(); ++i) {
        const DetLayer* l = all[i];
        nav::Layer& L = T.layers[i];
        L.barrel = l->isBarrel();
        const auto& bounds = l->surface().bounds();
        L.thickness = bounds.thickness();
        if (L.barrel) {
          L.rz = static_cast<BarrelDetLayer const*>(l)->specificSurface().radius();
          L.halfLength = bounds.length() * 0.5f;
          L.rin = L.rout = 0;
        } else {
          auto const& disk = static_cast<ForwardDetLayer const*>(l)->specificSurface();
          L.rz = disk.position().z();
          L.rin = disk.innerRadius();
          L.rout = disk.outerRadius();
          L.halfLength = 0;
        }
        const DLV sOut = school.nextLayers(*l, insideOut), sIn = school.nextLayers(*l, outsideIn);
        std::array<DLV, nav::kNLists> lists;
        if (L.barrel) {
          const DLV oB = pick(sOut, 0), oL = pick(sOut, 1), oR = pick(sOut, 2);
          const DLV iB = pick(sIn, 0), iL = pick(sIn, 1), iR = pick(sIn, 2);
          lists[nav::kNegOuter] = sorted(oB, oL, TkLayerLess());
          lists[nav::kPosOuter] = sorted(oB, oR, TkLayerLess());
          lists[nav::kNegInner] = sorted(iB, iL, TkLayerLess(outsideIn));
          lists[nav::kPosInner] = sorted(iB, iR, TkLayerLess(outsideIn));
          lists[nav::kIB] = iB, lists[nav::kIL] = iL, lists[nav::kIR] = iR;
          lists[nav::kOB] = oB, lists[nav::kOL] = oL, lists[nav::kOR] = oR;
        } else {
          lists[nav::kFOut] = sOut, lists[nav::kFIn] = sIn;
          lists[nav::kFIF] = pick(sIn, 3), lists[nav::kFOB] = pick(sOut, 0);
          lists[nav::kFIB] = pick(sIn, 0), lists[nav::kFOF] = pick(sOut, 3);
        }
        for (int k = 0; k < nav::kNLists; ++k) {
          L.off[k] = T.nIdx;
          L.len[k] = lists[k].size();
          for (auto c : lists[k]) {
            if (T.nIdx >= nav::kMaxIdx)
              throw cms::Exception("Configuration") << "MkFitAlpakaNavDeviceCheck: table overflow";
            T.idx[T.nIdx++] = index(c);
          }
        }
      }
      nLayers_ = T.nLayers;
      nIdx_ = T.nIdx;
      table_.emplace(cms::alpakatools::make_device_buffer<nav::Table>(queue));
      alpaka::memcpy(queue, *table_, host);
      alpaka::wait(queue);
    }

    const edm::EDGetTokenT<reco::TrackCollection> tracks_;
    const edm::ESGetToken<MagneticField, IdealMagneticFieldRecord> mf_;
    const edm::ESGetToken<NavigationSchool, NavigationSchoolRecord> nav_;
    const edm::ESGetToken<GeometricSearchTracker, TrackerRecoGeometryRecord> gst_;
    const edm::EDPutTokenT<int> dummy_;
    std::map<const DetLayer*, int> map_;
    std::optional<cms::alpakatools::device_buffer<Device, ::mkfitdev::nav::Table>> table_;
    static inline std::mutex mtx_;
    static inline std::array<long long, 4> tot_{};
    static inline long long unmapped_ = 0;
    static inline int nLayers_ = 0, nIdx_ = 0;
    static inline bool reported_ = false;
  };

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE

DEFINE_FWK_ALPAKA_MODULE(MkFitAlpakaNavDeviceCheck);
