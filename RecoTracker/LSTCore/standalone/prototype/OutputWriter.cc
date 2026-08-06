#include "OutputWriter.h"

#include <algorithm>
#include <cstdlib>

#include "TFile.h"
#include "TNamed.h"
#include "TTree.h"

namespace {
  constexpr float kMatchFrac = 0.75f;

  // DUPCUT diagnostics: per-TC outer-tracker hit rows, written only when the
  // PROTO_DUMP_TCHITS environment variable is set to a non-zero value. Off by
  // default, so every run without it is bit-identical to the legacy writer.
  bool dumpTcHits() {
    static const bool on = [] {
      const char* v = std::getenv("PROTO_DUMP_TCHITS");
      return v != nullptr && v[0] != '\0' && v[0] != '0';
    }();
    return on;
  }
}  // namespace

class OutputWriterImpl {
public:
  OutputWriterImpl(const std::string& outPath, const std::string& inputLabel) : inputLabel_(inputLabel) {
    file_ = new TFile(outPath.c_str(), "RECREATE");
    tree_ = new TTree("tree", "tree");

    tree_->Branch("sim_pt", &sim_pt_);
    tree_->Branch("sim_eta", &sim_eta_);
    tree_->Branch("sim_phi", &sim_phi_);
    tree_->Branch("sim_pca_dxy", &sim_pca_dxy_);
    tree_->Branch("sim_pca_dz", &sim_pca_dz_);
    tree_->Branch("sim_vx", &sim_vx_);
    tree_->Branch("sim_vy", &sim_vy_);
    tree_->Branch("sim_vz", &sim_vz_);
    tree_->Branch("sim_pdgId", &sim_pdgId_);
    tree_->Branch("sim_q", &sim_q_);

    tree_->Branch("tc_pt", &tc_pt_);
    tree_->Branch("tc_eta", &tc_eta_);
    tree_->Branch("tc_phi", &tc_phi_);
    tree_->Branch("tc_type", &tc_type_);
    tree_->Branch("tc_isFake", &tc_isFake_);
    tree_->Branch("tc_isDuplicate", &tc_isDuplicate_);
    tree_->Branch("tc_nhitOT", &tc_nhitOT_);

    tree_->Branch("sim_tcIdx", &sim_tcIdx_);

    // Diagnostics extras (hybrid mode; empty in other modes). Harmless to the harness —
    // it reads branches by name — and enable offline duplicate decomposition:
    // per-TC full-sim-list match rows + provenance. tc_isChain carries the M16 OutDeliv
    // code (0 carried / 1 chain / 2 attach-pT5 / 3 attach-pT3); values 0 and 1 are the
    // legacy semantics verbatim, so every pre-M16 file and legacy-mode run is unchanged.
    tree_->Branch("tc_simIdxAll", &tc_simIdxAll_out_);
    tree_->Branch("tc_isChain", &tc_isChain_);
    if (dumpTcHits())
      tree_->Branch("tc_hitOT", &tc_hitOT_);
    // FANOUT4 transition diagnostics (see OutTC in OutputWriter.h). Extra branches only;
    // the efficiency harness reads by name and never sees them.
    tree_->Branch("tc_dbgBr", &tc_dbgBr_);
    tree_->Branch("tc_dbgNL", &tc_dbgNL_);
    tree_->Branch("tc_dbgNMD", &tc_dbgNMD_);
    tree_->Branch("tc_dbgNB", &tc_dbgNB_);
    tree_->Branch("tc_dbgNPS", &tc_dbgNPS_);
    tree_->Branch("tc_dbgNN", &tc_dbgNN_);
    tree_->Branch("tc_dbgInLay", &tc_dbgInLay_);
    tree_->Branch("tc_dbgMP", &tc_dbgMP_);
    tree_->Branch("tc_dbgMD", &tc_dbgMD_);
    tree_->Branch("tc_dbgDca", &tc_dbgDca_);

    tree_->Branch("run", &run_, "run/i");
    tree_->Branch("lumi", &lumi_, "lumi/i");
    tree_->Branch("evt", &evt_, "evt/l");
  }

  ~OutputWriterImpl() {
    if (file_) {
      file_->Close();
      delete file_;
    }
  }

