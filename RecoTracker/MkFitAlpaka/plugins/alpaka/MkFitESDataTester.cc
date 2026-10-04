// Test of the MkFitAlpaka ES data: takes the device copy made by the framework, copies it back to the host,
// runs device kernels that use it (detid lookup, material lookup, layer predicates, config read), and checks
// every value against the STOCK host objects (MkFitGeometry/TrackerInfo, IterationConfig, mkfit::Config).
// Prints the sizes and the number of checked values / mismatches; throws on any mismatch.

#include <cmath>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include <alpaka/alpaka.hpp>

#include "CondFormats/DataRecord/interface/SiPixelQualityRcd.h"
#include "CondFormats/SiPixelObjects/interface/SiPixelQuality.h"
#include "FWCore/MessageLogger/interface/MessageLogger.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include "FWCore/Utilities/interface/ESInputTag.h"
#include "FWCore/Utilities/interface/Exception.h"
#include "FWCore/Utilities/interface/stringize.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/ESGetToken.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/global/EDProducer.h"
#include "HeterogeneousCore/AlpakaInterface/interface/config.h"
#include "HeterogeneousCore/AlpakaInterface/interface/memory.h"
#include "RecoTracker/MkFit/interface/MkFitGeometry.h"
#include "RecoTracker/MkFitCore/interface/Config.h"
#include "RecoTracker/MkFitCore/interface/HitStructures.h"
#include "RecoTracker/MkFitCore/interface/IterationConfig.h"
#include "RecoTracker/MkFitCore/interface/TrackerInfo.h"
#include "RecoTracker/Record/interface/TrackerRecoGeometryRecord.h"

#include "RecoTracker/MkFitAlpaka/interface/es/ESData.h"

#include "MkFitESDataTestKernels.h"

namespace ALPAKA_ACCELERATOR_NAMESPACE {

  namespace {

    // counts checked values and mismatches per category; prints the first few mismatches
    class Checker {
    public:
      explicit Checker(std::string name) : name_(std::move(name)) {}

      template <typename A, typename B>
      void eq(A const& dev, B const& ref, char const* what, long idx = -1) {
        ++nChecked_;
        if (!same(dev, ref)) {
          ++nBad_;
          if (nBad_ <= 10)
            edm::LogPrint("MkFitESDataTester") << "  MISMATCH " << name_ << " " << what << " [" << idx << "]: device "
                                               << std::setprecision(9) << +dev << " stock " << +ref;
        }
      }

      long checked() const { return nChecked_; }
      long bad() const { return nBad_; }
      std::string const& name() const { return name_; }

    private:
      template <typename A, typename B>
      static bool same(A const& a, B const& b) {
        if constexpr (std::is_floating_point_v<A> || std::is_floating_point_v<B>) {
          // exact equality of the stored value (no arithmetic happens in copies)
          return (std::isnan(double(a)) && std::isnan(double(b))) || double(a) == double(b);
        } else {
          return static_cast<long long>(a) == static_cast<long long>(b);
        }
      }

      std::string name_;
      long nChecked_ = 0;
      long nBad_ = 0;
    };

    template <typename TColl>
    size_t bytesOf(TColl const& c) {
      return alpaka::getExtentProduct(c.const_buffer());
    }

  }  // namespace

  class MkFitESDataTester : public global::EDProducer<> {
  public:
    MkFitESDataTester(edm::ParameterSet const& config)
        : EDProducer<>(config),
          esToken_(esConsumes(config.getParameter<edm::ESInputTag>("esData"))),
          geomToken_(esConsumes()),
          iterConfigToken_(esConsumes(config.getParameter<edm::ESInputTag>("iterationConfig"))),
          countPixelQuality_(config.getParameter<bool>("countPixelQuality")) {
      if (countPixelQuality_)
        pixelQualityToken_ = esConsumes();
    }

    static void fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
      edm::ParameterSetDescription desc;
      desc.add<edm::ESInputTag>("esData", edm::ESInputTag());
      desc.add<edm::ESInputTag>("iterationConfig", edm::ESInputTag("", "hltInitialStepTrackCandidatesMkFitConfig"));
      desc.add<bool>("countPixelQuality", true)
          ->setComment("report the SiPixelQuality bad components (source of the EventOfHits dead regions)");
      descriptions.addWithDefaultLabel(desc);
    }

    void produce(edm::StreamID, device::Event& iEvent, device::EventSetup const& iSetup) const override;

