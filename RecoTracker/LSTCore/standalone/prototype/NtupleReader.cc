// Implementation of NtupleReader (plan section 10). Plain ROOT only.
//
// Branch names on the LST side are identical to the LSTEventData field names
// (verified against smoke_test.root and write_lst_ntuple.cc). Only the branches
// actually consumed are enabled (SetBranchStatus pattern) so multi-hundred-event
// loops do not pay for the wide --allobj tree.

#include "NtupleReader.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <map>
#include <stdexcept>
#include <tuple>
#include <unordered_map>

#include <TFile.h>
#include <TTree.h>

namespace {

template <typename T>
void bindVec(TTree* tree, const char* name, std::vector<T>*& ptr) {
  tree->SetBranchStatus(name, 1);
  if (tree->SetBranchAddress(name, &ptr) < 0)
    throw std::runtime_error(std::string("NtupleReader: missing/mistyped branch ") + name);
}

template <typename T>
void bindScalar(TTree* tree, const char* name, T& value) {
  tree->SetBranchStatus(name, 1);
  if (tree->SetBranchAddress(name, &value) < 0)
    throw std::runtime_error(std::string("NtupleReader: missing/mistyped branch ") + name);
}

// trkSamplePath is a single file or a directory (all *.root inside, alphabetical),
// matching the standalone looper's input rule.
std::vector<std::string> resolveTrkFiles(const std::string& path) {
  namespace fs = std::filesystem;
  std::vector<std::string> files;
  if (fs::is_directory(path)) {
    for (auto const& entry : fs::directory_iterator(path)) {
      if (entry.path().extension() == ".root")
        files.push_back(entry.path().string());
    }
    std::sort(files.begin(), files.end());
  } else {
    files.push_back(path);
  }
  if (files.empty())
    throw std::runtime_error("NtupleReader: no .root files found at " + path);
  return files;
}

}  // namespace

// X-macro branch lists; branch name == struct field name.
#define LST_VF(X)                                                                                            \
  X(sim_pt) X(sim_eta) X(sim_phi) X(sim_pca_dxy) X(sim_pca_dz) X(sim_vx) X(sim_vy) X(sim_vz)                 \
  X(tc_pt) X(tc_eta) X(tc_phi)                                                                               \
  X(md_anchor_x) X(md_anchor_y) X(md_anchor_z) X(md_other_x) X(md_other_y) X(md_other_z) X(md_dphichange)    \
  X(t3_pt) X(t3_eta) X(t3_phi) X(t3_radius) X(t3_centerX) X(t3_centerY) X(t3_betaIn)                         \
  X(t3_fakeScore) X(t3_promptScore) X(t3_displacedScore) X(t3_pMatched)                                      \
  X(pLS_pt) X(pLS_ptErr) X(pLS_eta) X(pLS_etaErr) X(pLS_phi) X(pLS_px) X(pLS_py) X(pLS_pz)                   \
  X(pLS_circleCenterX) X(pLS_circleCenterY) X(pLS_circleRadius) X(pLS_deltaPhi)                              \
  X(pLS_hit0_x) X(pLS_hit0_y) X(pLS_hit0_z)

#define LST_VI(X)                                                                \
  X(sim_q) X(sim_pdgId)                                                          \
  X(tc_type) X(tc_isFake) X(tc_isDuplicate) X(tc_nhitOT) X(sim_tcIdx)            \
  X(tc_pt5Idx) X(tc_pt3Idx) X(tc_plsIdx)                                         \
  X(md_anchorHitIdx) X(md_otherHitIdx) X(md_type) X(md_layer) X(md_detId)        \
  X(ls_mdIdx0) X(ls_mdIdx1) X(t3_lsIdx0) X(t3_lsIdx1)                            \
  X(pLS_charge) X(pLS_nhit) X(pLS_seedIdx) X(pLS_isFake) X(pLS_isDuplicate)      \
  X(pT5_plsIdx) X(pT3_plsIdx) X(pT5_t5Idx)

#define LST_VB(X) X(md_isPLS) X(ls_isPLS) X(pLS_isQuad) X(t3_partOfPT5) X(t3_partOfPT3)

