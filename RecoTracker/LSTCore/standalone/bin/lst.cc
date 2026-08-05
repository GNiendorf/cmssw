#include "lst.h"
#include "LSTPrepareInput.h"

#include <typeinfo>

#include <boost/core/demangle.hpp>

using LSTEvent = ALPAKA_ACCELERATOR_NAMESPACE::lst::LSTEvent;
using LSTInputDeviceCollection = ALPAKA_ACCELERATOR_NAMESPACE::lst::LSTInputDeviceCollection;
using namespace ::lst;

//___________________________________________________________________________________________________________________________________________________________________________________________
int main(int argc, char **argv) {
  //********************************************************************************
  //
  // 0. Preliminary operations
  //
  //********************************************************************************

  // Checking the TRACKLOOPERDIR is set
  ana.track_looper_dir_path = gSystem->Getenv("TRACKLOOPERDIR");
  if (ana.track_looper_dir_path.IsNull()) {
    RooUtil::error(
        "TRACKLOOPERDIR is not set! Did you run $ source setup.sh from TrackLooper/ main repository directory?");
  }
  RooUtil::print(TString::Format("TRACKLOOPERDIR=%s", ana.track_looper_dir_path.Data()));

  // Write the command line used to run it
  // N.B. This needs to be before the argument parsing as it will change some values
  std::vector<std::string> allArgs(argv, argv + argc);
  ana.full_cmd_line = "";
  for (auto &str : allArgs) {
    ana.full_cmd_line += TString::Format(" %s", str.c_str());
  }

  //********************************************************************************
  //
  // 1. Parsing options
  //
  //********************************************************************************

  // cxxopts is just a tool to parse argc, and argv easily

  // Grand option setting
  cxxopts::Options options("\n  $ lst",
                           "\n         **********************\n         *                    *\n         *       "
                           "Looper       *\n         *                    *\n         **********************\n");

  // Read the options
  options.add_options()("m,mode", "Run mode (NOT DEFINED)", cxxopts::value<int>()->default_value("5"))(
      "i,input",
      "Comma separated input file list OR if just a directory is provided it will glob all in the directory BUT must "
      "end with '/' for the path",
      cxxopts::value<std::string>()->default_value("muonGun"))(
      "t,tree",
      "Name of the tree in the root file to open and loop over",
      cxxopts::value<std::string>()->default_value("trackingNtuple/tree"))(
      "o,output", "Output file name", cxxopts::value<std::string>())(
      "N,nmatch", "N match for MTV-like matching", cxxopts::value<int>()->default_value("9"))(
      "p,ptCut", "Min pT cut In GeV", cxxopts::value<float>()->default_value("0.8"))(
      "c,clustSizeCut", "Max cluster size cut", cxxopts::value<uint16_t>()->default_value("16"))(
      "n,nevents", "N events to loop over", cxxopts::value<int>()->default_value("-1"))(
      "x,event_index", "specific event index to process", cxxopts::value<int>()->default_value("-1"))(
      "g,pdg_id", "The simhit pdgId match option", cxxopts::value<int>()->default_value("0"))(
      "v,verbose",
      "Verbose mode (0: no print, 1: only final timing, 2: object multiplitcity",
      cxxopts::value<int>()->default_value("0"))(
      "w,write_ntuple", "Write Ntuple", cxxopts::value<int>()->default_value("1"))(
      "s,streams", "Set number of streams", cxxopts::value<int>()->default_value("1"))(
      "d,debug", "Run debug job. i.e. overrides output option to 'debug.root' and 'recreate's the file.")(
      "j,nsplit_jobs", "Enable splitting jobs by N blocks (--job_index must be set)", cxxopts::value<int>())(
      "I,job_index",
      "job_index of split jobs (--nsplit_jobs must be set. index starts from 0. i.e. 0, 1, 2, 3, etc...)",
      cxxopts::value<int>())("3,tc_pls_triplets", "Allow triplet pLSs in TC collection")(
      "2,no_pls_dupclean", "Disable pLS duplicate cleaning (both steps)")(
      "reduce_mem_by_full_precompute",
      "Run extra counting kernels to exactly size MD/LS/T3/T5/T4 buffers (lower mem, small runtime cost)")(
      "use_chain_tracking",
      "Enable the chain-tracking pipeline (phase P2.0: triplet compaction + incidence CSR only)")(
      "h,help", "Print help")("md", "Write MD branches in output ntuple.")("ls", "Write LS branches in output ntuple.")(
      "t3", "Write T3 branches in output ntuple.")("pls", "Write pLS branches in output ntuple.")(
      "occ", "Write occupancy branches in output ntuple.")("t3dnn", "Write T3 DNN branches in output ntuple.")(
      "allobj", "Write all object branches in output ntuple.")(
      "J,jet", "Accounts for specific jet branches in input root file for testing")(
      "sim", "Write extra sim branches in output ntuple");

  auto result = options.parse(argc, argv);

  // NOTE: When an option was provided (e.g. -i or --input), then the result.count("<option name>") is more than 0
  // Therefore, the option can be parsed easily by asking the condition if (result.count("<option name>");
  // That's how the several options are parsed below

  //_______________________________________________________________________________
  // --help
  if (result.count("help")) {
    std::cout << options.help() << std::endl;
    exit(1);
  }

  //_______________________________________________________________________________
  // --input
  ana.input_raw_string = result["input"].as<std::string>();

  // A default value one
  TString TrackingNtupleDir = gSystem->Getenv("TRACKINGNTUPLEDIR");

  if (ana.input_raw_string.EqualTo("muonGun"))
    ana.input_file_list_tstring = TString::Format("%s/trackingNtuple_10mu_pt_0p5_2.root", TrackingNtupleDir.Data());
  else if (ana.input_raw_string.EqualTo("muonGun_highPt"))
    ana.input_file_list_tstring = TString::Format("%s/trackingNtuple_10mu_pt_0p5_50.root", TrackingNtupleDir.Data());
  else if (ana.input_raw_string.EqualTo("pionGun"))
    ana.input_file_list_tstring =
        TString::Format("%s/trackingNtuple_6pion_1k_pt_0p5_50.root", TrackingNtupleDir.Data());
  else if (ana.input_raw_string.EqualTo("PU200"))
    ana.input_file_list_tstring = TString::Format("%s/trackingNtuple_ttbar_PU200.root", TrackingNtupleDir.Data());
  else if (ana.input_raw_string.EqualTo("PU200RelVal"))
    ana.input_file_list_tstring = TString::Format(
        "%s/RelValTTbar_14TeV_CMSSW_12_5_0_pre3/",
        (TrackingNtupleDir.Replace(31, 1, "5").Replace(38, 1, "3"))
            .Data());  // RelVal files under CMSSW_12_5_0_pre3 directory, not CMSSW_12_2_0_pre2 as is the case for the rest of the samples
  else if (ana.input_raw_string.EqualTo("cube5"))
    ana.input_file_list_tstring =
        TString::Format("%s/trackingNtuple_10mu_10k_pt_0p5_2_5cm_cube.root", TrackingNtupleDir.Data());
  else if (ana.input_raw_string.EqualTo("cube5_highPt"))
    ana.input_file_list_tstring =
        TString::Format("%s/trackingNtuple_10mu_10k_pt_0p5_50_5cm_cube.root", TrackingNtupleDir.Data());
  else if (ana.input_raw_string.EqualTo("cube50"))
    ana.input_file_list_tstring =
        TString::Format("%s/trackingNtuple_10mu_10k_pt_0p5_2_50cm_cube.root", TrackingNtupleDir.Data());
  else if (ana.input_raw_string.EqualTo("cube50_highPt"))
    ana.input_file_list_tstring =
        TString::Format("%s/trackingNtuple_10mu_10k_pt_0p5_50_50cm_cube.root", TrackingNtupleDir.Data());
  else {
    ana.input_file_list_tstring = ana.input_raw_string;
  }

  //_______________________________________________________________________________
  // --tree
  ana.input_tree_name = result["tree"].as<std::string>();

  //_______________________________________________________________________________
  // --debug
  if (result.count("debug")) {
    ana.output_tfile = new TFile("debug.root", "recreate");
  } else {
    //_______________________________________________________________________________
    // --output
    if (result.count("output")) {
      ana.output_tfile = new TFile(result["output"].as<std::string>().c_str(), "create");
      if (not ana.output_tfile->IsOpen()) {
        std::cout << options.help() << std::endl;
        std::cout << "ERROR: output already exists! provide new output name or delete old file. OUTPUTFILE="
                  << result["output"].as<std::string>() << std::endl;
        exit(1);
      }
    } else {
      std::cout
          << "Warning: Output file name is not provided! Check your arguments. Output file will be set to 'debug.root'"
          << std::endl;
      ana.output_tfile = new TFile("debug.root", "recreate");
    }
  }

  //_______________________________________________________________________________
  // --ptCut
  ana.ptCut = result["ptCut"].as<float>();

  //_______________________________________________________________________________
  // --clustSizeCut
  ana.clustSizeCut = result["clustSizeCut"].as<uint16_t>();

  //_______________________________________________________________________________
  // --nmatch
  ana.nmatch_threshold = result["nmatch"].as<int>();

  //_______________________________________________________________________________
  // --nevents
  ana.n_events = result["nevents"].as<int>();
  ana.specific_event_index = result["event_index"].as<int>();

  //_______________________________________________________________________________
  // --pdg_id
  ana.pdg_id = result["pdg_id"].as<int>();

  //_______________________________________________________________________________
  // --nsplit_jobs
  if (result.count("nsplit_jobs")) {
    ana.nsplit_jobs = result["nsplit_jobs"].as<int>();
    if (ana.nsplit_jobs <= 0) {
      std::cout << options.help() << std::endl;
      std::cout << "ERROR: option string --nsplit_jobs" << ana.nsplit_jobs << " has zero or negative value!"
                << std::endl;
      std::cout << "I am not sure what this means..." << std::endl;
      exit(1);
    }
  } else {
    ana.nsplit_jobs = -1;
  }

  //_______________________________________________________________________________
  // --job_index
  if (result.count("job_index")) {
    ana.job_index = result["job_index"].as<int>();
    if (ana.job_index < 0) {
      std::cout << options.help() << std::endl;
      std::cout << "ERROR: option string --job_index" << ana.job_index << " has negative value!" << std::endl;
      std::cout << "I am not sure what this means..." << std::endl;
      exit(1);
    }
  } else {
    ana.job_index = -1;
  }

  // Sanity check for split jobs (if one is set the other must be set too)
  if (result.count("job_index") or result.count("nsplit_jobs")) {
    // If one is not provided then throw error
    if (not(result.count("job_index") and result.count("nsplit_jobs"))) {
      std::cout << options.help() << std::endl;
      std::cout << "ERROR: option string --job_index and --nsplit_jobs must be set at the same time!" << std::endl;
      exit(1);
    }
    // If it is set then check for sanity
    else {
      if (ana.job_index >= ana.nsplit_jobs) {
        std::cout << options.help() << std::endl;
        std::cout << "ERROR: --job_index >= --nsplit_jobs ! This does not make sense..." << std::endl;
        exit(1);
      }
    }
  }

  //_______________________________________________________________________________
  // --verbose
  ana.verbose = result["verbose"].as<int>();

  //_______________________________________________________________________________
  // --streams
  ana.streams = result["streams"].as<int>();

  //_______________________________________________________________________________
  // --mode
  ana.mode = result["mode"].as<int>();

  //_______________________________________________________________________________
  // --write_ntuple
  ana.do_write_ntuple = result["write_ntuple"].as<int>();

  //_______________________________________________________________________________
  // --optimization

  //_______________________________________________________________________________
  // --tc_pls_triplets
  ana.tc_pls_triplets = result["tc_pls_triplets"].as<bool>();

  //_______________________________________________________________________________
  // --no_pls_dupclean
  ana.no_pls_dupclean = result["no_pls_dupclean"].as<bool>();

  //_______________________________________________________________________________
  // --reduce_mem_by_full_precompute
  ana.reduce_mem_by_full_precompute = result["reduce_mem_by_full_precompute"].as<bool>();

  //_______________________________________________________________________________
  // --use_chain_tracking
  ana.use_chain_tracking = result["use_chain_tracking"].as<bool>();

  //_______________________________________________________________________________
  // --md
  ana.md_branches = result["md"].as<bool>() || result["allobj"].as<bool>();

  //_______________________________________________________________________________
  // --ls
  ana.ls_branches = result["ls"].as<bool>() || result["allobj"].as<bool>();

  //_______________________________________________________________________________
  // --t3
  ana.t3_branches = result["t3"].as<bool>() || result["allobj"].as<bool>();

  //_______________________________________________________________________________
  // --pls
  ana.pls_branches = result["pls"].as<bool>() || result["allobj"].as<bool>();

  //_______________________________________________________________________________
  // P1 RE-BASELINE: the algorithmic isDup snapshots inside LSTCore are pure bookkeeping for
  // the pLS branches above. Turn the LSTCore-side host copies on exactly when that block is
  // being written; every other build and every CMSSW job leaves them off and pays nothing.
  if (ana.pls_branches)
    setenv("LST_DUP_SNAPSHOTS", "1", 1);

  //_______________________________________________________________________________
  // --occ
  ana.occ_branches = result["occ"].as<bool>() || result["allobj"].as<bool>();

  //_______________________________________________________________________________
  // --t3dnn
  ana.t3dnn_branches = result["t3dnn"].as<bool>() || result["allobj"].as<bool>();

  //_______________________________________________________________________________
  // --jet (Not triggered by allobj since most files don't have jet info)
  ana.jet_branches = result["jet"].as<bool>();

  //_______________________________________________________________________________
  // --sim
  ana.extra_sim_branches = result["sim"].as<bool>() || result["allobj"].as<bool>();

  // Printing out the option settings overview
  std::cout << "=========================================================" << std::endl;
  std::cout << " Running for Acc = " << alpaka::getAccName<ALPAKA_ACCELERATOR_NAMESPACE::Acc3D>() << std::endl;
  std::cout << " Setting of the analysis job based on provided arguments " << std::endl;
  std::cout << "---------------------------------------------------------" << std::endl;
  std::cout << " ana.input_file_list_tstring: " << ana.input_file_list_tstring << std::endl;
  std::cout << " ana.output_tfile: " << ana.output_tfile->GetName() << std::endl;
  std::cout << " ana.n_events: " << ana.n_events << std::endl;
  std::cout << " ana.ptCut: " << ana.ptCut << std::endl;
  std::cout << " ana.clustSizeCut: " << ana.clustSizeCut << std::endl;
  std::cout << " ana.nsplit_jobs: " << ana.nsplit_jobs << std::endl;
  std::cout << " ana.job_index: " << ana.job_index << std::endl;
  std::cout << " ana.specific_event_index: " << ana.specific_event_index << std::endl;
  std::cout << " ana.do_write_ntuple: " << ana.do_write_ntuple << std::endl;
  std::cout << " ana.mode: " << ana.mode << std::endl;
  std::cout << " ana.streams: " << ana.streams << std::endl;
  std::cout << " ana.verbose: " << ana.verbose << std::endl;
  std::cout << " ana.nmatch_threshold: " << ana.nmatch_threshold << std::endl;
  std::cout << " ana.tc_pls_triplets: " << ana.tc_pls_triplets << std::endl;
  std::cout << " ana.no_pls_dupclean: " << ana.no_pls_dupclean << std::endl;
  std::cout << " ana.reduce_mem_by_full_precompute: " << ana.reduce_mem_by_full_precompute << std::endl;
  std::cout << " ana.use_chain_tracking: " << ana.use_chain_tracking << std::endl;
  std::cout << "=========================================================" << std::endl;

  // Create the TChain that holds the TTree's of the baby ntuples
  ana.events_tchain = RooUtil::FileUtil::createTChain(ana.input_tree_name, ana.input_file_list_tstring);
  ana.looper.init(ana.events_tchain, &trk, ana.n_events);
  ana.looper.setSilent();

  // Set the cutflow object output file
  ana.cutflow.setTFile(ana.output_tfile);

  // Create ttree to output to the ana.output_tfile
  ana.output_ttree = new TTree("tree", "tree");

  // Create TTreeX instance that will take care of the interface part of TTree
  ana.tx = new RooUtil::TTreeX(ana.output_ttree);

  // Write metadata related to this run
  writeMetaData();

  // Run the code
  run_lst();

  return 0;
}

