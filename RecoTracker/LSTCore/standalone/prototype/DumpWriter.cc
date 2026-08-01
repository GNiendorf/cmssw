#include "DumpWriter.h"

#include <cstdio>
#include <stdexcept>

#include "PixelAttachPairs.h"  // kAttachFeat + kAttachFeatNames (PairDumpWriter)

#include "TFile.h"
#include "TNamed.h"
#include "TTree.h"

namespace {

// Ordered feature names; MUST stay in sync with the index comments in Features.h.
const char* const kNodeFeatNames[kNodeFeat] = {"kappaSigned",
                                               "log10R",
                                               "tanLambda",
                                               "chordEta",
                                               "dphi01",
                                               "dz01",
                                               "dz12",
                                               "drt01",
                                               "drt12",
                                               "innermostLayer",
                                               "nBarrel",
                                               "nPS",
                                               "fakeScoreT3"};

const char* const kEdgeFeatNames[kEdgeFeat] = {"etype",
                                               "dKappa",
                                               "dKappaRel",
                                               "chargeAgree",
                                               "dTanLambda",
                                               "kinkPhi",
                                               "kinkTheta",
                                               "centerDist",
                                               "centerDistRel",
                                               "sharedLayer",
                                               "sharedIsPS",
                                               "sharedIsBarrel",
                                               "degIn",
                                               "degOut"};

std::string buildFeatureSpec() {
  std::string spec = "ni:";
  for (int i = 0; i < kNodeFeat; ++i) {
    if (i > 0)
      spec += ",";
    spec += kNodeFeatNames[i];
  }
  spec += ";ef:";
  for (int i = 0; i < kEdgeFeat; ++i) {
    if (i > 0)
      spec += ",";
    spec += kEdgeFeatNames[i];
  }
  return spec;
}

}  // namespace

class DumpWriterImpl {
public:
  explicit DumpWriterImpl(const std::string& outPath) {
    file_ = TFile::Open(outPath.c_str(), "RECREATE");
    if (!file_ || file_->IsZombie())
      throw std::runtime_error("DumpWriter: cannot create output file " + outPath);
    tree_ = new TTree("edges", "chain-tracking edge training dump");
    // ~33M entries expected; flush every ~30 MB to keep basket memory bounded.
    tree_->SetAutoFlush(-30000000);

    tree_->Branch("evt", &evt_, "evt/l");
    tree_->Branch("lumi", &lumi_, "lumi/i");
    tree_->Branch("etype", &etype_, "etype/I");
    tree_->Branch("label", &label_, "label/I");
    tree_->Branch("simIdx", &simIdx_, "simIdx/I");
    tree_->Branch("simPt", &simPt_, "simPt/F");
    tree_->Branch("simEta", &simEta_, "simEta/F");
    tree_->Branch("simVxy", &simVxy_, "simVxy/F");

    char name[16], leaf[16];
    for (int i = 0; i < kNodeFeat; ++i) {
      std::snprintf(name, sizeof(name), "ni_%02d", i);
      std::snprintf(leaf, sizeof(leaf), "ni_%02d/F", i);
      tree_->Branch(name, &ni_[i], leaf);
    }
    for (int i = 0; i < kNodeFeat; ++i) {
      std::snprintf(name, sizeof(name), "no_%02d", i);
      std::snprintf(leaf, sizeof(leaf), "no_%02d/F", i);
      tree_->Branch(name, &no_[i], leaf);
    }
    for (int i = 0; i < kEdgeFeat; ++i) {
      std::snprintf(name, sizeof(name), "ef_%02d", i);
      std::snprintf(leaf, sizeof(leaf), "ef_%02d/F", i);
      tree_->Branch(name, &ef_[i], leaf);
    }
  }

  ~DumpWriterImpl() {
    if (file_) {
      file_->Close();
      delete file_;
    }
  }

