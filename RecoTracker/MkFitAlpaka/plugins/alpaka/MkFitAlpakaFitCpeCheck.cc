// Per-hit check of the portable track-angle PixelCPEGeneric position AND errors (interface/fit/CpeGeneric.h,
// round 4: GenError qbin + localError, tables from plugins/alpaka/MkFitAlpakaFitCpeTables.h) against the
// stock CPE calls of the mkFit final fit, recorded by the private stock MkFitFitProducer (MKFIT_FIT_CPE_DUMP, see
// doc/fit.txt). Module constants from the menu's PixelCPEFastParamsPhase2 (host product). Runs once, on the first
// event; prints |dx| / |dy| buckets in microns. Host code only (the same function runs on device).

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <unordered_map>
#include <vector>

#include "FWCore/MessageLogger/interface/MessageLogger.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include "FWCore/Utilities/interface/ESInputTag.h"
#include "Geometry/CommonTopologies/interface/SimplePixelTopology.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/global/EDProducer.h"
#include "HeterogeneousCore/AlpakaInterface/interface/config.h"
#include "RecoLocalTracker/Records/interface/PixelCPEFastParamsRecord.h"
#include "RecoLocalTracker/SiPixelRecHits/interface/PixelCPEFastParamsHost.h"

#include "CondFormats/DataRecord/interface/SiPixelGenErrorDBObjectRcd.h"
#include "Geometry/Records/interface/TrackerDigiGeometryRecord.h"
#include "MagneticField/Records/interface/IdealMagneticFieldRecord.h"
#include "RecoTracker/MkFitAlpaka/interface/fit/CpeGeneric.h"
#include "RecoTracker/MkFitAlpaka/plugins/alpaka/MkFitAlpakaFitCpeTables.h"

namespace ALPAKA_ACCELERATOR_NAMESPACE {

  class MkFitAlpakaFitCpeCheck : public global::EDProducer<> {
  public:
    using Params = PixelCPEFastParamsHost<pixelTopology::Phase2>;

    explicit MkFitAlpakaFitCpeCheck(edm::ParameterSet const& iConfig)
        : EDProducer<>(iConfig),
          dumpFile_{iConfig.getParameter<std::string>("dumpFile")},
          maxPrint_{iConfig.getParameter<int>("maxPrint")},
          paramsToken_{esConsumes(edm::ESInputTag("", iConfig.getParameter<std::string>("cpeFastParams")))},
          genErrToken_{esConsumes()},
          geomToken_{esConsumes()},
          mfToken_{esConsumes()} {}

    static void fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
      edm::ParameterSetDescription desc;
      desc.add<std::string>("dumpFile", "");
      desc.add<std::string>("cpeFastParams", "PixelCPEFastParamsPhase2");
      desc.add<int>("maxPrint", 5);
      descriptions.addWithDefaultLabel(desc);
    }

