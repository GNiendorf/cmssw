#include <cmath>
#include <limits>
#include <numbers>

#include <alpaka/alpaka.hpp>

#include "HeterogeneousCore/AlpakaInterface/interface/config.h"
#include "HeterogeneousCore/AlpakaInterface/interface/workdivision.h"

#include "MkFitAlpakaHpFeaturesKernels.h"

namespace ALPAKA_ACCELERATOR_NAMESPACE::mkfitdev::hpsel {

  using namespace cms::alpakatools;

  namespace {
    struct KernelHpFeatures {
      ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                    ::mkfitdev::TrackSoAConstView trk,
                                    int n,
                                    ::mkfitdev::LayerInfoSoA::ConstView layers,
                                    ::mkfitdev::hpsel::Params p,
                                    TrackTorchClassifierFeaturesSoA::View out) const {
        constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
        for (int t : uniform_elements(acc, n)) {
          // --- exact: hit counts, ndof, normalized chi2 (MkFitOutputTrackConverter + reco::TrackBase) ---
          int nPix = 0, nStr = 0;
          auto const& h = trk[t].hits().hot;
          int const nh = trk[t].nTotalHits() < ::mkfitdev::kMaxTrkHits ? trk[t].nTotalHits() : ::mkfitdev::kMaxTrkHits;
          for (int i = 0; i < nh; ++i) {
            if (h[i].index >= 0) {
              if (layers[h[i].layer].is_pixel())
                ++nPix;
              else
                ++nStr;
            }
          }
          float const ndof = static_cast<float>(2 * nPix + nStr - 5);
          float const chi2 = trk[t].chi2();
          out[t].ndof() = ndof;
          out[t].normalizedChi2() = ndof != 0.f ? chi2 / ndof : chi2 * 1e6f;
          out[t].validPixelHits() = static_cast<float>(nPix);
          out[t].validStripHits() = static_cast<float>(nStr);
          out[t].layersWithoutMeas() = 0.f;
          out[t].lostInnerHits() = kNaN;
          out[t].lostOuterHits() = kNaN;
          out[t].dxyError() = kNaN;
          out[t].dzError() = kNaN;
          out[t].etaError() = kNaN;
          out[t].phiError() = kNaN;

          // --- prototype: helix in a uniform Bz from the innermost-hit state to the beam spot (xy PCA) ---
          auto const& v = trk[t].params().v;  // x, y, z, 1/pT, momentum phi, theta (stock CCS)
          double const x0 = v[0], y0 = v[1], z0 = v[2], ipt = v[3], phi0 = v[4], theta = v[5];
          double const cotT = std::cos(theta) / std::sin(theta);
          float const r0 = std::sqrt(v[0] * v[0] + v[1] * v[1]);
          double const bz = ::mkfitdev::Config::bFieldFromZR(p.bField, v[2], r0);
          double const kappa = -double(trk[t].charge()) * 0.0029979245800 * bz * ipt;  // dphi / ds_T [1/cm]
          double xp = x0, yp = y0, zp = z0, phip = phi0;
          if (std::abs(kappa) > 1e-12) {
            double const xc = x0 - std::sin(phi0) / kappa, yc = y0 + std::cos(phi0) / kappa;
            double const dx = p.bsx - xc, dy = p.bsy - yc;
            double const dist = std::sqrt(dx * dx + dy * dy);
            double const rad = 1. / std::abs(kappa);
            xp = xc + dx / dist * rad;
            yp = yc + dy / dist * rad;
            phip = std::atan2(kappa * (xp - xc), -kappa * (yp - yc));
            double dphi = phip - phi0;
            if (dphi > std::numbers::pi)
              dphi -= 2. * std::numbers::pi;
            else if (dphi <= -std::numbers::pi)
              dphi += 2. * std::numbers::pi;
            zp = z0 + dphi / kappa * cotT;
          } else {  // straight line
            double const ux = std::cos(phi0), uy = std::sin(phi0);
            double const s = (p.bsx - x0) * ux + (p.bsy - y0) * uy;
            xp = x0 + s * ux;
            yp = y0 + s * uy;
            zp = z0 + s * cotT;
          }
          // reco::TrackBase::dxy(Point) / dz(Point) with the PCA position and momentum direction
          double const cp = std::cos(phip), sp = std::sin(phip);
          double const vx = xp - p.bsx, vy = yp - p.bsy;
          out[t].dxyBeamSpot() = static_cast<float>(-vx * sp + vy * cp);
          out[t].dzBeamSpot() = static_cast<float>((zp - p.bsz) - (vx * cp + vy * sp) * cotT);
          double const etaAbs = std::log(std::abs(cotT) + std::sqrt(cotT * cotT + 1.));  // asinh(|cot theta|)
          out[t].eta() = static_cast<float>(cotT < 0. ? -etaAbs : etaAbs);
          out[t].phi() = static_cast<float>(phip);
        }
      }
    };
  }  // namespace

  void launchFeatures(Queue& queue,
                      ::mkfitdev::TrackSoAConstView trk,
                      int n,
                      ::mkfitdev::LayerInfoSoA::ConstView layers,
                      ::mkfitdev::hpsel::Params const& p,
                      TrackTorchClassifierFeaturesSoA::View out) {
    if (n <= 0)
      return;
    constexpr int kBlock = 128;
    alpaka::exec<Acc1D>(
        queue, make_workdiv<Acc1D>(divide_up_by(n, kBlock), kBlock), KernelHpFeatures{}, trk, n, layers, p, out);
  }

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE::mkfitdev::hpsel
