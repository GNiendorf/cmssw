// MkFitCore's portable propagation (R, Z, plane, with material) and plane Kalman update on an Alpaka device, one
// track per thread (N = 1), against the same functions on the host with MkFitCore's CPU width (N = 8).

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

#include <alpaka/alpaka.hpp>

#define CATCH_CONFIG_MAIN
#include <catch2/catch_all.hpp>

#include "FWCore/Utilities/interface/stringize.h"
#include "HeterogeneousCore/AlpakaInterface/interface/config.h"
#include "HeterogeneousCore/AlpakaInterface/interface/memory.h"
#include "HeterogeneousCore/AlpakaInterface/interface/workdivision.h"
#include "RecoTracker/MkFitCore/interface/Config.h"
#include "RecoTracker/MkFitCore/interface/portable/KalmanUtilsMPlex.h"
#include "RecoTracker/MkFitCore/interface/portable/PropagationMPlex.h"

using namespace ALPAKA_ACCELERATOR_NAMESPACE;

namespace {

  constexpr int kHostWidth = 8;  // NN of MkFitCore's CPU build (x86-64-v3)

  enum class Target { R, Z, Plane, PlaneUpdate };

  struct TrackState {
    float par[6];   // x, y, z, 1/pT, phi, theta
    float err[21];  // lower triangle of the symmetric 6x6 covariance
    int charge;
  };

  struct Destination {
    float value;   // R or Z
    float pnt[3];  // a point on the plane
    float nrm[3];  // the plane normal
    float dir[3];  // a direction in the plane
    float hit[3];  // a hit on the plane (PlaneUpdate)
    float hitErr[6];
  };

  struct Result {
    float par[6];
    float err[21];
    float chi2;
    int fail;
  };

  // Propagates tracks [first, first + count) in the N lanes of one Matriplex; unused lanes repeat the first track.
  template <int N>
  ALPAKA_FN_HOST_ACC void propagateLanes(Target target,
                                         TrackState const* in,
                                         Destination const* dest,
                                         Result* out,
                                         int first,
                                         int count,
                                         mkfit::PropagationFlags const& pflags) {
    using namespace mkfit::portable;
    MPlexLS<N> inErr, outErr;
    MPlexLV<N> inPar, outPar;
    MPlexQI<N> inChg, failFlag;
    MPlexQF<N> msValue;
    MPlexHV<N> plPnt, plNrm, plDir, msPar;
    MPlexHS<N> msErr;
    MPlexQF<N> chi2(0.f);
    for (int n = 0; n < N; ++n) {
      const int i = first + (n < count ? n : 0);
      for (int k = 0; k < 6; ++k)
        inPar(n, k, 0) = in[i].par[k];
      for (int k = 0; k < 21; ++k)
        inErr.fArray[k * N + n] = in[i].err[k];
      inChg(n, 0, 0) = in[i].charge;
      msValue(n, 0, 0) = dest[i].value;
      for (int k = 0; k < 3; ++k) {
        plPnt(n, k, 0) = dest[i].pnt[k];
        plNrm(n, k, 0) = dest[i].nrm[k];
        plDir(n, k, 0) = dest[i].dir[k];
        msPar(n, k, 0) = dest[i].hit[k];
      }
      for (int k = 0; k < 6; ++k)
        msErr.fArray[k * N + n] = dest[i].hitErr[k];
      failFlag(n, 0, 0) = 0;
    }
    switch (target) {
      case Target::R:
        propagateHelixToRMPlex(inErr, inPar, inChg, msValue, outErr, outPar, failFlag, count, pflags);
        break;
      case Target::Z:
        propagateHelixToZMPlex(inErr, inPar, inChg, msValue, outErr, outPar, failFlag, count, pflags);
        break;
      case Target::Plane:
        propagateHelixToPlaneMPlex(inErr, inPar, inChg, plPnt, plNrm, outErr, outPar, failFlag, count, pflags);
        break;
      case Target::PlaneUpdate: {
        MPlexLS<N> propErr;
        MPlexLV<N> propPar;
        propagateHelixToPlaneMPlex(inErr, inPar, inChg, plPnt, plNrm, propErr, propPar, failFlag, count, pflags);
        kalmanOperationPlaneLocal<N, mkfit::cpe_cf*>(
            mkfit::KFO_Calculate_Chi2 | mkfit::KFO_Update_Params | mkfit::KFO_Local_Cov,
            propErr,
            propPar,
            inChg,
            msErr,
            msPar,
            plNrm,
            plDir,
            plPnt,
            outErr,
            outPar,
            chi2,
            count,
            nullptr,
            nullptr,
            &pflags.env);
        break;
      }
    }
    for (int n = 0; n < count; ++n) {
      for (int k = 0; k < 6; ++k)
        out[first + n].par[k] = outPar(n, k, 0);
      for (int k = 0; k < 21; ++k)
        out[first + n].err[k] = outErr.fArray[k * N + n];
      out[first + n].chi2 = chi2(n, 0, 0);
      out[first + n].fail = failFlag(n, 0, 0);
    }
  }

