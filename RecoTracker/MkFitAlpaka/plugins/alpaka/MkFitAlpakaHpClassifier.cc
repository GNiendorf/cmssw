// Stage C, HP option (b): drop-in replacement of TrackTorchClassifierAlpaka@alpaka on GPU backends (no libtorch on the
// device path, no per-stream libtorch memory pools): same input (TrackFeaturesDeviceCollection), same output
// (TrackScoresDeviceCollection, so TrackTorchClassifierFromSoA is unchanged), plus an optional device HP decision
// (instance "hpMask", 0/1 per track, TrackTorchClassifierFromSoA's rule).
// Weights: DERIVED FROM model.pt at construction (libtorch on the host, once per module instance): BatchNorm (eval,
// eps 1e-5) folded into the preceding Linear in double, rounded once to float32, as r7_hpsel ana/hp_export_weights.py;
// W stored transposed [nIn][nOut]. No second copy of the model in the package. checkProgramFile (untracked) compares the
// folded program byte by byte with an exported .bin (validation only).
// One device copy of the weights per stream (FixedQueue: the device of a stream is fixed), ~215 kB each.
// Round 9 (R8-M5, R8 lane L1): the fold runs once per job and model file (a process-wide cache shared by the stream
// instances), followed by a PROBE SELF-TEST: libtorch's own forward() and the folded program (evaluated on the host in
// the kernel's float order) on 256 fixed probe vectors spread over [feature_min, feature_max] (+ 16 just outside);
// any |dscore| >= 1e-5 throws. A retrained model.pt whose forward() differs from the folded graph (activation, skip
// placement, eps, input order) is refused instead of being folded silently into a different function.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <mutex>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <torch/script.h>

#include <alpaka/alpaka.hpp>

#include "FWCore/MessageLogger/interface/MessageLogger.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/FileInPath.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include "FWCore/Utilities/interface/Exception.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/EDGetToken.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/EDPutToken.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/Event.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/EventSetup.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/MakerMacros.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/stream/FixedQueueEDProducer.h"
#include "HeterogeneousCore/AlpakaInterface/interface/config.h"
#include "HeterogeneousCore/AlpakaInterface/interface/memory.h"
#include "RecoTracker/FinalTrackSelectors/interface/alpaka/TrackFeaturesDeviceCollection.h"
#include "RecoTracker/FinalTrackSelectors/interface/alpaka/TrackScoresDeviceCollection.h"

#include "MkFitAlpakaHpMlpKernels.h"

namespace ALPAKA_ACCELERATOR_NAMESPACE {

  class MkFitAlpakaHpClassifier : public stream::FixedQueueEDProducer<> {
  public:
    explicit MkFitAlpakaHpClassifier(edm::ParameterSet const& iConfig)
        : FixedQueueEDProducer<>(iConfig),
          featuresToken_{consumes(iConfig.getParameter<edm::InputTag>("features"))},
          scoresToken_{produces()},
          produceMask_{iConfig.getParameter<bool>("produceHpMask")} {
      if (produceMask_)
        maskToken_ = produces("hpMask");
      rule_.minScore = iConfig.getParameter<double>("minScore");
      rule_.dxyThreshold = iConfig.getParameter<double>("dxyThreshold");
      rule_.highDxyMinScore = iConfig.getParameter<double>("highDxyMinScore");
      auto const folded = foldedProgram(iConfig.getParameter<edm::FileInPath>("modelPath").fullPath());
      prog_ = folded->prog;
      wHost_ = folded->w;
      auto const check = iConfig.getUntrackedParameter<std::string>("checkProgramFile");
      if (!check.empty())
        checkProgram(check);
    }

