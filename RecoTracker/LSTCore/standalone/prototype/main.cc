// Driver for the chain-tracking offline prototype (plan section 10).
// Modes:
//   identity (M0): read the LST ntuple, re-emit tc_* / sim_tcIdx verbatim through
//                  OutputWriter so the production efficiency harness validates the loop.
//   graph    (M1): run K1 (incidence) + K2 (edge enumeration) per event and report
//                  exact-vs-emitted edge counts and stage wall times.
//   dump     (M2): graph + node/edge features + track-level edge labels, written as a
//                  flat per-edge training TTree via DumpWriter.
//   chains   (M3): graph + features + MLP edge inference (EdgeInference.h) + K6 welding;
//                  per-event chain multiplicity / nLayers histograms and stage timings.
//   oracle   (M3): K6 welding with TRUTH edge scores (logOdds +10 for label==1 edges,
//                  -10 otherwise, thetaEdge fixed to 0) instead of the MLP; reports the
//                  oracle ceiling table (chain-formable sims reached by oracle chains)
//                  against the baseline sim_tcIdx outcome.
//   hybrid   (M4): full A/B chain: graph + features + MLP inference + K6 welding + K9
//                  hit-claim arbitration + K10 TC assembly; output file = kept baseline
//                  pixel TCs (pT5/pT3/pLS) + the prototype chain TCs, written through
//                  OutputWriter::fillEventHybrid for the unchanged efficiency harness.
//                  -A 1 (M7, REJECTED v1, kept for reference) adds K8 pixel attach:
//                  after the theta gate and BEFORE the pixel-consumed drop, 5+-layer
//                  chains bid for pLS seeds via the trained attach head (threshold -a);
//                  attached chains bypass the partOfPT5 drop, become type-7 (pT5-class)
//                  TCs (pixel hits + OT hits, pt = pLS ptIn), and their pLS's baseline
//                  pT5/pLS rows are suppressed. Rejected: the bypass floods the ONE
//                  shared MD claim with re-admitted prompt chains which EVICT displaced
//                  chains, and own-row suppression leaves the dup floor unchanged.
//                  -A 2 (M7b) is the corrected integration: SUBORDINATE TWO-PASS claim
//                  (pass 1 = exactly the -A 0 pipeline, bit-identical accepted set by
//                  construction; pass 2 = attached chains pixdropped in pass 1 claim
//                  only against the post-pass-1 MD map -- they can never evict a pass-1
//                  chain) + attached pass-1 chains upgraded in place to type 7 + sim-
//                  blind SEED-FAMILY suppression (kept-baseline pT5/pT3/pLS rows whose
//                  own pLS shares >= 2 pixel hits with any attached pLS are dropped,
//                  mirroring production pixelHitsOverlapAny).
//   chaindump(M6): graph + features + MLP inference + K6 welding (same -e/-L knobs as
//                  hybrid, r5 shape = -e 0 -L 0.5), then EVERY welded chain
//                  PRE-arbitration is written to a flat TTree "chains" (one entry per
//                  chain: cf_00..cf_15 ChainFeatures + label/simVxy/simPt/nLayers/evt)
//                  -- the chain-gate training factory (plan 5a hard gate).
//   pairdump (M7): h4b-shaped pipeline (same knobs as hybrid; the night-winner shape is
//                  -G 2 -T4 2 -T5 0 -T6 0 -L 0.5 -F 0.3) through K9 arbitration, then
//                  for every ACCEPTED chain with nLayers >= 5 the K8 analytic PREFILTER
//                  ONLY runs over all pLS and every surviving pair is written to a flat
//                  TTree "pairs" (af_00..af_17 PixelAttach features + label/simVxy/
//                  simPt/chainNLayers/evt) -- the attach pair-head training factory.
//                  Also reports the prefilter's true-pair efficiency proxy per event
//                  and total (binding window attributed for failures).

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <vector>

#include <unistd.h>

#include "AttachInference.h"
#include "ChainFeatures.h"
#include "ChainInference.h"
#include "DumpWriter.h"
#include "EdgeInference.h"
#include "EventData.h"
#include "Features.h"
#include "Labels.h"
#include "NtupleReader.h"
#include "OutputWriter.h"
#include "PixelAttach.h"
#include "PixelAttachPairs.h"
#include "Stages.h"

namespace {

void usage(const char* prog) {
  std::fprintf(stderr,
               "Usage: %s -i <lst_ntuple.root> -t <tracking file-or-dir> [options]\n"
               "  -i <file>   LST --allobj ntuple (required)\n"
               "  -t <path>   tracking ntuple file or directory (required)\n"
               "  -o <file>   output ROOT file (required in identity, dump, chaindump, pairdump,\n"
               "              and hybrid modes)\n"
               "  -n <N>      max events to process (default -1 = all)\n"
               "  -m <mode>   identity | graph | dump | chains | oracle | hybrid | chaindump |\n"
               "              pairdump (default identity)\n"
               "  -l <label>  input label for the output TNamed (default PU200)\n"
               "  -e <theta>  thetaEdge: edge log-odds threshold for K6 welding in chains\n"
               "              and hybrid modes (default 0.0; oracle always uses thetaEdge=0)\n"
               "  -L <lam>    lambdaLen: chain-length prior weight passed to K6 in chains,\n"
               "              oracle, and hybrid modes (default 0.0)\n"
               "  -T4/-T5/-T6 <theta>  per-length thetaChain: K9 chain-score acceptance\n"
               "              threshold in hybrid mode, applied by chain nLayers\n"
               "              (==4 -> -T4, ==5 -> -T5, >=6 -> -T6; defaults 0.0)\n"
               "  -T <theta>  shorthand: set thetaChain4/5/6 all to <theta>\n"
               "  -F <frac>   maxClaimedFrac: max fraction of already-claimed MDs a chain may\n"
               "              tolerate in K9 arbitration, hybrid mode (default 0.3)\n"
               "  -P          keep pixel-consumed chains in K9 (default: chains containing a\n"
               "              t3_partOfPT5/pT3 member are dropped in hybrid mode)\n"
               "  -G <0|1|2>  chain gate (hybrid mode, default 1): 1 = K9 acceptance score per\n"
               "              chain is the chain-gate MLP LOGIT (ChainInference over the\n"
               "              ChainFeatures.h vector; thetaChain4/5/6 cut on that scale and\n"
               "              arbitration order uses it too); 0 = legacy K6 sum-logit score\n"
               "              (regression path); 2 = split: gate logit for nLayers <= 4 only\n"
               "              (thetaChain4 on the gate scale), legacy score for nLayers >= 5\n"
               "  -A <0|1|2>  hybrid mode (default 0): 1 = K8 pixel attach v1 (REJECTED,\n"
               "              reference): attached chains bypass the partOfPT5 drop, become\n"
               "              type-7 TCs, own pT5/pLS rows suppressed. 2 = M7b subordinate\n"
               "              two-pass claim: pass 1 is exactly the -A 0 pipeline (no bypass);\n"
               "              attached chains pixdropped in pass 1 claim only against the\n"
               "              post-pass-1 MD map (never evicting pass-1 chains) and become\n"
               "              type-7 TCs; attached pass-1 chains upgrade in place to type 7;\n"
               "              seed-family suppression drops kept-baseline pT5/pT3/pLS rows\n"
               "              whose pLS shares >= 2 pixel hits with any attached pLS\n"
               "  -a <theta>  thetaAttach: attach-head logit threshold for K8 (default 0.0;\n"
               "              only used with -A 1/2)\n",
               prog);
}

double msBetween(std::chrono::steady_clock::time_point a, std::chrono::steady_clock::time_point b) {
  return std::chrono::duration<double, std::milli>(b - a).count();
}

// Chain node-count histogram bins: 2, 3, 4, 5+ (K6 chains have >= 2 nodes by contract).
int nodeBin(int n) { return n >= 5 ? 3 : (n <= 2 ? 0 : n - 2); }

// Chain nLayers histogram bins (chains mode): <=4, 5, 6, 7, 8, 9+.
int layerBin9(int n) { return n <= 4 ? 0 : (n >= 9 ? 5 : n - 4); }

// Max-nLayers-per-sim bins (oracle table): <=4, 5, 6, 7, 8+.
int layerBin8(int n) { return n <= 4 ? 0 : (n >= 8 ? 4 : n - 4); }

// Oracle chain -> sim assignment: the sim (Labels simIdx space = FULL tracking-ntuple sim
// rows) present in the sim-sets of >= 2 member T3s, majority count; ties prefer accepted
// sims (row < nAccepted, i.e. kinematics known), then the smaller row. -1 if none reaches 2.
int chainSimIdx(const Chains& ch, int c, const T3SimSets& t3sims, int nAccepted) {
  std::unordered_map<int, int> cnt;
  for (int k = ch.offsets[c]; k < ch.offsets[c + 1]; ++k)
    for (int s : t3sims.sims[ch.items[k]])
      ++cnt[s];  // per-T3 sim sets are unique, so this counts member T3s per sim
  int best = -1, bestCnt = 0;
  bool bestAcc = false;
  for (const auto& kv : cnt) {
    if (kv.second < 2)
      continue;
    const bool acc = kv.first < nAccepted;
    bool better;
    if (kv.second != bestCnt)
      better = kv.second > bestCnt;
    else if (acc != bestAcc)
      better = acc;
    else
      better = best < 0 || kv.first < best;
    if (better) {
      best = kv.first;
      bestCnt = kv.second;
      bestAcc = acc;
    }
  }
  return best;
}

}  // namespace

