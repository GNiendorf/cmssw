#ifndef RecoTracker_LSTCore_interface_ChainConfig_h
#define RecoTracker_LSTCore_interface_ChainConfig_h

#include <cstdint>

namespace lst {

  // FROZEN chain-tracking configuration (standalone/fanout5/final/FREEZE_RECORD.txt, resolved
  // ANCHOR + CTL + FLAGSHIP + M19 with later flags winning). Every field below is a P2.2-scope
  // knob; the claim / attach knobs (-F -FC -W -B -BK -BT -PU -WE -WZ -EX -a) belong to P2.3/P2.4
  // and are deliberately absent.
  //
  // Kept as a plain struct with frozen defaults rather than as bare constexpr so that P2.3 can
  // fill it from the LSTProducer parameter set (port map section 3.3) with no kernel change; the
  // struct is passed to the kernels by value.
  struct ChainConfig {
    // -e 0 : K6 weld eligibility on the edge logit.
    float thetaEdge = 0.f;
    // -L 3.0 : chain score length weight (ANCHOR said 0.5, the M19 block overrides).
    float lambdaLen = 3.f;

    // -TR 1 / -TT 1.2 / -TA 1.0 / -TL 5 / -TP 1 : terminal trim.
    bool terminalTrim = true;
    float trimFactor = 1.2f;
    float trimAbsChi2 = 1.f;
    int trimMinLayersAfter = 5;
    int trimPasses = 1;

    // -X 0.5 : IP-compatibility boundary on the chain dcaXY, cm.
    float dcaSplit = 0.5f;
    // -Z 0 : extra dca floor for the T4-class exempt branch (exempt iff dca >= max(-X, -Z)).
    float t4ExemptDcaMin = 0.f;

    // -G 6 three-class margin kills. mP = zPrompt - zFake, mD = zDisp - zFake,
    // mX = max(zPrompt, zDisp) - zFake.
    float m3Theta4 = 4.f;      // -M4  : T4-class IP    kill iff mX < m3Theta4
    float m3Theta4D = -1.2f;   // -M4D : T4-class exempt kill iff mD < m3Theta4D
    float m3Theta5 = 1e9f;     // -M5  : IP nLayers == 5 kill iff mP < m3Theta5 (inert at 1e9)
    float m3Theta6 = 1e9f;     // -M6  : IP nLayers >= 6 kill iff mP < m3Theta6 (inert at 1e9)
    float m3ThetaD = 1e9f;     // -MD  : exempt 5+       kill iff mD < m3ThetaD (inert at 1e9)
    float m3ThetaRI = -0.5f;   // -MRI : IP-5+     OR-rescue floor on mX
    float m3ThetaR = -1.8f;    // -MR  : exempt-5+ OR-rescue floor on mX
    // -C25 0.0 / -C25D -2.0 : the (nNodes == 2, nLayers == 5) cell rule; kills only when BOTH
    // margins fail, and never re-kills an already-killed chain.
    float c25Theta = 0.f;
    float c25ThetaD = -2.f;

    // Transition-band levers. The band is keyed on |eta| of the chain's INNERMOST member T3
    // (the same quantity K10 gives the TC), and the deltas are ADDITIVE to the thresholds above.
    float zEta1 = 1.1f;    // -ZE1
    float zEta2 = 1.7f;    // -ZE2
    float zdM4 = -0.5f;    // -ZM4  : added to -M4
    float zdM4D = 1.2f;    // -ZM4D : added to -M4D
    float zdRI = 0.f;      // -ZRI  : added to -MRI
    float zdR = 0.f;       // -ZR   : added to -MR
    float zdR5 = 0.f;      // -ZR5  : added to -ZR for exempt nLayers == 5
    float zdR6 = 0.f;      // -ZR6  : added to -ZR for exempt nLayers >= 6
    float zdCP = 0.f;      // -ZCP  : added to -C25
    float zdCD = 0.f;      // -ZCD  : added to -C25D
    bool zInLayer1 = false;  // -ZIL : restrict every band lever to chains starting in layer 1

    // Reference-implementation constant, not a flag: the score subtraction that marks a killed
    // chain (prototype main.cc kGateKill).
    float gateKill = 1e9f;

    // True iff any band delta is live; reproduces the reference's `zOn` short-circuit exactly.
    constexpr bool etaBandActive() const {
      return zEta2 > zEta1 && (zdRI != 0.f || zdR != 0.f || zdR5 != 0.f || zdR6 != 0.f || zdM4 != 0.f ||
                               zdM4D != 0.f || zdCP != 0.f || zdCD != 0.f);
    }
  };

  // prototype/Stages.h kWeldSweeps.
  static constexpr int kChainWeldSweeps = 3;
  // Cycle guard for the weld path walk. Edges point strictly inward -> outward so a chain cannot
  // exceed the detector layer count; this is a device-safe stand-in for the reference's `visited`
  // array and has never been reached.
  static constexpr uint32_t kChainMaxNodes = 64;

}  // namespace lst

#endif
