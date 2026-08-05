#ifndef PROTOTYPE_DUMPWRITER_H
#define PROTOTYPE_DUMPWRITER_H

// Training dump (plan 10.3: the prototype is the training-data factory; feature builders
// are SHARED between dump and future in-prototype inference — one code path).
//
// Output: flat TTree "edges", ONE ENTRY PER EDGE (not per event), for direct
// uproot/numpy loading in python:
//   evt (ULong64), lumi (UInt), etype (Int)
//   label (Int 0/1), simIdx (Int), simPt, simEta, simVxy (Float)
//   node features inner: ni_00..ni_12 (Float, kNodeFeat, order as Features.h)
//   node features outer: no_00..no_12
//   edge features:       ef_00..ef_13 (kEdgeFeat, order as Features.h)
// Zero-padded two-digit suffixes; a TNamed "feature_spec" records the ordered feature
// names so python never hardcodes the mapping.

#include <memory>
#include <string>

#include "ChainFeatures.h"
#include "EventData.h"
#include "Features.h"
#include "Labels.h"
#include "Stages.h"

class DumpWriterImpl;

class DumpWriter {
public:
  explicit DumpWriter(const std::string& outPath);
  ~DumpWriter();
  void fillEvent(const LSTEventData& ev,
                 const ChainGraph& g,
                 const NodeFeatures& nf,
                 const EdgeFeatures& ef,
                 const EdgeLabels& labels);
  void writeAndClose();

private:
  std::unique_ptr<DumpWriterImpl> impl_;
};

// Chain-gate training dump (M6, plan 5a): flat TTree "chains", ONE ENTRY PER WELDED
// CHAIN (pre-arbitration, post-K6), for the chain-gate classifier loop:
//   cf_00..cf_15 (Float, kChainFeat, order = the frozen ChainFeatures.h contract)
//   label (Int 0/1, labelChains: 1 iff ALL member T3s share a sim -- braid variants of
//   a real track all label 1), simIdx (Int, labelChains convention: FULL tracking-ntuple
//   sim row, < nAccepted indexes the LST ntuple sim block directly; -1 if label 0),
//   simVxy/simPt (Float, accepted sims only, -999 otherwise), nLayers (Int), evt (ULong64)
// A TNamed "feature_spec" records the ordered cf feature names.
class ChainDumpWriterImpl;

class ChainDumpWriter {
public:
  explicit ChainDumpWriter(const std::string& outPath);
  ~ChainDumpWriter();
  void fillEvent(const LSTEventData& ev, const Chains& chains, const ChainFeatures& cf, const ChainLabels& labels);
  void writeAndClose();

private:
  std::unique_ptr<ChainDumpWriterImpl> impl_;
};

// K8 attach-head training dump (M7, plan 5a additive evidence; M16 GENERALIZED): flat
// TTree "pairs", ONE ENTRY PER PREFILTERED (target, pLS) PAIR over BOTH target kinds:
//   af_00..af_18 (Float, kAttachFeat = 19, order = the frozen PixelAttach.h contract;
//   exact definitions documented in PixelAttach.cc; af_18 = targetType, redundant with
//   the ttype branch on purpose -- the head consumes the feature, analysis code slices
//   on the branch)
//   ttype (Int, M16): 0 = ACCEPTED CHAIN target (nLayers >= 5), 1 = BARE T3 target (a T3
//   in no K9-accepted chain). The two delivery classes of the general pLS->OT attach.
//   label (Int 0/1: 1 iff the pLS shares a sim with the target -- pLS_simIdxAll, full
//   tracking-ntuple sim rows per the EventData.h M7 rule, intersects the target's sim
//   set; chain target -> the all-member T3SimSets intersection, T3 target -> that T3's
//   own >=2/3-MD T3SimSets entry)
//   chainNLayers (Int; 3 for every T3 target by construction)
//   wgt (Float, M16): inverse sampling weight of the entry. 1 for every kept pair; when
//   the caller downsamples FAKE pairs of a target kind by a stride N (volume control --
//   the bare-T3 universe is ~35x the chain universe), the surviving fakes carry wgt = N
//   so a weighted training loss reproduces the undownsampled fake population. TRUE pairs
//   are NEVER downsampled (wgt always 1). Reported statistics are always computed on the
//   FULL enumeration, never on the written subset.
//   simVxy/simPt (Float, shared-sim kinematics, accepted sims only, -999 for fake pairs)
//   evt (ULong64)
// A TNamed "feature_spec" records the ordered af feature names.
class PairDumpWriterImpl;

class PairDumpWriter {
public:
  explicit PairDumpWriter(const std::string& outPath);
  ~PairDumpWriter();
  void fillPair(unsigned long long evt,
                const float* f,
                int label,
                int chainNLayers,
                float simVxy,
                float simPt,
                int ttype,
                float wgt);
  void writeAndClose();

private:
  std::unique_ptr<PairDumpWriterImpl> impl_;
};

#endif
