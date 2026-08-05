#include "AccessHelper.h"

using namespace ALPAKA_ACCELERATOR_NAMESPACE::lst;

// ===============
// ----* Hit *----
// ===============

//____________________________________________________________________________________________
std::tuple<std::vector<unsigned int>, std::vector<HitType>> convertHitsToHitIdxsAndHitTypes(
    LSTEvent* event, std::vector<unsigned int> hits) {
  auto hitsBase = event->getInput<HitsBaseSoA>();
  std::vector<unsigned int> hitidxs;
  std::vector<HitType> hittypes;
  for (auto& hit : hits) {
    hitidxs.push_back(hitsBase.idxs()[hit]);
    if (hitsBase.detid()[hit] == kPixelModuleId)
      hittypes.push_back(HitType::Pixel);
    else
      hittypes.push_back(HitType::Phase2OT);
  }
  return std::make_tuple(hitidxs, hittypes);
}

// ===============
// ----* pLS *----
// ===============

//____________________________________________________________________________________________
std::vector<unsigned int> getHitsFrompLS(LSTEvent* event, unsigned int pLS) {
  SegmentsConst segments = event->getSegments<SegmentsSoA>();
  MiniDoubletsConst miniDoublets = event->getMiniDoublets<MiniDoubletsSoA>();
  auto ranges = event->getRanges();
  auto modulesEvt = event->getModules<ModulesSoA>();
  const unsigned int pLS_offset = ranges.segmentModuleIndices()[modulesEvt.nLowerModules()];
  unsigned int MD_1 = segments.mdIndices()[pLS + pLS_offset][0];
  unsigned int MD_2 = segments.mdIndices()[pLS + pLS_offset][1];
  unsigned int hit_1 = miniDoublets.anchorHitIndices()[MD_1];
  unsigned int hit_2 = miniDoublets.outerHitIndices()[MD_1];
  unsigned int hit_3 = miniDoublets.anchorHitIndices()[MD_2];
  unsigned int hit_4 = miniDoublets.outerHitIndices()[MD_2];
  if (hit_3 == hit_4)
    return {hit_1, hit_2, hit_3};
  else
    return {hit_1, hit_2, hit_3, hit_4};
}

//____________________________________________________________________________________________
std::vector<unsigned int> getHitIdxsFrompLS(LSTEvent* event, unsigned int pLS) {
  auto hitsBase = event->getInput<HitsBaseSoA>();
  std::vector<unsigned int> hits = getHitsFrompLS(event, pLS);
  std::vector<unsigned int> hitidxs;
  hitidxs.reserve(hits.size());
  for (auto& hit : hits)
    hitidxs.push_back(hitsBase.idxs()[hit]);
  return hitidxs;
}

//____________________________________________________________________________________________
std::vector<HitType> getHitTypesFrompLS(LSTEvent* event, unsigned int pLS) {
  std::vector<unsigned int> hits = getHitsFrompLS(event, pLS);
  std::vector<HitType> hittypes;
  hittypes.reserve(hits.size());
  auto hitsBase = event->getInput<HitsBaseSoA>();
  for (auto& hit : hits)
    hittypes.push_back(hitsBase.detid()[hit] == kPixelModuleId ? HitType::Pixel : HitType::Phase2OT);
  return hittypes;
}

//____________________________________________________________________________________________
std::tuple<std::vector<unsigned int>, std::vector<HitType>> getHitIdxsAndHitTypesFrompLS(LSTEvent* event,
                                                                                         unsigned pLS) {
  return convertHitsToHitIdxsAndHitTypes(event, getHitsFrompLS(event, pLS));
}

// ==============
// ----* MD *----
// ==============

//____________________________________________________________________________________________
std::vector<unsigned int> getHitsFromMD(LSTEvent* event, unsigned int MD) {
  MiniDoubletsConst miniDoublets = event->getMiniDoublets<MiniDoubletsSoA>();
  unsigned int hit_1 = miniDoublets.anchorHitIndices()[MD];
  unsigned int hit_2 = miniDoublets.outerHitIndices()[MD];
  return {hit_1, hit_2};
}

//____________________________________________________________________________________________
std::tuple<std::vector<unsigned int>, std::vector<HitType>> getHitIdxsAndHitTypesFromMD(LSTEvent* event, unsigned MD) {
  return convertHitsToHitIdxsAndHitTypes(event, getHitsFromMD(event, MD));
}

// ==============
// ----* LS *----
// ==============

//____________________________________________________________________________________________
std::vector<unsigned int> getMDsFromLS(LSTEvent* event, unsigned int LS) {
  SegmentsConst segments = event->getSegments<SegmentsSoA>();
  unsigned int MD_1 = segments.mdIndices()[LS][0];
  unsigned int MD_2 = segments.mdIndices()[LS][1];
  return {MD_1, MD_2};
}

