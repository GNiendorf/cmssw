#ifndef RecoTracker_LSTCore_src_alpaka_ChainTrimLearn_h
#define RecoTracker_LSTCore_src_alpaka_ChainTrimLearn_h

#include <cstdint>

#include "HeterogeneousCore/AlpakaInterface/interface/workdivision.h"

#include "RecoTracker/LSTCore/interface/alpaka/Common.h"
#include "RecoTracker/LSTCore/interface/ChainConfig.h"
#include "RecoTracker/LSTCore/interface/ChainsSoA.h"

#include "ChainGate.h"
#include "ChainWeld.h"

// TRIM-NN. The terminal-trim DECISION, expressed as a question the chain head can answer.
//
// K6f (ChainWeld.h ChainTrimTerminals) decides which of a chain's three terminal variants
// survives by a hand-set threshold on ONE proxy -- the ratio of combined helix chi2 before and
// after a drop. The variants themselves are mechanical: the outer-dropped MD union is the PREFIX
// of the full first-appearance union over the first nNodes - 1 members, and the inner-dropped
// union is built by the same walk starting one node in. What is learned here is only the choice
// between them, and it is scored on the objective the pipeline actually cares about (is this a
// real track) by the SAME K7a feature builder and the SAME frozen K7b head the chain row gets.
//
// Everything in this header is env-gated observation until a variant kernel is wired into the
// deployed sequence; with no environment variable set nothing here runs.

namespace ALPAKA_ACCELERATOR_NAMESPACE::lst {

  namespace chaintrim {
    // Per-variant probe block: nMD, nLayers, dcaXY, 3 logits, then the 25-float feature row.
    constexpr int kVarWords = 6 + Params_ChainFeat::kFeatures;
    // Per-chain probe row: chi2 of the three variants, then the three blocks.
    constexpr int kProbeWords = 3 + 3 * kVarWords;
    constexpr int kVariants = 3;  // 0 = full, 1 = inner-dropped, 2 = outer-dropped
  }                               // namespace chaintrim

  // Build the MD union of a terminal variant exactly as K6e/K6f do: deduped first-appearance walk
  // over the member triplets' {m0, m1, m2}, innermost member first. Returns the layer count and
  // fills `dst` (which for the OUTER variant may alias the chain's own prefix, since the walk
  // writes position q only after reading positions < q).
  ALPAKA_FN_ACC ALPAKA_FN_INLINE int chainVariantUnion(ModulesConst modules,
                                                       MiniDoubletsConst mds,
                                                       SegmentsConst segments,
                                                       TripletsConst triplets,
                                                       ChainNodesConst nodes,
                                                       ChainItemsConst items,
                                                       uint32_t off,
                                                       int nNodes,
                                                       uint32_t* dst,
                                                       int& nMDOut) {
    int n = 0;
    uint32_t mask = 0u;
    for (int k = 0; k < nNodes; ++k) {
      uint32_t const t3 = nodes.tripletIndex()[items.nodeItems()[off + k]];
      unsigned int m0, m1, m2;
      chainNodeMDs(triplets, segments, t3, m0, m1, m2);
      unsigned int const mdTriple[3] = {m0, m1, m2};
      for (int t = 0; t < 3; ++t) {
        uint32_t const md = static_cast<uint32_t>(mdTriple[t]);
        bool seen = false;
        for (int q = 0; q < n && !seen; ++q)
          seen = (dst[q] == md);
        if (!seen) {
          dst[n] = md;
          ++n;
          mask |= (1u << chainMdLayer(modules, mds, md));
        }
      }
    }
    nMDOut = n;
    int nLay = 0;
    for (uint32_t b = mask; b != 0u; b &= b - 1u)
      ++nLay;
    return nLay;
  }