#define LST_VVI(X) \
  X(t3_matched_simIdx) X(pLS_simIdxAll) X(md_simIdxAll) X(tc_simIdxAll) \
  X(t5_hitIndices) X(pT3_otHitIndices)

#define LST_VVF(X) X(md_simIdxAllFrac) X(tc_simIdxAllFrac)

// Tracking-ntuple branches (all verified to already have the TrkEventData types).
#define TRK_VI(X) X(simhit_simTrkIdx) X(sim_bunchCrossing) X(sim_event) X(sim_q)
#define TRK_VF(X) X(sim_pt)
#define TRK_VVI(X) X(ph2_simHitIdx) X(pix_simHitIdx) X(see_hitIdx) X(see_hitType)

class NtupleReaderImpl {
public:
  NtupleReaderImpl(const std::string& lstNtuplePath, const std::string& trkSamplePath)
      : trkFiles_(resolveTrkFiles(trkSamplePath)) {
    lstFile_.reset(TFile::Open(lstNtuplePath.c_str()));
    if (!lstFile_ || lstFile_->IsZombie())
      throw std::runtime_error("NtupleReader: cannot open LST ntuple " + lstNtuplePath);
    lstTree_ = dynamic_cast<TTree*>(lstFile_->Get("tree"));
    if (!lstTree_)
      throw std::runtime_error("NtupleReader: no 'tree' in " + lstNtuplePath);

    readModuleTable();
    setupLstBranches();
    scanTrkSample();
  }

  long long nEntries() const { return lstTree_->GetEntries(); }

  const ModuleTable& moduleTable() const { return moduleTable_; }

  bool loadEntry(long long i, LSTEventData& ev, TrkEventData& trk) {
    if (i < 0 || i >= lstTree_->GetEntries())
      return false;
    lstTree_->GetEntry(i);
    ev = evBuf_;

    // Derived per-T3 MD indices: {LS0.md0, LS0.md1 (shared middle), LS1.md1}
    // (AccessHelper.cc getMDsFromT3 convention).
    std::size_t nT3 = ev.t3_lsIdx0.size();
    ev.t3_md0.resize(nT3);
    ev.t3_md1.resize(nT3);
    ev.t3_md2.resize(nT3);
    for (std::size_t t = 0; t < nT3; ++t) {
      ev.t3_md0[t] = ev.ls_mdIdx0[ev.t3_lsIdx0[t]];
      ev.t3_md1[t] = ev.ls_mdIdx1[ev.t3_lsIdx0[t]];
      ev.t3_md2[t] = ev.ls_mdIdx1[ev.t3_lsIdx1[t]];
    }

    auto it = trkIndex_.find(std::make_tuple(ev.run, ev.lumi, ev.evt));
    if (it == trkIndex_.end())
      return false;
    std::pair<int, long long> target;
    if (it->second.size() == 1) {
      target = it->second.front();
    } else {
      // (run, lumi, event) is not unique in some tracking samples (e.g. the PU200
      // sample concatenates 5-event blocks, so each key repeats with different sim
      // content). Disambiguate by the accepted-sim pt fingerprint: the writer copies
      // trk sim_pt bit-exact into the LST sim block, in row order.
      auto& fpMap = fingerprints_[it->first];
      if (fpMap.empty()) {
        for (auto const& cand : it->second) {
          openTrkFile(cand.first);
          for (const char* bname : {"sim_bunchCrossing", "sim_event", "sim_pt"})
            trkTree_->GetBranch(bname)->GetEntry(cand.second);
          std::uint64_t h = kFnvOffset;
          for (std::size_t s = 0; s < trkBuf_.sim_bunchCrossing.size(); ++s) {
            if (trkBuf_.sim_bunchCrossing[s] == 0 && trkBuf_.sim_event[s] == 0)
              fnvMix(h, trkBuf_.sim_pt[s]);
          }
          fpMap[h] = cand;
        }
      }
      std::uint64_t h = kFnvOffset;
      for (float pt : ev.sim_pt)
        fnvMix(h, pt);
      auto fit = fpMap.find(h);
      if (fit == fpMap.end())
        return false;
      target = fit->second;
    }
    openTrkFile(target.first);
    trkTree_->GetEntry(target.second);
    trk = trkBuf_;

    // Mirror write_lst_ntuple.cc setSimTrackContainerBranches: accepted = rows with
    // bunchCrossing == 0 and event == 0, taken in row order, no cap.
    std::size_t nSimFull = trk.sim_bunchCrossing.size();
    trk.simFullToAccepted.assign(nSimFull, -1);
    int nAccepted = 0;
    for (std::size_t s = 0; s < nSimFull; ++s) {
      if (trk.sim_bunchCrossing[s] == 0 && trk.sim_event[s] == 0)
        trk.simFullToAccepted[s] = nAccepted++;
    }
    return true;
  }

private:
  void readModuleTable() {
    lstTree_->SetBranchStatus("*", 0);
    std::vector<int>* p_detIds = &moduleTable_.detIds;
    std::vector<int>* p_layers = &moduleTable_.layers;
    std::vector<int>* p_subdets = &moduleTable_.subdets;
    std::vector<int>* p_rings = &moduleTable_.rings;
    std::vector<int>* p_rods = &moduleTable_.rods;
    std::vector<int>* p_modules = &moduleTable_.modules;
    std::vector<bool>* p_isTilted = &moduleTable_.isTilted;
    std::vector<float>* p_eta = &moduleTable_.eta;
    std::vector<float>* p_r = &moduleTable_.r;
    bindVec(lstTree_, "module_detIds", p_detIds);
    bindVec(lstTree_, "module_layers", p_layers);
    bindVec(lstTree_, "module_subdets", p_subdets);
    bindVec(lstTree_, "module_rings", p_rings);
    bindVec(lstTree_, "module_rods", p_rods);
    bindVec(lstTree_, "module_modules", p_modules);
    bindVec(lstTree_, "module_isTilted", p_isTilted);
    bindVec(lstTree_, "module_eta", p_eta);
    bindVec(lstTree_, "module_r", p_r);
    if (lstTree_->GetEntries() == 0)
      throw std::runtime_error("NtupleReader: LST ntuple has no entries");
    lstTree_->GetEntry(0);
    lstTree_->ResetBranchAddresses();
  }