//____________________________________________________________________________________________
std::vector<unsigned int> getHitsFromLS(LSTEvent* event, unsigned int LS) {
  std::vector<unsigned int> MDs = getMDsFromLS(event, LS);
  std::vector<unsigned int> hits_0 = getHitsFromMD(event, MDs[0]);
  std::vector<unsigned int> hits_1 = getHitsFromMD(event, MDs[1]);
  return {hits_0[0], hits_0[1], hits_1[0], hits_1[1]};
}

//____________________________________________________________________________________________
std::tuple<std::vector<unsigned int>, std::vector<HitType>> getHitIdxsAndHitTypesFromLS(LSTEvent* event, unsigned LS) {
  return convertHitsToHitIdxsAndHitTypes(event, getHitsFromLS(event, LS));
}

// ==============
// ----* T3 *----
// ==============

//____________________________________________________________________________________________
std::vector<unsigned int> getLSsFromT3(LSTEvent* event, unsigned int t3) {
  auto const triplets = event->getTriplets<TripletsSoA>();
  unsigned int ls_1 = triplets.segmentIndices()[t3][0];
  unsigned int ls_2 = triplets.segmentIndices()[t3][1];
  return {ls_1, ls_2};
}

//____________________________________________________________________________________________
std::vector<unsigned int> getMDsFromT3(LSTEvent* event, unsigned int T3) {
  std::vector<unsigned int> LSs = getLSsFromT3(event, T3);
  std::vector<unsigned int> MDs_0 = getMDsFromLS(event, LSs[0]);
  std::vector<unsigned int> MDs_1 = getMDsFromLS(event, LSs[1]);
  return {MDs_0[0], MDs_0[1], MDs_1[1]};
}

//____________________________________________________________________________________________
std::vector<unsigned int> getHitsFromT3(LSTEvent* event, unsigned int T3) {
  std::vector<unsigned int> MDs = getMDsFromT3(event, T3);
  std::vector<unsigned int> hits_0 = getHitsFromMD(event, MDs[0]);
  std::vector<unsigned int> hits_1 = getHitsFromMD(event, MDs[1]);
  std::vector<unsigned int> hits_2 = getHitsFromMD(event, MDs[2]);
  return {hits_0[0], hits_0[1], hits_1[0], hits_1[1], hits_2[0], hits_2[1]};
}

//____________________________________________________________________________________________
std::tuple<std::vector<unsigned int>, std::vector<HitType>> getHitIdxsAndHitTypesFromT3(LSTEvent* event, unsigned T3) {
  return convertHitsToHitIdxsAndHitTypes(event, getHitsFromT3(event, T3));
}

// ==============
// ----* T4 *----
// ==============

//____________________________________________________________________________________________
std::vector<unsigned int> getModuleIdxsFromT3(LSTEvent* event, unsigned int T3) {
  std::vector<unsigned int> hits = getHitsFromT3(event, T3);
  std::vector<unsigned int> module_idxs;
  auto hitsEvt = event->getHits<HitsExtendedSoA>();
  for (auto& hitIdx : hits) {
    module_idxs.push_back(hitsEvt.moduleIndices()[hitIdx]);
  }
  return module_idxs;
}

//____________________________________________________________________________________________
std::vector<HitType> getHitTypesFromT3(LSTEvent* event, unsigned int T5) {
  return {Params_T3::kHits, HitType::Phase2OT};
}

std::pair<std::vector<unsigned int>, std::vector<HitType>> getHitIdxsAndHitTypesFromTC(LSTEvent* event,
                                                                                       unsigned int tc_idx) {
  auto const& base = event->getTrackCandidatesBase();
  auto const& ext = event->getTrackCandidatesExtended();
  auto const& hitsBase = event->getInput<HitsBaseSoA>();

  std::vector<unsigned int> hitIdx;
  hitIdx.reserve(Params_TC::kHits);
  std::vector<HitType> hitType;
  hitType.reserve(Params_TC::kHits);

  for (int layerSlot = 0; layerSlot < Params_TC::kLayers; ++layerSlot) {
    if (ext.lowerModuleIndices()[tc_idx][layerSlot] == lst::kTCEmptyLowerModule)
      continue;

    for (unsigned int hitSlot = 0; hitSlot < Params_TC::kHitsPerLayer; ++hitSlot) {
      const unsigned int hitLocal = base.hitIndices()[tc_idx][layerSlot][hitSlot];

      if (hitLocal == lst::kTCEmptyHitIdx)
        continue;

      // Get the GLOBAL ntuple indices
      const auto hitGlobal = hitsBase.idxs()[hitLocal];

      // Determine the type from the hit's detid
      const auto type = (hitsBase.detid()[hitLocal] == kPixelModuleId) ? HitType::Pixel : HitType::Phase2OT;

      // Push the GLOBAL index and type
      hitIdx.push_back(hitGlobal);
      hitType.push_back(type);
    }
  }
  return {hitIdx, hitType};
}
