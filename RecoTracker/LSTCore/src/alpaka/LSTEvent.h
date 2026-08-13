#ifndef RecoTracker_LSTCore_src_alpaka_LSTEvent_h
#define RecoTracker_LSTCore_src_alpaka_LSTEvent_h

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include "RecoTracker/LSTCore/interface/LSTInputHostCollection.h"
#include "RecoTracker/LSTCore/interface/ChainEdgesHostCollection.h"
#include "RecoTracker/LSTCore/interface/ChainIncidenceHostCollection.h"
#include "RecoTracker/LSTCore/interface/ChainNodesHostCollection.h"
#include "RecoTracker/LSTCore/interface/ChainsHostCollection.h"
#include "RecoTracker/LSTCore/interface/ChainConfig.h"
#include "RecoTracker/LSTCore/interface/HitsHostCollection.h"
#include "RecoTracker/LSTCore/interface/MiniDoubletsHostCollection.h"
#include "RecoTracker/LSTCore/interface/SegmentsHostCollection.h"
#include "RecoTracker/LSTCore/interface/PixelSegmentsHostCollection.h"
#include "RecoTracker/LSTCore/interface/TrackCandidatesHostCollection.h"
#include "RecoTracker/LSTCore/interface/TripletsHostCollection.h"
#include "RecoTracker/LSTCore/interface/ObjectRangesHostCollection.h"
#include "RecoTracker/LSTCore/interface/ModulesHostCollection.h"
#include "RecoTracker/LSTCore/interface/alpaka/Common.h"
#include "RecoTracker/LSTCore/interface/alpaka/LST.h"
#include "RecoTracker/LSTCore/interface/alpaka/LSTInputDeviceCollection.h"
#include "RecoTracker/LSTCore/interface/alpaka/ChainEdgesDeviceCollection.h"
#include "RecoTracker/LSTCore/interface/alpaka/ChainIncidenceDeviceCollection.h"
#include "RecoTracker/LSTCore/interface/alpaka/ChainNodesDeviceCollection.h"
#include "RecoTracker/LSTCore/interface/alpaka/ChainsDeviceCollection.h"
#include "RecoTracker/LSTCore/interface/alpaka/HitsDeviceCollection.h"
#include "RecoTracker/LSTCore/interface/alpaka/MiniDoubletsDeviceCollection.h"
#include "RecoTracker/LSTCore/interface/alpaka/SegmentsDeviceCollection.h"
#include "RecoTracker/LSTCore/interface/alpaka/PixelSegmentsDeviceCollection.h"
#include "RecoTracker/LSTCore/interface/alpaka/TrackCandidatesDeviceCollection.h"
#include "RecoTracker/LSTCore/interface/alpaka/TripletsDeviceCollection.h"
#include "RecoTracker/LSTCore/interface/alpaka/ModulesDeviceCollection.h"
#include "RecoTracker/LSTCore/interface/alpaka/ObjectRangesDeviceCollection.h"
#include "RecoTracker/LSTCore/interface/alpaka/EndcapGeometryDevDeviceCollection.h"

#include "HeterogeneousCore/AlpakaInterface/interface/host.h"
#include "HeterogeneousCore/AlpakaInterface/interface/memory.h"

namespace ALPAKA_ACCELERATOR_NAMESPACE::lst {

  // Chain-tracking P2.4 scratch record types; defined in src/alpaka/ChainAttach.h, which only the
  // implementation translation unit needs to see.
  struct AttachPlsPre;
  struct AttachTargetPre;
  struct ChainXcPair;

  class LSTEvent {
  private:
    Queue& queue_;
    const float ptCut_;
    const uint16_t clustSizeCut_;
    const bool reduceMemByFullPrecompute_;
    // CHAINFINAL2 chain-tracking configuration (the only track-building path). Supplied by
    // LSTProducer's grouped chainTracking PSets in CMSSW and left at the ChainConfig.h defaults
    // in the standalone.
    ChainConfig chainConfig_;

