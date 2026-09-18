#ifndef RecoTracker_LSTCore_interface_alpaka_Common_h
#define RecoTracker_LSTCore_interface_alpaka_Common_h

#include <numbers>

#include "FWCore/Utilities/interface/HostDeviceConstant.h"
#include "FWCore/MessageLogger/interface/MessageLogger.h"
#include "RecoTracker/LSTCore/interface/Common.h"

namespace ALPAKA_ACCELERATOR_NAMESPACE::lst {

  using namespace ::lst;

  ALPAKA_FN_HOST ALPAKA_FN_INLINE void lstWarning(std::string_view warning) {
#ifdef LST_STANDALONE
    printf("%s\n", warning.data());
#else
    edm::LogWarning("LST") << warning;
#endif
  }

  // The constants below are usually used in functions like alpaka::math::min(),
  // expecting a reference (T const&) in the arguments. Hence,
  // HOST_DEVICE_CONSTANT needs to be used instead of constexpr.

  HOST_DEVICE_CONSTANT float kPi = std::numbers::pi_v<float>;
  // 15 MeV constant from the approximate Bethe-Bloch formula
  HOST_DEVICE_CONSTANT float kMulsInGeV = 0.015;
  HOST_DEVICE_CONSTANT float kMiniMulsPtScaleBarrel[6] = {0.0052, 0.0038, 0.0034, 0.0034, 0.0032, 0.0034};
  HOST_DEVICE_CONSTANT float kMiniMulsPtScaleEndcap[5] = {0.006, 0.006, 0.006, 0.006, 0.006};
  HOST_DEVICE_CONSTANT float kMiniRminMeanBarrel[6] = {
      25.007152356, 37.2186993757, 52.3104270826, 68.6658656666, 85.9770373007, 108.301772384};
  HOST_DEVICE_CONSTANT float kMiniRminMeanEndcap[5] = {
      130.992832231, 154.813883559, 185.352604327, 221.635123002, 265.022076742};
  HOST_DEVICE_CONSTANT float k2Rinv1GeVf = (kC * kB) / 2;
  HOST_DEVICE_CONSTANT float kR1GeVf = 1. / (kC * kB);
  HOST_DEVICE_CONSTANT float kSinAlphaMax = 0.95;
  HOST_DEVICE_CONSTANT float kDeltaZLum = 15.0;
  HOST_DEVICE_CONSTANT float kPixelPSZpitch = 0.15;
  HOST_DEVICE_CONSTANT float kStripPSZpitch = 2.4;
  HOST_DEVICE_CONSTANT float kStrip2SZpitch = 5.0;
  HOST_DEVICE_CONSTANT float kWidth2S = 0.009;
  HOST_DEVICE_CONSTANT float kDisks2SMinRadius = 60.0;
  HOST_DEVICE_CONSTANT float kWidthPS = 0.01;
  HOST_DEVICE_CONSTANT float kPt_betaMax = 7.0;
  HOST_DEVICE_CONSTANT int kNTripletThreshold = 1000;
  HOST_DEVICE_CONSTANT int kNQuintupletThreshold = 100000;
  HOST_DEVICE_CONSTANT int kLogicalOTLayers = 11;  // logical OT layers are 1..11
  HOST_DEVICE_CONSTANT auto kMaxPLSHitBitsInHitsSoA = ::lst::kMaxPLSHitBitsInHitsSoA;