  void fillEventIdentity(const LSTEventData& ev) {
    fillSimAndIdentity(ev);
    tc_pt_ = ev.tc_pt;
    tc_eta_ = ev.tc_eta;
    tc_phi_ = ev.tc_phi;
    tc_type_ = ev.tc_type;
    tc_isFake_ = ev.tc_isFake;
    tc_isDuplicate_ = ev.tc_isDuplicate;
    tc_nhitOT_ = ev.tc_nhitOT;
    sim_tcIdx_ = ev.sim_tcIdx;
    tree_->Fill();
  }

  void fillEvent(const LSTEventData& ev, const TrkEventData& trk, const std::vector<OutTC>& tcs) {
    fillSimAndIdentity(ev);

    // Mirror of write_lst_ntuple.cc setTrackCandidateBranches: the sim->tc map is kept
    // over the FULL sim list (incl. pileup) because duplicates are counted against all
    // sim tracks; only the accepted subset is emitted as sim_tcIdx at the end.
    const size_t n_total_simtrk = std::max(trk.sim_pt.size(), trk.simFullToAccepted.size());
    std::vector<std::vector<int>> sim_tcIdxAll(n_total_simtrk);
    std::vector<std::vector<float>> sim_tcIdxAllFrac(n_total_simtrk);
    std::vector<std::vector<int>> tc_simIdxAll;
    std::vector<std::vector<float>> tc_simIdxAllFrac;

    for (size_t tc_idx = 0; tc_idx < tcs.size(); ++tc_idx) {
      const OutTC& tc = tcs[tc_idx];

      auto [simidx, simidxfrac] = proto::matchedSimTrkIdxsAndFracs(
          tc.hitIdxs, tc.hitTypes, trk.simhit_simTrkIdx, trk.ph2_simHitIdx, trk.pix_simHitIdx, false, kMatchFrac);
      // matchedSimTrkIdxsAndFracs already keeps only fractions strictly > kMatchFrac
      int isFake = simidx.size() == 0;

      tc_pt_.push_back(tc.pt);
      tc_eta_.push_back(tc.eta);
      tc_phi_.push_back(tc.phi);
      tc_type_.push_back(tc.type);
      tc_nhitOT_.push_back(tc.nhitOT);
      tc_isFake_.push_back(isFake);

      tc_simIdxAll.push_back(simidx);
      tc_simIdxAllFrac.push_back(simidxfrac);

      for (size_t is = 0; is < simidx.size(); ++is) {
        sim_tcIdxAll.at(simidx.at(is)).push_back(static_cast<int>(tc_idx));
        sim_tcIdxAllFrac.at(simidx.at(is)).push_back(simidxfrac.at(is));
      }
    }

    // A TC is a duplicate if any of its matched (full-list) sims is matched by more
    // than one TC with fraction > kMatchFrac.
    tc_isDuplicate_.assign(tc_simIdxAll.size(), 0);
    for (size_t tc_idx = 0; tc_idx < tc_simIdxAll.size(); ++tc_idx) {
      bool isDuplicate = false;
      for (size_t isim = 0; isim < tc_simIdxAll[tc_idx].size(); ++isim) {
        int sim_idx = tc_simIdxAll[tc_idx][isim];
        int n_sim_matched = 0;
        for (size_t ism = 0; ism < sim_tcIdxAll.at(sim_idx).size(); ++ism) {
          if (sim_tcIdxAllFrac.at(sim_idx).at(ism) > kMatchFrac) {
            n_sim_matched += 1;
            if (n_sim_matched > 1) {
              isDuplicate = true;
              break;
            }
          }
        }
      }
      tc_isDuplicate_[tc_idx] = isDuplicate;
    }

    // Per ACCEPTED sim row: best-fraction TC, only kept if the fraction beats kMatchFrac.
    // The production writer takes the first n_accepted rows of the full list; here the
    // reader-provided full->accepted map does the same restriction.
    sim_tcIdx_.assign(ev.sim_pt.size(), -999);
    for (size_t f = 0; f < trk.simFullToAccepted.size(); ++f) {
      int a = trk.simFullToAccepted[f];
      if (a < 0 || static_cast<size_t>(a) >= sim_tcIdx_.size())
        continue;
      int bestmatch_idx = -999;
      float bestmatch_frac = -999;
      for (size_t jj = 0; jj < sim_tcIdxAll.at(f).size(); ++jj) {
        int idx = sim_tcIdxAll.at(f).at(jj);
        float frac = sim_tcIdxAllFrac.at(f).at(jj);
        if (bestmatch_frac < frac) {
          bestmatch_idx = idx;
          bestmatch_frac = frac;
        }
      }
      if (bestmatch_frac > kMatchFrac)
        sim_tcIdx_[a] = bestmatch_idx;
      else
        sim_tcIdx_[a] = -999;
    }

    tree_->Fill();
  }

