// Attribution test for the P2.3 tc_eta / tc_phi shipping-flag residual.
//
// The reference's chain TC takes eta / phi from the ntuple's t3_eta / t3_phi branches, which are
// produced by standalone/code/core/write_lst_ntuple.cc through standalone/code/core/lst_math.h
// Hit::eta() / Hit::phi(), compiled by standalone/Makefile at "-g -O2".  The port computes the
// SAME formula on the SAME inputs (MiniDoublet::anchorX/Y/Z is assigned straight from
// hitsBase.xs/ys/zs, src/alpaka/MiniDoublet.h:90-92) inside LSTEvent.dev.cc, which LST/Makefile
// compiles at "-march=native -mtune=native -Ofast -fno-reciprocal-math".
//
// This translation unit is compiled TWICE, once with each flag set, and evaluates both formulas on
// the anchor hits of a real event.  If the -O2 build reproduces the ntuple's t3_eta / t3_phi
// bit-for-bit and the -Ofast build does not, the residual is a compiler-flag artefact of the LST
// library build and nothing else.
//
// usage: ulp_eta_phi <ntuple.root> [nevents]

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "TFile.h"
#include "TTree.h"
#include "TTreeReader.h"
#include "TTreeReaderValue.h"

namespace {
  // standalone/code/core/lst_math.h, verbatim.
  inline float Phi_mpi_pi(float x) {
    while (x >= M_PI)
      x -= 2. * M_PI;
    while (x < -M_PI)
      x += 2. * M_PI;
    return x;
  }
  inline float ATan2(float y, float x) {
    if (x != 0)
      return atan2(y, x);
    if (y == 0)
      return 0;
    if (y > 0)
      return M_PI / 2;
    return -M_PI / 2;
  }
  inline float hitEta(float x, float y, float z) {
    float r3 = sqrt(x * x + y * y + z * z);
    float rt = sqrt(x * x + y * y);
    return ((z > 0) - (z < 0)) * std::acosh(r3 / rt);
  }
  inline float hitPhi(float x, float y) { return Phi_mpi_pi(M_PI + ATan2(-y, -x)); }

  inline bool bitEq(float a, float b) {
    unsigned int ua, ub;
    __builtin_memcpy(&ua, &a, 4);
    __builtin_memcpy(&ub, &b, 4);
    return ua == ub;
  }
}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: %s <ntuple.root> [nevents]\n", argv[0]);
    return 1;
  }
  int const nEvt = (argc > 2) ? std::atoi(argv[2]) : 3;

  TFile* f = TFile::Open(argv[1]);
  TTreeReader r("tree", f);
  TTreeReaderValue<std::vector<int>> t3ls0(r, "t3_lsIdx0");
  TTreeReaderValue<std::vector<int>> t3ls1(r, "t3_lsIdx1");
  TTreeReaderValue<std::vector<float>> t3eta(r, "t3_eta");
  TTreeReaderValue<std::vector<float>> t3phi(r, "t3_phi");
  TTreeReaderValue<std::vector<int>> lsm0(r, "ls_mdIdx0");
  TTreeReaderValue<std::vector<int>> lsm1(r, "ls_mdIdx1");
  TTreeReaderValue<std::vector<float>> mx(r, "md_anchor_x");
  TTreeReaderValue<std::vector<float>> my(r, "md_anchor_y");
  TTreeReaderValue<std::vector<float>> mz(r, "md_anchor_z");

  long long n = 0, etaBad = 0, phiBad = 0, etaBig6 = 0, etaBig5 = 0;
  double etaMax = 0, phiMax = 0;
  int iev = 0;
  while (r.Next() && iev < nEvt) {
    ++iev;
    for (size_t t = 0; t < t3eta->size(); ++t) {
      int const m0 = (*lsm0)[(*t3ls0)[t]];
      int const m2 = (*lsm1)[(*t3ls1)[t]];
      float const e = hitEta((*mx)[m2], (*my)[m2], (*mz)[m2]);
      float const p = hitPhi((*mx)[m0], (*my)[m0]);
      ++n;
      if (!bitEq(e, (*t3eta)[t])) {
        ++etaBad;
        double const d = std::fabs(double(e) - double((*t3eta)[t]));
        if (d > etaMax)
          etaMax = d;
        if (d > 1e-6)
          ++etaBig6;
        if (d > 1e-5)
          ++etaBig5;
      }
      if (!bitEq(p, (*t3phi)[t])) {
        ++phiBad;
        double const d = std::fabs(double(p) - double((*t3phi)[t]));
        if (d > phiMax)
          phiMax = d;
      }
    }
  }
  std::printf(
      "%-10s t3 rows=%lld | eta differs %lld (%.3f%%) max|d|=%.3g >1e-6:%lld >1e-5:%lld | phi differs %lld (%.3f%%) "
      "max|d|=%.3g\n",
              BUILDTAG,
              n,
              etaBad,
              100.0 * etaBad / n,
              etaMax,
              etaBig6,
              etaBig5,
              phiBad,
              100.0 * phiBad / n,
              phiMax);
  return 0;
}