    std::array<unsigned int, 6> n_minidoublets_by_layer_barrel_{};
    std::array<unsigned int, 5> n_minidoublets_by_layer_endcap_{};
    std::array<unsigned int, 6> n_segments_by_layer_barrel_{};
    std::array<unsigned int, 5> n_segments_by_layer_endcap_{};
    std::array<unsigned int, 6> n_triplets_by_layer_barrel_{};
    std::array<unsigned int, 5> n_triplets_by_layer_endcap_{};
    unsigned int nTotalSegments_;
    unsigned int pixelSize_;
    uint16_t pixelModuleIndex_;
    unsigned int nChainNodes_ = 0;        // dense triplet-node count (K0)
    unsigned int nChainE1Edges_ = 0;      // exact MD-keyed edge count (K1b)
    unsigned int nChainE2Edges_ = 0;      // exact LS-keyed edge count (K1b)
    // The SAME two counts in 64 bit, and the ONLY versions the allocation guard is allowed to read.
    // `nEdgesExact` is a uint32 accumulation, so above 2^32 edges the two members above hold a
    // WRAPPED value and a guard fed one of them would pass a corrupting event (FINDINGS_JET.md
    // ceiling 3). These are equal to them whenever the host-side exactness certificate holds and
    // come from the K1b phase-2 64-bit recount otherwise; the guard vetoes on any inequality.
    uint64_t nChainE1Edges64_ = 0;
    uint64_t nChainE2Edges64_ = 0;
    unsigned int nChainCount_ = 0;        // welded chain count (K6d)
    unsigned int nChainWeldedNodes_ = 0;  // total member nodes over all chains (K6d)
    // Frozen chain-tracking configuration. P2.3 will fill this from the producer parameter set;

    //Device stuff
    LSTInputDeviceCollection const* lstInputDC_;  // not owned
    std::optional<ObjectRangesDeviceCollection> rangesDC_;
    std::optional<HitsDeviceCollection> hitsDC_;
    std::optional<MiniDoubletsDeviceCollection> miniDoubletsDC_;
    std::optional<SegmentsDeviceCollection> segmentsDC_;
    std::optional<PixelSegmentsDeviceCollection> pixelSegmentsDC_;
    std::optional<TripletsDeviceCollection> tripletsDC_;
    std::optional<TrackCandidatesBaseDeviceCollection> trackCandidatesBaseDC_;
    std::optional<TrackCandidatesExtendedDeviceCollection> trackCandidatesExtendedDC_;
    // Chain-tracking graph state, only allocated when useChainTracking_ is true.
    std::optional<ChainIncidenceDeviceCollection> chainMdIncidenceDC_;  // keyed by MiniDoublet index
    std::optional<ChainIncidenceDeviceCollection> chainLsIncidenceDC_;  // keyed by Segment index
    std::optional<ChainNodesDeviceCollection> chainNodesDC_;
    std::optional<ChainEdgesDeviceCollection> chainEdgesDC_;
    std::optional<ChainsDeviceCollection> chainsDC_;
    std::optional<ChainItemsDeviceCollection> chainItemsDC_;

    //CPU interface stuff
    std::optional<LSTInputHostCollection> lstInputHC_;
    std::optional<ObjectRangesHostCollection> rangesHC_;
    std::optional<HitsHostCollection> hitsHC_;
    std::optional<MiniDoubletsHostCollection> miniDoubletsHC_;
    std::optional<SegmentsHostCollection> segmentsHC_;
    std::optional<PixelSegmentsHostCollection> pixelSegmentsHC_;
    std::optional<TripletsHostCollection> tripletsHC_;
    std::optional<TrackCandidatesBaseHostCollection> trackCandidatesBaseHC_;
    std::optional<TrackCandidatesExtendedHostCollection> trackCandidatesExtendedHC_;
    std::optional<ModulesHostCollection> modulesHC_;
    // Chain tracking (P2.3): a chain TC's pt / eta / phi live in ChainsSoA rather than in any
    // object the TC row points at, so the ntuple writer needs a host view of the chains.
    std::optional<ChainsHostCollection> chainsHC_;