  // HYBRID (M4): merged TC list = [kept baseline pixel rows (tc_type 7/5/8, input order)]
  // + [chain TCs]. Baseline T5/T4-class rows (tc_type 4/9) are replaced by the chains.
  //
  // INDEX SPACE of ev.tc_simIdxAll: FULL tracking-ntuple sim rows. Verified against
  // write_lst_ntuple.cc setTrackCandidateBranches: the simidx list returned by
  // parseTrackCandidateAllMatch is pushed verbatim into the tc_simIdxAll branch AND used
  // directly to index the n_total_simtrk-sized sim_tcIdxAll bookkeeping (line ~2449);
  // only the sim->tc direction is truncated to the first n_accepted_simtrk rows before
  // writing. So baseline per-TC (simIdx, frac) pairs can be accumulated into the
  // full-sim-list map below without any remapping, exactly like chain-TC matches.
  void fillEventHybrid(const LSTEventData& ev,
                       const TrkEventData& trk,
                       const std::vector<OutTC>& chainTCs,
                       const std::vector<char>* suppressPlsRows,
                       int* nSuppressedOut,
                       bool suppressPT3Rows,
                       int* nSuppressedByType,
                       const std::vector<char>* suppressRowMask) {
    fillSimAndIdentity(ev);
    int nSuppressed = 0;
    int nSuppByType[3] = {0, 0, 0};  // {type 7, type 5, type 8}

    const size_t n_total_simtrk = std::max(trk.sim_pt.size(), trk.simFullToAccepted.size());
    std::vector<std::vector<int>> sim_tcIdxAll(n_total_simtrk);
    std::vector<std::vector<float>> sim_tcIdxAllFrac(n_total_simtrk);
    std::vector<std::vector<int>> tc_simIdxAll;
    std::vector<std::vector<float>> tc_simIdxAllFrac;

    auto accumulate = [&](const std::vector<int>& simidx, const std::vector<float>& simidxfrac) {
      const int tc_idx = static_cast<int>(tc_simIdxAll.size());
      tc_simIdxAll.push_back(simidx);
      tc_simIdxAllFrac.push_back(simidxfrac);
      for (size_t is = 0; is < simidx.size(); ++is) {
        sim_tcIdxAll.at(simidx.at(is)).push_back(tc_idx);
        sim_tcIdxAllFrac.at(simidx.at(is)).push_back(simidxfrac.at(is));
      }
    };

    // 1) Kept baseline pixel rows, in input order; kinematics copied from the input,
    //    matching taken from the input branches (already thresholded at >0.75 by the
    //    production writer).
    for (size_t in_idx = 0; in_idx < ev.tc_type.size(); ++in_idx) {
      const int type = ev.tc_type[in_idx];
      if (type != 7 && type != 5 && type != 8)  // keep pT5 / pT3 / pLS only
        continue;
      if (suppressRowMask != nullptr) {
        // M7c (-A 2): the caller resolved families to rows and applied the kinematic
        // suppression guard already; obey the per-row verdict.
        if (in_idx < suppressRowMask->size() && (*suppressRowMask)[in_idx] != 0) {
          ++nSuppressed;
          ++nSuppByType[type == 7 ? 0 : (type == 5 ? 1 : 2)];
          continue;
        }
      } else if (suppressPlsRows != nullptr) {
        // K8 structural crossclean (M7): a chain+pLS type-7 TC replaced this pixel
        // seed's baseline delivery -- drop the pT5/pLS rows whose pLS is masked.
        // pT3 (type 5) rows join only under suppressPT3Rows (M7b -A 2 seed-family
        // suppression); -A 1 keeps them untouched (rejected-v1 reference behavior).
        int pls = -1;
        if (type == 7 && in_idx < ev.tc_pt5Idx.size()) {
          const int i5 = ev.tc_pt5Idx[in_idx];
          if (i5 >= 0 && i5 < static_cast<int>(ev.pT5_plsIdx.size()))
            pls = ev.pT5_plsIdx[i5];
        } else if (type == 5 && suppressPT3Rows && in_idx < ev.tc_pt3Idx.size()) {
          const int i3 = ev.tc_pt3Idx[in_idx];
          if (i3 >= 0 && i3 < static_cast<int>(ev.pT3_plsIdx.size()))
            pls = ev.pT3_plsIdx[i3];
        } else if (type == 8 && in_idx < ev.tc_plsIdx.size()) {
          pls = ev.tc_plsIdx[in_idx];
        }
        if (pls >= 0 && pls < static_cast<int>(suppressPlsRows->size()) && (*suppressPlsRows)[pls] != 0) {
          ++nSuppressed;
          ++nSuppByType[type == 7 ? 0 : (type == 5 ? 1 : 2)];
          continue;
        }
      }
      tc_pt_.push_back(ev.tc_pt[in_idx]);
      tc_eta_.push_back(ev.tc_eta[in_idx]);
      tc_phi_.push_back(ev.tc_phi[in_idx]);
      tc_type_.push_back(type);
      tc_nhitOT_.push_back(ev.tc_nhitOT[in_idx]);
      const std::vector<int>& simidx = ev.tc_simIdxAll.at(in_idx);
      const std::vector<float>& simidxfrac = ev.tc_simIdxAllFrac.at(in_idx);
      tc_isFake_.push_back(simidx.empty() ? 1 : 0);
      tc_isChain_.push_back(0);
      if (dumpTcHits()) {
        // Carried-row OT content, exactly the mapping the -PU pre-claim uses:
        // type 7 -> pT5_t5Idx -> t5_hitIndices, type 5 -> pT3_otHitIndices,
        // type 8 -> no outer-tracker hits at all.
        std::vector<int> oth;
        if (type == 7 && in_idx < ev.tc_pt5Idx.size()) {
          const int p5 = ev.tc_pt5Idx[in_idx];
          if (p5 >= 0 && p5 < static_cast<int>(ev.pT5_t5Idx.size())) {
            const int t5 = ev.pT5_t5Idx[p5];
            if (t5 >= 0 && t5 < static_cast<int>(ev.t5_hitIndices.size()))
              oth = ev.t5_hitIndices[t5];
          }
        } else if (type == 5 && in_idx < ev.tc_pt3Idx.size()) {
          const int p3 = ev.tc_pt3Idx[in_idx];
          if (p3 >= 0 && p3 < static_cast<int>(ev.pT3_otHitIndices.size()))
            oth = ev.pT3_otHitIndices[p3];
        }
        tc_hitOT_.push_back(std::move(oth));
      }
      pushDbg(OutTC());
      accumulate(simidx, simidxfrac);
    }

    // 2) Chain TCs: match from their (all-Phase2OT) hit lists with the ported matcher.
    for (const OutTC& tc : chainTCs) {
      auto [simidx, simidxfrac] = proto::matchedSimTrkIdxsAndFracs(
          tc.hitIdxs, tc.hitTypes, trk.simhit_simTrkIdx, trk.ph2_simHitIdx, trk.pix_simHitIdx, false, kMatchFrac);
      tc_pt_.push_back(tc.pt);
      tc_eta_.push_back(tc.eta);
      tc_phi_.push_back(tc.phi);
      tc_type_.push_back(tc.type);
      tc_nhitOT_.push_back(tc.nhitOT);
      tc_isFake_.push_back(simidx.empty() ? 1 : 0);
      tc_isChain_.push_back(tc.deliv);  // M16 delivery class; 1 for every legacy caller
      if (dumpTcHits()) {
        std::vector<int> oth;
        for (size_t h = 0; h < tc.hitIdxs.size() && h < tc.hitTypes.size(); ++h)
          if (tc.hitTypes[h] == proto::HitType::Phase2OT)
            oth.push_back(static_cast<int>(tc.hitIdxs[h]));
        tc_hitOT_.push_back(std::move(oth));
      }
      pushDbg(tc);
      accumulate(simidx, simidxfrac);
    }

    // 3) Duplicates over the MERGED set, counted against the FULL sim list (identical
    //    logic to fillEvent / the production writer).
    tc_isDuplicate_.assign(tc_simIdxAll.size(), 0);
    for (size_t tc_idx = 0; tc_idx < tc_simIdxAll.size(); ++tc_idx) {
      bool isDuplicate = false;
      for (size_t isim = 0; isim < tc_simIdxAll[tc_idx].size(); ++isim) {
        int sim_idx = tc_simIdxAll[tc_idx][isim];
        int n_sim_matched = 0;
        for (size_t ism = 0; ism < sim_tcIdxAll.at(sim_idx).size(); ++ism) {
          if (sim_tcIdxAllFrac.at(sim_idx).at(ism) > kMatchFrac) {
            n_sim_matched += 1;
            if (n_sim_matched > 1) {
              isDuplicate = true;
              break;
            }
          }
        }
      }
      tc_isDuplicate_[tc_idx] = isDuplicate;
    }

    // Per-TC match rows out to the diagnostics branch (only sim_tcIdxAll is used below).
    tc_simIdxAll_out_ = std::move(tc_simIdxAll);

    // 4) Per ACCEPTED sim: best-fraction TC over the merged list, kept if > kMatchFrac.
    sim_tcIdx_.assign(ev.sim_pt.size(), -999);
    for (size_t f = 0; f < trk.simFullToAccepted.size(); ++f) {
      int a = trk.simFullToAccepted[f];
      if (a < 0 || static_cast<size_t>(a) >= sim_tcIdx_.size())
        continue;
      int bestmatch_idx = -999;
      float bestmatch_frac = -999;
      for (size_t jj = 0; jj < sim_tcIdxAll.at(f).size(); ++jj) {
        int idx = sim_tcIdxAll.at(f).at(jj);
        float frac = sim_tcIdxAllFrac.at(f).at(jj);
        if (bestmatch_frac < frac) {
          bestmatch_idx = idx;
          bestmatch_frac = frac;
        }
      }
      if (bestmatch_frac > kMatchFrac)
        sim_tcIdx_[a] = bestmatch_idx;
      else
        sim_tcIdx_[a] = -999;
    }

    if (nSuppressedOut != nullptr)
      *nSuppressedOut = nSuppressed;
    if (nSuppressedByType != nullptr)
      for (int t = 0; t < 3; ++t)
        nSuppressedByType[t] = nSuppByType[t];
    tree_->Fill();
  }

