#include "Stages.h"

#include <algorithm>
#include <cstdio>

// K6: mutual-best welding (Stages.h K6 contract, plan section 3 K6 / 5a).
//
// Per sweep (over a frozen slot snapshot):
//   bestOut(n) = highest-score eligible out-edge of n whose head's in-slot is free
//                (n's own out-slot must also be free);
//   bestIn(m)  = highest-score eligible in-edge of m whose tail's out-slot is free
//                (m's own in-slot must also be free);
//   weld e=(n,m) iff e == bestOut(n) AND e == bestIn(m).
// kWeldSweeps sweeps: welds taken in earlier sweeps remove slots, so losers re-run
// over the still-free slots and the second-best fallback emerges naturally.
// Eligibility: logOdds >= thetaEdge. Tie-break: higher logOdds, then lower edge index.
//
// Parallel edges (a node pair connected by both an E1 and an E2 edge) need no special
// handling: welding operates on edge indices, so mutual-best simply picks one of them
// by (score, index). Cycles cannot form because edges point strictly inward->outward,
// but the path walk carries a visited guard anyway (defensive, contract requirement).

namespace {

  // P2.5 (production ChainGraph.h chainMix32/chainNodeStableId, copied verbatim).
  inline uint32_t chainMix32(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
  }

  // Deterministic "edge a beats current best b": higher logOdds first, then the P2.5 STABLE
  // TIE WORD (larger wins, matching production's packed key whose low word is the tie and
  // where a larger key wins). The pre-P2.5 rule was "lower edge index", and an edge index is
  // a position in the K2 enumeration -- a numbering that descends from LST's atomicAdd
  // triplet slots and permutes run to run and backend to backend. Exact logit ties are
  // common (duplicate feature rows give bit-identical MLP outputs), so the welded chain set
  // was run-dependent. `tie` is nodeStableId[inner] ^ nodeStableId[outer], a function of hit
  // rows alone. The edge index survives as a third key so the order stays strict and total.
  inline bool beats(int a, int b, const std::vector<float>& logOdds, const std::vector<uint32_t>& tie) {
    if (b < 0)
      return true;
    if (logOdds[a] != logOdds[b])
      return logOdds[a] > logOdds[b];
    if (tie[a] != tie[b])
      return tie[a] > tie[b];
    return a < b;
  }

}  // namespace

uint32_t chainNodeStableId(const LSTEventData& ev, int t3) {
  const int md[3] = {ev.t3_md0[t3], ev.t3_md1[t3], ev.t3_md2[t3]};
  uint32_t s = 0x9e3779b9u;
  for (int k = 0; k < 3; ++k) {
    const int m = md[k];
    const uint32_t ha = (m >= 0 && m < static_cast<int>(ev.md_anchorHitIdx.size()))
                            ? static_cast<uint32_t>(ev.md_anchorHitIdx[m])
                            : 0xffffffffu;
    const uint32_t hb = (m >= 0 && m < static_cast<int>(ev.md_otherHitIdx.size()))
                            ? static_cast<uint32_t>(ev.md_otherHitIdx[m])
                            : 0xffffffffu;
    s = chainMix32(s ^ ha);
    s = chainMix32(s ^ hb);
  }
  return s;
}

void buildNodeStableIds(const LSTEventData& ev, std::vector<uint32_t>& out) {
  const int nT3 = static_cast<int>(ev.t3_lsIdx0.size());
  out.resize(nT3);
  for (int t = 0; t < nT3; ++t)
    out[t] = chainNodeStableId(ev, t);
}

