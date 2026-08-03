#ifndef RecoTracker_LSTCore_src_alpaka_LSTEvent_h
#define RecoTracker_LSTCore_src_alpaka_LSTEvent_h

#include <algorithm>
#include <optional>
#include <string>

#include "RecoTracker/LSTCore/interface/LSTInputHostCollection.h"
#include "RecoTracker/LSTCore/interface/ChainEdgesHostCollection.h"
#include "RecoTracker/LSTCore/interface/ChainIncidenceHostCollection.h"
#include "RecoTracker/LSTCore/interface/ChainNodesHostCollection.h"
#include "RecoTracker/LSTCore/interface/ChainsHostCollection.h"
#include "RecoTracker/LSTCore/interface/ChainConfig.h"
#include "RecoTracker/LSTCore/interface/HitsHostCollection.h"
#include "RecoTracker/LSTCore/interface/MiniDoubletsHostCollection.h"
#include "RecoTracker/LSTCore/interface/PixelQuintupletsHostCollection.h"
#include "RecoTracker/LSTCore/interface/PixelTripletsHostCollection.h"
#include "RecoTracker/LSTCore/interface/QuintupletsHostCollection.h"
#include "RecoTracker/LSTCore/interface/QuadrupletsHostCollection.h"
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
#include "RecoTracker/LSTCore/interface/alpaka/PixelQuintupletsDeviceCollection.h"
#include "RecoTracker/LSTCore/interface/alpaka/PixelTripletsDeviceCollection.h"
#include "RecoTracker/LSTCore/interface/alpaka/QuintupletsDeviceCollection.h"
#include "RecoTracker/LSTCore/interface/alpaka/QuadrupletsDeviceCollection.h"
#include "RecoTracker/LSTCore/interface/alpaka/SegmentsDeviceCollection.h"
#include "RecoTracker/LSTCore/interface/alpaka/PixelSegmentsDeviceCollection.h"
#include "RecoTracker/LSTCore/interface/alpaka/TrackCandidatesDeviceCollection.h"
#include "RecoTracker/LSTCore/interface/alpaka/TripletsDeviceCollection.h"
#include "RecoTracker/LSTCore/interface/alpaka/ModulesDeviceCollection.h"
#include "RecoTracker/LSTCore/interface/alpaka/ObjectRangesDeviceCollection.h"
#include "RecoTracker/LSTCore/interface/alpaka/EndcapGeometryDevDeviceCollection.h"

#include "HeterogeneousCore/AlpakaInterface/interface/host.h"

namespace ALPAKA_ACCELERATOR_NAMESPACE::lst {

  // Chain-tracking P2.4 scratch record types; defined in src/alpaka/ChainAttach.h, which only the
  // implementation translation unit needs to see.
  struct AttachPlsPre;
  struct AttachTargetPre;

  class LSTEvent {
  private:
    Queue& queue_;
    const float ptCut_;
    const uint16_t clustSizeCut_;
    const bool reduceMemByFullPrecompute_;
    // Master flag of the chain-tracking port (P2_PORT_MAP.md). At phase P2.0 it only enables the
    // triplet compaction and the incidence CSR build; nothing downstream consumes them yet, so the
    // track candidate collection is bit-identical with the flag either way.
    const bool useChainTracking_;
    // FROZEN chain-tracking configuration. Supplied by LSTProducer's grouped chainTracking PSets in
    // CMSSW and left at the FREEZE_RECORD defaults documented in ChainConfig.h in the standalone.
    ChainConfig chainConfig_;

    std::array<unsigned int, 6> n_minidoublets_by_layer_barrel_{};
    std::array<unsigned int, 5> n_minidoublets_by_layer_endcap_{};
    std::array<unsigned int, 6> n_segments_by_layer_barrel_{};
    std::array<unsigned int, 5> n_segments_by_layer_endcap_{};
    std::array<unsigned int, 6> n_triplets_by_layer_barrel_{};
    std::array<unsigned int, 5> n_triplets_by_layer_endcap_{};
    std::array<unsigned int, 6> n_quintuplets_by_layer_barrel_{};
    std::array<unsigned int, 5> n_quintuplets_by_layer_endcap_{};
    std::array<unsigned int, 6> n_quadruplets_by_layer_barrel_{};
    std::array<unsigned int, 5> n_quadruplets_by_layer_endcap_{};
    unsigned int nTotalSegments_;
    unsigned int pixelSize_;
    uint16_t pixelModuleIndex_;
    unsigned int nChainNodes_ = 0;   // dense triplet-node count (K0)
    unsigned int nChainE1Edges_ = 0; // exact MD-keyed edge count (K1b)
    unsigned int nChainE2Edges_ = 0; // exact LS-keyed edge count (K1b)
    unsigned int nChainCount_ = 0;   // welded chain count (K6d)
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
    std::optional<QuintupletsDeviceCollection> quintupletsDC_;
    std::optional<QuadrupletsDeviceCollection> quadrupletsDC_;
    std::optional<TrackCandidatesBaseDeviceCollection> trackCandidatesBaseDC_;
    std::optional<TrackCandidatesExtendedDeviceCollection> trackCandidatesExtendedDC_;
    std::optional<PixelTripletsDeviceCollection> pixelTripletsDC_;
    std::optional<PixelQuintupletsDeviceCollection> pixelQuintupletsDC_;
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
    std::optional<QuintupletsHostCollection> quintupletsHC_;
    std::optional<PixelTripletsHostCollection> pixelTripletsHC_;
    std::optional<PixelQuintupletsHostCollection> pixelQuintupletsHC_;
    std::optional<QuadrupletsHostCollection> quadrupletsHC_;
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