    static void fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
      edm::ParameterSetDescription desc;
      desc.add<edm::InputTag>("features", edm::InputTag("hltInitialStepTrackFeaturesDevice"));
      desc.add<edm::FileInPath>("modelPath",
                                edm::FileInPath("RecoTracker/FinalTrackSelectors/data/TrackTorchClassifier/model.pt"));
      desc.add<bool>("produceHpMask", false)->setComment("also the HP decision per track (instance 'hpMask', 0/1)");
      desc.add<double>("minScore", 0.377)->setComment("TrackTorchClassifierFromSoA working point (hpMask only)");
      desc.add<double>("dxyThreshold", 0.5);
      desc.add<double>("highDxyMinScore", 0.267);
      desc.addUntracked<std::string>("checkProgramFile", "")
          ->setComment("validation: compare the folded program with this exported .bin, byte by byte");
      descriptions.addWithDefaultLabel(desc);
    }

    void beginStream(edm::StreamID, Queue queue) override {
      wDev_ = cms::alpakatools::make_device_buffer<float[]>(queue, static_cast<int>(wHost_.size()));
      auto hv = cms::alpakatools::make_host_view(wHost_.data(), static_cast<int>(wHost_.size()));
      alpaka::memcpy(queue, *wDev_, hv);
      alpaka::wait(queue);  // once per stream
    }

    void produce(device::Event& iEvent, device::EventSetup const&) override {
      auto const& f = iEvent.get(featuresToken_);
      int const n = f.const_view().metadata().size();
      TrackScoresDeviceCollection out(iEvent.queue(), n);
      std::optional<TrackScoresDeviceCollection> mask;
      if (produceMask_)
        mask.emplace(iEvent.queue(), n);
      ALPAKA_ACCELERATOR_NAMESPACE::mkfitdev::hpsel::launchMlp(iEvent.queue(),
                                                               f.const_view(),
                                                               n,
                                                               prog_,
                                                               wDev_->data(),
                                                               out.view(),
                                                               rule_,
                                                               produceMask_ ? mask->view().score().data() : nullptr);
      iEvent.emplace(scoresToken_, std::move(out));
      if (produceMask_)
        iEvent.emplace(maskToken_, std::move(*mask));
    }

  private:
    using MlpProgram = ::mkfitdev::hpsel::MlpProgram;
    struct Folded {
      MlpProgram prog;
      std::vector<float> w;
    };

    // once per job and model file: fold + probe self-test (R8-M5)
    static std::shared_ptr<const Folded> foldedProgram(std::string const& path) {
      static std::mutex mutex;
      static std::map<std::string, std::shared_ptr<const Folded>> cache;
      std::lock_guard<std::mutex> lock(mutex);
      auto& entry = cache[path];
      if (!entry) {
        auto f = std::make_shared<Folded>();
        torch::jit::script::Module m = torch::jit::load(path, torch::kCPU);
        m.eval();
        foldModel(m, path, f->prog, f->w);
        probeSelfTest(m, path, f->prog, f->w);
        entry = std::move(f);
      }
      return entry;
    }

    // the folded program on the host, in the kernel's float order (CPU-backend loop: acc = b; acc += x_k W^T[k][o])
    static float evalFolded(MlpProgram const& prog, std::vector<float> const& w, float const* feat) {
      std::vector<float> cur(::mkfitdev::hpsel::kMlpMaxWidth), nxt(::mkfitdev::hpsel::kMlpMaxWidth),
          skip(::mkfitdev::hpsel::kMlpSkipWidth);
      for (int k = 0; k < MlpProgram::kNIn; ++k)
        cur[k] = (feat[k] - w[prog.minOffset + k]) / w[prog.denOffset + k];
      for (int l = 0; l < prog.nLayers; ++l) {
        auto const& L = prog.layers[l];
        if (L.kind == MlpProgram::kResFirst)
          std::copy(cur.begin(), cur.begin() + ::mkfitdev::hpsel::kMlpSkipWidth, skip.begin());
        for (int o = 0; o < L.nOut; ++o)
          nxt[o] = w[L.bOffset + o];
        for (int k = 0; k < L.nIn; ++k)
          for (int o = 0; o < L.nOut; ++o)
            nxt[o] += cur[k] * w[L.wOffset + k * L.nOut + o];
        for (int o = 0; o < L.nOut; ++o) {
          float a = nxt[o];
          if (L.kind == MlpProgram::kResSecond)
            a = skip[o] + a;
          nxt[o] = (L.kind == MlpProgram::kOutput) ? a : (a > 0.f ? a : std::exp(a) - 1.f);
        }
        std::swap(cur, nxt);
      }
      return 1.f / (1.f + std::exp(-cur[0]));
    }

