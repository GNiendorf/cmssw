// P2.1 parity reference dumper.
//
// Runs the FROZEN prototype code (standalone/prototype/, compiled unmodified and linked in
// here) over an LST --allobj ntuple and writes the per-event edge set with the edge-MLP
// logits in the same sidecar format the Alpaka port emits. Nothing in prototype/ is touched:
// this file only calls k1BuildIncidence / k2BuildEdges / computeNodeFeatures /
// computeEdgeFeatures / runEdgeInference, which is exactly the P2.1 scope of the pipeline.
//
// Sidecar format (little endian), repeated per event:
//   u32 magic 0x50323145 ('P21E')
//   u32 ievt, u32 run, u32 lumi, u64 evt
//   u32 nT3, u32 nE1exact, u32 nE2exact, u32 nE1kept, u32 nE2kept
//   (nE1kept + nE2kept) x { u32 inner, u32 outer, u32 type, f32 logit }
// "exact" = the degree-arithmetic count sum(degIn*degOut); "kept" = what survives the
// self-pair / E2-duplicates-E1 exclusions, i.e. the edges that actually carry a logit.
//
// With --feat <path> a second file carries the full feature rows of ONE event (default 0):
//   u32 nT3, u32 kNodeFeat, then nT3 x kNodeFeat floats
//   u32 nEdges, u32 kEdgeFeat, then nEdges x { u32 inner, u32 outer, u32 type,
//                                              kEdgeFeat floats }

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "EdgeInference.h"
#include "EventData.h"
#include "Features.h"
#include "NtupleReader.h"
#include "Stages.h"

namespace {

  template <typename T>
  void put(std::FILE* f, T v) {
    std::fwrite(&v, sizeof(T), 1, f);
  }

  void usage(const char* argv0) {
    std::fprintf(stderr,
                 "usage: %s -i <lst_ntuple.root> -t <tracking_sample> -o <out.bin>\n"
                 "          [-n <nevents>] [--feat <feat.bin>] [--featevt <i>]\n",
                 argv0);
  }

}  // namespace

int main(int argc, char** argv) {
  std::string lstPath, trkPath, outPath, featPath;
  long long maxEvents = -1;
  long long featEvt = 0;

  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto next = [&]() -> std::string { return (i + 1 < argc) ? std::string(argv[++i]) : std::string(); };
    if (a == "-i")
      lstPath = next();
    else if (a == "-t")
      trkPath = next();
    else if (a == "-o")
      outPath = next();
    else if (a == "-n")
      maxEvents = std::stoll(next());
    else if (a == "--feat")
      featPath = next();
    else if (a == "--featevt")
      featEvt = std::stoll(next());
    else {
      usage(argv[0]);
      return 1;
    }
  }
  if (lstPath.empty() || trkPath.empty() || outPath.empty()) {
    usage(argv[0]);
    return 1;
  }

  NtupleReader reader(lstPath, trkPath);
  const long long nTotal = reader.nEntries();
  const long long nRun = (maxEvents < 0 || maxEvents > nTotal) ? nTotal : maxEvents;
  std::printf("p21_ref_dump: %s (%lld entries), processing %lld\n", lstPath.c_str(), nTotal, nRun);

  std::FILE* out = std::fopen(outPath.c_str(), "wb");
  if (!out) {
    std::fprintf(stderr, "cannot open %s\n", outPath.c_str());
    return 1;
  }

  LSTEventData ev;
  TrkEventData trk;

  for (long long i = 0; i < nRun; ++i) {
    if (!reader.loadEntry(i, ev, trk)) {
      std::fprintf(stderr, "Error: no aligned tracking event for LST entry %lld.\n", i);
      return 1;
    }
    ChainGraph g;
    k1BuildIncidence(ev, g);
    k2BuildEdges(ev, g);
    NodeFeatures nf;
    computeNodeFeatures(ev, nf);
    EdgeFeatures ef;
    computeEdgeFeatures(ev, g, nf, ef);
    EdgeScores scores;
    runEdgeInference(g, nf, ef, scores);

    uint32_t nE1kept = 0, nE2kept = 0;
    for (const auto& e : g.edges) {
      if (e.type == 1)
        ++nE1kept;
      else if (e.type == 2)
        ++nE2kept;
    }

    put<uint32_t>(out, 0x50323145u);
    put<uint32_t>(out, static_cast<uint32_t>(i));
    put<uint32_t>(out, ev.run);
    put<uint32_t>(out, ev.lumi);
    put<uint64_t>(out, ev.evt);
    put<uint32_t>(out, static_cast<uint32_t>(ev.t3_lsIdx0.size()));
    put<uint32_t>(out, static_cast<uint32_t>(g.e1CountExact));
    put<uint32_t>(out, static_cast<uint32_t>(g.e2CountExact));
    put<uint32_t>(out, nE1kept);
    put<uint32_t>(out, nE2kept);
    for (std::size_t e = 0; e < g.edges.size(); ++e) {
      put<uint32_t>(out, static_cast<uint32_t>(g.edges[e].inner));
      put<uint32_t>(out, static_cast<uint32_t>(g.edges[e].outer));
      put<uint32_t>(out, static_cast<uint32_t>(g.edges[e].type));
      put<float>(out, scores.logOdds[e]);
    }

    std::printf("evt %lld (run %u lumi %u event %llu): nT3=%zu E1exact=%lld E2exact=%lld kept=%u/%u\n",
                i,
                ev.run,
                ev.lumi,
                ev.evt,
                ev.t3_lsIdx0.size(),
                g.e1CountExact,
                g.e2CountExact,
                nE1kept,
                nE2kept);

    if (!featPath.empty() && i == featEvt) {
      std::FILE* ff = std::fopen(featPath.c_str(), "wb");
      if (!ff) {
        std::fprintf(stderr, "cannot open %s\n", featPath.c_str());
        return 1;
      }
      const uint32_t nT3 = static_cast<uint32_t>(ev.t3_lsIdx0.size());
      put<uint32_t>(ff, nT3);
      put<uint32_t>(ff, static_cast<uint32_t>(kNodeFeat));
      std::fwrite(nf.f.data(), sizeof(float), nf.f.size(), ff);
      put<uint32_t>(ff, static_cast<uint32_t>(g.edges.size()));
      put<uint32_t>(ff, static_cast<uint32_t>(kEdgeFeat));
      for (std::size_t e = 0; e < g.edges.size(); ++e) {
        put<uint32_t>(ff, static_cast<uint32_t>(g.edges[e].inner));
        put<uint32_t>(ff, static_cast<uint32_t>(g.edges[e].outer));
        put<uint32_t>(ff, static_cast<uint32_t>(g.edges[e].type));
        std::fwrite(&ef.f[e * kEdgeFeat], sizeof(float), kEdgeFeat, ff);
      }
      std::fclose(ff);
      std::printf("  wrote feature reference for event %lld to %s\n", i, featPath.c_str());
    }
  }

  std::fclose(out);
  std::printf("p21_ref_dump: wrote %s\n", outPath.c_str());
  return 0;
}
