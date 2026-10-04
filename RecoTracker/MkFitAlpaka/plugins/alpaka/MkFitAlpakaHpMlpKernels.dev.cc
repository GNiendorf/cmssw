// Stage C, HP option (b) (lane hpsel draft, built and validated by lane stagec in round 8): the initial step's HP Torch
// classifier as a hand-written Alpaka MLP (no libtorch on the device path). Weights: the BN-folded float32 program,
// folded from model.pt by the producer (12 layers: 4 x dense+ELU, fc_in dense+ELU, 3 x residual, output), each W
// stored transposed [nIn][nOut] so that consecutive threads (consecutive outputs) read consecutive weights.
// Arithmetic order (acc = b; acc += x_k * W_ok for k = 0..nIn-1; float32) = r7_hpsel ana/hp_weights_check.py.
// GPU layout: one 128-thread block per tile of kTile tracks; the tile's activations in shared memory (ping-pong
// 2 x kTile x 256 floats + the residual skip, ~34 kB for kTile = 16); no per-thread arrays (LOCAL must stay 0).
#include <cmath>

#include <alpaka/alpaka.hpp>

#include "HeterogeneousCore/AlpakaInterface/interface/config.h"
#include "HeterogeneousCore/AlpakaInterface/interface/workdivision.h"
#include "RecoTracker/FinalTrackSelectors/interface/TrackTorchClassifierFeaturesSoA.h"

#include "MkFitAlpakaHpMlpKernels.h"

namespace ALPAKA_ACCELERATOR_NAMESPACE::mkfitdev::hpsel {

  using namespace cms::alpakatools;
  using ::mkfitdev::hpsel::MlpProgram;

  namespace {
    constexpr int kTile = 16;  // tracks per block
    constexpr int kWidth = ::mkfitdev::hpsel::kMlpMaxWidth;
    constexpr int kSkipWidth = ::mkfitdev::hpsel::kMlpSkipWidth;

    ALPAKA_FN_ACC ALPAKA_FN_INLINE float feature(TrackTorchClassifierFeaturesSoA::ConstView in, int g, int k) {
      switch (k) {  // the model's input order (TrackTorchClassifierAlpaka)
        case 0:
          return in[g].dxyBeamSpot();
        case 1:
          return in[g].dzBeamSpot();
        case 2:
          return in[g].dxyError();
        case 3:
          return in[g].dzError();
        case 4:
          return in[g].normalizedChi2();
        case 5:
          return in[g].eta();
        case 6:
          return in[g].phi();
        case 7:
          return in[g].etaError();
        case 8:
          return in[g].phiError();
        case 9:
          return in[g].ndof();
        case 10:
          return in[g].lostInnerHits();
        case 11:
          return in[g].lostOuterHits();
        case 12:
          return in[g].layersWithoutMeas();
        case 13:
          return in[g].validPixelHits();
        default:
          return in[g].validStripHits();
      }
    }

    template <typename TAcc>
    ALPAKA_FN_ACC ALPAKA_FN_INLINE float elu(TAcc const& acc, float x) {
      return x > 0.f ? x : alpaka::math::exp(acc, x) - 1.f;
    }

