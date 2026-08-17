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

  // Attach scratch record types; defined in src/alpaka/ChainAttach.h, which only the implementation
  // translation unit needs to see.
  struct AttachPlsPre;
  struct AttachTargetPre;
  struct ChainXcPair;
  struct ChainAttachPairRow;

  class LSTEvent {
  private:
    Queue& queue_;
    const float ptCut_;
    const uint16_t clustSizeCut_;
    const bool reduceMemByFullPrecompute_;
    // Chain-tracking configuration (the only track-building path). Supplied by LSTProducer's
    // grouped chainTracking PSets in CMSSW, and left at the ChainConfig.h defaults standalone.
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
    unsigned int nChainNodes_ = 0;    // dense triplet-node count
    unsigned int nChainE1Edges_ = 0;  // exact MD-keyed edge count
    unsigned int nChainE2Edges_ = 0;  // exact LS-keyed edge count
    // The SAME two counts in 64 bit, and the ONLY versions the allocation guard is allowed to read.
    // The device-side accumulation is uint32, so above 2^32 edges the two members above hold a
    // WRAPPED value and a guard fed one of them would pass an event that goes on to corrupt memory.
    // These are equal to them whenever the host-side exactness certificate holds and come from the
    // 64-bit recount otherwise; the guard vetoes on any inequality between the two.
    uint64_t nChainE1Edges64_ = 0;
    uint64_t nChainE2Edges64_ = 0;
    unsigned int nChainCount_ = 0;        // welded chain count
    unsigned int nChainWeldedNodes_ = 0;  // total member nodes over all chains

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
    // Chain-tracking graph state: nodes and edges over the triplets, then the welded chains.
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
    // A chain candidate's pt / eta / phi live in ChainsSoA rather than in any object its row points
    // at, so the ntuple writer needs a host view of the chains.
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
    // Stage attribution for the standalone timing table: the chain GRAPH work inside createTriplets
    // (incidence, edges, weld, trim, gate) and the chain CANDIDATE work inside createTrackCandidates
    // (claim, attach, emission, contention, crossclean, retirement).
    double chainBuildMs_ = 0.;
    double chainTCMs_ = 0.;

  public:
    // Constructor used for CMSSW integration. Uses an external queue.
    LSTEvent(bool verbose,
             const float ptCut,
             const uint16_t clustSizeCut,
             Queue& queue,
             const LSTESData<Device>* deviceESData,
             bool reduce_mem_by_full_precompute,
             ChainConfig const& chain_config = ChainConfig{})
        : queue_(queue),
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
      // Environment overrides of three configuration groups: the bare-seed retirement bars, the
      // 4-layer class policy, and the terminal trim. Each is inert with nothing set, so the
      // configuration above is what runs unless the environment says otherwise. See ChainConfig.h.
      chainConfigRetirementEnv(chainConfig_);
      chainConfigT4Env(chainConfig_);
      chainConfigTrimEnv(chainConfig_);
      chainConfigGateEnv(chainConfig_);  // [ARM-GATELP] 5+-layer gate bars, inert with nothing set
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

    // Triplet compaction into dense chain nodes, plus the incidence CSR build. Called at the end of
    // createTriplets, which is also where the two per-module raw->dense key bias arrays are built.
    void buildChainIncidence(uint32_t const* chainMdKeyBias, uint32_t const* chainLsKeyBias);
    // Zeroes the incidence tally columns; they serve as the tally counters and then as the CSR
    // write cursors.
    void resetChainIncidenceCounts();
    // Host-side verification of the CSR invariants, run under the verbose statistics path.
    void chainIncidenceStatistics();

    // Node features, edge enumeration and the edge head. Produces no track candidate; ends by
    // calling buildChains.
    void buildChainEdges();
    // Optional sidecar, enabled by the LST_CHAIN_EDGE_DUMP environment variable. Writes the
    // per-event edge set and its log-odds to a binary file and touches no ntuple branch.
    void dumpChainEdges();
    // Optional determinism sidecar, enabled by the LST_CHAIN_NODE_DUMP environment variable. Writes
    // the per-event node identity table (stableId plus the six hit rows it is built from), which is
    // what the weld-tie uniqueness check and the backend node-set attribution consume.
    void dumpChainNodes();

    // Weld sweeps, chain emission, terminal trim, chain features and the 3-class gate. Produces no
    // track candidate: the claim is the first consumer of the gate's verdict.
    void buildChains();

    // Hit-claim arbitration, attach, assembly, and the carried-row compactions that retire the
    // classes the chains replace. THIS IS THE ONLY CHAIN STAGE THAT CHANGES THE TRACK CANDIDATE
    // COLLECTION; it is called at the end of createTrackCandidates and its header block in
    // LSTEvent.dev.cc lists the sub-stages in launch order.
    void arbitrateChains(unsigned int nAllocatedTCs);

    // Attach stage A: pixel seeds onto accepted chains. Runs inside arbitrateChains. Scores the
    // (chain, pLS) candidates the shared grid admits with the pair head under the eta-banded
    // delivery margins -- for the deliverable targets and the score-only 4-layer tail alike -- and
    // resolves the one-pLS-one-owner contention and the seed-family dedup. The candidate row
    // upgrade itself is applied by ChainEmitTCs; the carried-row retirement is the FINAL pass of
    // arbitrateChains, because it must see every later verdict.
    // `accepted` is mutable and `blockedBy`/`blockedOther` are consumed here because of the claim
    // rescue (ChainConfig::attachRescue): a claim-rejected chain that wins a seed may replace its
    // blocker's slot in the accepted list. With the rescue off both are read-only bookkeeping.
    void attachPixels(unsigned int nHits,
                      uint32_t* accepted,
                      int32_t const* blockedBy,
                      int32_t const* blockedOther,
                      int32_t const* blockedShared,  // [ARM-HDEEP]
                      AttachPlsPre const* plsPre,
                      uint8_t* plsOwned,
                      uint32_t* plsBestChain,
                      uint32_t* hashKey,
                      int32_t* hashVal,
                      ChainXcPair* xcPairs,
                      uint32_t* xcCursor,
                      uint32_t xcCap,
                      uint8_t* plsMutual);
    // THE ONE ATTACH GRID, built from the UNION of the two stages' per-r-bin radial hulls and
    // shared by BOTH attach stages. One grid rather than two is safe because a union hull is a
    // SUPERSET generator and both scorers apply the exact analytic predicate before any observable
    // write, so the extra candidates are re-filtered identically (ChainAttachGridBounds carries the
    // argument); building it per stage instead costs a second count + prefix + scatter, a second
    // 96-byte-per-entry payload write and a second mid-stream device->host drain. Released in
    // arbitrateChains as soon as the second stage has consumed it.
    void buildAttachGrid(AttachPlsPre const* plsPre,
                         AttachTargetPre const* targetsA,
                         uint32_t nTargetsA,
                         AttachTargetPre const* targetsB,
                         uint32_t nTargetsB);
    std::optional<cms::alpakatools::device_buffer<Device, uint32_t[]>> attachGridOffs_;
    std::optional<cms::alpakatools::device_buffer<Device, AttachPlsPre[]>> attachGridItems_;
    uint32_t attachGridEntries_ = 0;
    // The buildAttachGrid sub-timings, formatted for the attach printout.
    std::string attachGridSummary_;
    double attachGridMs_ = 0.;
    // Env-gated (LST_CHAIN_ATTACH_AUDIT) grid-vs-exhaustive-scan superset verification.
    void attachGridAudit(unsigned int nTargets,
                         AttachPlsPre const* plsPre,
                         AttachTargetPre const* tgtPre,
                         uint32_t const* offsets,
                         AttachPlsPre const* items);
    // Per-event attach counters, formatted for the attach printout.
    std::string attachSummary_;

    // ---- Attach stage B (src/alpaka/ChainAttachT3.h): the pT3-class delivery -------------------
    // Scores the bare-triplet target universe against the LIVE ownership array stage A left behind,
    // resolves the stage-B contention and its half of the seed dedup, and records this stage's
    // retirement evidence in plsBestT3. The delivery itself (the hit-overlap contention sweep and
    // the row emission) runs later in arbitrateChains, after the chain rows exist; the owner arrays
    // below stay alive in between.
    void attachBareT3(unsigned int nHits,
                      AttachPlsPre const* plsPre,
                      uint8_t* plsOwned,
                      uint32_t* plsBestT3,
                      uint32_t* hashKey,
                      int32_t* hashVal);
    // The stage-B TARGET UNIVERSE (every triplet no accepted chain consumed, admitted on its own
    // fake score), split out of attachBareT3 so it can run BEFORE stage A: the shared grid's hull
    // needs both target sets, and this half depends only on the accepted-chain array and never on
    // stage A's verdicts, so running it earlier changes nothing it computes.
    void prepareBareT3Targets(uint32_t const* accepted);
    std::optional<cms::alpakatools::device_buffer<Device, AttachTargetPre[]>> bareT3TgtPre_;
    double bareT3PreMs_ = 0.;
    std::string attachT3Summary_;
    // Stage-B owner state, device-resident, alive from attachBareT3 until the contention sweep
    // consumes it. targets = dense node index per bare-triplet target; tgtPls / tgtLogit = the
    // per-target pick after contention and dedup.
    std::optional<cms::alpakatools::device_buffer<Device, uint32_t[]>> bareT3Targets_;
    std::optional<cms::alpakatools::device_buffer<Device, int32_t[]>> bareT3TgtPls_;
    std::optional<cms::alpakatools::device_buffer<Device, float[]>> bareT3TgtLogit_;
    // keep[pos] = 1 iff position pos is a DELIVERY (a stage-B owner that survived the dedup); the
    // contention sweep's gather reads it.
    std::optional<cms::alpakatools::device_buffer<Device, uint32_t[]>> bareT3Keep_;
    // The owner tag behind each seed-dedup table entry. PERSISTENT, allocated once on first use and
    // reused every event, because AN EXTRA PER-EVENT DEVICE BUFFER IN THIS BLOCK IS EXPENSIVE ON
    // THE CPU BACKEND: measured +26 ms/event, all of it in the pLS and candidate stages, i.e. in
    // stages this code never touches, via the caching allocator evicting buffers those stages were
    // reusing. Its size is chainattach::kSeedHashSlots, a compile-time constant, so there is nothing
    // to resize, and it needs no initialising: an entry is only read at a slot whose key matched,
    // and the thread that claimed that key wrote the tag first.
    std::optional<cms::alpakatools::device_buffer<Device, uint32_t[]>> rdHashOwner_;
    uint32_t nBareT3_ = 0;

    // ---- DUPLICATE-FLAG SNAPSHOTS --------------------------------------------------------------
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

    // Env-gated (LST_CHAIN_PAIR_DUMP) attach-pair sidecar: one record per scored (target, pLS)
    // pair of both attach scoring stages, carrying the standardized head inputs as consumed and
    // the resulting logit. These are the on-policy training rows for the attach head. Both
    // buffers stay empty and both scorers get nullptr unless the variable names an output file,
    // so with it unset nothing here is allocated, written or read.
    //   rows  append buffer, capacity chainPairDumpCap()
    //   ctl   [0] append cursor (may run past capacity), [1] rows dropped because it did
    // Allocated once on first use and reused every event, for the same caching-allocator reason
    // rdHashOwner_ is persistent.
    std::optional<cms::alpakatools::device_buffer<Device, ChainAttachPairRow[]>> pairDumpRows_;
    std::optional<cms::alpakatools::device_buffer<Device, uint32_t[]>> pairDumpCtl_;
    void beginChainPairDump();
    void dumpChainPairs();
    // Env-gated (LST_CHAIN_JOIN_DUMP) truth-join keys the pair dump deliberately does not carry:
    // per chain node its sparse triplet index and its three MDs' (anchor, other) ph2 hit rows,
    // and per pLS its tracking-ntuple see_* row and its eta. Identities only: no sim information
    // and no label.
    void dumpChainJoin();
    ChainAttachPairRow* pairDumpRowPtr() { return pairDumpRows_.has_value() ? pairDumpRows_->data() : nullptr; }
    uint32_t* pairDumpCtlPtr() { return pairDumpCtl_.has_value() ? pairDumpCtl_->data() : nullptr; }

    // Env-gated (LST_CHAIN_TC_DUMP) candidate-level sidecar; writes nothing otherwise.
    void dumpChainTCs();
    // Env-gated (LST_CHAIN_CHAIN_DUMP) chain-level sidecar; writes nothing otherwise.
    void dumpChains();
    // Terminal-variant probe sidecar, enabled by LST_CHAIN_VARIANT_DUMP: one record per TERMINAL
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