void k6WeldChains(
    const LSTEventData& ev, const ChainGraph& g, const EdgeScores& s, float thetaEdge, float lambdaLen, Chains& out) {
  const int nT3 = static_cast<int>(ev.t3_lsIdx0.size());
  const int nEdges = static_cast<int>(g.edges.size());

  // Weld slots: at most one in-weld and one out-weld per node. Value = edge index or -1.
  std::vector<int> outWeld(nT3, -1), inWeld(nT3, -1);
  std::vector<int> bestOut(nT3), bestIn(nT3);

  // P2.5 determinism: the per-node stable identity and the per-edge stable tie word
  // (production ChainEdges.h: edges.tie() = stableId(inner) ^ stableId(outer)).
  std::vector<uint32_t> nodeSid;
  buildNodeStableIds(ev, nodeSid);
  std::vector<uint32_t> edgeTie(nEdges, 0);
  for (int e = 0; e < nEdges; ++e) {
    const int in = g.edges[e].inner, ou = g.edges[e].outer;
    const uint32_t si = (in >= 0 && in < nT3) ? nodeSid[in] : 0u;
    const uint32_t so = (ou >= 0 && ou < nT3) ? nodeSid[ou] : 0u;
    edgeTie[e] = si ^ so;
  }

  for (int sweep = 0; sweep < kWeldSweeps; ++sweep) {
    // Phase 1: per-node best over a frozen snapshot of the slots. A single pass over the
    // edge array in index order plus beats() yields the deterministic winner (this is the
    // grid-stride-over-edges + atomic-argmax shape the GPU port will use).
    std::fill(bestOut.begin(), bestOut.end(), -1);
    std::fill(bestIn.begin(), bestIn.end(), -1);
    for (int e = 0; e < nEdges; ++e) {
      if (s.logOdds[e] < thetaEdge)
        continue;  // eligibility gate
      const int n = g.edges[e].inner;
      const int m = g.edges[e].outer;
      if (outWeld[n] != -1 || inWeld[m] != -1)
        continue;  // tail's out-slot or head's in-slot already taken
      if (beats(e, bestOut[n], s.logOdds, edgeTie))
        bestOut[n] = e;
      if (beats(e, bestIn[m], s.logOdds, edgeTie))
        bestIn[m] = e;
    }
    // Phase 2: weld mutual pairs. bestOut is unique per tail and bestIn unique per head,
    // so the mutual set is conflict-free and can be applied wholesale.
    int welded = 0;
    for (int n = 0; n < nT3; ++n) {
      const int e = bestOut[n];
      if (e < 0)
        continue;
      const int m = g.edges[e].outer;
      if (bestIn[m] != e)
        continue;
      outWeld[n] = e;
      inWeld[m] = e;
      ++welded;
    }
    if (welded == 0)
      break;  // snapshot unchanged next sweep -> nothing new can pair
  }

  // Chain extraction: in/out degree <= 1 => disjoint simple paths. Heads are nodes with
  // no in-weld; walk out-welds innermost -> outermost. Keep paths with >= 2 nodes.
  out.offsets.assign(1, 0);
  out.items.clear();
  out.score.clear();
  out.nLayers.clear();
  out.mdOffsets.assign(1, 0);
  out.mdItems.clear();
  out.edgeOffsets.assign(1, 0);
  out.edgeItems.clear();
  out.stableKey.clear();

  std::vector<char> visited(nT3, 0);
  for (int start = 0; start < nT3; ++start) {
    if (inWeld[start] != -1)
      continue;  // interior or tail of some path, not a head
    if (outWeld[start] == -1)
      continue;  // isolated node: a single T3 is not a chain
    const size_t itemsBase = out.items.size();
    const size_t edgesBase = out.edgeItems.size();
    float edgeSum = 0.f;
    int n = start;
    while (true) {
      visited[n] = 1;
      out.items.push_back(n);
      const int e = outWeld[n];
      if (e == -1)
        break;
      const int next = g.edges[e].outer;
      if (visited[next]) {  // defensive: cannot happen for inward->outward edges
        std::fprintf(stderr, "k6WeldChains: weld cycle guard tripped at node %d\n", next);
        break;
      }
      edgeSum += s.logOdds[e];
      out.edgeItems.push_back(e);
      n = next;
    }
    const int nNodes = static_cast<int>(out.items.size() - itemsBase);
    if (nNodes < 2) {  // defensive: only reachable through the cycle guard
      out.items.resize(itemsBase);
      out.edgeItems.resize(edgesBase);
      continue;
    }
    // Deduped MD union of the members' {md0, md1, md2}, ordered by first appearance
    // walking the path innermost-first. Chains are short, so the linear dedup scan over
    // the chain's own MD slice is cheap and deterministic.
    const size_t mdBase = out.mdItems.size();
    uint32_t layerMask = 0;  // md_layer is 1-6 barrel, 7-11 endcap: fits a bitmask
    for (size_t k = itemsBase; k < out.items.size(); ++k) {
      const int t3 = out.items[k];
      const int mds[3] = {ev.t3_md0[t3], ev.t3_md1[t3], ev.t3_md2[t3]};
      for (int md : mds) {
        bool seen = false;
        for (size_t q = mdBase; q < out.mdItems.size() && !seen; ++q)
          seen = (out.mdItems[q] == md);
        if (!seen) {
          out.mdItems.push_back(md);
          layerMask |= (1u << ev.md_layer[md]);
        }
      }
    }
    int nLayers = 0;
    for (uint32_t b = layerMask; b != 0u; b &= b - 1)
      ++nLayers;
    out.nLayers.push_back(nLayers);
    out.score.push_back(edgeSum + lambdaLen * static_cast<float>(nLayers));
    // P2.5: the head node names the chain (production ChainWeld.h:307). `start` is the
    // head of the walk that just produced this chain, i.e. the PRE-trim head node.
    out.stableKey.push_back(nodeSid[start]);
    out.offsets.push_back(static_cast<int>(out.items.size()));
    out.mdOffsets.push_back(static_cast<int>(out.mdItems.size()));
    out.edgeOffsets.push_back(static_cast<int>(out.edgeItems.size()));
  }

  // Defensive: a welded node that was never visited would mean a pure weld cycle (every
  // member has an in-weld, so no head exists). Nodes with only an out-weld are heads and
  // always visited, so checking the in-weld slot covers all unreachable welded nodes.
  int orphaned = 0;
  for (int n = 0; n < nT3; ++n)
    if (!visited[n] && inWeld[n] != -1)
      ++orphaned;
  if (orphaned > 0)
    std::fprintf(stderr, "k6WeldChains: %d welded nodes unreachable from any head (cycle?)\n", orphaned);
}