    const uint16_t nModules_;
    const uint16_t nLowerModules_;
    const unsigned int nPixels_;
    const unsigned int nEndCapMap_;
    ModulesDeviceCollection const& modules_;
    PixelMap const& pixelMapping_;
    EndcapGeometryDevDeviceCollection const& endcapGeometry_;
    bool objectsStatistics_ = false;
    double memoryAllocatedMB_ = 0;
    // -v 1 stage attribution for the standalone timing table: the chain GRAPH work inside
    // createTriplets (incidence + edges + weld + gate) and the chain TC work inside
    // createTrackCandidates (K9 + attach + CC + XC + emission + retirement).
    double chainBuildMs_ = 0.;
    double chainTCMs_ = 0.;

  public:
    // Constructor used for CMSSW integration. Uses an external queue.
    LSTEvent(bool verbose,
             const float ptCut,
             const uint16_t clustSizeCut,
             Queue& q,
             const LSTESData<Device>* deviceESData,
             bool reduce_mem_by_full_precompute,
             ChainConfig const& chain_config = ChainConfig{})
        : queue_(q),
          ptCut_(ptCut),
          clustSizeCut_(clustSizeCut),
          reduceMemByFullPrecompute_(reduce_mem_by_full_precompute),
          chainConfig_(chain_config),
          nModules_(deviceESData->nModules),
          nLowerModules_(deviceESData->nLowerModules),
          nPixels_(deviceESData->nPixels),
          nEndCapMap_(deviceESData->nEndCapMap),
          modules_(*deviceESData->modules),
          pixelMapping_(*deviceESData->pixelMapping),
          endcapGeometry_(*deviceESData->endcapGeometry),
          objectsStatistics_(verbose) {
      // JET ROUND 2 (D): env overrides of the bare-seed retirement bars only. Inert with nothing
      // set (see interface/ChainConfig.h).
      chainConfigRetirementEnv(chainConfig_);
      // JET ROUND 3 (T4): env overrides of the 4-layer class policy group. Inert with nothing set.
      chainConfigT4Env(chainConfig_);
      // COORDINATOR PROBE: terminal-trim A/B. Inert with nothing set.
      chainConfigTrimEnv(chainConfig_);
      if (ptCut < 0.6f) {
        throw std::invalid_argument("Minimum pT cut must be at least 0.6 GeV. Provided value: " +
                                    std::to_string(ptCut));
      }
    }
    void initSync();        // synchronizes, for standalone usage
    void resetEventSync();  // synchronizes, for standalone usage
    void wait() const { alpaka::wait(queue_); }

    void addInputToEvent(LSTInputDeviceCollection const* lstInputDC);
    // Calls the appropriate hit function, then increments the counter
    void addHitToEvent();
    void addPixelSegmentToEventStart();

    void createMiniDoublets();
    void addPixelSegmentToEventFinalize();
    void createSegmentsWithModuleMap();
    void createTriplets();
    void createTrackCandidates(bool no_pls_dupclean, bool tc_pls_triplets);
    void pixelLineSegmentCleaning(bool no_pls_dupclean);

    // Chain-tracking phase P2.0: K0 triplet compaction plus the K1b/K1c incidence CSR build.
    // Only called when useChainTracking_ is true; writes nothing any other stage reads.
    // The two per-module raw->dense key bias arrays are built in createTriplets (P2.6c).
    void buildChainIncidence(uint32_t const* chainMdKeyBias, uint32_t const* chainLsKeyBias);
    // Zeroes the incidence tally columns; they are the K1a counters and then the K1c cursors.
    void resetChainIncidenceCounts();
    // Host-side verification of the CSR invariants, run under the verbose statistics path.
    void chainIncidenceStatistics();