    static void probeSelfTest(torch::jit::script::Module& m,
                              std::string const& path,
                              MlpProgram const& prog,
                              std::vector<float> const& w) {
      constexpr int kInside = 256, kOutside = 16, kN = kInside + kOutside, kNIn = MlpProgram::kNIn;
      std::vector<float> x(size_t(kN) * kNIn);
      for (int i = 0; i < kN; ++i)
        for (int k = 0; k < kNIn; ++k) {
          float const lo = w[prog.minOffset + k], span = w[prog.denOffset + k] - 1e-8f;
          // inside: a fixed stratified spread per feature (coprime strides decorrelate the features);
          // outside: 10% beyond either end of the training range
          float const u = i < kInside ? (float((i * (2 * k + 3) + 7 * k) % kInside) + 0.5f) / kInside
                                      : ((i - kInside + k) % 2 == 0 ? -0.1f : 1.1f);
          x[size_t(i) * kNIn + k] = lo + u * span;
        }
      auto const in = torch::from_blob(x.data(), {kN, kNIn}, torch::kFloat32).clone();
      torch::NoGradGuard noGrad;
      auto const outT = m.forward({in}).toTensor().to(torch::kFloat32).contiguous().reshape({-1});
      if (outT.numel() != kN)
        throw cms::Exception("Configuration") << "MkFitAlpakaHpClassifier: " << path << " forward() returned "
                                              << outT.numel() << " values for " << kN << " probes";
      auto const* ref = outT.data_ptr<float>();
      double maxd = 0;
      int worst = 0;
      for (int i = 0; i < kN; ++i) {
        double const d = std::abs(double(evalFolded(prog, w, &x[size_t(i) * kNIn])) - double(ref[i]));
        if (!(d <= maxd))  // NaN-safe: a NaN difference is the worst
          maxd = d, worst = i;
      }
      if (!(maxd < 1e-5))
        throw cms::Exception("Configuration")
            << "MkFitAlpakaHpClassifier: the folded program does not reproduce " << path << " forward(): max |dscore| "
            << maxd << " at probe " << worst << " (model graph outside what foldModel transcribes)";
      edm::LogPrint("MkFitAlpakaHpClassifier") << "[hpmlp] probe self-test vs libtorch forward(): " << kN
                                               << " probes, max |dscore| " << maxd << " (limit 1e-5): PASS";
    }

