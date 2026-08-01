// M12 golden-value parity tool for the 3-class chain gate (plan 5c discipline).
//
// Reads the FIRST N rows of a chain dump (cf_00..cf_<kChainFeat-1> + dcaXY), runs the
// production C++ inference (chainGate3Logits, i.e. exactly what -G 6 calls), and writes
// the three raw logits per row as JSON. chain3_parity.py produces the same rows from the
// torch checkpoint + norm json; the two must agree to < 1e-3.
//
// Built out-of-tree (prototype/Makefile globs *.cc and would collide on main()):
//   g++ $(root-config --cflags) -O2 -std=c++17 tools/chain3_parity.cc \
//       ChainInference.o ChainFeatures.o $(root-config --libs) -I. -o tools/chain3_parity

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "TFile.h"
#include "TTree.h"

#include "../ChainFeatures.h"
#include "../ChainInference.h"

int main(int argc, char** argv) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: chain3_parity <chain_dump.root> <out.json> [nChains]\n");
    return 1;
  }
  const std::string in = argv[1], out = argv[2];
  const long long nWant = argc > 3 ? std::atoll(argv[3]) : 1000;

  TFile* f = TFile::Open(in.c_str());
  if (!f || f->IsZombie()) {
    std::fprintf(stderr, "cannot open %s\n", in.c_str());
    return 1;
  }
  TTree* t = dynamic_cast<TTree*>(f->Get("chains"));
  if (!t) {
    std::fprintf(stderr, "no 'chains' tree in %s\n", in.c_str());
    return 1;
  }
  std::vector<float> cf(kChainFeat, 0.f);
  float dca = 0.f;
  char name[16];
  for (int i = 0; i < kChainFeat; ++i) {
    std::snprintf(name, sizeof(name), "cf_%02d", i);
    t->SetBranchAddress(name, &cf[i]);
  }
  t->SetBranchAddress("dcaXY", &dca);

  const long long n = std::min<long long>(nWant, t->GetEntries());
  FILE* o = std::fopen(out.c_str(), "w");
  std::fprintf(o, "{\n");
  for (long long i = 0; i < n; ++i) {
    t->GetEntry(i);
    float z[3] = {0.f, 0.f, 0.f};
    chainGate3Logits(cf.data(), dca, z);
    std::fprintf(o, "\"%lld\": [%.9g, %.9g, %.9g]%s\n", i, z[0], z[1], z[2], i + 1 < n ? "," : "");
  }
  std::fprintf(o, "}\n");
  std::fclose(o);
  std::printf("wrote %s: %lld rows, kInput=%d, kChainFeat=%d\n", out.c_str(), n, chainGate3NumInputs(), kChainFeat);
  return 0;
}