    // Chain-tracking phase P2.1: K2 edge enumeration, K3 node features, K5 edge-MLP inference.
    // Only called when useChainTracking_ is true; nothing downstream consumes ChainEdges yet.
    void buildChainEdges();
    // Optional parity sidecar, enabled by the LST_CHAIN_EDGE_DUMP environment variable. Writes
    // the per-event edge set and logits to a binary file and touches no ntuple branch.
    void dumpChainEdges();
    // Optional determinism sidecar, enabled by the LST_CHAIN_NODE_DUMP environment variable. Writes
    // the per-event node identity table (stableId plus the six hit rows it is built from), which is
    // what the P2.5 weld-tie uniqueness census and the CPU-vs-GPU node-set attribution consume.
    void dumpChainNodes();

    // Chain-tracking phase P2.2: K6a-K6f weld + terminal trim, K7a-K7c chain features + 3-class
    // gate + the -G 6 kill. Only called when useChainTracking_ is true; NOTHING consumes the kill
    // bit yet, so this phase is output-neutral like P2.0 and P2.1.
    void buildChains();

    // Chain-tracking phase P2.3: K9 hit-claim arbitration, chain extension and K10 assembly, plus
    // the carried-row compaction that retires the classes the chains replace. THIS IS THE FIRST
    // PHASE THAT CHANGES THE TRACK CANDIDATE COLLECTION. Called at the end of
    // createTrackCandidates, only when useChainTracking_ is true.
    void arbitrateChains(unsigned int nAllocatedTCs);

    // Chain-tracking phase P2.4: K8 pixel attach, stage A (chain targets). Runs inside
    // arbitrateChains, on the K9-accepted chains and before the extension. Builds the
    // invariant-keyed grid prefilter, scores the (chain, pLS) candidates with the r2 pair head
    // under the banded -a margins (for the stage-A 5+ targets and the score-only 4-layer -XC4
    // tail alike), and resolves the one-pLS-one-owner contention and the -RD seed-family dedup.
    // The type-7 upgrade itself is applied by ChainEmitTCs; the carried-row retirement is the
    // FINAL pass of arbitrateChains.
    void attachPixels(unsigned int nHits,
                      uint32_t const* accepted,
                      AttachPlsPre const* plsPre,
                      uint8_t* plsOwned,
                      uint32_t* plsBestChain,
                      uint32_t* hashKey,
                      int32_t* hashVal,
                      ChainXcPair* xcPairs,
                      uint32_t* xcCursor,
                      uint32_t xcCap,
                      uint8_t* plsMutual);
    // THE ONE K8a GRID, built from the UNION of the two stages' per-r-bin radial hulls and shared
    // by BOTH attach stages. It used to be built twice -- same plsPre, same ChainConfig, same cell
    // layout, only a different hull -- which cost a second GridCount + prefix + GridScatter, a
    // second 96-byte-per-entry payload write, and a second mid-stream device->host drain. A union
    // hull is a SUPERSET generator, and both scorers apply the exact analytic predicate before any
    // observable write, so the extra candidates are re-filtered identically (ChainAttachGridBounds
    // carries the argument). Built by buildAttachGrid, released in arbitrateChains right after the
    // second stage has consumed it.
    void buildAttachGrid(AttachPlsPre const* plsPre,
                         AttachTargetPre const* tgtA,
                         uint32_t nA,
                         AttachTargetPre const* tgtB,
                         uint32_t nB);
    std::optional<cms::alpakatools::device_buffer<Device, uint32_t[]>> attachGridOffs_;
    std::optional<cms::alpakatools::device_buffer<Device, AttachPlsPre[]>> attachGridItems_;
    uint32_t attachGridEntries_ = 0;
    // The buildAttachGrid sub-timings, formatted for the [CHAIN K8] printout.
    std::string attachGridSummary_;
    double attachGridMs_ = 0.;
    // Env-gated (LST_CHAIN_ATTACH_AUDIT) grid-vs-exhaustive-scan superset verification.
    void attachGridAudit(unsigned int nTargets,
                         AttachPlsPre const* plsPre,
                         AttachTargetPre const* tgtPre,
                         uint32_t const* offsets,
                         AttachPlsPre const* items);
    // Per-event attach counters, formatted for the [CHAIN K8] printout.
    std::string attachSummary_;