  // ------------------------------------------------------------------------------------------
  // TRIM-NN PROBE (pure observation; runs only when the host allocates its buffers). For every
  // chain with nNodes >= 3 it builds the three variants, records the combined-fit chi2 the K6f
  // rule would have compared, and scores each variant with the deployed feature builder and head.
  struct ChainVariantProbe {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  ModulesConst modules,
                                  MiniDoubletsConst mds,
                                  SegmentsConst segments,
                                  TripletsConst triplets,
                                  ChainNodesConst nodes,
                                  ChainEdgesConst edges,
                                  ChainIncidenceConst mdIncidence,
                                  ChainIncidenceConst lsIncidence,
                                  ChainItemsConst items,
                                  ChainsConst chains,
                                  uint32_t* innerMD,
                                  float* out) const {
      uint32_t const nChains = static_cast<uint32_t>(chains.metadata().size());

      for (uint32_t c : cms::alpakatools::uniform_elements(acc, nChains)) {
        float* o = out + static_cast<size_t>(c) * chaintrim::kProbeWords;
        for (int i = 0; i < chaintrim::kProbeWords; ++i)
          o[i] = 0.f;

        int const nN = chains.nNodes()[c];
        if (nN < 3)
          continue;
        uint32_t const off = chains.nodeOffset()[c];
        int const nMD = chains.nMDs()[c];
        uint32_t const mdBase = 3u * off;
        uint32_t const* full = &items.mdItems()[mdBase];

        // Outer-dropped union: the prefix of the full union over the first nN - 1 members. Read
        // out of the chain's own list, so only its LENGTH and layer mask need computing.
        int nMDOut = 0;
        uint32_t maskOut = 0u;
        for (int k = 0; k + 1 < nN; ++k) {
          uint32_t const t3 = nodes.tripletIndex()[items.nodeItems()[off + k]];
          unsigned int m0, m1, m2;
          chainNodeMDs(triplets, segments, t3, m0, m1, m2);
          unsigned int const mdTriple[3] = {m0, m1, m2};
          for (int t = 0; t < 3; ++t) {
            uint32_t const md = static_cast<uint32_t>(mdTriple[t]);
            bool seen = false;
            for (int q = 0; q < nMDOut && !seen; ++q)
              seen = (full[q] == md);
            if (!seen) {
              ++nMDOut;
              maskOut |= (1u << chainMdLayer(modules, mds, md));
            }
          }
        }
        int nLayO = 0;
        for (uint32_t b = maskOut; b != 0u; b &= b - 1u)
          ++nLayO;

        int nMDIn = 0;
        int const nLayI = chainVariantUnion(
            modules, mds, segments, triplets, nodes, items, off + 1u, nN - 1, &innerMD[mdBase], nMDIn);

        uint32_t const offv[chaintrim::kVariants] = {off, off + 1u, off};
        int const nNv[chaintrim::kVariants] = {nN, nN - 1, nN - 1};
        uint32_t const* mdv[chaintrim::kVariants] = {full, &innerMD[mdBase], full};
        int const nMDv[chaintrim::kVariants] = {nMD, nMDIn, nMDOut};
        int const nLayv[chaintrim::kVariants] = {chains.nLayers()[c], nLayI, nLayO};

        for (int v = 0; v < chaintrim::kVariants; ++v) {
          o[v] = static_cast<float>(chainFitChi2Combined(acc, mds, mdv[v], nMDv[v]));
          float f[Params_ChainFeat::kFeatures];
          float dca = 1e9f;
          chainBuildFeatures(acc,
                             modules,
                             mds,
                             segments,
                             triplets,
                             nodes,
                             edges,
                             mdIncidence,
                             lsIncidence,
                             items,
                             offv[v],
                             nNv[v],
                             mdv[v],
                             nMDv[v],
                             nLayv[v],
                             f,
                             dca);
          float z[dnn::chain3mlp::kOutput];
          chainGateLogits(acc, f, dca, z);
          float* b = o + 3 + v * chaintrim::kVarWords;
          b[0] = static_cast<float>(nMDv[v]);
          b[1] = static_cast<float>(nLayv[v]);
          b[2] = dca;
          b[3] = z[0];
          b[4] = z[1];
          b[5] = z[2];
          for (int i = 0; i < Params_ChainFeat::kFeatures; ++i)
            b[6 + i] = f[i];
        }
      }
    }
  };

  // ------------------------------------------------------------------------------------------
  // K6f'. The terminal trim with the DECISION taken by the chain head instead of by a hand-set
  // threshold on the helix chi2 ratio.
  //
  // The variants are the same three objects K6f builds and the edit is the same O(1) endpoint
  // move (inner drop = nodeOffset + 1, outer drop = nNodes - 1, both inside the chain's original
  // 3 * nNodes MD allocation). What changes is only the comparison: each variant's 25-float row is
  // built by the deployed K7a builder, scored by the deployed frozen K7b head, and the variant with
  // the largest realness margin mX = max(zPrompt, zDisp) - zFake wins. Ties keep the FULL chain,
  // and inner beats outer on an exact tie, which reproduces K6f's determinism convention.
  //
  // No layer floor is needed as a constant: a chain with nNodes >= 3 whose terminal is dropped
  // still spans >= 4 layers by construction of the weld (measured: the minimum over 928k variants
  // of both samples is exactly 4, the emission floor), so cfg.trimMinLayersAfter only exists to
  // FORBID the 5 -> 4 layer move, which is a policy choice and is exposed as trimMode 2.
  struct ChainTrimLearned {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  ModulesConst modules,
                                  MiniDoubletsConst mds,
                                  SegmentsConst segments,
                                  TripletsConst triplets,
                                  ChainNodesConst nodes,
                                  ChainEdgesConst edges,
                                  ChainIncidenceConst mdIncidence,
                                  ChainIncidenceConst lsIncidence,
                                  ChainItemsConst citems,
                                  Chains chains,
                                  ChainItems items,
                                  ChainConfig cfg) const {
      uint32_t const nChains = static_cast<uint32_t>(chains.metadata().size());

      for (uint32_t c : cms::alpakatools::uniform_elements(acc, nChains)) {
        int const nN = chains.nNodes()[c];
        if (nN < 3)
          continue;
        uint32_t const off = chains.nodeOffset()[c];
        int const nMD = chains.nMDs()[c];
        uint32_t const mdBase = 3u * off;
        uint32_t const* full = &items.mdItems()[mdBase];

        if (cfg.trimMode == 3) {
          // The K6f concentrating guard, kept: a chain that already fits well is untouched.
          double const chi2Full = chainFitChi2Combined(acc, mds, full, nMD);
          if (chi2Full <= static_cast<double>(cfg.trimAbsChi2))
            continue;
        }

        // Outer-dropped union: the PREFIX of the full first-appearance union over the first
        // nN - 1 members, so only its length and layer mask have to be recomputed.
        int nMDOut = 0;
        uint32_t maskOut = 0u;
        for (int k = 0; k + 1 < nN; ++k) {
          uint32_t const t3 = nodes.tripletIndex()[citems.nodeItems()[off + k]];
          unsigned int m0, m1, m2;
          chainNodeMDs(triplets, segments, t3, m0, m1, m2);
          unsigned int const mdTriple[3] = {m0, m1, m2};
          for (int t = 0; t < 3; ++t) {
            uint32_t const md = static_cast<uint32_t>(mdTriple[t]);
            bool seen = false;
            for (int q = 0; q < nMDOut && !seen; ++q)
              seen = (full[q] == md);
            if (!seen) {
              ALPAKA_ASSERT_ACC(full[nMDOut] == md);
              ++nMDOut;
              maskOut |= (1u << chainMdLayer(modules, mds, md));
            }
          }
        }
        int nLayO = 0;
        for (uint32_t b = maskOut; b != 0u; b &= b - 1u)
          ++nLayO;

        // Inner-dropped union, built into the per-chain scratch region so the read of the full
        // union stays hazard-free; copied into place only if it wins.
        int nMDIn = 0;
        int const nLayI = chainVariantUnion(
            modules, mds, segments, triplets, nodes, citems, off + 1u, nN - 1, &items.mdScratch()[mdBase], nMDIn);

        uint32_t const offv[chaintrim::kVariants] = {off, off + 1u, off};
        int const nNv[chaintrim::kVariants] = {nN, nN - 1, nN - 1};
        uint32_t const* mdv[chaintrim::kVariants] = {full, &items.mdScratch()[mdBase], full};
        int const nMDv[chaintrim::kVariants] = {nMD, nMDIn, nMDOut};
        int const nLayv[chaintrim::kVariants] = {chains.nLayers()[c], nLayI, nLayO};

        int drop = 0;
        // trimMode 4 is the REFEREE CONTROL, not a candidate: the same unguarded 3-way argmax
        // the head takes, decided by the PROXY instead. It isolates "the head chooses better"
        // from "trimming more is better".
        if (cfg.trimMode == 4) {
          double bestChi2 = 0.0;
          for (int v = 0; v < chaintrim::kVariants; ++v) {
            double const chi2 = chainFitChi2Combined(acc, mds, mdv[v], nMDv[v]);
            if (v == 0 || chi2 < bestChi2) {
              bestChi2 = chi2;
              drop = v;
            }
          }
          if (drop == 0)
            continue;
          uint32_t const newOff4 = (drop == 1) ? off + 1u : off;
          float edgeSum4 = 0.f;
          for (int k = 0; k < nN - 2; ++k)
            edgeSum4 += edges.logOdds()[citems.edgeItems()[newOff4 + k]];
          if (drop == 1) {
            for (int q = 0; q < nMDIn; ++q)
              items.mdItems()[3u * newOff4 + q] = items.mdScratch()[mdBase + q];
            chains.nMDs()[c] = static_cast<uint16_t>(nMDIn);
          } else {
            chains.nMDs()[c] = static_cast<uint16_t>(nMDOut);
          }
          chains.nodeOffset()[c] = newOff4;
          chains.nNodes()[c] = static_cast<uint16_t>(nN - 1);
          chains.nLayers()[c] = static_cast<uint8_t>(nLayv[drop]);
          chains.score()[c] = edgeSum4 + cfg.lambdaLen * static_cast<float>(nLayv[drop]);
          chains.trimAction()[c] = static_cast<int8_t>(drop);
          continue;
        }
        float bestMargin = 0.f;
        float fullMargin = 0.f;
        // COORDINATOR OPTIMISATION: the winning variant's feature row and dcaXY are kept instead of
        // being recomputed by K7a. K7a builds the SAME row from the SAME geometry (the trim has
        // already written nodeOffset / nNodes / nMDs / nLayers by then), so caching is an identity,
        // not an approximation -- and it removes one of the four ChainFeatures builds an eligible
        // chain otherwise pays.
        float bestF[Params_ChainFeat::kFeatures];
        float fullF[Params_ChainFeat::kFeatures];
        float bestDca = 1e9f;
        float fullDca = 1e9f;
        for (int v = 0; v < chaintrim::kVariants; ++v) {
          if (v > 0 && cfg.trimMode == 2 && nLayv[v] < cfg.trimMinLayersAfter)
            continue;
          float f[Params_ChainFeat::kFeatures];
          float dca = 1e9f;
          chainBuildFeatures(acc,
                             modules,
                             mds,
                             segments,
                             triplets,
                             nodes,
                             edges,
                             mdIncidence,
                             lsIncidence,
                             citems,
                             offv[v],
                             nNv[v],
                             mdv[v],
                             nMDv[v],
                             nLayv[v],
                             f,
                             dca);
          float z[dnn::chain3mlp::kOutput];
          chainGateLogits(acc, f, dca, z);
          float const margin = chainMaxf(z[1], z[2]) - z[0];
          if (v == 0)
            fullMargin = margin;
          if (v == 0 || margin > bestMargin) {
            bestMargin = margin;
            drop = v;
            for (int i = 0; i < Params_ChainFeat::kFeatures; ++i)
              bestF[i] = f[i];
            bestDca = dca;
          }
          if (v == 0) {
            for (int i = 0; i < Params_ChainFeat::kFeatures; ++i)
              fullF[i] = f[i];
            fullDca = dca;
          }
        }
        // trimMode 5: the head must be DECISIVE, not merely ahead. One constant that replaces the
        // four the chi2 rule needs.
        if (cfg.trimMode == 5 && bestMargin - fullMargin < cfg.trimMarginGap) {
          drop = 0;
          for (int i = 0; i < Params_ChainFeat::kFeatures; ++i)
            bestF[i] = fullF[i];
          bestDca = fullDca;
        }
        // Publish the winner's row either way: whether or not the geometry is edited, the row this
        // chain will be gated on has already been built and K7a can skip it.
        for (int i = 0; i < Params_ChainFeat::kFeatures; ++i)
          chains.features()[c][i] = bestF[i];
        chains.dcaXY()[c] = bestDca;
        chains.featValid()[c] = 1u;
        if (drop == 0)
          continue;

        uint32_t const newOff = (drop == 1) ? off + 1u : off;
        int const newNodes = nN - 1;
        float edgeSum = 0.f;
        for (int k = 0; k < newNodes - 1; ++k)
          edgeSum += edges.logOdds()[citems.edgeItems()[newOff + k]];

        if (drop == 1) {
          uint32_t const newMdBase = 3u * newOff;
          for (int q = 0; q < nMDIn; ++q)
            items.mdItems()[newMdBase + q] = items.mdScratch()[mdBase + q];
          chains.nMDs()[c] = static_cast<uint16_t>(nMDIn);
        } else {
          chains.nMDs()[c] = static_cast<uint16_t>(nMDOut);
        }
        chains.nodeOffset()[c] = newOff;
        chains.nNodes()[c] = static_cast<uint16_t>(newNodes);
        chains.nLayers()[c] = static_cast<uint8_t>(nLayv[drop]);
        chains.score()[c] = edgeSum + cfg.lambdaLen * static_cast<float>(nLayv[drop]);
        chains.trimAction()[c] = static_cast<int8_t>(drop);
      }
    }
  };

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE::lst

#endif