    // DEVIATION D5: float32 MLP re-implementation of the libtorch classifier (|dscore| ~1e-6, 0 HP flips measured)
    struct KernelHpMlp {
      ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                    TrackTorchClassifierFeaturesSoA::ConstView in,
                                    int n,
                                    MlpProgram prog,
                                    float const* __restrict__ w,
                                    TrackTorchClassifierScoresSoA::View out,
                                    ::mkfitdev::hpsel::HpRule rule,
                                    float* hpMask) const {
        auto& bufA = alpaka::declareSharedVar<float[kTile * kWidth], __COUNTER__>(acc);
        auto& bufB = alpaka::declareSharedVar<float[kTile * kWidth], __COUNTER__>(acc);
        auto& skip = alpaka::declareSharedVar<float[kTile * kSkipWidth], __COUNTER__>(acc);
        for (auto tile : independent_groups(acc, divide_up_by(n, kTile))) {
          int const t0 = tile * kTile;
          // inputs, normalised as the TorchScript wrapper: (x - min) / (max - min + 1e-8), float32
          for (auto idx : independent_group_elements(acc, kTile * MlpProgram::kNIn)) {
            int const t = idx / MlpProgram::kNIn, k = idx % MlpProgram::kNIn;
            int const g = t0 + t;
            float const x = g < n ? feature(in, g, k) : 0.f;
            bufA[t * kWidth + k] = (x - w[prog.minOffset + k]) / w[prog.denOffset + k];
          }
          alpaka::syncBlockThreads(acc);
          float* cur = bufA;
          float* nxt = bufB;
          for (int l = 0; l < prog.nLayers; ++l) {
            auto const L = prog.layers[l];
            if (L.kind == MlpProgram::kResFirst) {  // keep the block input for the skip connection
              for (auto idx : independent_group_elements(acc, kTile * kSkipWidth)) {
                int const t = idx / kSkipWidth, k = idx % kSkipWidth;
                skip[idx] = cur[t * kWidth + k];
              }
            }
#if defined(ALPAKA_ACC_GPU_CUDA_ENABLED) || defined(ALPAKA_ACC_GPU_HIP_ENABLED)
            // GPU: one thread per (track, output); the k loop reads W^T[k][o] coalesced across the warp
            for (auto idx : independent_group_elements(acc, kTile * L.nOut)) {
              int const t = idx / L.nOut, o = idx % L.nOut;
              float a = w[L.bOffset + o];
              float const* x = cur + t * kWidth;
              for (int k = 0; k < L.nIn; ++k)
                a += x[k] * w[L.wOffset + k * L.nOut + o];  // W^T[k][o]
              if (L.kind == MlpProgram::kResSecond)
                a = skip[t * kSkipWidth + o] + a;
              nxt[t * kWidth + o] = (L.kind == MlpProgram::kOutput) ? a : elu(acc, a);
            }
#else
            // CPU backends (one thread per block in CMSSW): the same sums in the same k order, with the o loop
            // innermost over contiguous W^T rows so that it vectorises. Serial cost 10.1-10.3 ms per job event vs
            // libtorch 4.5-4.7 (ttbar; a k -> track -> o order that reads each W^T row once per tile measured the same,
            // so the scalar exp of the ELUs is the likely limit): use the kernel on GPU menus only
            if (alpaka::getIdx<alpaka::Block, alpaka::Threads>(acc)[0u] == 0u) {
              for (int t = 0; t < kTile; ++t) {
                float* y = nxt + t * kWidth;
                float const* x = cur + t * kWidth;
                float const* bb = w + L.bOffset;
                for (int o = 0; o < L.nOut; ++o)
                  y[o] = bb[o];
                for (int k = 0; k < L.nIn; ++k) {
                  float const xk = x[k];
                  float const* wk = w + L.wOffset + k * L.nOut;
                  for (int o = 0; o < L.nOut; ++o)
                    y[o] += xk * wk[o];
                }
                for (int o = 0; o < L.nOut; ++o) {
                  float a = y[o];
                  if (L.kind == MlpProgram::kResSecond)
                    a = skip[t * kSkipWidth + o] + a;
                  y[o] = (L.kind == MlpProgram::kOutput) ? a : elu(acc, a);
                }
              }
            }
#endif
            alpaka::syncBlockThreads(acc);
            float* tmp = cur;
            cur = nxt;
            nxt = tmp;
          }
          for (auto t : independent_group_elements(acc, kTile)) {
            int const g = t0 + t;
            if (g < n) {
              float const s = 1.f / (1.f + alpaka::math::exp(acc, -cur[t * kWidth]));
              out[g].score() = s;
              if (hpMask != nullptr) {  // TrackTorchClassifierFromSoA's rule
                bool const hiDxy = alpaka::math::abs(acc, in[g].dxyBeamSpot()) > rule.dxyThreshold;
                hpMask[g] = (s >= rule.minScore || (hiDxy && s >= rule.highDxyMinScore)) ? 1.f : 0.f;
              }
            }
          }
          alpaka::syncBlockThreads(acc);
        }
      }
    };
  }  // namespace

  void launchMlp(Queue& queue,
                 TrackTorchClassifierFeaturesSoA::ConstView in,
                 int n,
                 MlpProgram const& prog,
                 float const* w,
                 TrackTorchClassifierScoresSoA::View out,
                 ::mkfitdev::hpsel::HpRule const& rule,
                 float* hpMask) {
    if (n <= 0)
      return;
    alpaka::exec<Acc1D>(
        queue, make_workdiv<Acc1D>(divide_up_by(n, kTile), 128), KernelHpMlp{}, in, n, prog, w, out, rule, hpMask);
  }

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE::mkfitdev::hpsel