    // ---- Stage B of the general attach (src/alpaka/ChainAttachT3.h): the pT3-class delivery ---
    // Builds the bare-T3 target universe (K9-accepted bareness + the -T3F fake gate), its own
    // grid, scores it against the LIVE ownership array, resolves the stage-B contention and the
    // -RDT dedup, and records the bare-T3 retirement evidence in plsBestT3. The delivery itself
    // (the -CC contention sweep + type-5 emission) runs later in arbitrateChains, after the chain
    // rows are emitted; the owner arrays below stay alive in between.
    void attachBareT3(unsigned int nHits,
                      AttachPlsPre const* plsPre,
                      uint8_t* plsOwned,
                      uint32_t* plsBestT3,
                      uint32_t* hashKey,
                      int32_t* hashVal);
    // The stage-B TARGET UNIVERSE, split out of attachBareT3 so it can run BEFORE stage A: the
    // shared grid's hull needs both target sets, and this half depends only on the K9 accepted
    // array (never on stage A's verdicts), so moving it earlier changes nothing it computes.
    void prepareBareT3Targets(uint32_t const* accepted);
    std::optional<cms::alpakatools::device_buffer<Device, AttachTargetPre[]>> bareT3TgtPre_;
    double bareT3PreMs_ = 0.;
    std::string attachT3Summary_;
    // Stage-B owner state, device-resident, alive from attachBareT3 until the -CC sweep consumes
    // it (ChainT3CCSweepEmit). targets = dense node index per bare-T3 target; tgtPls / tgtLogit =
    // the per-target pick after contention and -RDT.
    std::optional<cms::alpakatools::device_buffer<Device, uint32_t[]>> bareT3Targets_;
    std::optional<cms::alpakatools::device_buffer<Device, int32_t[]>> bareT3TgtPls_;
    std::optional<cms::alpakatools::device_buffer<Device, float[]>> bareT3TgtLogit_;
    // keep[pos] = 1 iff position pos is a DELIVERY (stage-B owner that survived the -RDT dedup);
    // the -CC sweep's gather+rank reads it.
    std::optional<cms::alpakatools::device_buffer<Device, uint32_t[]>> bareT3Keep_;
    // The owner tag behind each -RD table entry. PERSISTENT, allocated once on first use and reused
    // every event, because AN EXTRA PER-EVENT DEVICE BUFFER IN THIS BLOCK IS EXPENSIVE ON THE CPU
    // BACKEND: measured +26 ms/event, all of it in pLS and TC, i.e. in stages this code never
    // touches, via the caching allocator evicting buffers those stages were reusing. Its size is
    // chainattach::kSeedHashSlots, a compile-time constant, so there is nothing to resize. It needs
    // no initialising either: an entry is only read at a slot whose key matched, and the thread that
    // claimed that key wrote the tag first. See gpu_wt/g3/T3_STATUS.md and FINDINGS_GPU.md [T3 10:20].
    std::optional<cms::alpakatools::device_buffer<Device, uint32_t[]>> rdHashOwner_;
    uint32_t nBareT3_ = 0;

    // ---- P1 RE-BASELINE INSTRUMENT: ALGORITHMIC DUPLICATE-FLAG SNAPSHOTS -------------------
    // BOOKKEEPING ONLY. Each vector is a host copy of one device isDup column, taken at the
    // moment BEFORE a later kernel overwrites it. Nothing in the algorithm ever reads these
    // back; no cut, threshold or kernel behaviour depends on them. They exist because the
    // end-of-run collection cannot show the earlier states: CrossCleanpLS writes isDup = true
    // and clobbers the 1 / 2 bitmask that the two CheckHitspLS self-cleaning passes wrote.
    // Filled only when dupSnapshotsEnabled() (env LST_DUP_SNAPSHOTS, set by the standalone
    // driver for --allobj); otherwise every vector stays empty and no copy is made.
    std::vector<char> plsIsDupSelf_;   // end of pixelLineSegmentCleaning (CheckHitspLS pass 1)
    std::vector<char> plsIsDupPass2_;  // after the second CheckHitspLS (both self-clean passes)
    std::vector<char> plsIsDupFinal_;  // after CrossCleanpLS, before AddpLSasTrackCandidate