  void writeAndClose() {
    if (!file_)
      return;
    file_->cd();
    tree_->Write();
    // helper.cc (efficiency harness) Get()s these three by name and dereferences without
    // a null check; all must exist even when empty.
    TNamed code_tag_data("code_tag_data", "chainproto0");
    code_tag_data.Write();
    TNamed gitdiff("gitdiff", "");
    gitdiff.Write();
    TNamed input("input", inputLabel_.c_str());
    input.Write();
    file_->Close();
    delete file_;
    file_ = nullptr;
    tree_ = nullptr;
  }

private:
  void fillSimAndIdentity(const LSTEventData& ev) {
    clearBranches();
    sim_pt_ = ev.sim_pt;
    sim_eta_ = ev.sim_eta;
    sim_phi_ = ev.sim_phi;
    sim_pca_dxy_ = ev.sim_pca_dxy;
    sim_pca_dz_ = ev.sim_pca_dz;
    sim_vx_ = ev.sim_vx;
    sim_vy_ = ev.sim_vy;
    sim_vz_ = ev.sim_vz;
    sim_pdgId_ = ev.sim_pdgId;
    sim_q_ = ev.sim_q;
    run_ = ev.run;
    lumi_ = ev.lumi;
    evt_ = ev.evt;
  }

  void clearBranches() {
    sim_pt_.clear();
    sim_eta_.clear();
    sim_phi_.clear();
    sim_pca_dxy_.clear();
    sim_pca_dz_.clear();
    sim_vx_.clear();
    sim_vy_.clear();
    sim_vz_.clear();
    sim_pdgId_.clear();
    sim_q_.clear();
    tc_pt_.clear();
    tc_eta_.clear();
    tc_phi_.clear();
    tc_type_.clear();
    tc_isFake_.clear();
    tc_isDuplicate_.clear();
    tc_nhitOT_.clear();
    sim_tcIdx_.clear();
    tc_simIdxAll_out_.clear();
    tc_isChain_.clear();
    tc_hitOT_.clear();
    tc_dbgBr_.clear();
    tc_dbgNL_.clear();
    tc_dbgNMD_.clear();
    tc_dbgNB_.clear();
    tc_dbgNPS_.clear();
    tc_dbgNN_.clear();
    tc_dbgInLay_.clear();
    tc_dbgMP_.clear();
    tc_dbgMD_.clear();
    tc_dbgDca_.clear();
    run_ = 0;
    lumi_ = 0;
    evt_ = 0;
  }