  public:
    // Constructor used for CMSSW integration. Uses an external queue.
    LSTEvent(bool verbose,
             const float ptCut,
             const uint16_t clustSizeCut,
             Queue& q,
             const LSTESData<Device>* deviceESData,
             bool reduce_mem_by_full_precompute,
             bool use_chain_tracking = false,
             ChainConfig const& chain_config = ChainConfig{})
        : queue_(q),
          ptCut_(ptCut),
          clustSizeCut_(clustSizeCut),
          reduceMemByFullPrecompute_(reduce_mem_by_full_precompute),
          useChainTracking_(use_chain_tracking),
          chainConfig_(chain_config),
          nModules_(deviceESData->nModules),
          nLowerModules_(deviceESData->nLowerModules),
          nPixels_(deviceESData->nPixels),
          nEndCapMap_(deviceESData->nEndCapMap),
          modules_(*deviceESData->modules),
          pixelMapping_(*deviceESData->pixelMapping),
          endcapGeometry_(*deviceESData->endcapGeometry),
          objectsStatistics_(verbose) {
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
    void createPixelTriplets();
    void createQuintuplets();
    void pixelLineSegmentCleaning(bool no_pls_dupclean);
    void createPixelQuintuplets();
    void createQuadruplets();

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

    // Chain-tracking phase P2.4: K8 pixel attach. Runs inside arbitrateChains, on the K9-accepted
    // chains and before the extension. Builds the invariant-keyed grid prefilter, scores the
    // (chain, pLS) candidates with the r2 pair head, resolves the one-pLS-one-owner contention and
    // the -RD seed-family dedup, and retires the carried pixel rows the attach replaced. The type-7
    // upgrade itself is applied by ChainEmitTCs.
    void attachPixels(unsigned int nHits, uint32_t const* accepted, unsigned int nAllocatedTCs);
    // Env-gated (LST_CHAIN_ATTACH_AUDIT) grid-vs-exhaustive-scan superset verification.
    void attachGridAudit(unsigned int nTargets,
                         AttachPlsPre const* plsPre,
                         AttachTargetPre const* tgtPre,
                         uint32_t const* offsets,
                         AttachPlsPre const* items);
    // Per-event attach counters, formatted for the [CHAIN K8] printout.
    std::string attachSummary_;
    // Env-gated (LST_CHAIN_TC_DUMP) TC-level parity sidecar; writes nothing otherwise.
    void dumpChainTCs();
    // Optional parity sidecar, enabled by the LST_CHAIN_CHAIN_DUMP environment variable.
    void dumpChains();

    unsigned int getNumberOfChains() const { return nChainCount_; }
    unsigned int getNumberOfChainNodes() const { return nChainNodes_; }
    unsigned int getNumberOfChainE1Edges() const { return nChainE1Edges_; }
    unsigned int getNumberOfChainE2Edges() const { return nChainE2Edges_; }

    // functions that map the objects to the appropriate modules
    void addMiniDoubletsToEventExplicit();
    void addSegmentsToEventExplicit();
    void addQuintupletsToEventExplicit();
    void addTripletsToEventExplicit();
    void resetObjectsInModule();
    void addQuadrupletsToEventExplicit();

    unsigned int getNumberOfMiniDoublets();
    unsigned int getNumberOfMiniDoubletsByLayerBarrel(unsigned int layer);
    unsigned int getNumberOfMiniDoubletsByLayerEndcap(unsigned int layer);

    unsigned int getNumberOfSegments();
    unsigned int getNumberOfSegmentsByLayerBarrel(unsigned int layer);
    unsigned int getNumberOfSegmentsByLayerEndcap(unsigned int layer);

    unsigned int getNumberOfTriplets();
    unsigned int getNumberOfTripletsByLayerBarrel(unsigned int layer);
    unsigned int getNumberOfTripletsByLayerEndcap(unsigned int layer);

    int getNumberOfPixelTriplets();
    int getNumberOfPixelQuintuplets();

    unsigned int getNumberOfQuintuplets();
    unsigned int getNumberOfQuintupletsByLayerBarrel(unsigned int layer);
    unsigned int getNumberOfQuintupletsByLayerEndcap(unsigned int layer);

    int getNumberOfTrackCandidates();
    int getNumberOfPT5TrackCandidates();
    int getNumberOfPT3TrackCandidates();
    int getNumberOfPLSTrackCandidates();
    int getNumberOfPixelTrackCandidates();
    int getNumberOfT5TrackCandidates();
    int getNumberOfT4TrackCandidates();

    unsigned int getNumberOfQuadruplets();
    unsigned int getNumberOfQuadrupletsByLayerBarrel(unsigned int layer);
    unsigned int getNumberOfQuadrupletsByLayerEndcap(unsigned int layer);

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
    template <typename TSoA, typename TDev = Device>
    typename TSoA::ConstView getQuadruplets(bool sync = true);
    template <typename TSoA, typename TDev = Device>
    typename TSoA::ConstView getQuintuplets(bool sync = true);
    template <typename TDev = Device>
    PixelTripletsConst getPixelTriplets(bool sync = true);
    template <typename TDev = Device>
    PixelSegmentsConst getPixelSegments(bool sync = true);
    template <typename TDev = Device>
    PixelQuintupletsConst getPixelQuintuplets(bool sync = true);
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