  private:
    const device::ESGetToken<mkfitdev::ESData<Device>, TrackerRecoGeometryRecord> esToken_;
    const edm::ESGetToken<MkFitGeometry, TrackerRecoGeometryRecord> geomToken_;
    const edm::ESGetToken<mkfit::IterationConfig, TrackerRecoGeometryRecord> iterConfigToken_;
    const bool countPixelQuality_;
    edm::ESGetToken<SiPixelQuality, SiPixelQualityRcd> pixelQualityToken_;
  };

  void MkFitESDataTester::produce(edm::StreamID, device::Event& iEvent, device::EventSetup const& iSetup) const {
    using namespace mkfitdev;
    auto& queue = iEvent.queue();
    ESData<Device> const& dev = iSetup.getData(esToken_);
    mkfit::TrackerInfo const& ti = iSetup.getData(geomToken_).trackerInfo();
    mkfit::IterationConfig const& ic = iSetup.getData(iterConfigToken_);
    ESSizes const& sz = dev.sizes;
    const ESView view = dev.view();

    // ---------------- copy everything back to the host
    PortableHostCollection<LayerInfoSoA> hL(queue, sz.nLayers);
    PortableHostCollection<ModuleInfoSoA> hM(queue, sz.nModules);
    PortableHostCollection<ModuleShapeSoA> hS(queue, sz.nShapes);
    PortableHostCollection<DetIdMapSoA> hD(queue, sz.detIdMapCapacity);
    PortableHostCollection<MaterialSoA> hMat(queue, sz.nMaterialBins);
    PortableHostObject<ESConfig> hC(queue);
    alpaka::memcpy(queue, hL.buffer(), dev.layers->const_buffer());
    alpaka::memcpy(queue, hM.buffer(), dev.modules->const_buffer());
    alpaka::memcpy(queue, hS.buffer(), dev.shapes->const_buffer());
    alpaka::memcpy(queue, hD.buffer(), dev.detIdMap->const_buffer());
    alpaka::memcpy(queue, hMat.buffer(), dev.material->const_buffer());
    alpaka::memcpy(queue, hC.buffer(), dev.config->const_buffer());

    // ---------------- device kernels using the ES data
    // (1) detid lookups: every module detid, plus detid+1 probes (mostly unknown detids)
    std::unordered_map<uint32_t, std::pair<int, int>> stockMap;  // detid -> (layer, sid) from stock modules
    for (int l = 0; l < ti.n_layers(); ++l)
      for (int s = 0; s < ti.layer(l).n_modules(); ++s)
        stockMap[ti.layer(l).module_info(s).detid] = {l, s};
    const int nDet = 2 * sz.nModules;
    auto hDetid = cms::alpakatools::make_host_buffer<uint32_t[]>(queue, nDet);
    auto hDetLayer = cms::alpakatools::make_host_buffer<int[]>(queue, nDet);
    {
      int i = 0;
      for (int l = 0; l < ti.n_layers(); ++l)
        for (int s = 0; s < ti.layer(l).n_modules(); ++s, ++i) {
          const uint32_t d = ti.layer(l).module_info(s).detid;
          hDetid[i] = d;
          hDetLayer[i] = l;
          hDetid[sz.nModules + i] = d + 1;
          hDetLayer[sz.nModules + i] = l;
        }
    }
    auto dDetid = cms::alpakatools::make_device_buffer<uint32_t[]>(queue, nDet);
    auto dDetLayer = cms::alpakatools::make_device_buffer<int[]>(queue, nDet);
    auto dOutModule = cms::alpakatools::make_device_buffer<int[]>(queue, nDet);
    auto dOutSid = cms::alpakatools::make_device_buffer<int[]>(queue, nDet);
    alpaka::memcpy(queue, dDetid, hDetid);
    alpaka::memcpy(queue, dDetLayer, hDetLayer);
    mkfitdev_estest::launchDetIdLookups(
        queue, view, nDet, dDetid.data(), dDetLayer.data(), dOutModule.data(), dOutSid.data());
    auto hOutModule = cms::alpakatools::make_host_buffer<int[]>(queue, nDet);
    auto hOutSid = cms::alpakatools::make_host_buffer<int[]>(queue, nDet);
    alpaka::memcpy(queue, hOutModule, dOutModule);
    alpaka::memcpy(queue, hOutSid, dOutSid);

    // (2) material lookups on a (z, r) grid finer than the map and beyond its range
    std::vector<float> matZ, matR;
    for (float z = -310.f; z <= 310.f; z += 0.37f)
      for (float r = -2.f; r <= 125.f; r += 0.29f) {
        matZ.push_back(z);
        matR.push_back(r);
      }
    const int nMat = matZ.size();
    auto hMatZ = cms::alpakatools::make_host_buffer<float[]>(queue, nMat);
    auto hMatR = cms::alpakatools::make_host_buffer<float[]>(queue, nMat);
    std::memcpy(hMatZ.data(), matZ.data(), nMat * sizeof(float));
    std::memcpy(hMatR.data(), matR.data(), nMat * sizeof(float));
    auto dMatZ = cms::alpakatools::make_device_buffer<float[]>(queue, nMat);
    auto dMatR = cms::alpakatools::make_device_buffer<float[]>(queue, nMat);
    auto dMatB = cms::alpakatools::make_device_buffer<float[]>(queue, nMat);
    auto dMatX = cms::alpakatools::make_device_buffer<float[]>(queue, nMat);
    alpaka::memcpy(queue, dMatZ, hMatZ);
    alpaka::memcpy(queue, dMatR, hMatR);
    mkfitdev_estest::launchMaterialLookups(queue, view, nMat, dMatZ.data(), dMatR.data(), dMatB.data(), dMatX.data());
    auto hMatB = cms::alpakatools::make_host_buffer<float[]>(queue, nMat);
    auto hMatX = cms::alpakatools::make_host_buffer<float[]>(queue, nMat);
    alpaka::memcpy(queue, hMatB, dMatB);
    alpaka::memcpy(queue, hMatX, dMatX);

    // (3) layer predicates: per layer, q over the z range and the r range (+-30 cm), three window sizes
    std::vector<int> prL;
    std::vector<float> prQ, prD;
    const float dqs[3] = {0.f, 0.5f, 3.f};
    for (int l = 0; l < ti.n_layers(); ++l) {
      mkfit::LayerInfo const& li = ti.layer(l);
      for (float d : dqs) {
        for (float q = li.zmin() - 30.f; q <= li.zmax() + 30.f; q += 0.0731f) {
          prL.push_back(l);
          prQ.push_back(q);
          prD.push_back(d);
        }
        for (float q = li.rin() - 30.f; q <= li.rout() + 30.f; q += 0.0131f) {
          prL.push_back(l);
          prQ.push_back(q);
          prD.push_back(d);
        }
      }
    }
    const int nPr = prL.size();
    auto hPrL = cms::alpakatools::make_host_buffer<int[]>(queue, nPr);
    auto hPrQ = cms::alpakatools::make_host_buffer<float[]>(queue, nPr);
    auto hPrD = cms::alpakatools::make_host_buffer<float[]>(queue, nPr);
    std::memcpy(hPrL.data(), prL.data(), nPr * sizeof(int));
    std::memcpy(hPrQ.data(), prQ.data(), nPr * sizeof(float));
    std::memcpy(hPrD.data(), prD.data(), nPr * sizeof(float));
    auto dPrL = cms::alpakatools::make_device_buffer<int[]>(queue, nPr);
    auto dPrQ = cms::alpakatools::make_device_buffer<float[]>(queue, nPr);
    auto dPrD = cms::alpakatools::make_device_buffer<float[]>(queue, nPr);
    auto dPrOut = cms::alpakatools::make_device_buffer<int[]>(queue, 4 * nPr);
    alpaka::memcpy(queue, dPrL, hPrL);
    alpaka::memcpy(queue, dPrQ, hPrQ);
    alpaka::memcpy(queue, dPrD, hPrD);
    mkfitdev_estest::launchLayerPredicates(queue, view, nPr, dPrL.data(), dPrQ.data(), dPrD.data(), dPrOut.data());
    auto hPrOut = cms::alpakatools::make_host_buffer<int[]>(queue, 4 * nPr);
    alpaka::memcpy(queue, hPrOut, dPrOut);

    // (4) config read through the device pointer in ESView
    auto dCfgBytes = cms::alpakatools::make_device_buffer<unsigned char[]>(queue, sizeof(ESConfig));
    mkfitdev_estest::launchConfigRead(queue, view, dCfgBytes.data());
    auto hCfgBytes = cms::alpakatools::make_host_buffer<unsigned char[]>(queue, sizeof(ESConfig));
    alpaka::memcpy(queue, hCfgBytes, dCfgBytes);

    // (5) field-by-field config reads on the device
    auto dCfgFields = cms::alpakatools::make_device_buffer<float[]>(queue, mkfitdev_estest::kNConfigFields);
    mkfitdev_estest::launchConfigFields(queue, view, dCfgFields.data());
    auto hCfgFields = cms::alpakatools::make_host_buffer<float[]>(queue, mkfitdev_estest::kNConfigFields);
    alpaka::memcpy(queue, hCfgFields, dCfgFields);

    alpaka::wait(queue);

    // ---------------- checks against the stock objects
    std::vector<Checker> checks;

    // layers
    {
      Checker c("layers");
      auto L = hL.const_view();
      c.eq(sz.nLayers, ti.n_layers(), "n_layers");
      for (int l = 0; l < ti.n_layers(); ++l) {
        mkfit::LayerInfo const& li = ti.layer(l);
        c.eq(L[l].layer_id(), li.layer_id(), "layer_id", l);
        c.eq(L[l].layer_type(), static_cast<int>(li.layer_type()), "layer_type", l);
        c.eq(L[l].subdet(), li.subdet(), "subdet", l);
        c.eq(L[l].rin(), li.rin(), "rin", l);
        c.eq(L[l].rout(), li.rout(), "rout", l);
        c.eq(L[l].zmin(), li.zmin(), "zmin", l);
        c.eq(L[l].zmax(), li.zmax(), "zmax", l);
        c.eq(L[l].propagate_to(), li.propagate_to(), "propagate_to", l);
        c.eq(L[l].q_bin(), li.q_bin(), "q_bin", l);
        c.eq(L[l].is_stereo(), li.is_stereo(), "is_stereo", l);
        c.eq(L[l].is_pixel(), li.is_pixel(), "is_pixel", l);
        c.eq(L[l].has_charge(), li.has_charge(), "has_charge", l);
        c.eq(L[l].n_modules(), li.n_modules(), "n_modules", l);
        c.eq(L[l].n_shapes(), li.n_shapes(), "n_shapes", l);
        mkfit::LayerOfHits::Initializator init(li);
        c.eq(L[l].q_min(), init.m_qmin, "q_min", l);
        c.eq(L[l].q_max(), init.m_qmax, "q_max", l);
        c.eq(L[l].n_q(), init.m_nq, "n_q", l);
        mkfit::IterationLayerConfig const& lc = ic.m_layer_configs[l];
        c.eq(L[l].select_min_dphi(), lc.min_dphi(), "select_min_dphi", l);
        c.eq(L[l].select_max_dphi(), lc.max_dphi(), "select_max_dphi", l);
        c.eq(L[l].select_min_dq(), lc.min_dq(), "select_min_dq", l);
        c.eq(L[l].select_max_dq(), lc.max_dq(), "select_max_dq", l);
        c.eq(lc.m_winpars_fwd.size() + lc.m_winpars_bkw.size(), 0, "winpars_empty", l);
      }
      checks.push_back(c);
    }

    // modules
    {
      Checker c("modules");
      auto M = hM.const_view();
      auto L = hL.const_view();
      c.eq(sz.nModules, ti.n_total_modules(), "n_total_modules");
      int row = 0;
      for (int l = 0; l < ti.n_layers(); ++l) {
        c.eq(L[l].module_begin(), row, "module_begin", l);
        for (int s = 0; s < ti.layer(l).n_modules(); ++s, ++row) {
          mkfit::ModuleInfo const& mi = ti.layer(l).module_info(s);
          c.eq(M[row].pos_x(), mi.pos[0], "pos_x", row);
          c.eq(M[row].pos_y(), mi.pos[1], "pos_y", row);
          c.eq(M[row].pos_z(), mi.pos[2], "pos_z", row);
          c.eq(M[row].zdir_x(), mi.zdir[0], "zdir_x", row);
          c.eq(M[row].zdir_y(), mi.zdir[1], "zdir_y", row);
          c.eq(M[row].zdir_z(), mi.zdir[2], "zdir_z", row);
          c.eq(M[row].xdir_x(), mi.xdir[0], "xdir_x", row);
          c.eq(M[row].xdir_y(), mi.xdir[1], "xdir_y", row);
          c.eq(M[row].xdir_z(), mi.xdir[2], "xdir_z", row);
          c.eq(M[row].detid(), mi.detid, "detid", row);
          c.eq(M[row].shapeid(), mi.shapeid, "shapeid", row);
          c.eq(M[row].layer(), l, "layer", row);
          c.eq(M[row].sid(), s, "sid", row);
          c.eq(M[row].sid(), ti.layer(l).short_id(mi.detid), "stock_short_id", row);
          c.eq(M[row].radl(), mi.radl, "radl", row);  // trackreco#186 per-module material
          c.eq(M[row].bbxi(), mi.bbxi, "bbxi_module", row);
        }
      }
      checks.push_back(c);
    }

    // shapes
    {
      Checker c("shapes");
      auto S = hS.const_view();
      auto L = hL.const_view();
      int row = 0;
      for (int l = 0; l < ti.n_layers(); ++l) {
        c.eq(L[l].shape_begin(), row, "shape_begin", l);
        for (int s = 0; s < ti.layer(l).n_shapes(); ++s, ++row) {
          mkfit::ModuleShape const& ms = ti.layer(l).module_shape(s);
          c.eq(S[row].dx1(), ms.dx1, "dx1", row);
          c.eq(S[row].dx2(), ms.dx2, "dx2", row);
          c.eq(S[row].dy(), ms.dy, "dy", row);
          c.eq(S[row].dz(), ms.dz, "dz", row);
        }
      }
      checks.push_back(c);
    }

    // detid map: table content (host copy) and device lookups
    int nUnknownProbes = 0;
    long sumProbe = 0;
    {
      Checker c("detid_map");
      auto D = hD.const_view();
      int nFilled = 0;
      for (int i = 0; i < sz.detIdMapCapacity; ++i) {
        if (D[i].key() == 0)
          continue;
        ++nFilled;
        const uint32_t home = detIdHashSlot(D[i].key(), D.hash_shift());
        sumProbe += (static_cast<uint32_t>(i) - home) & static_cast<uint32_t>(sz.detIdMapCapacity - 1);
        auto it = stockMap.find(D[i].key());
        c.eq(it != stockMap.end(), true, "table_key_known", i);
        if (it != stockMap.end()) {
          auto const& [l, s] = it->second;
          c.eq(D[i].module(), hL.const_view()[l].module_begin() + s, "table_value", i);
        }
      }
      c.eq(nFilled, static_cast<int>(stockMap.size()), "table_n_filled");
      c.eq(D.n_entries(), sz.nModules, "n_entries");
      for (int i = 0; i < nDet; ++i) {
        auto it = stockMap.find(hDetid[i]);
        const int expModule = (it == stockMap.end())
                                  ? es::kNoModule
                                  : hL.const_view()[it->second.first].module_begin() + it->second.second;
        const int expSid = (it == stockMap.end() || it->second.first != hDetLayer[i]) ? -1 : it->second.second;
        if (it == stockMap.end())
          ++nUnknownProbes;
        c.eq(hOutModule[i], expModule, "device_findModule", i);
        c.eq(hOutSid[i], expSid, "device_shortId", i);
      }
      checks.push_back(c);
    }

    // material: full map (host copy) + device lookups vs TrackerInfo::material_checked
    {
      Checker c("material");
      auto Mt = hMat.const_view();
      c.eq(Mt.nbins_z(), ti.mat_nbins_z(), "nbins_z");
      c.eq(Mt.nbins_r(), ti.mat_nbins_r(), "nbins_r");
      c.eq(Mt.range_z(), ti.mat_range_z(), "range_z");
      c.eq(Mt.range_r(), ti.mat_range_r(), "range_r");
      for (int iz = 0; iz < ti.mat_nbins_z(); ++iz)
        for (int ir = 0; ir < ti.mat_nbins_r(); ++ir) {
          const int i = iz * ti.mat_nbins_r() + ir;
          c.eq(Mt[i].bbxi(), ti.material_bbxi(iz, ir), "bbxi", i);
          c.eq(Mt[i].radl(), ti.material_radl(iz, ir), "radl", i);
        }
      for (int i = 0; i < nMat; ++i) {
        const auto m = ti.material_checked(matZ[i], matR[i]);
        c.eq(hMatB[i], m.bbxi, "device_material_checked_bbxi", i);
        c.eq(hMatX[i], m.radl, "device_material_checked_radl", i);
        c.eq(view.material.binZ(matZ[i]), ti.mat_bin_z(matZ[i]), "bin_z", i);
        c.eq(view.material.binR(matR[i]), ti.mat_bin_r(matR[i]), "bin_r", i);
      }
      checks.push_back(c);
    }

    // layer predicates (device) vs stock LayerInfo
    {
      Checker c("layer_predicates");
      for (int i = 0; i < nPr; ++i) {
        mkfit::LayerInfo const& li = ti.layer(prL[i]);
        const mkfit::WSR_Result wz = li.is_within_z_sensitive_region(prQ[i], prD[i]);
        const mkfit::WSR_Result wr = li.is_within_r_sensitive_region(prQ[i], prD[i]);
        c.eq(hPrOut[4 * i + 0], int(wz.m_wsr) * 2 + (wz.m_in_gap ? 1 : 0), "wsr_z", i);
        c.eq(hPrOut[4 * i + 1], int(wr.m_wsr) * 2 + (wr.m_in_gap ? 1 : 0), "wsr_r", i);
        c.eq(hPrOut[4 * i + 2], li.is_within_q_limits(prQ[i]) ? 1 : 0, "q_limits", i);
        c.eq(hPrOut[4 * i + 3], li.is_in_r_hole(prQ[i]) ? 1 : 0, "r_hole", i);
      }
      checks.push_back(c);
    }

    // scalar configuration vs stock IterationConfig / TrackerInfo / mkfit::Config
    int nHoleLayers = 0;
    {
      Checker c("config");
      ESConfig const& C = hC.const_value();
      c.eq(std::memcmp(&C, hCfgBytes.data(), sizeof(ESConfig)), 0, "device_read_equals_copy");
      c.eq(std::memcmp(&C, &dev.hostConfigValue(), sizeof(ESConfig)), 0, "copy_equals_host_product");
      {
        float hostFields[mkfitdev_estest::kNConfigFields];
        mkfitdev_estest::extractConfigFields(C, hostFields);
        for (int k = 0; k < mkfitdev_estest::kNConfigFields; ++k)
          c.eq(hCfgFields[k], hostFields[k], "device_field_read", k);
      }
      c.eq(C.n_layers, ti.n_layers(), "n_layers");
      c.eq(C.n_barrel_layers, ti.barrel_layers().size(), "n_barrel_layers");
      c.eq(C.n_ecap_pos_layers, ti.endcap_pos_layers().size(), "n_ecap_pos_layers");
      c.eq(C.n_ecap_neg_layers, ti.endcap_neg_layers().size(), "n_ecap_neg_layers");
      c.eq(C.outer_barrel_layer, ti.outer_barrel_layer().layer_id(), "outer_barrel_layer");
      c.eq(C.n_total_modules, ti.n_total_modules(), "n_total_modules");
      c.eq(C.usePropToPlane, mkfit::Config::usePropToPlane, "usePropToPlane");
      c.eq(C.usePtMultScat, mkfit::Config::usePtMultScat, "usePtMultScat");
      // trackreco#186 runtime globals
      c.eq(C.refit.bFieldAtMid, mkfit::Config::refitBFieldAtMid, "refitBFieldAtMid");
      c.eq(C.refit.radialFieldCorr, mkfit::Config::refitRadialFieldCorr, "refitRadialFieldCorr");
      c.eq(C.refit.elossSignFromPass, mkfit::Config::refitElossSignFromPass, "refitElossSignFromPass");
      c.eq(C.refit.bkwMsFixedMomentum, mkfit::Config::refitBkwMsFixedMomentum, "refitBkwMsFixedMomentum");
      c.eq(C.refit.bkwSubSteps, mkfit::Config::refitBkwSubSteps, "refitBkwSubSteps");
      c.eq(C.refit.materialPerModule, mkfit::Config::refitMaterialPerModule, "refitMaterialPerModule");
      c.eq(C.mag_c1, mkfit::Config::mag_c1, "mag_c1");
      c.eq(C.mag_b0, mkfit::Config::mag_b0, "mag_b0");
      c.eq(C.mag_b1, mkfit::Config::mag_b1, "mag_b1");
      c.eq(C.mag_a, mkfit::Config::mag_a, "mag_a");
      c.eq(sz.bField.c1, mkfit::Config::mag_c1, "bField.c1");
      c.eq(sz.bField.b0, mkfit::Config::mag_b0, "bField.b0");
      c.eq(sz.bField.b1, mkfit::Config::mag_b1, "bField.b1");
      c.eq(sz.bField.a, mkfit::Config::mag_a, "bField.a");
      c.eq(C.maxdPt, mkfit::Config::maxdPt, "maxdPt");
      c.eq(C.maxdPhi, mkfit::Config::maxdPhi, "maxdPhi");
      c.eq(C.maxdEta, mkfit::Config::maxdEta, "maxdEta");
      c.eq(C.maxdR, mkfit::Config::maxdR, "maxdR");
      c.eq(C.minFracHitsShared, mkfit::Config::minFracHitsShared, "minFracHitsShared");
      c.eq(C.maxd1pt, mkfit::Config::maxd1pt, "maxd1pt");
      c.eq(C.maxdphi, mkfit::Config::maxdphi, "maxdphi");
      c.eq(C.maxdcth, mkfit::Config::maxdcth, "maxdcth");
      c.eq(C.maxcth_ob, mkfit::Config::maxcth_ob, "maxcth_ob");
      c.eq(C.maxcth_fw, mkfit::Config::maxcth_fw, "maxcth_fw");

      mkfit::PropagationConfig const& pc = ti.prop_config();
      c.eq(C.prop_config.backward_fit_to_pca, pc.backward_fit_to_pca, "backward_fit_to_pca");
      c.eq(C.prop_config.finding_requires_propagation_to_hit_pos,
           pc.finding_requires_propagation_to_hit_pos,
           "finding_requires_propagation_to_hit_pos");
      auto cmpFlags = [&c](PropFlags const& a, mkfit::PropagationFlags const& b, const char* n) {
        c.eq(a.use_param_b_field, bool(b.use_param_b_field), n);
        c.eq(a.apply_material, bool(b.apply_material), n);
        c.eq(a.copy_input_state_on_fail, bool(b.copy_input_state_on_fail), n);
      };
      cmpFlags(C.prop_config.finding_inter_layer_pflags, pc.finding_inter_layer_pflags, "finding_inter_layer_pflags");
      cmpFlags(C.prop_config.finding_intra_layer_pflags, pc.finding_intra_layer_pflags, "finding_intra_layer_pflags");
      cmpFlags(C.prop_config.backward_fit_pflags, pc.backward_fit_pflags, "backward_fit_pflags");
      cmpFlags(C.prop_config.forward_fit_pflags, pc.forward_fit_pflags, "forward_fit_pflags");
      cmpFlags(C.prop_config.seed_fit_pflags, pc.seed_fit_pflags, "seed_fit_pflags");
      cmpFlags(C.prop_config.pca_prop_pflags, pc.pca_prop_pflags, "pca_prop_pflags");

      c.eq(C.iteration_index, ic.m_iteration_index, "iteration_index");
      c.eq(C.track_algorithm, ic.m_track_algorithm, "track_algorithm");
      c.eq(C.requires_seed_hit_sorting, ic.m_requires_seed_hit_sorting, "requires_seed_hit_sorting");
      c.eq(C.backward_search, ic.m_backward_search, "backward_search");
      c.eq(C.backward_drop_seed_hits, ic.m_backward_drop_seed_hits, "backward_drop_seed_hits");
      c.eq(C.backward_fit_min_hits, ic.m_backward_fit_min_hits, "backward_fit_min_hits");
      c.eq(C.merge_seed_hits_during_cleaning(), ic.merge_seed_hits_during_cleaning(), "merge_seed_hits");
      c.eq(C.sc_ptthr_hpt, ic.sc_ptthr_hpt, "sc_ptthr_hpt");
      c.eq(C.sc_drmax_bh, ic.sc_drmax_bh, "sc_drmax_bh");
      c.eq(C.sc_dzmax_bh, ic.sc_dzmax_bh, "sc_dzmax_bh");
      c.eq(C.sc_drmax_eh, ic.sc_drmax_eh, "sc_drmax_eh");
      c.eq(C.sc_dzmax_eh, ic.sc_dzmax_eh, "sc_dzmax_eh");
      c.eq(C.sc_drmax_bl, ic.sc_drmax_bl, "sc_drmax_bl");
      c.eq(C.sc_dzmax_bl, ic.sc_dzmax_bl, "sc_dzmax_bl");
      c.eq(C.sc_drmax_el, ic.sc_drmax_el, "sc_drmax_el");
      c.eq(C.sc_dzmax_el, ic.sc_dzmax_el, "sc_dzmax_el");
      c.eq(C.dc_fracSharedHits, ic.dc_fracSharedHits, "dc_fracSharedHits");
      c.eq(C.dc_drth_central, ic.dc_drth_central, "dc_drth_central");
      c.eq(C.dc_drth_obarrel, ic.dc_drth_obarrel, "dc_drth_obarrel");
      c.eq(C.dc_drth_forward, ic.dc_drth_forward, "dc_drth_forward");
      auto cmpParams = [&c](IterParams const& a, mkfit::IterationParams const& b, const char* n) {
        c.eq(a.nlayers_per_seed, b.nlayers_per_seed, n);
        c.eq(a.maxCandsPerSeed, b.maxCandsPerSeed, n);
        c.eq(a.maxHolesPerCand, b.maxHolesPerCand, n);
        c.eq(a.maxConsecHoles, b.maxConsecHoles, n);
        c.eq(a.chi2Cut_min, b.chi2Cut_min, n);
        c.eq(a.chi2CutOverlap, b.chi2CutOverlap, n);
        c.eq(a.pTCutOverlap, b.pTCutOverlap, n);
        c.eq(a.recheckOverlap, b.recheckOverlap, n);
        c.eq(a.useHitSelectionV2, b.useHitSelectionV2, n);
        c.eq(a.minHitsQF, b.minHitsQF, n);
        c.eq(a.minPtCut, b.minPtCut, n);
        c.eq(a.maxClusterSize, b.maxClusterSize, n);
      };
      cmpParams(C.params, ic.m_params, "params");
      cmpParams(C.backward_params, ic.m_backward_params, "backward_params");

      c.eq(C.n_regions, ic.m_n_regions, "n_regions");
      for (int r = 0; r < ic.m_n_regions; ++r) {
        c.eq(C.region_order[r], ic.m_region_order[r], "region_order", r);
        mkfit::SteeringParams const& sp = ic.m_steering_params[r];
        SteeringRegion const& s = C.steering_params[r];
        c.eq(s.region, sp.m_region, "region", r);
        c.eq(s.n_plan, sp.m_layer_plan.size(), "n_plan", r);
        c.eq(s.fwd_search_pickup, sp.m_fwd_search_pickup, "fwd_search_pickup", r);
        c.eq(s.bkw_fit_last, sp.m_bkw_fit_last, "bkw_fit_last", r);
        c.eq(s.bkw_search_pickup, sp.m_bkw_search_pickup, "bkw_search_pickup", r);
        c.eq(s.has_bksearch_plan(), sp.has_bksearch_plan(), "has_bksearch_plan", r);
        for (size_t i = 0; i < sp.m_layer_plan.size(); ++i)
          c.eq(s.layer[i], sp.m_layer_plan[i].m_layer, "layer_plan", r * 1000 + i);
        // the three stock iterators vs SteeringRegion::begin_index/end_index/step/is_pickup_only
        auto checkIter = [&](mkfit::SteeringParams::IterationType_e stype, IterationType type, const char* n) {
          auto it = sp.make_iterator(stype);
          int count = 0;
          for (int i = s.begin_index(type); i != s.end_index(type); i += s.step(type), ++it, ++count) {
            c.eq(it.is_valid(), true, n, r);
            if (!it.is_valid())
              break;
            c.eq(it.index(), i, n, r);
            c.eq(s.layer[i], it.layer(), n, r);
            if (stype != mkfit::SteeringParams::IT_BkwFit)
              c.eq(s.is_pickup_only(type, i), it.is_pickup_only(), n, r);
          }
          c.eq(it.is_valid(), false, n, r);  // stock iterator ends where ours does
          c.eq(count > 0, true, n, r);
        };
        checkIter(mkfit::SteeringParams::IT_FwdSearch, IT_FwdSearch, "fwd_search_iter");
        checkIter(mkfit::SteeringParams::IT_BkwFit, IT_BkwFit, "bkw_fit_iter");
        if (s.has_bksearch_plan())
          checkIter(mkfit::SteeringParams::IT_BkwSearch, IT_BkwSearch, "bkw_search_iter");
        // scorer: stock resolves empty names to the default scorer
        c.eq(static_cast<int>(s.track_scorer),
             static_cast<int>(sp.m_track_scorer_name.empty() ? C.default_track_scorer : TrackScorer::Default),
             "track_scorer",
             r);
        c.eq(bool(sp.m_track_scorer), true, "stock_track_scorer_set", r);
      }
      // function names -> enums (names as registered in MkStdSeqs.cc / MkSeedPartitioners-phase2.cc)
      c.eq(static_cast<int>(C.seed_cleaner) == 0, ic.m_seed_cleaner_name.empty(), "seed_cleaner");
      c.eq(static_cast<int>(C.seed_partitioner) == static_cast<int>(SeedPartitioner::Phase2_1),
           ic.m_seed_partitioner_name == "phase2:1",
           "seed_partitioner");
      c.eq(static_cast<int>(C.pre_bkfit_filter) == static_cast<int>(CandFilter::NHitsPixSeed),
           ic.m_pre_bkfit_filter_name == "phase1:qfilter_n_hits_pixseed",
           "pre_bkfit_filter");
      c.eq(static_cast<int>(C.post_bkfit_filter) == static_cast<int>(CandFilter::NHitsPixSeed),
           ic.m_post_bkfit_filter_name == "phase1:qfilter_n_hits_pixseed",
           "post_bkfit_filter");
      c.eq(static_cast<int>(C.duplicate_cleaner) == static_cast<int>(DuplicateCleaner::SharedHitsPixelSeed),
           ic.m_duplicate_cleaner_name == "phase1:clean_duplicates_sharedhits_pixelseed",
           "duplicate_cleaner");
      c.eq(static_cast<int>(C.default_track_scorer) == static_cast<int>(TrackScorer::Default),
           ic.m_default_track_scorer_name == "phase1:default" || ic.m_default_track_scorer_name == "default",
           "default_track_scorer");
      c.eq(bool(ic.m_seed_partitioner), C.seed_partitioner != SeedPartitioner::None, "stock_partitioner_set");
      c.eq(bool(ic.m_pre_bkfit_filter), C.pre_bkfit_filter != CandFilter::None, "stock_pre_filter_set");
      c.eq(bool(ic.m_post_bkfit_filter), C.post_bkfit_filter != CandFilter::None, "stock_post_filter_set");
      c.eq(bool(ic.m_duplicate_cleaner), C.duplicate_cleaner != DuplicateCleaner::None, "stock_dup_cleaner_set");
      c.eq(bool(ic.m_seed_cleaner), C.seed_cleaner != SeedCleaner::None, "stock_seed_cleaner_set");
      for (int l = 0; l < sz.nLayers; ++l)
        nHoleLayers += hL.const_view()[l].has_r_range_hole() ? 1 : 0;
      checks.push_back(c);
    }

    // ---------------- report
    std::ostringstream os;
    os << "MkFitESDataTester [" << EDM_STRINGIZE(ALPAKA_ACCELERATOR_NAMESPACE) << "] device "
       << alpaka::getName(alpaka::getDev(queue)) << "\n";
    os << "  sizes: layers " << sz.nLayers << " (" << bytesOf(hL) << " B), modules " << sz.nModules << " ("
       << bytesOf(hM) << " B), shapes " << sz.nShapes << " (" << bytesOf(hS) << " B), detid map capacity "
       << sz.detIdMapCapacity << " (" << bytesOf(hD) << " B, max probe " << hD.const_view().max_probe()
       << ", mean probe " << std::setprecision(3) << double(sumProbe) / std::max(1, sz.nModules) << "), material "
       << sz.matNBinsZ << " x " << sz.matNBinsR << " (" << bytesOf(hMat) << " B), config " << sizeof(ESConfig)
       << " B; total " << bytesOf(hL) + bytesOf(hM) + bytesOf(hS) + bytesOf(hD) + bytesOf(hMat) + sizeof(ESConfig)
       << " B\n";
    const ESConfig& C = hC.const_value();
    os << "  config: iteration " << C.iteration_index << ", regions " << C.n_regions << ", maxCandsPerSeed "
       << C.params.maxCandsPerSeed << ", chi2Cut_min " << C.params.chi2Cut_min << ", minPtCut " << C.params.minPtCut
       << ", maxClusterSize " << C.params.maxClusterSize << ", backward_search " << C.backward_search
       << ", usePropToPlane " << C.usePropToPlane << ", layers with r-hole " << nHoleLayers << "\n";
    os << "  probes: " << nDet << " detid lookups (" << nUnknownProbes << " unknown), " << nMat << " material lookups, "
       << nPr << " layer-predicate points\n";
    if (countPixelQuality_) {
      // MkFitEventOfHitsProducer (usePixelQualityDB = True in the HLT menu) turns every bad component with
      // errorType 0 into a DeadRegion of its layer; the device EventOfHits will need them if there are any.
      auto const& badList = iSetup.getData(pixelQualityToken_).getBadComponentList();
      int nType0 = 0;
      for (auto const& bp : badList)
        nType0 += bp.errorType == 0 ? 1 : 0;
      os << "  SiPixelQuality: " << badList.size() << " bad components, " << nType0
         << " with errorType 0 (-> mkFit dead regions)\n";
    }
    long totChecked = 0, totBad = 0;
    for (auto const& c : checks) {
      os << "  check " << std::left << std::setw(18) << c.name() << " checked " << std::setw(9) << c.checked()
         << " mismatches " << c.bad() << "\n";
      totChecked += c.checked();
      totBad += c.bad();
    }
    os << "  TOTAL checked " << totChecked << " mismatches " << totBad << (totBad == 0 ? "  -> PASS" : "  -> FAIL");
    edm::LogPrint("MkFitESDataTester") << os.str();
    if (totBad != 0)
      throw cms::Exception("MkFitESDataTester") << totBad << " mismatches against the stock ES objects";
  }

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE

#include "HeterogeneousCore/AlpakaCore/interface/alpaka/MakerMacros.h"
DEFINE_FWK_ALPAKA_MODULE(MkFitESDataTester);