  void fillEvent(const LSTEventData& ev,
                 const ChainGraph& g,
                 const NodeFeatures& nf,
                 const EdgeFeatures& ef,
                 const EdgeLabels& labels) {
    const std::size_t nEdges = g.edges.size();
    const std::size_t nT3 = ev.t3_lsIdx0.size();
    if (nf.f.size() != nT3 * kNodeFeat)
      throw std::runtime_error("DumpWriter: NodeFeatures size mismatch");
    if (ef.f.size() != nEdges * kEdgeFeat)
      throw std::runtime_error("DumpWriter: EdgeFeatures size mismatch");
    if (labels.label.size() != nEdges || labels.simIdx.size() != nEdges || labels.simPt.size() != nEdges ||
        labels.simEta.size() != nEdges || labels.simVxy.size() != nEdges)
      throw std::runtime_error("DumpWriter: EdgeLabels size mismatch");

    evt_ = ev.evt;
    lumi_ = ev.lumi;
    for (std::size_t e = 0; e < nEdges; ++e) {
      const auto& edge = g.edges[e];
      etype_ = static_cast<Int_t>(edge.type);
      label_ = static_cast<Int_t>(labels.label[e]);
      simIdx_ = labels.simIdx[e];
      simPt_ = labels.simPt[e];
      simEta_ = labels.simEta[e];
      simVxy_ = labels.simVxy[e];
      const float* niSrc = &nf.f[static_cast<std::size_t>(edge.inner) * kNodeFeat];
      const float* noSrc = &nf.f[static_cast<std::size_t>(edge.outer) * kNodeFeat];
      for (int i = 0; i < kNodeFeat; ++i) {
        ni_[i] = niSrc[i];
        no_[i] = noSrc[i];
      }
      const float* efSrc = &ef.f[e * kEdgeFeat];
      for (int i = 0; i < kEdgeFeat; ++i)
        ef_[i] = efSrc[i];
      tree_->Fill();
    }
  }

  void writeAndClose() {
    if (!file_)
      return;
    file_->cd();
    TNamed spec("feature_spec", buildFeatureSpec().c_str());
    spec.Write();
    tree_->Write();
    file_->Close();
    delete file_;
    file_ = nullptr;
    tree_ = nullptr;  // owned by the file; gone after Close
  }

private:
  TFile* file_ = nullptr;
  TTree* tree_ = nullptr;

  ULong64_t evt_ = 0;
  UInt_t lumi_ = 0;
  Int_t etype_ = 0;
  Int_t label_ = 0;
  Int_t simIdx_ = -1;
  Float_t simPt_ = -999.f;
  Float_t simEta_ = -999.f;
  Float_t simVxy_ = -999.f;
  Float_t ni_[kNodeFeat] = {};
  Float_t no_[kNodeFeat] = {};
  Float_t ef_[kEdgeFeat] = {};
};

DumpWriter::DumpWriter(const std::string& outPath) : impl_(std::make_unique<DumpWriterImpl>(outPath)) {}

DumpWriter::~DumpWriter() = default;

void DumpWriter::fillEvent(const LSTEventData& ev,
                           const ChainGraph& g,
                           const NodeFeatures& nf,
                           const EdgeFeatures& ef,
                           const EdgeLabels& labels) {
  impl_->fillEvent(ev, g, nf, ef, labels);
}

void DumpWriter::writeAndClose() { impl_->writeAndClose(); }

// ---------------------------------------------------------------------------------------
// ChainDumpWriter (M6): one entry per welded chain, branch layout in DumpWriter.h.

class ChainDumpWriterImpl {
public:
  explicit ChainDumpWriterImpl(const std::string& outPath) {
    file_ = TFile::Open(outPath.c_str(), "RECREATE");
    if (!file_ || file_->IsZombie())
      throw std::runtime_error("ChainDumpWriter: cannot create output file " + outPath);
    tree_ = new TTree("chains", "chain-gate training dump (pre-arbitration welded chains)");
    tree_->SetAutoFlush(-30000000);

    tree_->Branch("evt", &evt_, "evt/l");
    tree_->Branch("label", &label_, "label/I");
    tree_->Branch("simVxy", &simVxy_, "simVxy/F");
    tree_->Branch("simPt", &simPt_, "simPt/F");
    tree_->Branch("nLayers", &nLayers_, "nLayers/I");
    char name[16], leaf[16];
    for (int i = 0; i < kChainFeat; ++i) {
      std::snprintf(name, sizeof(name), "cf_%02d", i);
      std::snprintf(leaf, sizeof(leaf), "cf_%02d/F", i);
      tree_->Branch(name, &cf_[i], leaf);
    }
  }

  ~ChainDumpWriterImpl() {
    if (file_) {
      file_->Close();
      delete file_;
    }
  }

  void fillEvent(const LSTEventData& ev, const Chains& chains, const ChainFeatures& cf, const ChainLabels& labels) {
    const std::size_t nChains = chains.offsets.empty() ? 0 : chains.offsets.size() - 1;
    if (cf.f.size() != nChains * kChainFeat)
      throw std::runtime_error("ChainDumpWriter: ChainFeatures size mismatch");
    if (labels.label.size() != nChains || labels.simPt.size() != nChains || labels.simVxy.size() != nChains)
      throw std::runtime_error("ChainDumpWriter: ChainLabels size mismatch");

    evt_ = ev.evt;
    for (std::size_t c = 0; c < nChains; ++c) {
      label_ = static_cast<Int_t>(labels.label[c]);
      simVxy_ = labels.simVxy[c];
      simPt_ = labels.simPt[c];
      nLayers_ = chains.nLayers[c];
      const float* src = &cf.f[c * kChainFeat];
      for (int i = 0; i < kChainFeat; ++i)
        cf_[i] = src[i];
      tree_->Fill();
    }
  }

