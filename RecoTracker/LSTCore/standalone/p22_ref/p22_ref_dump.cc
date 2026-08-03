// P2.2 parity reference dumper.
//
// Runs the FROZEN prototype code (standalone/prototype/, compiled unmodified and linked in here)
// over an LST --allobj ntuple and writes the per-event welded-chain set, the terminal-trim result,
// the 25 chain features + dcaXY, the 3-class gate logits and the -G 6 kill decision in the same
// sidecar format the Alpaka port emits. Nothing in prototype/ is touched: this file only calls
// k1BuildIncidence / k2BuildEdges / computeNodeFeatures / computeEdgeFeatures / runEdgeInference /
// k6WeldChains / k6TrimTerminals / computeChainFeatures / k8ChainDcaXY / runChainInference3, and
// then transcribes the -G 6 kill block of prototype/main.cc verbatim.
//
// FROZEN CONFIGURATION (standalone/fanout5/final/FREEZE_RECORD.txt, ANCHOR + CTL + FLAGSHIP + M19
// resolved with later flags winning) -- P2.2 scope only:
//   -e 0  -L 3.0  -G 6  -X 0.5  -Z 0
//   -TR 1  -TT 1.2  -TA 1.0  -TL 5  -TP 1
//   -M4 4.0  -M4D -1.2  -M5 1e9  -M6 1e9  -MD 1e9  -MRI -0.5  -MR -1.800
//   -C25 0.0  -C25D -2.0
//   -ZE1 1.1  -ZE2 1.7  -ZM4 -0.5  -ZM4D 1.2  (every other -Z* delta 0, -ZIL 0)
//
// Sidecar format (little endian), repeated per event:
//   u32 magic 0x50323243 ('P22C')
//   u32 ievt, u32 nT3, u32 nEdgeRows, u32 nChains
//   nChains x {
//     u32 preNodes, u32 postNodes, u32 nMD, u32 nLayers, i32 branch, u32 trimAction, u32 flags,
//     f32 score, dcaXY, zFake, zPrompt, zDisp, mP, mD, mX,
//     25 x f32 features,
//     preNodes x u32 nodeIdx           (PRE-trim member node list, innermost first)
//     (preNodes - 1) x u32 edgeType    (PRE-trim welded edge types, innermost first)
//     nMD x { u32 anchorHitIdx, u32 otherHitIdx }   (POST-trim MD union, first-appearance order)
//   }
// nEdgeRows is the enumerated edge-row count on the production side and the kept edge count on the
// reference side; it is reported, not gated (the P2.1 gate already proved the two agree).

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "ChainFeatures.h"
#include "ChainInference.h"
#include "EdgeInference.h"
#include "EventData.h"
#include "Features.h"
#include "NtupleReader.h"
#include "PixelAttach.h"
#include "Stages.h"
#include "Trim.h"

namespace {

  // ---- the frozen P2.2 configuration -------------------------------------------------------
  constexpr float kThetaEdge = 0.f;
  constexpr float kLambdaLen = 3.f;
  constexpr float kTrimFactor = 1.2f;
  constexpr float kTrimAbsChi2 = 1.f;
  constexpr int kTrimMinLay = 5;
  constexpr int kTrimPasses = 1;
  constexpr float kDcaSplit = 0.5f;
  constexpr float kT4ExemptDcaMin = 0.f;
  constexpr float kM3Theta4 = 4.f;
  constexpr float kM3Theta4D = -1.2f;
  constexpr float kM3Theta5 = 1e9f;
  constexpr float kM3Theta6 = 1e9f;
  constexpr float kM3ThetaD = 1e9f;
  constexpr float kM3ThetaRI = -0.5f;
  constexpr float kM3ThetaR = -1.8f;
  constexpr float kC25Theta = 0.f;
  constexpr float kC25ThetaD = -2.f;
  constexpr float kZEta1 = 1.1f;
  constexpr float kZEta2 = 1.7f;
  constexpr float kZdM4 = -0.5f;
  constexpr float kZdM4D = 1.2f;
  constexpr float kZdRI = 0.f;
  constexpr float kZdR = 0.f;
  constexpr float kZdR5 = 0.f;
  constexpr float kZdR6 = 0.f;
  constexpr float kZdCP = 0.f;
  constexpr float kZdCD = 0.f;
  constexpr float kZInLay1 = 0.f;
  constexpr float kGateKill = 1e9f;

  template <typename T>
  void put(std::FILE* f, T v) {
    std::fwrite(&v, sizeof(T), 1, f);
  }

  void usage(const char* argv0) {
    std::fprintf(stderr,
                 "usage: %s -i <lst_ntuple.root> -t <tracking_sample> -o <out.bin> [-n <nevents>]\n",
                 argv0);
  }

}  // namespace