  HOST_DEVICE_CONSTANT float kMiniDeltaTilted[3] = {0.26f, 0.26f, 0.26f};
  HOST_DEVICE_CONSTANT float kMiniDeltaFlat[6] = {0.26f, 0.16f, 0.16f, 0.18f, 0.18f, 0.18f};
  HOST_DEVICE_CONSTANT float kMiniDeltaLooseTilted[3] = {0.4f, 0.4f, 0.4f};
  HOST_DEVICE_CONSTANT float kMiniDeltaEndcap[5][15] = {
      {0.4f, 0.4f, 0.4f, 0.4f, 0.4f, 0.4f, 0.4f, 0.4f, 0.4f, 0.4f, /*10*/ 0.18f, 0.18f, 0.18f, 0.18f, 0.18f},
      {0.4f, 0.4f, 0.4f, 0.4f, 0.4f, 0.4f, 0.4f, 0.4f, 0.4f, 0.4f, /*10*/ 0.18f, 0.18f, 0.18f, 0.18f, 0.18f},
      {0.4f, 0.4f, 0.4f, 0.4f, 0.4f, 0.4f, 0.4f, 0.4f, 0.18f, 0.18f, /*10*/ 0.18f, 0.18f, 0.18f, 0.18f, 0.18f},
      {0.4f, 0.4f, 0.4f, 0.4f, 0.4f, 0.4f, 0.4f, 0.4f, 0.18f, 0.18f, /*10*/ 0.18f, 0.18f, 0.18f, 0.18f, 0.18f},
      {0.4f, 0.4f, 0.4f, 0.4f, 0.4f, 0.4f, 0.4f, 0.4f, 0.4f, 0.18f, /*10*/ 0.18f, 0.18f, 0.18f, 0.18f, 0.18f}};

  namespace dnn {

    // Common constants for both DNNs
    HOST_DEVICE_CONSTANT float kPhi_norm = kPi;
    HOST_DEVICE_CONSTANT float kEtaSize = 0.25f;  // Bin size in eta.
    constexpr unsigned int kPtBins = 2;
    constexpr unsigned int kEtaBins = 10;

    namespace plsembdnn {
      HOST_DEVICE_CONSTANT float kEta_norm = 4.0f;
      HOST_DEVICE_CONSTANT float kEtaErr_norm = 0.00139f;
      HOST_DEVICE_CONSTANT float kWP[kEtaBins] = {
          0.9235f, 0.8974f, 0.9061f, 0.9431f, 0.8262f, 0.7998f, 0.7714f, 0.7017f, 0.6749f, 0.6624f};
    }  // namespace plsembdnn

    namespace t3dnn {
      HOST_DEVICE_CONSTANT float kEta_norm = 2.5f;
      HOST_DEVICE_CONSTANT float kZ_max = 224.149505f;
      HOST_DEVICE_CONSTANT float kR_max = 98.932365f;
      HOST_DEVICE_CONSTANT unsigned int kOutputFeatures = 3;
      HOST_DEVICE_CONSTANT float kWp_prompt[kPtBins][kEtaBins] = {
          {0.4957f, 0.5052f, 0.5201f, 0.5340f, 0.4275f, 0.4708f, 0.4890f, 0.4932f, 0.5400f, 0.5449f},
          {0.0302f, 0.0415f, 0.0994f, 0.1791f, 0.1960f, 0.2467f, 0.3227f, 0.3242f, 0.2367f, 0.2187f}};
      HOST_DEVICE_CONSTANT float kWp_displaced[kPtBins][kEtaBins] = {
          {0.0334f, 0.0504f, 0.0748f, 0.0994f, 0.1128f, 0.1123f, 0.1118f, 0.1525f, 0.1867f, 0.1847f},
          {0.0091f, 0.0075f, 0.0350f, 0.0213f, 0.0435f, 0.0676f, 0.1957f, 0.1649f, 0.1080f, 0.1046f}};
    }  // namespace t3dnn

    namespace t5dnn {
      HOST_DEVICE_CONSTANT float kEta_norm = 2.5f;
      HOST_DEVICE_CONSTANT float kZ_max = 267.2349854f;
      HOST_DEVICE_CONSTANT float kR_max = 110.1099396f;
      HOST_DEVICE_CONSTANT float kWp98[kPtBins][kEtaBins] = {
          {0.4493f, 0.4939f, 0.5715f, 0.6488f, 0.5709f, 0.5938f, 0.7164f, 0.7565f, 0.8103f, 0.8593f},
          {0.4488f, 0.4448f, 0.5067f, 0.5929f, 0.4836f, 0.4112f, 0.4968f, 0.4403f, 0.5597f, 0.5067f}};
      // 93% retention working points, same binning as kWp98.
      HOST_DEVICE_CONSTANT float kWp93[kPtBins][kEtaBins] = {
          {0.7831f, 0.8153f, 0.8313f, 0.823f, 0.7426f, 0.7532f, 0.8392f, 0.8636f, 0.9172f, 0.9389f},
          {0.6982f, 0.7335f, 0.7395f, 0.8015f, 0.7356f, 0.6149f, 0.6848f, 0.6468f, 0.7187f, 0.7079f}};
    }  // namespace t5dnn