    // model.pt (TorchScript) -> the folded program. Layer names as in model.pt (r7_hpsel ana/hp_export_weights.py):
    // 4 x (Linear initial_layers.{0,4,8,12} + BatchNorm .{1,5,9,13}, ELU) -> fc_in (Linear, ELU) -> 3 x residual
    // {block.0 + BN block.1, ELU, block.4 + BN block.5, + skip, ELU} -> out (Linear) -> sigmoid.
    static void foldModel(torch::jit::script::Module& m,
                          std::string const& path,
                          MlpProgram& prog,
                          std::vector<float>& w) {
      std::map<std::string, std::vector<double>> val;
      std::map<std::string, std::vector<int64_t>> shape;
      auto take = [&](std::string const& name, torch::Tensor const& t) {
        if (t.scalar_type() == torch::kLong)
          return;  // num_batches_tracked
        auto const d = t.detach().to(torch::kDouble).contiguous();
        val[name] = std::vector<double>(d.data_ptr<double>(), d.data_ptr<double>() + d.numel());
        shape[name] = std::vector<int64_t>(d.sizes().begin(), d.sizes().end());
      };
      for (auto const& p : m.named_parameters(true))
        take(p.name, p.value);
      for (auto const& b : m.named_buffers(true))
        take(b.name, b.value);
      auto get = [&](std::string const& name) -> std::vector<double> const& {
        auto it = val.find(name);
        if (it == val.end())
          throw cms::Exception("Configuration") << "MkFitAlpakaHpClassifier: " << path << " has no tensor " << name
                                                << " (unexpected model structure)";
        return it->second;
      };
      // input normalisation: (x - min) / (max - min + 1e-8) in float32, as the TorchScript wrapper
      auto const& fmin = get("feature_min");
      auto const& fmax = get("feature_max");
      if (fmin.size() != size_t(MlpProgram::kNIn) || fmax.size() != size_t(MlpProgram::kNIn))
        throw cms::Exception("Configuration") << "MkFitAlpakaHpClassifier: expected 15 input features in " << path;
      w.clear();
      for (int k = 0; k < MlpProgram::kNIn; ++k)
        w.push_back(static_cast<float>(fmin[k]));
      for (int k = 0; k < MlpProgram::kNIn; ++k) {
        float const den = static_cast<float>(fmax[k]) - static_cast<float>(fmin[k]);
        w.push_back(den + 1e-8f);
      }
      prog.minOffset = 0;
      prog.denOffset = MlpProgram::kNIn;
      prog.nLayers = 0;
      auto addLayer = [&](std::string const& lin, std::string const& bn, int kind) {
        auto const& W = get(lin + ".weight");
        auto const& b = get(lin + ".bias");
        int const nOut = shape[lin + ".weight"].at(0), nIn = shape[lin + ".weight"].at(1);
        std::vector<double> s(nOut, 1.);
        if (!bn.empty()) {  // eval BatchNorm: y = (x - mean) * gamma / sqrt(var + eps) + beta
          auto const& g = get(bn + ".weight");
          auto const& beta = get(bn + ".bias");
          auto const& mean = get(bn + ".running_mean");
          auto const& var = get(bn + ".running_var");
          for (int o = 0; o < nOut; ++o)
            s[o] = g[o] / std::sqrt(var[o] + 1e-5);
          if (prog.nLayers >= MlpProgram::kMaxLayers)
            throw cms::Exception("Configuration") << "MkFitAlpakaHpClassifier: too many layers";
          auto& L = prog.layers[prog.nLayers];
          L.nOut = nOut;
          L.nIn = nIn;
          L.kind = kind;
          L.wOffset = static_cast<int>(w.size());
          for (int k = 0; k < nIn; ++k)
            for (int o = 0; o < nOut; ++o)
              w.push_back(static_cast<float>(W[size_t(o) * nIn + k] * s[o]));
          L.bOffset = static_cast<int>(w.size());
          for (int o = 0; o < nOut; ++o)
            w.push_back(static_cast<float>((b[o] - mean[o]) * s[o] + beta[o]));
        } else {
          if (prog.nLayers >= MlpProgram::kMaxLayers)
            throw cms::Exception("Configuration") << "MkFitAlpakaHpClassifier: too many layers";
          auto& L = prog.layers[prog.nLayers];
          L.nOut = nOut;
          L.nIn = nIn;
          L.kind = kind;
          L.wOffset = static_cast<int>(w.size());
          for (int k = 0; k < nIn; ++k)
            for (int o = 0; o < nOut; ++o)
              w.push_back(static_cast<float>(W[size_t(o) * nIn + k]));
          L.bOffset = static_cast<int>(w.size());
          for (int o = 0; o < nOut; ++o)
            w.push_back(static_cast<float>(b[o]));
        }
        auto const& L = prog.layers[prog.nLayers];
        if (L.nOut > ::mkfitdev::hpsel::kMlpMaxWidth || L.nIn > ::mkfitdev::hpsel::kMlpMaxWidth ||
            ((kind == MlpProgram::kResFirst || kind == MlpProgram::kResSecond) &&
             (L.nIn != ::mkfitdev::hpsel::kMlpSkipWidth || L.nOut != ::mkfitdev::hpsel::kMlpSkipWidth)) ||
            (kind == MlpProgram::kOutput && L.nOut != 1) ||
            (prog.nLayers == 0 ? L.nIn != MlpProgram::kNIn : L.nIn != prog.layers[prog.nLayers - 1].nOut))
          throw cms::Exception("Configuration") << "MkFitAlpakaHpClassifier: layer " << lin << " (" << L.nOut << " x "
                                                << L.nIn << ") outside the kernel's shape envelope";
        ++prog.nLayers;
      };
      for (int i : {0, 4, 8, 12})
        addLayer("model.initial_layers." + std::to_string(i), "model.initial_layers." + std::to_string(i + 1),
                 MlpProgram::kDense);
      addLayer("model.fc_in", "", MlpProgram::kDense);
      for (int r = 0; r < 3; ++r) {
        std::string const blk = "model.res_blocks." + std::to_string(r) + ".block.";
        addLayer(blk + "0", blk + "1", MlpProgram::kResFirst);
        addLayer(blk + "4", blk + "5", MlpProgram::kResSecond);
      }
      addLayer("model.out", "", MlpProgram::kOutput);
    }