  struct PropagateKernel {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  Target target,
                                  TrackState const* __restrict__ in,
                                  Destination const* __restrict__ dest,
                                  Result* __restrict__ out,
                                  int size,
                                  mkfit::PropagationFlags pflags) const {
      for (auto i : cms::alpakatools::uniform_elements(acc, size)) {
        propagateLanes<1>(target, in, dest, out, i, 1, pflags);
      }
    }
  };

  // A (|z|, r) material map with thin layers every 10 cm in r and in |z|, 1 cm bins.
  std::vector<mkfit::PropagationEnv::Material> makeMaterial(int nBinsZ, int nBinsR) {
    std::vector<mkfit::PropagationEnv::Material> mat(nBinsZ * nBinsR);
    for (int iz = 0; iz < nBinsZ; ++iz)
      for (int ir = 0; ir < nBinsR; ++ir)
        if (ir % 10 == 5 || (iz % 10 == 5 && ir > 20)) {
          mat[iz * nBinsR + ir].radl = 0.01f + 0.0002f * ir;
          mat[iz * nBinsR + ir].bbxi = 2.e-5f + 1.e-7f * iz;
        }
    return mat;
  }

  void makeTracks(Target target, int size, std::vector<TrackState>& tracks, std::vector<Destination>& dests) {
    std::mt19937 gen(12345 + static_cast<int>(target));
    std::uniform_real_distribution<float> uni(0.f, 1.f);
    tracks.resize(size);
    dests.resize(size);
    for (int i = 0; i < size; ++i) {
      TrackState& trk = tracks[i];
      const float radius = 3.f + 27.f * uni(gen);
      const float posPhi = mkfit::Const::TwoPI * uni(gen) - mkfit::Const::PI;
      const float pt = 0.8f + 19.2f * uni(gen);
      const float eta =
          target == Target::Z ? (uni(gen) < 0.5f ? -1.f : 1.f) * (1.4f + 1.1f * uni(gen)) : -1.5f + 3.f * uni(gen);
      trk.par[0] = radius * std::cos(posPhi);
      trk.par[1] = radius * std::sin(posPhi);
      trk.par[2] = -15.f + 30.f * uni(gen);
      trk.par[3] = 1.f / pt;
      trk.par[4] = posPhi + 0.2f * (uni(gen) - 0.5f);
      trk.par[5] = 2.f * std::atan(std::exp(-eta));
      trk.charge = uni(gen) < 0.5f ? -1 : 1;
      std::fill(std::begin(trk.err), std::end(trk.err), 0.f);
      const float diag[6] = {1.e-4f, 1.e-4f, 4.e-4f, 1.e-4f * trk.par[3] * trk.par[3], 1.e-5f, 1.e-5f};
      for (int k = 0; k < 6; ++k)
        trk.err[k * (k + 1) / 2 + k] = diag[k];
      trk.err[1] = 2.e-5f;   // (1, 0)
      trk.err[18] = 1.e-6f;  // (5, 3)

      Destination& dst = dests[i];
      const float step = 5.f + 35.f * uni(gen);
      const float rTarget = radius + step;
      dst.value = target == Target::Z ? trk.par[2] + (eta > 0 ? step : -step) : rTarget;
      dst.pnt[0] = rTarget * std::cos(posPhi);
      dst.pnt[1] = rTarget * std::sin(posPhi);
      dst.pnt[2] = trk.par[2];
      dst.nrm[0] = std::cos(posPhi);
      dst.nrm[1] = std::sin(posPhi);
      dst.nrm[2] = 0.f;
      dst.dir[0] = 0.f;
      dst.dir[1] = 0.f;
      dst.dir[2] = 1.f;
      // 30 um x 30 um x 100 um, symmetric 3x3 lower triangle
      const float hitErr[6] = {9.e-6f, 0.f, 9.e-6f, 0.f, 0.f, 1.e-4f};
      std::copy(std::begin(hitErr), std::end(hitErr), dst.hitErr);
    }
  }

  // relative agreement to float rounding accumulated over one propagation step
  bool close(float device, float host, float tolerance, float floor) {
    return std::abs(device - host) <= tolerance * std::max(std::abs(host), floor);
  }

  void compareTarget(Queue& queue, Target target, char const* name, int size) {
    std::vector<TrackState> tracks;
    std::vector<Destination> dests;
    makeTracks(target, size, tracks, dests);

    constexpr int nBinsZ = 300, nBinsR = 120;
    const auto material = makeMaterial(nBinsZ, nBinsR);

    mkfit::PropagationEnv env;
    env.mag_c1 = mkfit::Config::mag_c1;
    env.mag_b0 = mkfit::Config::mag_b0;
    env.mag_b1 = mkfit::Config::mag_b1;
    env.mag_a = mkfit::Config::mag_a;
    env.use_pt_mult_scat = true;
    env.mat_nbins_z = nBinsZ;
    env.mat_nbins_r = nBinsR;
    env.mat_fac_z = 1.f;
    env.mat_fac_r = 1.f;

    // host: MkFitCore's width
    mkfit::PropagationFlags hostFlags(mkfit::PF_use_param_b_field | mkfit::PF_apply_material);
    hostFlags.env = env;
    hostFlags.env.material = material.data();
    std::vector<Result> hostResult(size);
    if (target == Target::PlaneUpdate) {
      // hits 30 um (r-phi) and 60 um (z) away from where the tracks cross their planes
      for (int first = 0; first < size; first += kHostWidth)
        propagateLanes<kHostWidth>(Target::Plane,
                                   tracks.data(),
                                   dests.data(),
                                   hostResult.data(),
                                   first,
                                   std::min(kHostWidth, size - first),
                                   hostFlags);
      for (int i = 0; i < size; ++i) {
        const float phi = std::atan2(dests[i].nrm[1], dests[i].nrm[0]);
        dests[i].hit[0] = hostResult[i].par[0] - 0.003f * std::sin(phi);
        dests[i].hit[1] = hostResult[i].par[1] + 0.003f * std::cos(phi);
        dests[i].hit[2] = hostResult[i].par[2] + 0.006f;
      }
    }
    for (int first = 0; first < size; first += kHostWidth)
      propagateLanes<kHostWidth>(
          target, tracks.data(), dests.data(), hostResult.data(), first, std::min(kHostWidth, size - first), hostFlags);

    // device: one lane per thread
    auto in_d = cms::alpakatools::make_device_buffer<TrackState[]>(queue, size);
    auto dest_d = cms::alpakatools::make_device_buffer<Destination[]>(queue, size);
    auto mat_d = cms::alpakatools::make_device_buffer<mkfit::PropagationEnv::Material[]>(queue, material.size());
    auto out_d = cms::alpakatools::make_device_buffer<Result[]>(queue, size);
    auto out_h = cms::alpakatools::make_host_buffer<Result[]>(queue, size);
    alpaka::memcpy(queue, in_d, cms::alpakatools::make_host_view(tracks.data(), size));
    alpaka::memcpy(queue, dest_d, cms::alpakatools::make_host_view(dests.data(), size));
    alpaka::memcpy(queue, mat_d, cms::alpakatools::make_host_view(material.data(), material.size()));
    mkfit::PropagationFlags deviceFlags = hostFlags;
    deviceFlags.env.material = mat_d.data();
    auto workDiv = cms::alpakatools::make_workdiv<Acc1D>(cms::alpakatools::divide_up_by(size, 64), 64);
    alpaka::exec<Acc1D>(
        queue, workDiv, PropagateKernel{}, target, in_d.data(), dest_d.data(), out_d.data(), size, deviceFlags);
    alpaka::memcpy(queue, out_h, out_d);
    alpaka::wait(queue);

    int failMismatch = 0, parMismatch = 0, errMismatch = 0, failed = 0;
    float maxParDiff = 0.f, maxErrDiff = 0.f;
    for (int i = 0; i < size; ++i) {
      const Result& dev = out_h[i];
      const Result& host = hostResult[i];
      if (dev.fail != host.fail) {
        ++failMismatch;
        continue;
      }
      if (host.fail) {
        ++failed;
        continue;
      }
      if (!close(dev.chi2, host.chi2, 1.e-3f, 1.f))
        ++parMismatch;
      for (int k = 0; k < 6; ++k) {
        const float diff = std::abs(dev.par[k] - host.par[k]) / std::max(std::abs(host.par[k]), 1.f);
        maxParDiff = std::max(maxParDiff, diff);
        if (!close(dev.par[k], host.par[k], 1.e-4f, 1.f))
          ++parMismatch;
      }
      for (int i1 = 0, k = 0; i1 < 6; ++i1) {
        for (int i2 = 0; i2 <= i1; ++i2, ++k) {
          // relative to the diagonal terms after and before the propagation: propagation to a surface makes the
          // covariance nearly singular, and the rounding of each width is amplified in its smallest directions
          const TrackState& trk = tracks[i];
          const float scale = std::sqrt(std::abs(host.err[i1 * (i1 + 1) / 2 + i1] * host.err[i2 * (i2 + 1) / 2 + i2]));
          const float scaleIn = std::sqrt(trk.err[i1 * (i1 + 1) / 2 + i1] * trk.err[i2 * (i2 + 1) / 2 + i2]);
          const float diff = std::abs(dev.err[k] - host.err[k]);
          if (diff > 5.e-3f * (std::abs(host.err[k]) + scale) + 1.e-5f * scaleIn)
            ++errMismatch;
          maxErrDiff = std::max(maxErrDiff, diff / (std::abs(host.err[k]) + scale + 1.e-2f * scaleIn));
        }
      }
    }
    std::printf(
        "%s: %d tracks, %d failed on both, fail-flag mismatches %d, parameter mismatches %d (max rel %.2e), "
        "covariance mismatches %d (max %.2e)\n",
        name,
        size,
        failed,
        failMismatch,
        parMismatch,
        maxParDiff,
        errMismatch,
        maxErrDiff);
    REQUIRE(failed < size / 2);
    REQUIRE(failMismatch == 0);
    REQUIRE(parMismatch == 0);
    REQUIRE(errMismatch == 0);
  }

}  // namespace

TEST_CASE("MkFitCore portable propagation on the " EDM_STRINGIZE(ALPAKA_ACCELERATOR_NAMESPACE) " backend",
          "[" EDM_STRINGIZE(ALPAKA_ACCELERATOR_NAMESPACE) "]") {
  auto const& devices = cms::alpakatools::devices<Platform>();
  if (devices.empty()) {
    FAIL("No devices available for the " EDM_STRINGIZE(ALPAKA_ACCELERATOR_NAMESPACE) " backend");
  }
  for (auto const& device : devices) {
    Queue queue(device);
    compareTarget(queue, Target::R, "propagation to R", 4000);
    compareTarget(queue, Target::Z, "propagation to Z", 4000);
    compareTarget(queue, Target::Plane, "propagation to plane", 4000);
    compareTarget(queue, Target::PlaneUpdate, "propagation to plane and Kalman update", 4000);
  }
}