  void setupLstBranches() {
    lstTree_->SetBranchStatus("*", 0);
    bindScalar(lstTree_, "run", evBuf_.run);
    bindScalar(lstTree_, "lumi", evBuf_.lumi);
    bindScalar(lstTree_, "evt", evBuf_.evt);
#define BIND(f)          \
  p_##f##_ = &evBuf_.f;  \
  bindVec(lstTree_, #f, p_##f##_);
    LST_VF(BIND)
    LST_VI(BIND)
    LST_VB(BIND)
    LST_VVI(BIND)
    LST_VVF(BIND)
#undef BIND
  }

  // One pass over every tracking entry reading only the identity scalars, building
  // (run, lumi, event) -> (file, entry). LST entries are stream-completion ordered,
  // so this map is what aligns them back to truth.
  void scanTrkSample() {
    for (std::size_t fi = 0; fi < trkFiles_.size(); ++fi) {
      std::unique_ptr<TFile> f(TFile::Open(trkFiles_[fi].c_str()));
      if (!f || f->IsZombie())
        throw std::runtime_error("NtupleReader: cannot open tracking file " + trkFiles_[fi]);
      TTree* t = dynamic_cast<TTree*>(f->Get("trackingNtuple/tree"));
      if (!t)
        throw std::runtime_error("NtupleReader: no trackingNtuple/tree in " + trkFiles_[fi]);
      t->SetBranchStatus("*", 0);
      unsigned int run = 0, lumi = 0;
      unsigned long long event = 0;
      bindScalar(t, "run", run);
      bindScalar(t, "lumi", lumi);
      bindScalar(t, "event", event);
      long long n = t->GetEntries();
      for (long long e = 0; e < n; ++e) {
        t->GetEntry(e);
        trkIndex_[std::make_tuple(run, lumi, event)].emplace_back(static_cast<int>(fi), e);
      }
    }
  }

  void openTrkFile(int fileIdx) {
    if (fileIdx == curTrkFileIdx_)
      return;
    trkTree_ = nullptr;
    trkFile_.reset(TFile::Open(trkFiles_[fileIdx].c_str()));
    if (!trkFile_ || trkFile_->IsZombie())
      throw std::runtime_error("NtupleReader: cannot open tracking file " + trkFiles_[fileIdx]);
    trkTree_ = dynamic_cast<TTree*>(trkFile_->Get("trackingNtuple/tree"));
    if (!trkTree_)
      throw std::runtime_error("NtupleReader: no trackingNtuple/tree in " + trkFiles_[fileIdx]);
    trkTree_->SetBranchStatus("*", 0);
#define BIND(f)            \
  tp_##f##_ = &trkBuf_.f;  \
  bindVec(trkTree_, #f, tp_##f##_);
    TRK_VI(BIND)
    TRK_VF(BIND)
    TRK_VVI(BIND)
#undef BIND
    curTrkFileIdx_ = fileIdx;
  }

  std::vector<std::string> trkFiles_;

  std::unique_ptr<TFile> lstFile_;
  TTree* lstTree_ = nullptr;

  std::unique_ptr<TFile> trkFile_;  // only one tracking file kept open at a time
  TTree* trkTree_ = nullptr;
  int curTrkFileIdx_ = -1;

  ModuleTable moduleTable_;

  // ROOT reads into these persistent buffers (declared before the pointer members
  // that reference their fields); loadEntry copies them out.
  LSTEventData evBuf_;
  TrkEventData trkBuf_;

#define DECL(f) std::vector<float>* p_##f##_ = nullptr;
  LST_VF(DECL)
#undef DECL
#define DECL(f) std::vector<int>* p_##f##_ = nullptr;
  LST_VI(DECL)
#undef DECL
#define DECL(f) std::vector<bool>* p_##f##_ = nullptr;
  LST_VB(DECL)
#undef DECL
#define DECL(f) std::vector<std::vector<int>>* p_##f##_ = nullptr;
  LST_VVI(DECL)
#undef DECL
#define DECL(f) std::vector<std::vector<float>>* p_##f##_ = nullptr;
  LST_VVF(DECL)
#undef DECL

#define DECL(f) std::vector<int>* tp_##f##_ = nullptr;
  TRK_VI(DECL)
#undef DECL
#define DECL(f) std::vector<float>* tp_##f##_ = nullptr;
  TRK_VF(DECL)
#undef DECL
#define DECL(f) std::vector<std::vector<int>>* tp_##f##_ = nullptr;
  TRK_VVI(DECL)
#undef DECL

  using TrkKey = std::tuple<unsigned int, unsigned int, unsigned long long>;
  std::map<TrkKey, std::vector<std::pair<int, long long>>> trkIndex_;
  // Lazily-built accepted-sim-pt fingerprints, only for keys with multiple candidates.
  std::map<TrkKey, std::unordered_map<std::uint64_t, std::pair<int, long long>>> fingerprints_;

  static constexpr std::uint64_t kFnvOffset = 1469598103934665603ULL;
  static void fnvMix(std::uint64_t& h, float v) {
    std::uint32_t bits;
    std::memcpy(&bits, &v, sizeof(bits));
    h ^= bits;
    h *= 1099511628211ULL;
  }
};

NtupleReader::NtupleReader(const std::string& lstNtuplePath, const std::string& trkSamplePath)
    : impl_(std::make_unique<NtupleReaderImpl>(lstNtuplePath, trkSamplePath)) {}

NtupleReader::~NtupleReader() = default;

long long NtupleReader::nEntries() const { return impl_->nEntries(); }

bool NtupleReader::loadEntry(long long i, LSTEventData& ev, TrkEventData& trk) {
  return impl_->loadEntry(i, ev, trk);
}

const ModuleTable& NtupleReader::moduleTable() const { return impl_->moduleTable(); }