//___________________________________________________________________________________________________________________________________________________________________________________________
void run_lst() {
  ALPAKA_ACCELERATOR_NAMESPACE::Device devAcc = alpaka::getDevByIdx(ALPAKA_ACCELERATOR_NAMESPACE::Platform{}, 0u);
  std::vector<ALPAKA_ACCELERATOR_NAMESPACE::Queue> queues;
  for (int s = 0; s < ana.streams; s++) {
    queues.push_back(ALPAKA_ACCELERATOR_NAMESPACE::Queue(devAcc));
  }

  // Load various maps used in the lst reconstruction
  TStopwatch full_timer;
  full_timer.Start();
  // Determine which maps to use based on given pt cut for standalone.
  std::string ptCutString = (ana.ptCut >= 0.8) ? "0.8" : "0.6";
  auto hostESData = lst::loadAndFillESDataHost(ptCutString);
  auto deviceESData =
      cms::alpakatools::CopyToDevice<lst::LSTESData<alpaka_common::DevHost>>::copyAsync(queues[0], *hostESData.get());
  float timeForMapLoading = full_timer.RealTime() * 1000;

  if (ana.do_write_ntuple) {
    createOutputBranches();
  }

  std::vector<LSTInputHostCollection> out_lstInputHC;
  std::vector<int> evt_num;
  std::vector<TString> file_name;

  // Backwards compatibility
  const auto hasClustSize = trk.contains("ph2_clustSize");
  if (ana.verbose >= 1) {
    std::cout << "hasClustSize = " << hasClustSize << std::endl;
  }

  // Looping input file
  full_timer.Reset();
  full_timer.Start();
  while (ana.looper.nextEvent()) {
    if (ana.verbose >= 1)
      std::cout << "PreLoading event number = " << ana.looper.getCurrentEventIndex() << std::endl;

    if (not goodEvent())
      continue;

    // Backwards compatibility
    const auto trk_ph2_clustSize =
        hasClustSize ? trk.getVUS("ph2_clustSize") : std::vector<uint16_t>(trk.getVF("ph2_x").size());

    // Chain-tracking attach (P2.4) needs the global position of every seed's INNERMOST rec hit;
    // it is not derivable from the LST hits SoA, whose pixel rows carry trajectory quantities. The
    // resolution here is the same one the ntuple writer uses for its pLS_hit0_* branches.
    std::vector<float> see_hit0X, see_hit0Y, see_hit0Z;
    {
      auto const& see_hitIdx = trk.getVVI("see_hitIdx");
      auto const& see_hitType = trk.getVVI("see_hitType");
      auto const& pix_x = trk.getVF("pix_x");
      auto const& pix_y = trk.getVF("pix_y");
      auto const& pix_z = trk.getVF("pix_z");
      auto const& ph2_x = trk.getVF("ph2_x");
      auto const& ph2_y = trk.getVF("ph2_y");
      auto const& ph2_z = trk.getVF("ph2_z");
      see_hit0X.resize(see_hitIdx.size(), 0.f);
      see_hit0Y.resize(see_hitIdx.size(), 0.f);
      see_hit0Z.resize(see_hitIdx.size(), 0.f);
      for (size_t iSeed = 0; iSeed < see_hitIdx.size(); ++iSeed) {
        if (see_hitIdx[iSeed].empty())
          continue;
        int const h = see_hitIdx[iSeed][0];
        bool const isPixel = static_cast<lst::HitType>(see_hitType[iSeed][0]) == lst::HitType::Pixel;
        see_hit0X[iSeed] = isPixel ? pix_x[h] : ph2_x[h];
        see_hit0Y[iSeed] = isPixel ? pix_y[h] : ph2_y[h];
        see_hit0Z[iSeed] = isPixel ? pix_z[h] : ph2_z[h];
      }
    }

    auto lstInputHC = prepareInput(trk.getVF("see_px"),
                                   trk.getVF("see_py"),
                                   trk.getVF("see_pz"),
                                   trk.getVF("see_dxy"),
                                   trk.getVF("see_dz"),
                                   trk.getVF("see_ptErr"),
                                   trk.getVF("see_etaErr"),
                                   trk.getVF("see_stateTrajGlbX"),
                                   trk.getVF("see_stateTrajGlbY"),
                                   trk.getVF("see_stateTrajGlbZ"),
                                   trk.getVF("see_stateTrajGlbPx"),
                                   trk.getVF("see_stateTrajGlbPy"),
                                   trk.getVF("see_stateTrajGlbPz"),
                                   trk.getVI("see_q"),
                                   trk.getVVI("see_hitIdx"),
                                   trk.getVVI("see_hitType"),
                                   trk.getVU("see_algo"),
                                   trk.getVU("ph2_detId"),
                                   trk_ph2_clustSize,
                                   trk.getVF("ph2_x"),
                                   trk.getVF("ph2_y"),
                                   trk.getVF("ph2_z"),
                                   see_hit0X,
                                   see_hit0Y,
                                   see_hit0Z,
                                   ana.ptCut,
                                   queues[0]);

    out_lstInputHC.push_back(std::move(lstInputHC));

    evt_num.push_back(ana.looper.getCurrentEventIndex());
    file_name.push_back(ana.looper.getCurrentFileName());
  }
  float timeForInputLoading = full_timer.RealTime() * 1000;

  full_timer.Reset();
  full_timer.Start();
  std::vector<LSTEvent *> events;
  std::vector<ALPAKA_ACCELERATOR_NAMESPACE::Queue *> event_queues;
  for (int s = 0; s < ana.streams; s++) {
    LSTEvent *event = new LSTEvent(ana.verbose >= 2,
                                   ana.ptCut,
                                   ana.clustSizeCut,
                                   queues[s],
                                   &deviceESData,
                                   ana.reduce_mem_by_full_precompute,
                                   ana.use_chain_tracking);
    events.push_back(event);
    event_queues.push_back(&queues[s]);
  }
  float timeForEventCreation = full_timer.RealTime() * 1000;

  std::vector<std::vector<float>> timevec;
  full_timer.Reset();
  full_timer.Start();
  float full_elapsed = 0;
#pragma omp parallel num_threads(ana.streams)  // private(event)
  {
    std::vector<std::vector<float>> timing_information;
    float timing_input_loading;
    float timing_MD;
    float timing_LS;
    float timing_T3;
    float timing_pLS;
    float timing_TC;

#pragma omp for  // nowait// private(event)
    for (int evt = 0; evt < static_cast<int>(out_lstInputHC.size()); evt++) {
      if (ana.verbose >= 1)
        std::cout << "Running Event number = " << evt << " " << omp_get_thread_num() << std::endl;

      events.at(omp_get_thread_num())->initSync();

      // We need to initialize it here so that it stays in scope
      auto &queue = *event_queues.at(omp_get_thread_num());
      LSTInputDeviceCollection lstInputDC(queue, out_lstInputHC.at(evt)->metadata().size());

      timing_input_loading =
          addInputsToEventPreLoad(events.at(omp_get_thread_num()), &out_lstInputHC.at(evt), &lstInputDC, queue);

      timing_MD = runMiniDoublet(events.at(omp_get_thread_num()), evt);
      timing_LS = runSegment(events.at(omp_get_thread_num()));
      timing_T3 = runT3(events.at(omp_get_thread_num()));
      timing_pLS = runPixelLineSegment(events.at(omp_get_thread_num()), ana.no_pls_dupclean);
      timing_TC = runTrackCandidate(events.at(omp_get_thread_num()), ana.no_pls_dupclean, ana.tc_pls_triplets);
      // Chain-stage attribution: the graph build (incidence + edges + weld + gate) ran inside the
      // T3 stage and the chain TC work (K9 + attach + CC + XC + emission + retirement) inside the
      // TC stage; both are re-attributed to their own columns.
      float const timing_chain_graph = static_cast<float>(events.at(omp_get_thread_num())->getChainBuildMs()) / 1000.f;
      float const timing_chain_tc = static_cast<float>(events.at(omp_get_thread_num())->getChainTCMs()) / 1000.f;
      timing_T3 -= timing_chain_graph;
      timing_TC -= timing_chain_tc;

      if (ana.verbose == 4) {
#pragma omp critical
        {
          // TODO BROKEN //
          // printAllObjects(events.at(omp_get_thread_num()));
        }
      }

      if (ana.verbose == 5) {
#pragma omp critical
        {
          // TODO: debugPrintOutlierMultiplicities
        }
      }

      if (ana.do_write_ntuple) {
#pragma omp critical
        {
          unsigned int trkev = evt_num.at(evt);
          TString fname = file_name.at(evt);
          TFile *f = TFile::Open(fname.Data(), "open");
          TTree *t = (TTree *)f->Get(ana.input_tree_name.Data());
          trk.Init(t);
          trk.GetEntry(trkev);
          fillOutputBranches(events.at(omp_get_thread_num()));
          f->Close();
        }
      }

      // Clear this event
      TStopwatch my_timer;
      my_timer.Start();
      events.at(omp_get_thread_num())->resetEventSync();
      float timing_resetEvent = my_timer.RealTime();

      timing_information.push_back({timing_input_loading,
                                    timing_MD,
                                    timing_LS,
                                    timing_T3,
                                    timing_chain_graph,
                                    timing_pLS,
                                    timing_chain_tc,
                                    timing_TC,
                                    timing_resetEvent});
    }

    full_elapsed =
        full_timer.RealTime() *
        1000.f;  // for loop has implicit barrier I think. So this stops onces all cpu threads have finished but before the next critical section.
#pragma omp critical
    timevec.insert(timevec.end(), timing_information.begin(), timing_information.end());
  }

  float avg_elapsed = full_elapsed / out_lstInputHC.size();

  std::cout << "Time for map loading = " << timeForMapLoading << " ms\n";
  std::cout << "Time for input loading = " << timeForInputLoading << " ms\n";
  std::cout << "Time for event creation = " << timeForEventCreation << " ms\n";
  printTimingInformation(timevec, full_elapsed, avg_elapsed);

  if (ana.do_write_ntuple) {
    // Writing ttree output to file
    ana.output_tfile->cd();
    ana.cutflow.saveOutput();
    ana.output_ttree->Write();
  }

  for (int s = 0; s < ana.streams; s++) {
    delete events.at(s);
  }

  delete ana.output_tfile;
}

// Dummy implementation of edm::typeDemangle (without extra replacements)
// to avoid having to link extra libraries
namespace edm {
  std::string typeDemangle(char const *mangledName) { return boost::core::demangle(mangledName); }
}  // namespace edm

// Dummy implementation of SoACommon.cc from DataFormats/SoATemplate/src
// to avoid having to link extra libraries
namespace cms::soa::detail {
  [[noreturn]] void throwRuntimeError(const char *message) { throw std::runtime_error(message); }

  [[noreturn]] void throwOutOfRangeError(const char *message, cms::soa::size_type index, cms::soa::size_type range) {
    throw std::out_of_range(std::format("{}: index {} out of range {}", message, index, range));
  }
}  // namespace cms::soa::detail
