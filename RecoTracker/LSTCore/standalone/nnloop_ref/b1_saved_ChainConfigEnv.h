#ifndef RecoTracker_LSTCore_src_alpaka_ChainConfigEnv_h
#define RecoTracker_LSTCore_src_alpaka_ChainConfigEnv_h

#include <cstdio>
#include <cstdlib>

#include "RecoTracker/LSTCore/interface/ChainConfig.h"

// A4 env-knob instrument. Applied in the LSTEvent CONSTRUCTOR, not in LST::run: the standalone
// driver (standalone/bin/lst.cc) constructs LSTEvent DIRECTLY and never calls LST::run, so a hook
// there is invisible to every standalone measurement. Physics-inert with nothing set. Every override
// PRINTS once, so the absence of a print is the signal that you did not actually set it.
namespace lst::chainenv {

  inline void envFloat(char const* name, float& dst) {
    if (char const* v = std::getenv(name)) {
      float const before = dst;
      dst = static_cast<float>(std::atof(v));
      unsigned h = 0;
      for (char const* p = name; *p; ++p)
        h = h * 131u + static_cast<unsigned char>(*p);
      static bool seen[128] = {};
      if (!seen[h % 128u]) {
        seen[h % 128u] = true;
        std::printf("[A4 env] %s: %g -> %g\n", name, static_cast<double>(before),
                    static_cast<double>(dst));
        std::fflush(stdout);
      }
    }
  }

  inline void apply(ChainConfig& cfg) {
    // The eight attach / retirement / crossclean bars. A retrained attach head moves the logit
    // SCALE and these are the thresholds expressed on it, so a retrain makes each of them a free
    // parameter again; this is how they get re-derived without a rebuild per point.
    envFloat("LSTCHAIN_attachTheta", cfg.attachTheta);
    envFloat("LSTCHAIN_attachThetaT", cfg.attachThetaT);
    envFloat("LSTCHAIN_attachThetaE", cfg.attachThetaE);
    envFloat("LSTCHAIN_attachThetaT3", cfg.attachThetaT3);
    envFloat("LSTCHAIN_rpsThetaChain", cfg.rpsThetaChain);
    envFloat("LSTCHAIN_xcTheta", cfg.xcTheta);
    envFloat("LSTCHAIN_xcThetaT", cfg.xcThetaT);
    envFloat("LSTCHAIN_xcThetaE", cfg.xcThetaE);
    // A single additive offset on the three DELIVERY bars. This is the shape the CONVERSION RATE
    // responds to -- conversion is monotone decreasing in each bar -- and conversion, not the
    // pair-level acceptance rate, is what a retrained head actually perturbs.
    if (char const* v = std::getenv("LSTCHAIN_attachDelta")) {
      float const d = static_cast<float>(std::atof(v));
      cfg.attachTheta += d;
      cfg.attachThetaT += d;
      cfg.attachThetaE += d;
      static bool said = false;
      if (!said) {
        said = true;
        std::printf("[A4 env] LSTCHAIN_attachDelta %+g -> attachTheta/T/E = %g / %g / %g\n",
                    static_cast<double>(d),
                    static_cast<double>(cfg.attachTheta),
                    static_cast<double>(cfg.attachThetaT),
                    static_cast<double>(cfg.attachThetaE));
        std::fflush(stdout);
      }
    }
    if (char const* v = std::getenv("LSTCHAIN_xcDelta")) {
      float const d = static_cast<float>(std::atof(v));
      cfg.xcTheta += d;
      cfg.xcThetaT += d;
      cfg.xcThetaE += d;
    }
    if (char const* v = std::getenv("LSTCHAIN_rpsDelta")) {
      cfg.rpsThetaChain += static_cast<float>(std::atof(v));
    }
    if (char const* v = std::getenv("LSTCHAIN_t3Delta")) {
      cfg.attachThetaT3 += static_cast<float>(std::atof(v));
    }
  }

}  // namespace lst::chainenv

#endif