int main(int argc, char** argv) {
  std::string lstPath, trkPath, outPath;
  std::string mode = "identity";
  std::string label = "PU200";
  long long maxEvents = -1;
  float thetaEdge = 0.0f;         // -e: K6 edge log-odds threshold (chains/hybrid modes)
  float lambdaLen = 0.0f;         // -L: K6 chain-length prior weight
  float thetaChain4 = 0.0f;       // -T4: K9 acceptance threshold, chain nLayers <= 4 (hybrid)
  float thetaChain5 = 0.0f;       // -T5: K9 acceptance threshold, chain nLayers == 5 (hybrid)
  float thetaChain6 = 0.0f;       // -T6: K9 acceptance threshold, chain nLayers >= 6 (hybrid)
  float maxClaimedFrac = 0.3f;    // -F: K9 max already-claimed-MD fraction (hybrid mode)
  bool dropPixelConsumed = true;  // -P clears it: K9 pixel-consumed chain drop (hybrid mode)
  int chainGateMode = 1;          // -G: 0 = legacy K6 sum-logit score, 1 = gate logit for ALL
                                  // chains, 2 = split: gate logit for nLayers <= 4 only,
                                  // legacy score for nLayers >= 5 (hybrid mode)
  int attachMode = 0;             // -A: 1 = K8 pixel attach v1 (rejected, reference), 2 = M7b
                                  // subordinate two-pass claim + seed-family suppression
                                  // (default 0 so the -A-less regression path is untouched)
  float thetaAttach = 0.0f;       // -a: attach-head logit threshold (hybrid -A 1)

  // Pre-scan for the multi-char flags -T4/-T5/-T6 (getopt cannot express them: "-T4"
  // would parse as -T with value "4"); consume flag+value pairs here and hand the
  // compacted argv to getopt. "-T <v>" stays in getopt as set-all-three shorthand.
  std::vector<char*> args;
  args.push_back(argv[0]);
  for (int a = 1; a < argc; ++a) {
    const std::string s = argv[a];
    float* dst = nullptr;
    if (s == "-T4")
      dst = &thetaChain4;
    else if (s == "-T5")
      dst = &thetaChain5;
    else if (s == "-T6")
      dst = &thetaChain6;
    if (dst == nullptr) {
      args.push_back(argv[a]);
      continue;
    }
    if (a + 1 >= argc) {
      std::fprintf(stderr, "Error: %s requires a value.\n", s.c_str());
      usage(argv[0]);
      return 1;
    }
    *dst = static_cast<float>(std::atof(argv[++a]));
  }
  int nArgs = static_cast<int>(args.size());

  int opt;
  while ((opt = getopt(nArgs, args.data(), "i:t:o:n:m:l:e:L:T:F:G:A:a:Ph")) != -1) {
    switch (opt) {
      case 'i':
        lstPath = optarg;
        break;
      case 't':
        trkPath = optarg;
        break;
      case 'o':
        outPath = optarg;
        break;
      case 'n':
        maxEvents = std::atoll(optarg);
        break;
      case 'm':
        mode = optarg;
        break;
      case 'l':
        label = optarg;
        break;
      case 'e':
        thetaEdge = static_cast<float>(std::atof(optarg));
        break;
      case 'L':
        lambdaLen = static_cast<float>(std::atof(optarg));
        break;
      case 'T':
        thetaChain4 = thetaChain5 = thetaChain6 = static_cast<float>(std::atof(optarg));
        break;
      case 'F':
        maxClaimedFrac = static_cast<float>(std::atof(optarg));
        break;
      case 'P':
        dropPixelConsumed = false;
        break;
      case 'G':
        chainGateMode = std::atoi(optarg);
        if (chainGateMode < 0 || chainGateMode > 2) {
          std::fprintf(stderr, "Error: -G expects 0, 1, or 2.\n");
          return 1;
        }
        break;
      case 'A':
        attachMode = std::atoi(optarg);
        if (attachMode < 0 || attachMode > 2) {
          std::fprintf(stderr, "Error: -A expects 0, 1, or 2.\n");
          return 1;
        }
        break;
      case 'a':
        thetaAttach = static_cast<float>(std::atof(optarg));
        break;
      case 'h':
        usage(argv[0]);
        return 0;
      default:
        usage(argv[0]);
        return 1;
    }
  }

  if (lstPath.empty() || trkPath.empty()) {
    std::fprintf(stderr, "Error: -i and -t are required.\n");
    usage(argv[0]);
    return 1;
  }
  if (mode != "identity" && mode != "graph" && mode != "dump" && mode != "chains" && mode != "oracle" &&
      mode != "hybrid" && mode != "chaindump" && mode != "pairdump") {
    std::fprintf(stderr,
                 "Error: unknown mode '%s' (expected identity, graph, dump, chains, oracle, hybrid, chaindump,"
                 " or pairdump).\n",
                 mode.c_str());
    return 1;
  }
  if ((mode == "identity" || mode == "dump" || mode == "hybrid" || mode == "chaindump" || mode == "pairdump") &&
      outPath.empty()) {
    std::fprintf(stderr, "Error: -o is required in %s mode.\n", mode.c_str());
    return 1;
  }

  NtupleReader reader(lstPath, trkPath);
  const long long nTotal = reader.nEntries();
  const long long nRun = (maxEvents < 0 || maxEvents > nTotal) ? nTotal : maxEvents;
  std::printf("Input: %s (%lld entries), tracking: %s, processing %lld event(s), mode=%s\n",
              lstPath.c_str(), nTotal, trkPath.c_str(), nRun, mode.c_str());

  LSTEventData ev;
  TrkEventData trk;

  if (mode == "identity") {
    OutputWriter writer(outPath, label);
    long long totalTCs = 0;
    for (long long i = 0; i < nRun; ++i) {
      if (!reader.loadEntry(i, ev, trk)) {
        std::fprintf(stderr, "Error: no aligned tracking event for LST entry %lld.\n", i);
        return 1;
      }
      writer.fillEventIdentity(ev);
      totalTCs += static_cast<long long>(ev.tc_pt.size());
    }
    writer.writeAndClose();
    std::printf("identity summary: %lld events, %lld TCs total, wrote %s\n", nRun, totalTCs, outPath.c_str());
    return 0;
  }

  if (mode == "dump") {
    DumpWriter dumpWriter(outPath);
    long long totEdges = 0, totTrue = 0;
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
      T3SimSets t3sims;
      buildT3SimSets(ev, t3sims);
      EdgeLabels labels;
      labelEdges(ev, g, t3sims, labels);
      dumpWriter.fillEvent(ev, g, nf, ef, labels);

      long long nTrue = 0;
      for (int8_t l : labels.label)
        if (l == 1)
          ++nTrue;
      totEdges += static_cast<long long>(g.edges.size());
      totTrue += nTrue;
      std::printf("evt %lld (run %u lumi %u event %llu): edges=%zu true=%lld\n", i, ev.run, ev.lumi, ev.evt,
                  g.edges.size(), nTrue);
    }
    dumpWriter.writeAndClose();
    std::printf("dump summary: %lld events, %lld edges dumped, true fraction=%.4f, wrote %s\n", nRun, totEdges,
                totEdges > 0 ? static_cast<double>(totTrue) / static_cast<double>(totEdges) : 0.0, outPath.c_str());
    return 0;
  }

  if (mode == "chaindump") {
    // M6: chain-gate training dump. r5-shape pipeline up to and including K6 (the -e/-L
    // knobs are the same ones hybrid uses; the winner shape is -e 0 -L 0.5), NO K9
    // arbitration: EVERY welded chain is dumped, so the gate classifier sees the full
    // pre-decision population it will be applied to (plan 5a: the hard decision trains
    // on maximum evidence, before any irreversible trimming).
    std::printf("chaindump mode: thetaEdge=%.3f lambdaLen=%.3f kWeldSweeps=%d\n", thetaEdge, lambdaLen, kWeldSweeps);
    ChainDumpWriter chainWriter(outPath);
    long long totChains = 0, totTrue = 0;
    constexpr int kMaxLayBin = 12;  // chains top out at 7 distinct layers (M3); headroom
    long long layAll[kMaxLayBin + 1] = {};
    long long layTrue[kMaxLayBin + 1] = {};
    double totInferMs = 0.0, totWeldMs = 0.0, totFeatMs = 0.0;

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
      const auto t0 = std::chrono::steady_clock::now();
      runEdgeInference(g, nf, ef, scores);
      const auto t1 = std::chrono::steady_clock::now();
      Chains chains;
      k6WeldChains(ev, g, scores, thetaEdge, lambdaLen, chains);
      const auto t2 = std::chrono::steady_clock::now();

      ChainFeatures cf;
      computeChainFeatures(ev, g, chains, scores, cf);
      T3SimSets t3sims;
      buildT3SimSets(ev, t3sims);
      ChainLabels labels;
      labelChains(ev, chains, t3sims, labels);
      const auto t3 = std::chrono::steady_clock::now();
      chainWriter.fillEvent(ev, chains, cf, labels);

      const long long nChains = chains.offsets.empty() ? 0 : static_cast<long long>(chains.offsets.size()) - 1;
      long long nTrue = 0;
      for (long long c = 0; c < nChains; ++c) {
        const int lb = std::min(chains.nLayers[c], kMaxLayBin);
        ++layAll[lb];
        if (labels.label[c] == 1) {
          ++nTrue;
          ++layTrue[lb];
        }
      }
      const double inferMs = msBetween(t0, t1);
      const double weldMs = msBetween(t1, t2);
      const double featMs = msBetween(t2, t3);
      std::printf("evt %lld (run %u lumi %u event %llu): chains=%lld true=%lld (%.3f)"
                  " | infer=%.3f weld=%.3f feat+label=%.3f ms\n",
                  i, ev.run, ev.lumi, ev.evt, nChains, nTrue,
                  nChains > 0 ? static_cast<double>(nTrue) / static_cast<double>(nChains) : 0.0, inferMs, weldMs,
                  featMs);
      totChains += nChains;
      totTrue += nTrue;
      totInferMs += inferMs;
      totWeldMs += weldMs;
      totFeatMs += featMs;
    }
    chainWriter.writeAndClose();

    const double nEvD = nRun > 0 ? static_cast<double>(nRun) : 1.0;
    std::printf("chaindump summary: %lld events (thetaEdge=%.3f lambdaLen=%.3f)\n", nRun, thetaEdge, lambdaLen);
    std::printf("  chains total=%lld mean=%.1f | label 1 total=%lld fraction=%.4f\n", totChains, totChains / nEvD,
                totTrue, totChains > 0 ? static_cast<double>(totTrue) / static_cast<double>(totChains) : 0.0);
    std::printf("  per-nLayers label balance (all / true / trueFrac):\n");
    for (int b = 0; b <= kMaxLayBin; ++b) {
      if (layAll[b] == 0)
        continue;
      std::printf("    nLayers%s%2d : %10lld %10lld  %.4f\n", b == kMaxLayBin ? ">=" : " =", b, layAll[b], layTrue[b],
                  static_cast<double>(layTrue[b]) / static_cast<double>(layAll[b]));
    }
    std::printf("  time mean/evt infer=%.3f weld=%.3f feat+label=%.3f ms\n", totInferMs / nEvD, totWeldMs / nEvD,
                totFeatMs / nEvD);
    std::printf("  wrote %s\n", outPath.c_str());
    return 0;
  }

  if (mode == "pairdump") {
    // M7: K8 attach-head training dump (mode contract in the header comment). The
    // pipeline through K9 mirrors hybrid mode EXACTLY (same knobs, same -G semantics);
    // ChainFeatures + gate logits are computed unconditionally because attach features
    // 7-11 need them (feature 11 is ALWAYS the gate logit, whatever -G used for K9).
    AttachParams apre;  // default prefilter windows (PixelAttach.h: 0.6 / 0.4)
    std::printf(
        "pairdump mode: thetaEdge=%.3f lambdaLen=%.3f thetaChain4/5/6=%.3f/%.3f/%.3f"
        " maxClaimedFrac=%.3f dropPixelConsumed=%s chainGateMode=%d kWeldSweeps=%d"
        " | prefDTanL=%.3f prefDPhi=%.3f attachHead=%s\n",
        thetaEdge, lambdaLen, thetaChain4, thetaChain5, thetaChain6, maxClaimedFrac,
        dropPixelConsumed ? "on" : "off", chainGateMode, kWeldSweeps, apre.prefDTanL, apre.prefDPhi,
        attachHeadAvailable() ? "trained" : "sentinel");
    PairDumpWriter pairWriter(outPath);

    long long totChains5 = 0, totPairs = 0, totTruePairs = 0;
    long long totProxyDen = 0, totProxyNum = 0;
    long long totBindDTanL = 0, totBindDPhi = 0, totBindBoth = 0;
    double totEnumMs = 0.0;

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
      Chains chains;
      k6WeldChains(ev, g, scores, thetaEdge, lambdaLen, chains);

      ChainFeatures cf;
      computeChainFeatures(ev, g, chains, scores, cf);
      std::vector<float> gateLogit;
      runChainInference(cf, gateLogit);
      if (chainGateMode >= 1) {
        for (std::size_t c = 0; c < gateLogit.size(); ++c)
          if (chainGateMode == 1 || chains.nLayers[c] <= 4)
            chains.score[c] = gateLogit[c];
      }

      ArbitrationParams ap;
      ap.thetaChain4 = thetaChain4;
      ap.thetaChain5 = thetaChain5;
      ap.thetaChain6 = thetaChain6;
      ap.maxClaimedFrac = maxClaimedFrac;
      ap.dropPixelConsumed = dropPixelConsumed;
      std::vector<int> accepted;
      k9Arbitrate(ev, chains, ap, accepted);

      // --- truth: chain-side sim intersection sets (accepted 5+-layer chains only) and
      // pLS-side sim sets (pLS_simIdxAll = FULL tracking-ntuple sim rows, the
      // EventData.h M7 rule -- SAME space as T3SimSets, direct intersection).
      T3SimSets t3sims;
      buildT3SimSets(ev, t3sims);
      const int nAcc = static_cast<int>(accepted.size());
      const int nAccSim = static_cast<int>(ev.sim_pt.size());
      std::vector<std::vector<int>> chainSims(nAcc);
      {
        std::vector<int> tmp;
        for (int pos = 0; pos < nAcc; ++pos) {
          const int c = accepted[pos];
          if (chains.nLayers[c] < 5)
            continue;
          const int ib = chains.offsets[c], ie = chains.offsets[c + 1];
          std::vector<int> common = t3sims.sims[chains.items[ib]];
          for (int k = ib + 1; k < ie && !common.empty(); ++k) {
            const auto& s = t3sims.sims[chains.items[k]];
            tmp.clear();
            std::set_intersection(common.begin(), common.end(), s.begin(), s.end(), std::back_inserter(tmp));
            common.swap(tmp);
          }
          chainSims[pos] = std::move(common);
        }
      }
      const int nPls = static_cast<int>(ev.pLS_pt.size());
      std::vector<std::vector<int>> plsSims(nPls);
      std::unordered_map<int, std::vector<int>> simToPls;
      for (int p = 0; p < nPls; ++p) {
        std::vector<int> v = ev.pLS_simIdxAll[p];
        std::sort(v.begin(), v.end());
        v.erase(std::unique(v.begin(), v.end()), v.end());
        for (int s : v)
          simToPls[s].push_back(p);
        plsSims[p] = std::move(v);
      }

      // --- prefilter-only pair enumeration (the shared K8 code path) ------------------
      const auto t0 = std::chrono::steady_clock::now();
      std::vector<AttachPair> pairs;
      k8EnumeratePrefilteredPairs(ev, chains, accepted, cf, gateLogit, apre, pairs);
      const auto t1 = std::chrono::steady_clock::now();
      const double enumMs = msBetween(t0, t1);

      // Per-chainPos spans (k8EnumeratePrefilteredPairs emits chainPos-ascending,
      // plsRow-ascending within a chain).
      std::vector<int> pairBegin(nAcc + 1, 0);
      for (const AttachPair& pr : pairs)
        ++pairBegin[pr.chainPos + 1];
      for (int pos = 0; pos < nAcc; ++pos)
        pairBegin[pos + 1] += pairBegin[pos];

      // --- label + write every prefiltered pair ---------------------------------------
      long long nTruePairs = 0;
      {
        std::vector<int> shared;
        for (const AttachPair& pr : pairs) {
          const auto& cs = chainSims[pr.chainPos];
          const auto& ps = plsSims[pr.plsRow];
          shared.clear();
          std::set_intersection(cs.begin(), cs.end(), ps.begin(), ps.end(), std::back_inserter(shared));
          const int label = shared.empty() ? 0 : 1;
          float simVxy = -999.f, simPt = -999.f;
          if (label == 1) {
            ++nTruePairs;
            int best = -1;
            float bestPt = -1.f;
            for (int s : shared) {
              if (s < nAccSim && ev.sim_pt[s] > bestPt) {
                best = s;
                bestPt = ev.sim_pt[s];
              }
            }
            if (best >= 0) {  // pileup-only shared sim keeps label 1 with -999 kinematics
              simPt = ev.sim_pt[best];
              simVxy = std::sqrt(ev.sim_vx[best] * ev.sim_vx[best] + ev.sim_vy[best] * ev.sim_vy[best]);
            }
          }
          pairWriter.fillPair(ev.evt, pr.f, label, chains.nLayers[accepted[pr.chainPos]], simVxy, simPt);
        }
      }

      // --- prefilter true-pair efficiency proxy ---------------------------------------
      // Denominator: accepted 5+-layer chains with a non-empty sim set whose sim(s) have
      // a matching pLS ANYWHERE in the event. Numerator: one of those pLS survived the
      // prefilter FOR THIS CHAIN. Failures are window-attributed per candidate pLS.
      long long evChains5 = 0, evDen = 0, evNum = 0;
      std::vector<int> cand;
      for (int pos = 0; pos < nAcc; ++pos) {
        const int c = accepted[pos];
        if (chains.nLayers[c] < 5)
          continue;
        ++evChains5;
        const auto& cs = chainSims[pos];
        if (cs.empty())
          continue;  // fake chain: no truth to measure
        cand.clear();
        for (int s : cs) {
          auto it = simToPls.find(s);
          if (it != simToPls.end())
            cand.insert(cand.end(), it->second.begin(), it->second.end());
        }
        if (cand.empty())
          continue;  // the chain's sim has no matched pLS anywhere
        std::sort(cand.begin(), cand.end());
        cand.erase(std::unique(cand.begin(), cand.end()), cand.end());
        ++evDen;
        bool survived = false;
        const auto spanB = pairs.begin() + pairBegin[pos];
        const auto spanE = pairs.begin() + pairBegin[pos + 1];
        for (int p : cand) {
          const auto it =
              std::lower_bound(spanB, spanE, p, [](const AttachPair& a, int v) { return a.plsRow < v; });
          if (it != spanE && it->plsRow == p) {
            survived = true;
            break;
          }
        }
        if (survived) {
          ++evNum;
          continue;
        }
        for (int p : cand) {
          float aDT = 0.f, aDP = 0.f;
          k8ProbePairWindows(ev, chains, c, apre, p, aDT, aDP);
          const bool fDT = aDT >= apre.prefDTanL;
          const bool fDP = aDP >= apre.prefDPhi;
          if (fDT && fDP)
            ++totBindBoth;
          else if (fDT)
            ++totBindDTanL;
          else if (fDP)
            ++totBindDPhi;
        }
      }

      std::printf(
          "evt %lld (run %u lumi %u event %llu): accepted=%d chains5p=%lld nPls=%d pairs=%zu true=%lld (%.4f)"
          " | proxy pLS-matched=%lld survived=%lld (%.3f) | enum=%.1f ms\n",
          i, ev.run, ev.lumi, ev.evt, nAcc, evChains5, nPls, pairs.size(), nTruePairs,
          pairs.empty() ? 0.0 : static_cast<double>(nTruePairs) / static_cast<double>(pairs.size()), evDen, evNum,
          evDen > 0 ? static_cast<double>(evNum) / static_cast<double>(evDen) : 0.0, enumMs);

      totChains5 += evChains5;
      totPairs += static_cast<long long>(pairs.size());
      totTruePairs += nTruePairs;
      totProxyDen += evDen;
      totProxyNum += evNum;
      totEnumMs += enumMs;
    }
    pairWriter.writeAndClose();

    const double nEvD = nRun > 0 ? static_cast<double>(nRun) : 1.0;
    const double proxyEff = totProxyDen > 0 ? static_cast<double>(totProxyNum) / static_cast<double>(totProxyDen) : 0.0;
    std::printf("pairdump summary: %lld events (prefDTanL=%.3f prefDPhi=%.3f)\n", nRun, apre.prefDTanL, apre.prefDPhi);
    std::printf("  chains5p total=%lld mean=%.1f | pairs total=%lld mean=%.1f | true=%lld fraction=%.4f\n", totChains5,
                totChains5 / nEvD, totPairs, totPairs / nEvD, totTruePairs,
                totPairs > 0 ? static_cast<double>(totTruePairs) / static_cast<double>(totPairs) : 0.0);
    std::printf("  PREFILTER TRUE-PAIR EFFICIENCY (chains w/ matched pLS anywhere): %lld / %lld = %.4f\n", totProxyNum,
                totProxyDen, proxyEff);
    std::printf("  failed true-pair window attribution (per candidate pLS): dTanL-only=%lld dPhi-only=%lld both=%lld\n",
                totBindDTanL, totBindDPhi, totBindBoth);
    if (proxyEff < 0.97 && totProxyDen > 0) {
      const char* binding = (totBindDTanL >= totBindDPhi && totBindDTanL >= totBindBoth) ? "prefDTanL"
                            : (totBindDPhi >= totBindBoth)                               ? "prefDPhi"
                                                                                         : "both windows";
      std::printf("  NOTE: efficiency < 0.97 -- binding window by failure count: %s\n", binding);
    }
    std::printf("  enum time total=%.1f ms mean=%.1f ms\n", totEnumMs, totEnumMs / nEvD);
    std::printf("  wrote %s\n", outPath.c_str());
    return 0;
  }

  if (mode == "chains") {
    std::printf("chains mode: thetaEdge=%.3f lambdaLen=%.3f kWeldSweeps=%d\n", thetaEdge, lambdaLen, kWeldSweeps);
    long long totT3 = 0, totEdges = 0, totPass = 0, totChains = 0;
    long long nodeHistTot[4] = {0, 0, 0, 0};
    long long layHistTot[6] = {0, 0, 0, 0, 0, 0};
    double totInferMs = 0.0, totWeldMs = 0.0;

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
      const auto t0 = std::chrono::steady_clock::now();
      runEdgeInference(g, nf, ef, scores);
      const auto t1 = std::chrono::steady_clock::now();
      Chains chains;
      k6WeldChains(ev, g, scores, thetaEdge, lambdaLen, chains);
      const auto t2 = std::chrono::steady_clock::now();
      const double inferMs = msBetween(t0, t1);
      const double weldMs = msBetween(t1, t2);

      long long nPass = 0;
      for (float lo : scores.logOdds)
        if (lo >= thetaEdge)
          ++nPass;
      const long long nT3 = static_cast<long long>(ev.t3_lsIdx0.size());
      const long long nEdges = static_cast<long long>(g.edges.size());
      const long long nChains = chains.offsets.empty() ? 0 : static_cast<long long>(chains.offsets.size()) - 1;

      long long nodeHist[4] = {0, 0, 0, 0};
      long long layHist[6] = {0, 0, 0, 0, 0, 0};
      for (long long c = 0; c < nChains; ++c) {
        ++nodeHist[nodeBin(chains.offsets[c + 1] - chains.offsets[c])];
        ++layHist[layerBin9(chains.nLayers[c])];
      }

      std::printf(
          "evt %lld (run %u lumi %u event %llu): nT3=%lld edges=%lld pass=%lld chains=%lld"
          " nodes[2/3/4/5+]=%lld/%lld/%lld/%lld"
          " nLayers[<=4/5/6/7/8/9+]=%lld/%lld/%lld/%lld/%lld/%lld"
          " | infer=%.3f ms weld=%.3f ms\n",
          i, ev.run, ev.lumi, ev.evt, nT3, nEdges, nPass, nChains, nodeHist[0], nodeHist[1], nodeHist[2], nodeHist[3],
          layHist[0], layHist[1], layHist[2], layHist[3], layHist[4], layHist[5], inferMs, weldMs);

      totT3 += nT3;
      totEdges += nEdges;
      totPass += nPass;
      totChains += nChains;
      for (int b = 0; b < 4; ++b)
        nodeHistTot[b] += nodeHist[b];
      for (int b = 0; b < 6; ++b)
        layHistTot[b] += layHist[b];
      totInferMs += inferMs;
      totWeldMs += weldMs;
    }

    const double nEvD = nRun > 0 ? static_cast<double>(nRun) : 1.0;
    std::printf("chains summary: %lld events (thetaEdge=%.3f lambdaLen=%.3f)\n", nRun, thetaEdge, lambdaLen);
    std::printf("  nT3    total=%lld mean=%.1f\n", totT3, totT3 / nEvD);
    std::printf("  edges  total=%lld mean=%.1f | passing thetaEdge total=%lld mean=%.1f (%.2f%%)\n", totEdges,
                totEdges / nEvD, totPass, totPass / nEvD,
                totEdges > 0 ? 100.0 * static_cast<double>(totPass) / static_cast<double>(totEdges) : 0.0);
    std::printf("  chains total=%lld mean=%.1f\n", totChains, totChains / nEvD);
    std::printf("  chain nodes   [2/3/4/5+]        = %lld/%lld/%lld/%lld\n", nodeHistTot[0], nodeHistTot[1],
                nodeHistTot[2], nodeHistTot[3]);
    std::printf("  chain nLayers [<=4/5/6/7/8/9+]  = %lld/%lld/%lld/%lld/%lld/%lld\n", layHistTot[0], layHistTot[1],
                layHistTot[2], layHistTot[3], layHistTot[4], layHistTot[5]);
    std::printf("  time infer total=%.3f ms mean=%.3f ms | weld total=%.3f ms mean=%.3f ms\n", totInferMs,
                totInferMs / nEvD, totWeldMs, totWeldMs / nEvD);
    return 0;
  }

  if (mode == "hybrid") {
    // M4 A/B: kept baseline pixel TCs + prototype chain TCs through the impersonating
    // writer (OutputWriter::fillEventHybrid), so createPerfNumDenHists / compare_ab.py
    // judge the swap against base300_hists.root with production-identical definitions.
    std::printf(
        "hybrid mode: thetaEdge=%.3f lambdaLen=%.3f thetaChain4/5/6=%.3f/%.3f/%.3f"
        " maxClaimedFrac=%.3f dropPixelConsumed=%s chainGateMode=%d kWeldSweeps=%d\n",
        thetaEdge, lambdaLen, thetaChain4, thetaChain5, thetaChain6, maxClaimedFrac,
        dropPixelConsumed ? "on" : "off", chainGateMode, kWeldSweeps);
    if (attachMode == 1) {
      AttachParams apDefaults;
      std::printf(
          "attach (-A 1): thetaAttach=%.3f attachHead=%s prefDTanL=%.3f prefDPhi=%.3f"
          " (K8 runs after the theta gate and BEFORE the pixel-consumed drop; attached"
          " chains bypass partOfPT5, still respect partOfPT3, become type-7 TCs with"
          " pt = pLS ptIn + the pLS pixel hits prepended, and suppress their pLS's"
          " baseline pT5/pLS rows; pT3 rows untouched)\n",
          thetaAttach, attachHeadAvailable() ? "trained" : "sentinel", apDefaults.prefDTanL, apDefaults.prefDPhi);
    } else if (attachMode == 2) {
      AttachParams apDefaults;
      std::printf(
          "attach (-A 2, M7b): thetaAttach=%.3f attachHead=%s prefDTanL=%.3f prefDPhi=%.3f"
          " (subordinate two-pass claim: pass 1 = exact -A 0 pipeline, no bypass;"
          " pass 2 = attached chains pixdropped by partOfPT5 claim against the"
          " post-pass-1 MD map only, becoming type-7 TCs; attached pass-1 chains"
          " upgrade in place to type 7; seed-family suppression drops kept-baseline"
          " pT5/pT3/pLS rows sharing >= 2 pixel hits with any attached pLS)\n",
          thetaAttach, attachHeadAvailable() ? "trained" : "sentinel", apDefaults.prefDTanL, apDefaults.prefDPhi);
    }
    OutputWriter writer(outPath, label);

    long long totPixKept = 0;
    long long totChainsIn = 0, totAfterTheta = 0, totAfterPixDrop = 0, totAfterClaim = 0;
    long long totChainTCs = 0, totT5c = 0, totT4c = 0;
    long long totAttached = 0, totPixSuppressed = 0, totPairsPref = 0, totPairsScored = 0;
    long long totPass2Cand = 0, totPass2Acc = 0, totUpgraded = 0;
    long long totSuppByType[3] = {0, 0, 0};  // {pT5 rows, pT3 rows, pLS rows} (-A 2)
    double totInferMs = 0.0, totWeldMs = 0.0, totArbMs = 0.0, totFillMs = 0.0, totAttachMs = 0.0;

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
      const auto t0 = std::chrono::steady_clock::now();
      runEdgeInference(g, nf, ef, scores);
      const auto t1 = std::chrono::steady_clock::now();
      Chains chains;
      k6WeldChains(ev, g, scores, thetaEdge, lambdaLen, chains);

      // Chain gate (-G 1, default): the K9 acceptance score per chain becomes the
      // chain-gate MLP LOGIT (plan 5a hard gate) -- thetaChain4/5/6 cut on that scale
      // and arbitration ORDER uses it too. -G 0 keeps the legacy K6 sum-logit score
      // (chains.score untouched; regression path, no ChainFeatures computed).
      // -G 2 (split / T4-class-only gate): chains with nLayers <= 4 get the gate logit
      // (thetaChain4 cuts on the gate scale); chains with nLayers >= 5 keep the legacy
      // K6 sum-logit score (thetaChain5/6 on the legacy scale, 0 = structural no-op) --
      // ordering mixes the two scales, which sinks short chains to the end of the
      // claim order (long legacy scores dominate the gate logit range).
      // With -A 1, ChainFeatures + gate logits are computed regardless of -G because
      // attach feature 11 is ALWAYS the gate logit (the pairdump training convention).
      ChainFeatures cfHyb;
      std::vector<float> gateLogit;
      if (chainGateMode >= 1 || attachMode) {
        computeChainFeatures(ev, g, chains, scores, cfHyb);
        runChainInference(cfHyb, gateLogit);
        if (chainGateMode >= 1)
          for (std::size_t c = 0; c < gateLogit.size(); ++c)
            if (chainGateMode == 1 || chains.nLayers[c] <= 4)
              chains.score[c] = gateLogit[c];
      }
      const auto t2 = std::chrono::steady_clock::now();

      ArbitrationParams ap;
      ap.thetaChain4 = thetaChain4;
      ap.thetaChain5 = thetaChain5;
      ap.thetaChain6 = thetaChain6;
      ap.maxClaimedFrac = maxClaimedFrac;
      ap.dropPixelConsumed = dropPixelConsumed;

      // K8 pixel attach (-A 1 and -A 2): the attach decision is made over ALL
      // theta-passing chains BEFORE any pixel-consumed drop or claim (K8 itself
      // considers only nLayers >= 5; one chain per pLS, contention by logit).
      // -A 1 (rejected v1): pixdrop with a per-chain partOfPT5 bypass for attached
      // chains -> single MD-claim arbitration (the bypass floods the claim; kept as
      // reference). -A 2 (M7b): NO bypass anywhere -- pass 1 of the two-pass claim is
      // the exact -A 0 pipeline; attachment only (a) upgrades attached pass-1-accepted
      // chains to type 7 in place and (b) qualifies attached-but-pixdropped chains for
      // the subordinate pass 2. In both modes a chain that attached but produced no TC
      // triggers NO suppression (its pLS keeps its baseline rows; no fallback).
      Attachments att;
      std::vector<int> thetaPass;
      std::vector<char> attachBypass;
      std::vector<int> chainAttachPls;  // per-chain: attached pLS row or -1
      double attachMs = 0.0;
      if (attachMode) {
        const int nChainsAll = static_cast<int>(chains.score.size());
        for (int c = 0; c < nChainsAll; ++c)
          if (chains.score[c] >= ap.thetaFor(chains.nLayers[c]))
            thetaPass.push_back(c);
        AttachParams apar;
        apar.thetaAttach = thetaAttach;
        const auto ta0 = std::chrono::steady_clock::now();
        k8AttachPixels(ev, chains, thetaPass, cfHyb, gateLogit, apar, att);
        const auto ta1 = std::chrono::steady_clock::now();
        attachMs = msBetween(ta0, ta1);
        attachBypass.assign(nChainsAll, 0);
        chainAttachPls.assign(nChainsAll, -1);
        for (std::size_t pos = 0; pos < thetaPass.size(); ++pos) {
          if (att.plsRow[pos] >= 0) {
            attachBypass[thetaPass[pos]] = 1;
            chainAttachPls[thetaPass[pos]] = att.plsRow[pos];
          }
        }
      }

      // Arbitration. -A 0: legacy. -A 1: single pass with the (rejected) partOfPT5
      // bypass for attached chains. -A 2 (M7b): subordinate two-pass claim -- pass 1 is
      // the exact -A 0 call (bit-identical accepted set by construction), pass 2 lets
      // attached-but-pixdropped chains claim only what pass 1 left free. The accepted
      // list is pass 1 followed by pass 2 (nPass1 marks the boundary).
      std::vector<int> accepted;
      std::size_t nPass1 = 0;
      if (attachMode == 2) {
        std::vector<int> acceptedP2;
        k9ArbitrateTwoPass(ev, chains, ap, chainAttachPls, accepted, acceptedP2);
        nPass1 = accepted.size();
        accepted.insert(accepted.end(), acceptedP2.begin(), acceptedP2.end());
      } else {
        k9Arbitrate(ev, chains, ap, accepted, attachMode == 1 ? &attachBypass : nullptr);
        nPass1 = accepted.size();
      }
      std::vector<ChainTC> chainTCs;
      k10AssembleChainTCs(ev, chains, accepted, chainTCs);
      const auto t3 = std::chrono::steady_clock::now();

      // Funnel counts for the report: replicate K9's two pre-claim gates (theta gate,
      // then pixel-consumed drop) so the per-stage attrition is visible per event.
      const long long nChainsIn = chains.offsets.empty() ? 0 : static_cast<long long>(chains.offsets.size()) - 1;
      const int nT3 = static_cast<int>(ev.t3_lsIdx0.size());
      const bool havePixFlags = static_cast<int>(ev.t3_partOfPT5.size()) == nT3 &&
                                static_cast<int>(ev.t3_partOfPT3.size()) == nT3;
      long long nAfterTheta = 0, nAfterPixDrop = 0, nPass2Cand = 0;
      for (long long c = 0; c < nChainsIn; ++c) {
        if (chains.score[c] < ap.thetaFor(chains.nLayers[c]))
          continue;
        ++nAfterTheta;
        bool hasPT5 = false, hasPT3 = false;
        if (dropPixelConsumed && havePixFlags) {
          for (int k = chains.offsets[c]; k < chains.offsets[c + 1] && !hasPT3; ++k) {
            const int t3n = chains.items[k];
            hasPT5 = hasPT5 || ev.t3_partOfPT5[t3n];
            hasPT3 = ev.t3_partOfPT3[t3n];
          }
        }
        // Mirror of the K9 attach bypass (-A 1 ONLY): attached chains skip the partOfPT5
        // half of the drop but still respect partOfPT3. -A 2 pass 1 has NO bypass.
        const bool bypassPT5 = attachMode == 1 && attachBypass[c] != 0;
        if (!((hasPT5 && !bypassPT5) || hasPT3))
          ++nAfterPixDrop;
        else if (attachMode == 2 && chainAttachPls[c] >= 0 && hasPT5 && !hasPT3)
          ++nPass2Cand;  // mirror of the k9ArbitrateTwoPass pass-2 candidate predicate
      }
      const long long nAfterClaim = static_cast<long long>(nPass1);
      const long long nPass2Acc = static_cast<long long>(accepted.size() - nPass1);

      long long nT5c = 0, nT4c = 0;
      for (const ChainTC& ctc : chainTCs)
        (ctc.type == 4 ? nT5c : nT4c) += 1;

      long long nPixKept = 0;
      for (int t : ev.tc_type)
        if (t == 7 || t == 5 || t == 8)
          ++nPixKept;

      // ChainTC -> OutTC: bare chains are pure outer-tracker objects, every hit a ph2
      // row. With -A 1, a chain with a K8 attachment becomes a type-7 (pT5-class) TC:
      //   hit list = the pLS's PIXEL hits (pix rows via pLS_seedIdx -> trk.see_hitIdx,
      //   entries with see_hitType == Pixel) followed by the chain's OT hits;
      //   nhitOT stays the OT count; pt = the pLS ptIn (the pixel-seed pt is better
      //   measured than the chain's median T3 pt -- documented v1 choice); eta/phi stay
      //   the chain's. Its pLS's baseline pT5/pLS rows are dropped in fillEventHybrid.
      // The chainTCs/accepted lockstep below relies on the K10 contract (accepted order,
      // skipping nLayers < 4).
      // Helper: pixel hit rows of a pLS (seedIdx -> trk see_hitIdx, Pixel-type entries).
      auto plsPixelHits = [&](int p, std::vector<int>& hits) {
        hits.clear();
        const int seed = ev.pLS_seedIdx[p];
        if (seed < 0 || seed >= static_cast<int>(trk.see_hitIdx.size()))
          return;
        const auto& hIdx = trk.see_hitIdx[seed];
        const auto& hTyp = trk.see_hitType[seed];
        for (std::size_t h = 0; h < hIdx.size() && h < hTyp.size(); ++h)
          if (hTyp[h] == static_cast<int>(proto::HitType::Pixel))
            hits.push_back(hIdx[h]);
      };

      std::vector<OutTC> outTCs;
      outTCs.reserve(chainTCs.size());
      std::vector<char> plsSuppressed;
      long long nAttached = 0, nUpgraded = 0;
      if (attachMode)
        plsSuppressed.assign(ev.pLS_pt.size(), 0);
      {
        std::vector<int> pixHits;
        std::size_t tcPos = 0;
        for (std::size_t ai = 0; ai < accepted.size(); ++ai) {
          const int c = accepted[ai];
          if (chains.nLayers[c] < 4)
            continue;  // K10 dropped it; keep the lockstep aligned
          ChainTC& ctc = chainTCs[tcPos++];
          OutTC otc;
          otc.pt = ctc.pt;
          otc.eta = ctc.eta;
          otc.phi = ctc.phi;
          otc.type = ctc.type;
          otc.nhitOT = ctc.nhitOT;
          const int p = attachMode ? chainAttachPls[c] : -1;
          if (p >= 0) {
            ++nAttached;
            if (attachMode == 2 && ai < nPass1)
              ++nUpgraded;  // pass-1-accepted chain upgraded in place (no claim change)
            plsSuppressed[p] = 1;
            otc.type = 7;           // pT5-class: chain + attached pLS
            otc.pt = ev.pLS_pt[p];  // pLS ptIn (pixel pt is better measured)
            plsPixelHits(p, pixHits);
            for (int hi : pixHits) {
              otc.hitIdxs.push_back(static_cast<unsigned int>(hi));
              otc.hitTypes.push_back(proto::HitType::Pixel);
            }
          }
          for (unsigned int hi : ctc.hitIdxs) {
            otc.hitIdxs.push_back(hi);
            otc.hitTypes.push_back(proto::HitType::Phase2OT);
          }
          outTCs.push_back(std::move(otc));
        }
      }

      // M7b SEED-FAMILY suppression mask (-A 2 only; sim-blind, mirrors production
      // pixelHitsOverlapAny): expand the attached-pLS mark set to every pLS sharing
      // >= 2 pixel hit rows with any attached pLS. The attached pLS themselves are
      // family members trivially (full self-overlap), so this is a strict superset of
      // the v1 own-pLS mask. The writer then drops kept-baseline rows of ANY pixel
      // type (7, 5, AND 8) whose own pLS is in the family -- the M7 diagnosis (b):
      // own-row-only suppression left the same sims' OTHER pixel deliveries
      // (pLS-duplicate seeds, pT3 partners) alive, so the dup floor never drained.
      if (attachMode == 2) {
        std::unordered_map<int, std::vector<int>> hit2att;  // pixel hit row -> attached pLS
        std::vector<int> pixHits;
        const int nPls = static_cast<int>(ev.pLS_pt.size());
        for (int p = 0; p < nPls; ++p) {
          if (!plsSuppressed[p])
            continue;
          plsPixelHits(p, pixHits);
          for (int h : pixHits)
            hit2att[h].push_back(p);
        }
        if (!hit2att.empty()) {
          std::unordered_map<int, int> cnt;  // attached pLS -> shared-hit count with q
          for (int q = 0; q < nPls; ++q) {
            if (plsSuppressed[q])
              continue;
            plsPixelHits(q, pixHits);
            if (static_cast<int>(pixHits.size()) < 2)
              continue;
            cnt.clear();
            bool inFamily = false;
            for (int h : pixHits) {
              const auto it = hit2att.find(h);
              if (it == hit2att.end())
                continue;
              for (int p : it->second)
                if (++cnt[p] >= 2) {
                  inFamily = true;
                  break;
                }
              if (inFamily)
                break;
            }
            if (inFamily)
              plsSuppressed[q] = 1;
          }
        }
      }

      int nPixSuppressed = 0;
      int nSuppByType[3] = {0, 0, 0};  // {pT5 rows, pT3 rows, pLS rows}
      writer.fillEventHybrid(ev, trk, outTCs, attachMode ? &plsSuppressed : nullptr, &nPixSuppressed,
                             attachMode == 2, attachMode == 2 ? nSuppByType : nullptr);
      const auto t4 = std::chrono::steady_clock::now();

      const double inferMs = msBetween(t0, t1);
      const double weldMs = msBetween(t1, t2);
      // The K8 attach call sits inside the t2..t3 window; report it separately so the
      // arb bucket keeps meaning K9+K10 only.
      const double arbMs = msBetween(t2, t3) - attachMs;
      const double fillMs = msBetween(t3, t4);

      if (attachMode == 2) {
        // claim= is the PASS-1 count (bit-identical to -A 0 by construction); p2= is
        // subordinate pass-2 accepted/candidates; upg= attached pass-1 chains upgraded
        // in place to type 7; attach= all TCs with a pLS (upgrades + pass-2);
        // pixSupp= seed-family-suppressed kept-baseline rows [pT5/pT3/pLS].
        std::printf(
            "evt %lld (run %u lumi %u event %llu): pixKept=%lld chains=%lld -> theta=%lld ->"
            " pixdrop=%lld -> claim=%lld p2=%lld/%lld | chainTC=%zu (T5c=%lld T4c=%lld upg=%lld"
            " attach=%lld pixSupp=%d[%d/%d/%d]) | infer=%.3f weld=%.3f attach=%.3f arb=%.3f fill=%.3f ms\n",
            i, ev.run, ev.lumi, ev.evt, nPixKept, nChainsIn, nAfterTheta, nAfterPixDrop, nAfterClaim, nPass2Acc,
            nPass2Cand, chainTCs.size(), nT5c, nT4c, nUpgraded, nAttached, nPixSuppressed, nSuppByType[0],
            nSuppByType[1], nSuppByType[2], inferMs, weldMs, attachMs, arbMs, fillMs);
      } else if (attachMode == 1) {
        // T5c/T4c stay the nLayers-class counts (attached chains are counted inside
        // T5c AND in attach=; their output tc_type is 7). pixSupp = kept-baseline rows
        // dropped by the K8 suppression (pixKept still counts pre-suppression rows).
        std::printf(
            "evt %lld (run %u lumi %u event %llu): pixKept=%lld chains=%lld -> theta=%lld ->"
            " pixdrop=%lld -> claim=%lld | chainTC=%zu (T5c=%lld T4c=%lld attach=%lld pixSupp=%d)"
            " | infer=%.3f weld=%.3f attach=%.3f arb=%.3f fill=%.3f ms\n",
            i, ev.run, ev.lumi, ev.evt, nPixKept, nChainsIn, nAfterTheta, nAfterPixDrop, nAfterClaim, chainTCs.size(),
            nT5c, nT4c, nAttached, nPixSuppressed, inferMs, weldMs, attachMs, arbMs, fillMs);
      } else {
        std::printf(
            "evt %lld (run %u lumi %u event %llu): pixKept=%lld chains=%lld -> theta=%lld ->"
            " pixdrop=%lld -> claim=%lld | chainTC=%zu (T5c=%lld T4c=%lld)"
            " | infer=%.3f weld=%.3f arb=%.3f fill=%.3f ms\n",
            i, ev.run, ev.lumi, ev.evt, nPixKept, nChainsIn, nAfterTheta, nAfterPixDrop, nAfterClaim, chainTCs.size(),
            nT5c, nT4c, inferMs, weldMs, arbMs, fillMs);
      }

      totPixKept += nPixKept;
      totChainsIn += nChainsIn;
      totAfterTheta += nAfterTheta;
      totAfterPixDrop += nAfterPixDrop;
      totAfterClaim += nAfterClaim;
      totChainTCs += static_cast<long long>(chainTCs.size());
      totT5c += nT5c;
      totT4c += nT4c;
      totAttached += nAttached;
      totPixSuppressed += nPixSuppressed;
      totPass2Cand += nPass2Cand;
      totPass2Acc += nPass2Acc;
      totUpgraded += nUpgraded;
      for (int t = 0; t < 3; ++t)
        totSuppByType[t] += nSuppByType[t];
      totPairsPref += att.nPairsPrefiltered;
      totPairsScored += att.nPairsScored;
      totInferMs += inferMs;
      totWeldMs += weldMs;
      totAttachMs += attachMs;
      totArbMs += arbMs;
      totFillMs += fillMs;
    }
    writer.writeAndClose();

    const double nEvD = nRun > 0 ? static_cast<double>(nRun) : 1.0;
    std::printf("hybrid summary: %lld events (thetaEdge=%.3f lambdaLen=%.3f thetaChain4/5/6=%.3f/%.3f/%.3f"
                " maxClaimedFrac=%.3f dropPixelConsumed=%s)\n",
                nRun, thetaEdge, lambdaLen, thetaChain4, thetaChain5, thetaChain6, maxClaimedFrac,
                dropPixelConsumed ? "on" : "off");
    const long long totAcceptedAll = totAfterClaim + totPass2Acc;
    std::printf("  pixel TCs kept  total=%lld mean=%.1f\n", totPixKept, totPixKept / nEvD);
    if (attachMode == 2)
      std::printf("  chain funnel    in=%lld -> theta=%lld -> pixdrop=%lld -> claim(pass1)=%lld"
                  " | pass2 cand=%lld accepted=%lld\n",
                  totChainsIn, totAfterTheta, totAfterPixDrop, totAfterClaim, totPass2Cand, totPass2Acc);
    else
      std::printf("  chain funnel    in=%lld -> theta=%lld -> pixdrop=%lld -> claim=%lld\n", totChainsIn, totAfterTheta,
                  totAfterPixDrop, totAfterClaim);
    std::printf("  chain TCs       total=%lld mean=%.1f (T5-class=%lld T4-class=%lld; %lld accepted chains"
                " below 4 layers dropped by K10)\n",
                totChainTCs, totChainTCs / nEvD, totT5c, totT4c, totAcceptedAll - totChainTCs);
    if (attachMode) {
      std::printf("  K8 attach       attached=%lld mean=%.1f | baseline pixel rows suppressed=%lld mean=%.1f"
                  " | pairs prefiltered=%lld scored=%lld (thetaAttach=%.3f head=%s)\n",
                  totAttached, totAttached / nEvD, totPixSuppressed, totPixSuppressed / nEvD, totPairsPref,
                  totPairsScored, thetaAttach, attachHeadAvailable() ? "trained" : "sentinel");
      if (attachMode == 2)
        std::printf("  M7b breakdown   pass1 upgrades=%lld mean=%.1f | pass2 TCs=%lld mean=%.1f"
                    " | suppressed rows by type pT5=%lld pT3=%lld pLS=%lld\n",
                    totUpgraded, totUpgraded / nEvD, totPass2Acc, totPass2Acc / nEvD, totSuppByType[0],
                    totSuppByType[1], totSuppByType[2]);
      std::printf("  output TCs/evt  mean=%.1f (pixel kept %.1f - suppressed %.1f + chain %.1f)\n",
                  (totPixKept - totPixSuppressed + totChainTCs) / nEvD, totPixKept / nEvD, totPixSuppressed / nEvD,
                  totChainTCs / nEvD);
      std::printf("  time mean/evt   infer=%.3f weld=%.3f attach=%.3f arb+asm=%.3f fill=%.3f ms\n", totInferMs / nEvD,
                  totWeldMs / nEvD, totAttachMs / nEvD, totArbMs / nEvD, totFillMs / nEvD);
    } else {
      std::printf("  output TCs/evt  mean=%.1f (pixel %.1f + chain %.1f)\n", (totPixKept + totChainTCs) / nEvD,
                  totPixKept / nEvD, totChainTCs / nEvD);
      std::printf("  time mean/evt   infer=%.3f weld=%.3f arb+asm=%.3f fill=%.3f ms\n", totInferMs / nEvD,
                  totWeldMs / nEvD, totArbMs / nEvD, totFillMs / nEvD);
    }
    std::printf("  wrote %s\n", outPath.c_str());
    return 0;
  }

  if (mode == "oracle") {
    // Truth-scored welding: logOdds = +10 for label==1 edges, -10 otherwise, thetaEdge=0,
    // so K6 welds exclusively along true edges -- the ceiling of what chain-tracking can
    // reach with a perfect edge classifier under the mutual-best weld rule.
    std::printf("oracle mode: thetaEdge=0 (forced) lambdaLen=%.3f kWeldSweeps=%d\n", lambdaLen, kWeldSweeps);
    long long totT3 = 0, totEdges = 0, totTrue = 0, totChains = 0;
    long long totChainsSim = 0, totChainsPileup = 0, totChainsNone = 0;
    double totWeldMs = 0.0;

    // Ceiling accumulators. Categories overlap by design: [0] prompt vxy<1,
    // [1] displaced vxy>=1 (includes very displaced), [2] very displaced vxy>=5.
    long long formable[3] = {0, 0, 0};
    long long chained[3] = {0, 0, 0};
    long long layHist[3][5] = {{0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}};
    long long baseAny[3] = {0, 0, 0};
    long long baseT5T4[3] = {0, 0, 0};

    for (long long i = 0; i < nRun; ++i) {
      if (!reader.loadEntry(i, ev, trk)) {
        std::fprintf(stderr, "Error: no aligned tracking event for LST entry %lld.\n", i);
        return 1;
      }
      ChainGraph g;
      k1BuildIncidence(ev, g);
      k2BuildEdges(ev, g);
      T3SimSets t3sims;
      buildT3SimSets(ev, t3sims);
      EdgeLabels labels;
      labelEdges(ev, g, t3sims, labels);

      EdgeScores scores;
      scores.logOdds.resize(g.edges.size());
      for (std::size_t e = 0; e < g.edges.size(); ++e)
        scores.logOdds[e] = labels.label[e] == 1 ? 10.0f : -10.0f;

      Chains chains;
      const auto t0 = std::chrono::steady_clock::now();
      k6WeldChains(ev, g, scores, 0.0f, lambdaLen, chains);
      const auto t1 = std::chrono::steady_clock::now();
      const double weldMs = msBetween(t0, t1);

      const int nAccepted = static_cast<int>(ev.sim_pt.size());

      // Per-accepted-sim truth-edge mark: labels.simIdx keeps only ONE sim per edge, so
      // redo the sim-set intersection to credit EVERY accepted sim shared by both T3s.
      std::vector<char> hasTrueEdge(nAccepted, 0);
      std::vector<int> common;
      long long nTrue = 0;
      for (std::size_t e = 0; e < g.edges.size(); ++e) {
        if (labels.label[e] != 1)
          continue;
        ++nTrue;
        const auto& a = t3sims.sims[g.edges[e].inner];
        const auto& b = t3sims.sims[g.edges[e].outer];
        common.clear();
        std::set_intersection(a.begin(), a.end(), b.begin(), b.end(), std::back_inserter(common));
        for (int s : common)
          if (s < nAccepted)
            hasTrueEdge[s] = 1;
      }

      // Chain -> sim assignment; per accepted sim keep the max chain nLayers.
      const long long nChains = chains.offsets.empty() ? 0 : static_cast<long long>(chains.offsets.size()) - 1;
      std::vector<int> maxLay(nAccepted, -1);
      long long nSimChains = 0, nPileupChains = 0, nNoneChains = 0;
      for (long long c = 0; c < nChains; ++c) {
        const int s = chainSimIdx(chains, static_cast<int>(c), t3sims, nAccepted);
        if (s < 0)
          ++nNoneChains;
        else if (s >= nAccepted)
          ++nPileupChains;  // pileup sim: counted in totals, no kinematics -> not in table
        else {
          ++nSimChains;
          if (chains.nLayers[c] > maxLay[s])
            maxLay[s] = chains.nLayers[c];
        }
      }

      // Ceiling accumulation over accepted sims in the kinematic window.
      for (int s = 0; s < nAccepted; ++s) {
        if (!hasTrueEdge[s])
          continue;
        if (!(ev.sim_pt[s] > 0.9f) || !(std::fabs(ev.sim_eta[s]) < 4.5f))
          continue;
        const float vxy = std::sqrt(ev.sim_vx[s] * ev.sim_vx[s] + ev.sim_vy[s] * ev.sim_vy[s]);
        const bool cat[3] = {vxy < 1.0f, vxy >= 1.0f, vxy >= 5.0f};
        const int tcIdx = s < static_cast<int>(ev.sim_tcIdx.size()) ? ev.sim_tcIdx[s] : -999;
        const bool anyTC = tcIdx >= 0 && tcIdx < static_cast<int>(ev.tc_type.size());
        const bool t5t4TC = anyTC && (ev.tc_type[tcIdx] == 4 || ev.tc_type[tcIdx] == 9);
        for (int k = 0; k < 3; ++k) {
          if (!cat[k])
            continue;
          ++formable[k];
          if (maxLay[s] >= 0) {
            ++chained[k];
            ++layHist[k][layerBin8(maxLay[s])];
          }
          if (anyTC)
            ++baseAny[k];
          if (t5t4TC)
            ++baseT5T4[k];
        }
      }

      const long long nT3 = static_cast<long long>(ev.t3_lsIdx0.size());
      const long long nEdges = static_cast<long long>(g.edges.size());
      std::printf(
          "evt %lld (run %u lumi %u event %llu): nT3=%lld edges=%lld true=%lld chains=%lld"
          " (sim=%lld pileup=%lld none=%lld) | weld=%.3f ms\n",
          i, ev.run, ev.lumi, ev.evt, nT3, nEdges, nTrue, nChains, nSimChains, nPileupChains, nNoneChains, weldMs);

      totT3 += nT3;
      totEdges += nEdges;
      totTrue += nTrue;
      totChains += nChains;
      totChainsSim += nSimChains;
      totChainsPileup += nPileupChains;
      totChainsNone += nNoneChains;
      totWeldMs += weldMs;
    }

    const double nEvD = nRun > 0 ? static_cast<double>(nRun) : 1.0;
    std::printf("oracle summary: %lld events (lambdaLen=%.3f)\n", nRun, lambdaLen);
    std::printf("  nT3 total=%lld mean=%.1f | edges total=%lld mean=%.1f | true edges total=%lld mean=%.1f\n", totT3,
                totT3 / nEvD, totEdges, totEdges / nEvD, totTrue, totTrue / nEvD);
    std::printf("  chains total=%lld mean=%.1f : accepted-sim=%lld pileup-sim=%lld unmatched=%lld\n", totChains,
                totChains / nEvD, totChainsSim, totChainsPileup, totChainsNone);
    std::printf("  weld time total=%.3f ms mean=%.3f ms\n", totWeldMs, totWeldMs / nEvD);

    // The ceiling table. Percentages are relative to the formable row of each column.
    auto pct = [](long long n, long long d) { return d > 0 ? 100.0 * static_cast<double>(n) / d : 0.0; };
    std::printf("\nORACLE CEILING (accepted sims, sim_pt>0.9, |sim_eta|<4.5;");
    std::printf(" columns overlap: disp includes vDisp)\n");
    std::printf("  %-26s %16s %16s %16s\n", "", "prompt(vxy<1)", "disp(vxy>=1)", "vDisp(vxy>=5)");
    std::printf("  %-26s %16lld %16lld %16lld\n", "formable (>=1 true edge)", formable[0], formable[1], formable[2]);
    std::printf("  %-26s %8lld (%4.1f%%) %8lld (%4.1f%%) %8lld (%4.1f%%)\n", "oracle chained (>=2 T3)", chained[0],
                pct(chained[0], formable[0]), chained[1], pct(chained[1], formable[1]), chained[2],
                pct(chained[2], formable[2]));
    static const char* layRow[5] = {"  max nLayers <= 4", "  max nLayers = 5", "  max nLayers = 6",
                                    "  max nLayers = 7", "  max nLayers >= 8"};
    for (int b = 0; b < 5; ++b)
      std::printf("  %-26s %16lld %16lld %16lld\n", layRow[b], layHist[0][b], layHist[1][b], layHist[2][b]);
    std::printf("  %-26s %8lld (%4.1f%%) %8lld (%4.1f%%) %8lld (%4.1f%%)\n", "baseline: any TC", baseAny[0],
                pct(baseAny[0], formable[0]), baseAny[1], pct(baseAny[1], formable[1]), baseAny[2],
                pct(baseAny[2], formable[2]));
    std::printf("  %-26s %8lld (%4.1f%%) %8lld (%4.1f%%) %8lld (%4.1f%%)\n", "baseline: T5/T4-type TC", baseT5T4[0],
                pct(baseT5T4[0], formable[0]), baseT5T4[1], pct(baseT5T4[1], formable[1]), baseT5T4[2],
                pct(baseT5T4[2], formable[2]));
    return 0;
  }

  // graph mode
  long long totT3 = 0;
  long long totE1Exact = 0, totE1Emitted = 0;
  long long totE2Exact = 0, totE2Emitted = 0;
  long long totE2Suppressed = 0;
  long long totEdges = 0;
  long long minEdges = -1, maxEdges = -1;
  double totK1Ms = 0.0, totK2Ms = 0.0;

  for (long long i = 0; i < nRun; ++i) {
    if (!reader.loadEntry(i, ev, trk)) {
      std::fprintf(stderr, "Error: no aligned tracking event for LST entry %lld.\n", i);
      return 1;
    }

    ChainGraph g;
    const auto t0 = std::chrono::steady_clock::now();
    k1BuildIncidence(ev, g);
    const auto t1 = std::chrono::steady_clock::now();
    k2BuildEdges(ev, g);
    const auto t2 = std::chrono::steady_clock::now();
    const double k1Ms = msBetween(t0, t1);
    const double k2Ms = msBetween(t1, t2);

    long long e1Emitted = 0, e2Emitted = 0;
    for (const auto& e : g.edges) {
      if (e.type == 1)
        ++e1Emitted;
      else if (e.type == 2)
        ++e2Emitted;
    }
    // K2 drops E2 edges that duplicate an E1 edge, so the shortfall vs the
    // degree-arithmetic exact count is exactly the suppressed-as-E1 count.
    const long long e2Suppressed = g.e2CountExact - e2Emitted;
    const long long nT3 = static_cast<long long>(ev.t3_lsIdx0.size());
    const long long nEdges = static_cast<long long>(g.edges.size());

    std::printf(
        "evt %lld (run %u lumi %u event %llu): nT3=%lld"
        " E1 exact=%lld emitted=%lld | E2 exact=%lld emitted=%lld suppressedAsE1=%lld"
        " | edges=%lld | k1=%.3f ms k2=%.3f ms\n",
        i, ev.run, ev.lumi, ev.evt, nT3, g.e1CountExact, e1Emitted, g.e2CountExact, e2Emitted, e2Suppressed, nEdges,
        k1Ms, k2Ms);
    if (e1Emitted != g.e1CountExact)
      std::fprintf(stderr, "WARNING: evt %lld E1 emitted (%lld) != exact (%lld)\n", i, e1Emitted, g.e1CountExact);

    totT3 += nT3;
    totE1Exact += g.e1CountExact;
    totE1Emitted += e1Emitted;
    totE2Exact += g.e2CountExact;
    totE2Emitted += e2Emitted;
    totE2Suppressed += e2Suppressed;
    totEdges += nEdges;
    if (minEdges < 0 || nEdges < minEdges)
      minEdges = nEdges;
    if (nEdges > maxEdges)
      maxEdges = nEdges;
    totK1Ms += k1Ms;
    totK2Ms += k2Ms;
  }

  const double nEvD = nRun > 0 ? static_cast<double>(nRun) : 1.0;
  std::printf("graph summary: %lld events\n", nRun);
  std::printf("  nT3 total=%lld\n", totT3);
  std::printf("  E1 exact=%lld emitted=%lld%s\n", totE1Exact, totE1Emitted,
              totE1Exact == totE1Emitted ? "" : "  [MISMATCH]");
  std::printf("  E2 exact=%lld emitted=%lld suppressedAsE1=%lld\n", totE2Exact, totE2Emitted, totE2Suppressed);
  std::printf("  edges/event min=%lld mean=%.1f max=%lld\n", nRun > 0 ? minEdges : 0, totEdges / nEvD,
              nRun > 0 ? maxEdges : 0);
  std::printf("  time k1 total=%.3f ms mean=%.3f ms | k2 total=%.3f ms mean=%.3f ms\n", totK1Ms, totK1Ms / nEvD,
              totK2Ms, totK2Ms / nEvD);
  return 0;
}