  void writeAndClose() {
    if (!file_)
      return;
    file_->cd();
    std::string spec = "cf:";
    for (int i = 0; i < kChainFeat; ++i) {
      if (i > 0)
        spec += ",";
      spec += kChainFeatNames[i];
    }
    TNamed specNamed("feature_spec", spec.c_str());
    specNamed.Write();
    tree_->Write();
    file_->Close();
    delete file_;
    file_ = nullptr;
    tree_ = nullptr;  // owned by the file; gone after Close
  }

private:
  TFile* file_ = nullptr;
  TTree* tree_ = nullptr;

  ULong64_t evt_ = 0;
  Int_t label_ = 0;
  Float_t simVxy_ = -999.f;
  Float_t simPt_ = -999.f;
  Int_t nLayers_ = 0;
  Float_t cf_[kChainFeat] = {};
};

ChainDumpWriter::ChainDumpWriter(const std::string& outPath) : impl_(std::make_unique<ChainDumpWriterImpl>(outPath)) {}

ChainDumpWriter::~ChainDumpWriter() = default;

void ChainDumpWriter::fillEvent(const LSTEventData& ev,
                                const Chains& chains,
                                const ChainFeatures& cf,
                                const ChainLabels& labels) {
  impl_->fillEvent(ev, chains, cf, labels);
}

void ChainDumpWriter::writeAndClose() { impl_->writeAndClose(); }

// ---------------------------------------------------------------------------------------
// PairDumpWriter (M7): one entry per prefiltered (chain, pLS) pair, layout in
// DumpWriter.h; feature names come from PixelAttach.cc via PixelAttachPairs.h.

class PairDumpWriterImpl {
public:
  explicit PairDumpWriterImpl(const std::string& outPath) {
    file_ = TFile::Open(outPath.c_str(), "RECREATE");
    if (!file_ || file_->IsZombie())
      throw std::runtime_error("PairDumpWriter: cannot create output file " + outPath);
    tree_ = new TTree("pairs", "K8 attach pair training dump (prefiltered chain-pLS pairs)");
    tree_->SetAutoFlush(-30000000);

    tree_->Branch("evt", &evt_, "evt/l");
    tree_->Branch("label", &label_, "label/I");
    tree_->Branch("chainNLayers", &chainNLayers_, "chainNLayers/I");
    tree_->Branch("simVxy", &simVxy_, "simVxy/F");
    tree_->Branch("simPt", &simPt_, "simPt/F");
    char name[16], leaf[16];
    for (int i = 0; i < kAttachFeat; ++i) {
      std::snprintf(name, sizeof(name), "af_%02d", i);
      std::snprintf(leaf, sizeof(leaf), "af_%02d/F", i);
      tree_->Branch(name, &af_[i], leaf);
    }
  }

  ~PairDumpWriterImpl() {
    if (file_) {
      file_->Close();
      delete file_;
    }
  }

  void fillPair(unsigned long long evt, const float* f, int label, int chainNLayers, float simVxy, float simPt) {
    evt_ = evt;
    label_ = label;
    chainNLayers_ = chainNLayers;
    simVxy_ = simVxy;
    simPt_ = simPt;
    for (int i = 0; i < kAttachFeat; ++i)
      af_[i] = f[i];
    tree_->Fill();
  }

  void writeAndClose() {
    if (!file_)
      return;
    file_->cd();
    std::string spec = "af:";
    for (int i = 0; i < kAttachFeat; ++i) {
      if (i > 0)
        spec += ",";
      spec += kAttachFeatNames[i];
    }
    TNamed specNamed("feature_spec", spec.c_str());
    specNamed.Write();
    tree_->Write();
    file_->Close();
    delete file_;
    file_ = nullptr;
    tree_ = nullptr;  // owned by the file; gone after Close
  }

private:
  TFile* file_ = nullptr;
  TTree* tree_ = nullptr;

  ULong64_t evt_ = 0;
  Int_t label_ = 0;
  Int_t chainNLayers_ = 0;
  Float_t simVxy_ = -999.f;
  Float_t simPt_ = -999.f;
  Float_t af_[kAttachFeat] = {};
};

PairDumpWriter::PairDumpWriter(const std::string& outPath) : impl_(std::make_unique<PairDumpWriterImpl>(outPath)) {}

PairDumpWriter::~PairDumpWriter() = default;

void PairDumpWriter::fillPair(
    unsigned long long evt, const float* f, int label, int chainNLayers, float simVxy, float simPt) {
  impl_->fillPair(evt, f, label, chainNLayers, simVxy, simPt);
}

void PairDumpWriter::writeAndClose() { impl_->writeAndClose(); }