    namespace pt3dnn {
      HOST_DEVICE_CONSTANT float kEta_norm = 2.5f;

      // 95% sig-efficiency for abs(eta) <= 1.25, 84% for abs(eta) > 1.25
      HOST_DEVICE_CONSTANT float kWp_pT3[kEtaBins] = {
          0.6288f, 0.8014f, 0.7218f, 0.743f, 0.7519f, 0.8633f, 0.6934f, 0.6983f, 0.6502f, 0.7037f};
      // 95% sig-efficiency for high pT bin
      HOST_DEVICE_CONSTANT float kWpHigh_pT3 = 0.657f;
      // 99.5% sig-efficiency for abs(eta) <= 1.25, 99% for abs(eta) > 1.25
      HOST_DEVICE_CONSTANT float kWp_pT5[kEtaBins] = {
          0.1227f, 0.1901f, 0.218f, 0.3438f, 0.1011f, 0.1502f, 0.0391f, 0.0471f, 0.1444f, 0.1007f};
      // 99.5% signal efficiency for high pT bin
      HOST_DEVICE_CONSTANT float kWpHigh_pT5 = 0.1498f;

      // kWp's must be defined with inline static in the structs to compile.
      struct pT3WP {
        ALPAKA_FN_ACC static inline float wp(unsigned i) { return kWp_pT3[i]; }
        ALPAKA_FN_ACC static inline float wpHigh() { return kWpHigh_pT3; }
      };
      struct pT5WP {
        ALPAKA_FN_ACC static inline float wp(unsigned i) { return kWp_pT5[i]; }
        ALPAKA_FN_ACC static inline float wpHigh() { return kWpHigh_pT5; }
      };
    }  // namespace pt3dnn

    namespace t4dnn {
      HOST_DEVICE_CONSTANT float kZ_max = 267.2349854f;
      HOST_DEVICE_CONSTANT float kR_max = 110.1099396f;
      HOST_DEVICE_CONSTANT float kEta_norm = 2.5f;
      constexpr unsigned int kEtaBins = 25;

      HOST_DEVICE_CONSTANT float kWp_displaced[kPtBins][kEtaBins] = {
          {0.1981f, 0.2071f, 0.1651f, 0.2351f, 0.232f, 0.2366f, 0.3681f, 0.3035f, 0.2574f, 0.4382f, 0.3126f, 0.3057f, 0.3526f, 0.261f, 0.3053f, 0.4718f, 0.4004f, 0.3037f, 0.301f, 0.2001f, 0.2483f, 0.2288f, 0.099f, 0.0992f, 0.0847f},
          {0.0245f, 0.033f, 0.1931f, 0.0502f, 0.0179f, 0.8189f, 0.8216f, 0.5082f, 0.3526f, 0.2734f, 0.4204f, 0.0582f, 0.0184f, 0.1018f, 0.0899f, 0.2338f, 0.2594f, 0.2093f, 0.1854f, 0.1399f, 0.2743f, 0.6624f, 0.7046f, 0.064f, 0.2394f}};

      HOST_DEVICE_CONSTANT float kWp_fake[kPtBins][kEtaBins] = {
          {0.618f, 0.6521f, 0.6022f, 0.588f, 0.5662f, 0.4464f, 0.4244f, 0.3351f, 0.3012f, 0.4045f, 0.4663f, 0.5329f, 0.4525f, 0.6504f, 0.2746f, 0.1339f, 0.179f, 0.1685f, 0.2066f, 0.1302f, 0.096f, 0.0623f, 0.0498f, 0.0278f, 0.0167f},
          {0.9115f, 0.9605f, 0.666f, 0.7374f, 0.9263f, 0.1698f, 0.1485f, 0.359f, 0.5302f, 0.6662f, 0.1273f, 0.5445f, 0.5916f, 0.5985f, 0.7687f, 0.1317f, 0.2187f, 0.116f, 0.481f, 0.1532f, 0.318f, 0.0155f, 0.0111f, 0.1336f, 0.1455f}};
    }  // namespace t4dnn

  }  // namespace dnn

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE::lst
#endif