  TFile* file_ = nullptr;
  TTree* tree_ = nullptr;
  std::string inputLabel_;

  std::vector<float> sim_pt_, sim_eta_, sim_phi_, sim_pca_dxy_, sim_pca_dz_, sim_vx_, sim_vy_, sim_vz_;
  std::vector<int> sim_pdgId_, sim_q_;
  std::vector<float> tc_pt_, tc_eta_, tc_phi_;
  std::vector<int> tc_type_, tc_isFake_, tc_isDuplicate_, tc_nhitOT_;
  std::vector<int> sim_tcIdx_;
  std::vector<std::vector<int>> tc_simIdxAll_out_;  // branch "tc_simIdxAll" (hybrid diagnostics)
  std::vector<std::vector<int>> tc_hitOT_;          // branch "tc_hitOT" (PROTO_DUMP_TCHITS)
  std::vector<int> tc_dbgBr_, tc_dbgNL_, tc_dbgNMD_, tc_dbgNB_, tc_dbgNPS_, tc_dbgNN_, tc_dbgInLay_;
  std::vector<float> tc_dbgMP_, tc_dbgMD_, tc_dbgDca_;
  void pushDbg(const OutTC& t) {
    tc_dbgBr_.push_back(t.dbgBranch);
    tc_dbgNL_.push_back(t.dbgNL);
    tc_dbgNMD_.push_back(t.dbgNMD);
    tc_dbgNB_.push_back(t.dbgNB);
    tc_dbgNPS_.push_back(t.dbgNPS);
    tc_dbgNN_.push_back(t.dbgNNodes);
    tc_dbgInLay_.push_back(t.dbgInLay);
    tc_dbgMP_.push_back(t.dbgMP);
    tc_dbgMD_.push_back(t.dbgMDm);
    tc_dbgDca_.push_back(t.dbgDca);
  }
  std::vector<int> tc_isChain_;  // OutDeliv: 0 carried, 1 chain,
                                 // 2 attach-pT5, 3 attach-pT3 (M16)
  unsigned int run_ = 0, lumi_ = 0;
  unsigned long long evt_ = 0;
};

OutputWriter::OutputWriter(const std::string& outPath, const std::string& inputLabel)
    : impl_(std::make_unique<OutputWriterImpl>(outPath, inputLabel)) {}

OutputWriter::~OutputWriter() = default;

void OutputWriter::fillEventIdentity(const LSTEventData& ev) { impl_->fillEventIdentity(ev); }

void OutputWriter::fillEvent(const LSTEventData& ev, const TrkEventData& trk, const std::vector<OutTC>& tcs) {
  impl_->fillEvent(ev, trk, tcs);
}

void OutputWriter::fillEventHybrid(const LSTEventData& ev,
                                   const TrkEventData& trk,
                                   const std::vector<OutTC>& chainTCs,
                                   const std::vector<char>* suppressPlsRows,
                                   int* nSuppressedOut,
                                   bool suppressPT3Rows,
                                   int* nSuppressedByType,
                                   const std::vector<char>* suppressRowMask) {
  impl_->fillEventHybrid(
      ev, trk, chainTCs, suppressPlsRows, nSuppressedOut, suppressPT3Rows, nSuppressedByType, suppressRowMask);
}

void OutputWriter::writeAndClose() { impl_->writeAndClose(); }