int main(int argc, char** argv) {
  std::string lstPath, trkPath, outPath;
  long long maxEvents = -1;

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
  std::printf("p22_ref_dump: %s (%lld entries), processing %lld\n", lstPath.c_str(), nTotal, nRun);

  std::FILE* out = std::fopen(outPath.c_str(), "wb");
  if (!out) {
    std::fprintf(stderr, "cannot open %s\n", outPath.c_str());
    return 1;
  }

  LSTEventData ev;
  TrkEventData trk;

  long long totChains = 0, totKilled = 0, totTrimIn = 0, totTrimOut = 0;
  long long totWeldTies = 0;  // beats() calls decided by the INDEX tie-break, not the logit

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

    // Independent measurement of the ONE place where the port's edge ORDER could matter: the
    // beats() tie-break. Counted over the FIRST argmax sweep, which is where every eligible edge
    // is still in play; a zero here means the packed-key ordering cannot diverge from the
    // reference for any CSR permutation.
    {
      const int nT3 = static_cast<int>(ev.t3_lsIdx0.size());
      std::vector<int> bestOut(nT3, -1), bestIn(nT3, -1);
      for (std::size_t e = 0; e < g.edges.size(); ++e) {
        if (scores.logOdds[e] < kThetaEdge)
          continue;
        const int n = g.edges[e].inner, m = g.edges[e].outer;
        if (bestOut[n] >= 0 && scores.logOdds[e] == scores.logOdds[bestOut[n]])
          ++totWeldTies;
        if (bestIn[m] >= 0 && scores.logOdds[e] == scores.logOdds[bestIn[m]])
          ++totWeldTies;
        if (bestOut[n] < 0 || scores.logOdds[e] > scores.logOdds[bestOut[n]])
          bestOut[n] = static_cast<int>(e);
        if (bestIn[m] < 0 || scores.logOdds[e] > scores.logOdds[bestIn[m]])
          bestIn[m] = static_cast<int>(e);
      }
    }

    Chains chains;
    k6WeldChains(ev, g, scores, kThetaEdge, kLambdaLen, chains);

    // PRE-trim snapshot: the welded chain set and the welded edge set, which gate (a) and (b)
    // compare independently of anything the trim then does.
    Chains pre = chains;

    std::vector<int8_t> action;
    TrimStats st;
    for (int pass = 0; pass < kTrimPasses; ++pass)
      k6TrimTerminals(ev, scores, kLambdaLen, kTrimFactor, kTrimMinLay, kTrimAbsChi2, chains, &action, st);
    totTrimIn += st.nTrimInner;
    totTrimOut += st.nTrimOuter;

    ChainFeatures cf;
    computeChainFeatures(ev, g, chains, scores, cf);

    const std::size_t nC = chains.score.size();
    std::vector<float> dca(nC);
    for (std::size_t c = 0; c < nC; ++c)
      dca[c] = k8ChainDcaXY(ev, chains, static_cast<int>(c));

    std::vector<float> z3;
    runChainInference3(cf, dca, z3);

    // ---- the -G 6 kill block, transcribed from prototype/main.cc -----------------------------
    const bool zOn = kZEta2 > kZEta1 && (kZdRI != 0.f || kZdR != 0.f || kZdR5 != 0.f || kZdR6 != 0.f ||
                                         kZdM4 != 0.f || kZdM4D != 0.f || kZdCP != 0.f || kZdCD != 0.f);
    std::vector<float> mPv(nC), mDv(nC), mXv(nC);
    std::vector<int> brv(nC);
    std::vector<uint32_t> flagsv(nC, 0u);
    for (std::size_t c = 0; c < nC; ++c) {
      const float* z = &z3[3 * c];
      const float mP = z[1] - z[0];
      const float mD = z[2] - z[0];
      const float mX = std::max(z[1], z[2]) - z[0];
      mPv[c] = mP;
      mDv[c] = mD;
      mXv[c] = mX;
      const int nL = chains.nLayers[c];
      brv[c] = nL <= 4 ? (dca[c] >= std::max(kDcaSplit, kT4ExemptDcaMin) ? 1 : 0) : (dca[c] < kDcaSplit ? 2 : 3);

      bool inZ = false;
      if (kZEta2 > kZEta1 && chains.offsets[c + 1] > chains.offsets[c]) {
        const int t3In = chains.items[chains.offsets[c]];
        if (t3In >= 0 && t3In < static_cast<int>(ev.t3_eta.size())) {
          const float ae = std::fabs(ev.t3_eta[t3In]);
          inZ = ae >= kZEta1 && ae < kZEta2;
        }
      }
      if (inZ)
        flagsv[c] |= 0x4u;
      bool ilOk = true;
      if (kZInLay1 >= 0.5f) {
        ilOk = false;
        if (chains.mdOffsets[c + 1] > chains.mdOffsets[c]) {
          const int m0 = chains.mdItems[chains.mdOffsets[c]];
          ilOk = m0 >= 0 && m0 < static_cast<int>(ev.md_layer.size()) && ev.md_layer[m0] == 1;
        }
      }
      const bool zz = zOn && inZ && ilOk;
      const float dRI = zz ? kZdRI : 0.f;
      const float dR = zz ? (kZdR + (nL == 5 ? kZdR5 : kZdR6)) : 0.f;
      const float d4 = zz ? kZdM4 : 0.f;
      const float d4D = zz ? kZdM4D : 0.f;
      const float dCP = zz ? kZdCP : 0.f;
      const float dCD = zz ? kZdCD : 0.f;
      if (nL <= 4) {
        if (dca[c] >= std::max(kDcaSplit, kT4ExemptDcaMin)) {
          if (mD < kM3Theta4D + d4D) {
            chains.score[c] -= kGateKill;
            flagsv[c] |= 0x1u;
          }
          flagsv[c] |= 0x2u;
        } else if (mX < kM3Theta4 + d4) {
          chains.score[c] -= kGateKill;
          flagsv[c] |= 0x1u;
        }
      } else if (dca[c] < kDcaSplit) {
        const float thr = nL >= 6 ? kM3Theta6 : kM3Theta5;
        if (mP < thr && mX < kM3ThetaRI + dRI) {
          chains.score[c] -= kGateKill;
          flagsv[c] |= 0x1u;
        }
      } else {
        if (mD < kM3ThetaD && mX < kM3ThetaR + dR) {
          chains.score[c] -= kGateKill;
          flagsv[c] |= 0x1u;
        }
        flagsv[c] |= 0x2u;
      }
      if (kC25Theta > -1e9f && nL == 5 && (chains.offsets[c + 1] - chains.offsets[c]) == 2) {
        if (chains.score[c] > -0.5f * kGateKill) {
          if (mP < kC25Theta + dCP && mD < kC25ThetaD + dCD) {
            chains.score[c] -= kGateKill;
            flagsv[c] |= 0x1u;
            flagsv[c] |= 0x8u;
          }
        }
      }
      if (flagsv[c] & 0x1u)
        ++totKilled;
    }

    // ---- emit ---------------------------------------------------------------------------------
    put<uint32_t>(out, 0x50323243u);
    put<uint32_t>(out, static_cast<uint32_t>(i));
    put<uint32_t>(out, static_cast<uint32_t>(ev.t3_lsIdx0.size()));
    put<uint32_t>(out, static_cast<uint32_t>(g.edges.size()));
    put<uint32_t>(out, static_cast<uint32_t>(nC));
    for (std::size_t c = 0; c < nC; ++c) {
      const int pib = pre.offsets[c], pie = pre.offsets[c + 1];
      const int peb = pre.edgeOffsets[c], pee = pre.edgeOffsets[c + 1];
      const int mb = chains.mdOffsets[c], me = chains.mdOffsets[c + 1];
      put<uint32_t>(out, static_cast<uint32_t>(pie - pib));
      put<uint32_t>(out, static_cast<uint32_t>(chains.offsets[c + 1] - chains.offsets[c]));
      put<uint32_t>(out, static_cast<uint32_t>(me - mb));
      put<uint32_t>(out, static_cast<uint32_t>(chains.nLayers[c]));
      put<int32_t>(out, brv[c]);
      put<uint32_t>(out, static_cast<uint32_t>(action.empty() ? 0 : action[c]));
      put<uint32_t>(out, flagsv[c]);
      put<float>(out, chains.score[c]);
      put<float>(out, dca[c]);
      put<float>(out, z3[3 * c + 0]);
      put<float>(out, z3[3 * c + 1]);
      put<float>(out, z3[3 * c + 2]);
      put<float>(out, mPv[c]);
      put<float>(out, mDv[c]);
      put<float>(out, mXv[c]);
      for (int k = 0; k < kChainFeat; ++k)
        put<float>(out, cf.f[c * kChainFeat + k]);
      for (int k = pib; k < pie; ++k)
        put<uint32_t>(out, static_cast<uint32_t>(pre.items[k]));
      for (int k = peb; k < pee; ++k)
        put<uint32_t>(out, static_cast<uint32_t>(g.edges[pre.edgeItems[k]].type));
      for (int k = mb; k < me; ++k) {
        const int md = chains.mdItems[k];
        put<uint32_t>(out, static_cast<uint32_t>(ev.md_anchorHitIdx[md]));
        put<uint32_t>(out, static_cast<uint32_t>(ev.md_otherHitIdx[md]));
      }
    }

    totChains += static_cast<long long>(nC);
    std::printf("evt %lld: nT3=%zu edges=%zu chains=%zu trim(in/out)=%lld/%lld\n",
                i,
                ev.t3_lsIdx0.size(),
                g.edges.size(),
                nC,
                st.nTrimInner,
                st.nTrimOuter);
  }

  std::fclose(out);
  std::printf("p22_ref_dump: wrote %s | chains=%lld killed=%lld trimIn=%lld trimOut=%lld weldLogitTies=%lld\n",
              outPath.c_str(),
              totChains,
              totKilled,
              totTrimIn,
              totTrimOut,
              totWeldTies);
  return 0;
}