    void produce(edm::StreamID, device::Event&, device::EventSetup const& iSetup) const override {
      bool expected = false;
      if (!done_.compare_exchange_strong(expected, true))
        return;
      auto const& params = iSetup.getData(paramsToken_);
      auto const& p = *params.buffer().data();
      std::unordered_map<uint32_t, int> rawToIndex;
      for (int i = 0; i < int(pixelTopology::Phase2::numberOfModules); ++i)
        rawToIndex[p.detParams(i).rawId] = i;

      constexpr int kErrB = 6;  // relative error buckets: == 0, < 1e-6, < 1e-5, < 1e-4, < 1e-3, >= 1e-3
      ::mkfitdev::cpe::CpeTablesHost tables;
      ::mkfitdev::cpe::buildCpeTables(
          params, iSetup.getData(genErrToken_), iSetup.getData(geomToken_), iSetup.getData(mfToken_), tables);
      const auto tv = tables.view();
      {
        int noTempl = 0, nBig = 0;
        for (auto const& m : tables.modules) {
          noTempl += m.templ < 0;
          nBig += (m.bigPerRocX | m.bigPerRocY) != 0;
        }
        edm::LogPrint("MkFitAlpakaFitCpeCheck")
            << "[cpecheck] tables: " << tables.modules.size() << " modules (" << noTempl << " without GenError, " << nBig
            << " with big pixels), " << tables.templ.size() << " GenError templates, pool " << tables.pool.size()
            << " floats";
      }
      long he[3][kErrB] = {}, nErr = 0, nFail = 0;
      double maxRel[3] = {0, 0, 0};

      FILE* f = std::fopen(dumpFile_.c_str(), "rb");
      if (!f) {
        edm::LogError("MkFitAlpakaFitCpeCheck") << "cannot open " << dumpFile_;
        return;
      }
      struct Rec {
        uint64_t event;
        int32_t hitIdx;
        uint32_t rawId;
        float ltp[6];
        float out[5];
        uint16_t nPix, minRow, minCol, pad;
      };
      static_assert(sizeof(Rec) == 72);
      constexpr int kB = 8;  // buckets: == 0, < 1e-3, 1e-2, 1e-1, 1, 10, 100, >= 100 microns
      long hx[kB] = {}, hy[kB] = {}, n = 0, noModule = 0, sizeHist[4] = {};
      double maxdx = 0, maxdy = 0;
      int printed = 0;
      auto bucket = [](double um) {
        if (um == 0)
          return 0;
        const double lim[] = {1e-3, 1e-2, 1e-1, 1, 10, 100};
        for (int b = 0; b < 6; ++b)
          if (um < lim[b])
            return b + 1;
        return 7;
      };
      Rec r;
      std::vector<uint16_t> pix;
      while (std::fread(&r, sizeof(r), 1, f) == 1) {
        pix.resize(4 * r.nPix);
        if (std::fread(pix.data(), 8, r.nPix, f) != r.nPix)
          break;
        auto it = rawToIndex.find(r.rawId);
        if (it == rawToIndex.end()) {
          ++noModule;
          continue;
        }
        auto const& dp = p.detParams(it->second);
        ::mkfitdev::cpe::ClusterEdges c{65535, 0, 65535, 0, 0, 0, 0, 0};
        for (unsigned k = 0; k < r.nPix; ++k) {
          const int x = pix[4 * k], y = pix[4 * k + 1];
          c.minRow = std::min(c.minRow, x);
          c.maxRow = std::max(c.maxRow, x);
          c.minCol = std::min(c.minCol, y);
          c.maxCol = std::max(c.maxCol, y);
        }
        for (unsigned k = 0; k < r.nPix; ++k) {
          const int x = pix[4 * k], y = pix[4 * k + 1], adc = pix[4 * k + 2];
          if (x == c.minRow)
            c.qfX += adc;
          if (x == c.maxRow)
            c.qlX += adc;
          if (y == c.minCol)
            c.qfY += adc;
          if (y == c.maxCol)
            c.qlY += adc;
        }
        (void)dp;
        ::mkfitdev::cpe::ModuleCpe const& m = tables.modules[it->second];
        float x, y;
        ::mkfitdev::cpe::localPositionTrackAngles(m, c, r.ltp[1], r.ltp[2], x, y);
        {
          ::mkfitdev::cpe::ClusterCpe cc{c, 0.f, it->second};
          for (unsigned k = 0; k < r.nPix; ++k)
            cc.charge += pix[4 * k + 2];
          float o[5];
          if (!::mkfitdev::cpe::cpeTrackAngles(tv, cc, r.ltp[1], r.ltp[2], o)) {
            ++nFail;
          } else {
            ++nErr;
            const int idx[3] = {2, 3, 4};
            for (int q = 0; q < 3; ++q) {
              const double ref = r.out[idx[q]], got = o[idx[q]];
              const double d = std::abs(got - ref), rel = ref != 0 ? d / std::abs(ref) : d;
              maxRel[q] = std::max(maxRel[q], rel);
              const int b = d == 0 ? 0 : rel < 1e-6 ? 1 : rel < 1e-5 ? 2 : rel < 1e-4 ? 3 : rel < 1e-3 ? 4 : 5;
              ++he[q][b];
              if (b == 5 && printed < maxPrint_) {
                ++printed;
                edm::LogPrint("MkFitAlpakaFitCpeCheck")
                    << "  ERR ev " << r.event << " hit " << r.hitIdx << " raw " << r.rawId << " e" << idx[q]
                    << " port " << got << " stock " << ref << " size " << c.maxRow - c.minRow + 1 << "x"
                    << c.maxCol - c.minCol + 1 << " rows " << c.minRow << "-" << c.maxRow << " cols " << c.minCol
                    << "-" << c.maxCol << " q " << cc.charge << " cot " << r.ltp[1] << " " << r.ltp[2] << " templ "
                    << m.templ << " bx " << m.bx << " bz " << m.bz;
              }
            }
          }
        }
        const double dx = std::abs(double(x) - r.out[0]) * 1e4, dy = std::abs(double(y) - r.out[1]) * 1e4;
        ++hx[bucket(dx)];
        ++hy[bucket(dy)];
        maxdx = std::max(maxdx, dx);
        maxdy = std::max(maxdy, dy);
        ++n;
        ++sizeHist[std::min(3, c.maxRow - c.minRow + 1 > 2 || c.maxCol - c.minCol + 1 > 2 ? 3 : 1)];
        if ((dx > 0.1 || dy > 0.1) && printed < maxPrint_) {
          ++printed;
          edm::LogPrint("MkFitAlpakaFitCpeCheck")
              << "  ev " << r.event << " hit " << r.hitIdx << " raw " << r.rawId << " rows " << c.minRow << "-"
              << c.maxRow << " cols " << c.minCol << "-" << c.maxCol << " q " << c.qfX << "/" << c.qlX << " "
              << c.qfY << "/" << c.qlY << " cot " << r.ltp[1] << " " << r.ltp[2] << " port " << x << " " << y
              << " stock " << r.out[0] << " " << r.out[1] << " nRows " << dp.nRows << " nCols " << dp.nCols;
        }
      }
      std::fclose(f);
      auto line = [&](const char* w, const long* h, double mx) {
        std::string s;
        for (int b = 0; b < kB; ++b)
          s += " " + std::to_string(h[b]);
        edm::LogPrint("MkFitAlpakaFitCpeCheck") << "[cpecheck] " << w << " |port - stock| buckets [um] ==0 <1e-3 <1e-2 "
                                                << "<0.1 <1 <10 <100 >=100:" << s << "  max " << mx;
      };
      edm::LogPrint("MkFitAlpakaFitCpeCheck")
          << "[cpecheck] CPE calls " << n << " (no module " << noModule << ") from " << dumpFile_;
      line("x", hx, maxdx);
      line("y", hy, maxdy);
      const char* en[3] = {"exx", "exy", "eyy"};
      for (int q = 0; q < 3; ++q) {
        std::string s;
        for (int b = 0; b < kErrB; ++b)
          s += " " + std::to_string(he[q][b]);
        edm::LogPrint("MkFitAlpakaFitCpeCheck") << "[cpecheck] " << en[q] << " |port - stock| / stock buckets ==0 <1e-6 "
                                                << "<1e-5 <1e-4 <1e-3 >=1e-3:" << s << "  max rel " << maxRel[q];
      }
      edm::LogPrint("MkFitAlpakaFitCpeCheck") << "[cpecheck] errors compared " << nErr << ", no CPE (false) " << nFail;
    }

  private:
    const std::string dumpFile_;
    const int maxPrint_;
    const edm::ESGetToken<Params, PixelCPEFastParamsRecord> paramsToken_;
    const edm::ESGetToken<SiPixelGenErrorDBObject, SiPixelGenErrorDBObjectRcd> genErrToken_;
    const edm::ESGetToken<TrackerGeometry, TrackerDigiGeometryRecord> geomToken_;
    const edm::ESGetToken<MagneticField, IdealMagneticFieldRecord> mfToken_;
    mutable std::atomic<bool> done_{false};
  };

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE

#include "HeterogeneousCore/AlpakaCore/interface/alpaka/MakerMacros.h"
DEFINE_FWK_ALPAKA_MODULE(MkFitAlpakaFitCpeCheck);