    // validation: the exported .bin (r7_hpsel ana/hp_export_weights.py layout: W row-major [nOut][nIn]) vs the fold
    void checkProgram(std::string const& path) const {
      std::ifstream in(path, std::ios::binary);
      std::vector<char> buf((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
      size_t off = 0;
      auto rd = [&](void* dst, size_t nbytes) {
        if (off + nbytes > buf.size())
          throw cms::Exception("Configuration") << "MkFitAlpakaHpClassifier: truncated " << path;
        std::memcpy(dst, buf.data() + off, nbytes);
        off += nbytes;
      };
      int32_t magic = 0, nl = 0;
      rd(&magic, 4);
      rd(&nl, 4);
      long diff = 0, total = 0;
      if (magic != 0x48505331 || nl != prog_.nLayers)
        throw cms::Exception("Configuration") << "MkFitAlpakaHpClassifier: " << path << " layer count " << nl
                                              << " vs folded " << prog_.nLayers;
      for (int l = 0; l < nl; ++l) {
        int32_t s[3];
        rd(s, 12);
        if (s[0] != prog_.layers[l].nOut || s[1] != prog_.layers[l].nIn || s[2] != prog_.layers[l].kind)
          throw cms::Exception("Configuration") << "MkFitAlpakaHpClassifier: layer " << l << " shape differs in " << path;
      }
      std::vector<float> head(2 * MlpProgram::kNIn);
      rd(head.data(), head.size() * sizeof(float));
      for (size_t k = 0; k < head.size(); ++k, ++total)
        diff += std::memcmp(&head[k], &wHost_[k], sizeof(float)) != 0;
      for (int l = 0; l < nl; ++l) {
        auto const& L = prog_.layers[l];
        std::vector<float> W(size_t(L.nOut) * L.nIn), b(L.nOut);
        rd(W.data(), W.size() * sizeof(float));
        rd(b.data(), b.size() * sizeof(float));
        for (int o = 0; o < L.nOut; ++o) {
          for (int k = 0; k < L.nIn; ++k, ++total)
            diff += std::memcmp(&W[size_t(o) * L.nIn + k], &wHost_[L.wOffset + size_t(k) * L.nOut + o], 4) != 0;
          diff += std::memcmp(&b[o], &wHost_[L.bOffset + o], 4) != 0;
          ++total;
        }
      }
      edm::LogPrint("MkFitAlpakaHpClassifier")
          << "[hpmlp] folded program vs " << path << ": " << total << " floats, " << diff << " differ (bytes)"
          << (off == buf.size() ? "" : ", TRAILING BYTES in the file");
    }

    const device::EDGetToken<TrackFeaturesDeviceCollection> featuresToken_;
    const device::EDPutToken<TrackScoresDeviceCollection> scoresToken_;
    const bool produceMask_;
    device::EDPutToken<TrackScoresDeviceCollection> maskToken_;
    MlpProgram prog_;
    ::mkfitdev::hpsel::HpRule rule_;
    std::vector<float> wHost_;
    std::optional<cms::alpakatools::device_buffer<Device, float[]>> wDev_;
  };

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE

DEFINE_FWK_ALPAKA_MODULE(MkFitAlpakaHpClassifier);
