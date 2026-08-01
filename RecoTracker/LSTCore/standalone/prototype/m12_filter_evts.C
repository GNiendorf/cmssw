// Copy only the entries whose `evt` is in a given list into a new file (same tree layout),
// so createPerfNumDenHists can be run on the frozen test-60 subset of any LST-format ntuple.
// Uses TTree::CopyTree with a selection expression (a manual CloneTree(0)+Fill loop produced
// a file that crashed the RooUtil reader).
void m12_filter_evts(const char* in, const char* out, const char* evtlist) {
  std::ifstream fs(evtlist);
  std::vector<ULong64_t> keep;
  ULong64_t v;
  while (fs >> v) keep.push_back(v);
  TString sel;
  for (size_t i = 0; i < keep.size(); ++i) {
    if (i) sel += "||";
    sel += TString::Format("evt==%llu", keep[i]);
  }
  printf("keeping %zu event keys\n", keep.size());
  TFile* fi = TFile::Open(in);
  TTree* ti = (TTree*)fi->Get("tree");
  TFile* fo = TFile::Open(out, "RECREATE");
  TTree* to = ti->CopyTree(sel);
  printf("copied %lld / %lld entries\n", to->GetEntries(), ti->GetEntries());
  fo->cd();
  to->Write();
  // Preserve the top-level TNamed metadata the harness reads.
  TIter next(fi->GetListOfKeys());
  TKey* k;
  while ((k = (TKey*)next())) {
    if (TString(k->GetClassName()) == "TNamed") {
      TNamed* n = (TNamed*)k->ReadObj();
      fo->cd();
      n->Write();
    }
  }
  fo->Close();
  fi->Close();
}