    // Env-gated (LST_CHAIN_TC_DUMP) TC-level parity sidecar; writes nothing otherwise.
    void dumpChainTCs();
    // Optional parity sidecar, enabled by the LST_CHAIN_CHAIN_DUMP environment variable.
    void dumpChains();
    // TRIM-NN probe sidecar, enabled by LST_CHAIN_VARIANT_DUMP: one P22C record per TERMINAL
    // VARIANT of every chain with nNodes >= 3, taken PRE-TRIM. Writes nothing otherwise.
    void dumpChainVariants(char const* path, uint32_t const* innerMDDev, float const* probeDev);

    unsigned int getNumberOfChains() const { return nChainCount_; }
    double getChainBuildMs() const { return chainBuildMs_; }
    double getChainTCMs() const { return chainTCMs_; }
    unsigned int getNumberOfChainNodes() const { return nChainNodes_; }
    unsigned int getNumberOfChainE1Edges() const { return nChainE1Edges_; }
    unsigned int getNumberOfChainE2Edges() const { return nChainE2Edges_; }

    // functions that map the objects to the appropriate modules
    void addMiniDoubletsToEventExplicit();
    void addSegmentsToEventExplicit();
    void addTripletsToEventExplicit();
    void resetObjectsInModule();

    unsigned int getNumberOfMiniDoublets();
    unsigned int getNumberOfMiniDoubletsByLayerBarrel(unsigned int layer);
    unsigned int getNumberOfMiniDoubletsByLayerEndcap(unsigned int layer);

    unsigned int getNumberOfSegments();
    unsigned int getNumberOfSegmentsByLayerBarrel(unsigned int layer);
    unsigned int getNumberOfSegmentsByLayerEndcap(unsigned int layer);

    unsigned int getNumberOfTriplets();
    unsigned int getNumberOfTripletsByLayerBarrel(unsigned int layer);
    unsigned int getNumberOfTripletsByLayerEndcap(unsigned int layer);

    int getNumberOfTrackCandidates();
    int getNumberOfPT5TrackCandidates();
    int getNumberOfPT3TrackCandidates();
    int getNumberOfPLSTrackCandidates();
    int getNumberOfPixelTrackCandidates();
    int getNumberOfT5TrackCandidates();
    int getNumberOfT4TrackCandidates();

    double getMemoryAllocatedMB() const { return memoryAllocatedMB_; }

    // sync adds alpaka::wait at the end of filling a buffer during lazy fill
    // (has no effect on repeated calls)
    // set to false may allow faster operation with concurrent calls of get*
    // HANDLE WITH CARE
    template <typename TSoA, typename TDev = Device>
    typename TSoA::ConstView getInput(bool sync = true);
    template <typename TSoA, typename TDev = Device>
    typename TSoA::ConstView getHits(bool sync = true);
    template <typename TDev = Device>
    ObjectRangesConst getRanges(bool sync = true);
    template <typename TSoA, typename TDev = Device>
    typename TSoA::ConstView getMiniDoublets(bool sync = true);
    template <typename TSoA, typename TDev = Device>
    typename TSoA::ConstView getSegments(bool sync = true);
    template <typename TSoA, typename TDev = Device>
    typename TSoA::ConstView getTriplets(bool sync = true);
    template <typename TDev = Device>
    PixelSegmentsConst getPixelSegments(bool sync = true);
    template <typename TDev = Device>
    TrackCandidatesBaseConst getTrackCandidatesBase(bool sync = true);
    template <typename TDev = Device>
    TrackCandidatesExtendedConst getTrackCandidatesExtended(bool sync = true);
    template <typename TDev = Device>
    ChainsConst getChains(bool sync = true);
    std::unique_ptr<TrackCandidatesBaseDeviceCollection> releaseTrackCandidatesBaseDeviceCollection();
    template <typename TSoA, typename TDev = Device>
    typename TSoA::ConstView getModules(bool sync = true);
  };

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE::lst
#endif
