#!/usr/bin/env python3
"""Lane BUILD-DESIGN (rung 4): apply the T4/T5 reject probe to a ladder worktree (textual, asserted edits).
usage: apply_probe.py <worktree root>      edits Quintuplet.h, Quadruplet.h, RungProbe.h, LSTEvent.h, LSTEvent.dev.cc"""
import sys


def sub(txt, old, new, count=1):
    assert txt.count(old) == count, (txt.count(old), old[:80])
    return txt.replace(old, new)


def main():
    root = sys.argv[1].rstrip("/") + "/RecoTracker/LSTCore/src/alpaka/"
    # ---------------- Quintuplet.h
    p = root + "Quintuplet.h"
    t = open(p).read()
    assert "T5Probe" not in t, "probe already applied"
    TPL = "  template <alpaka::concepts::Acc TAcc>\n  ALPAKA_FN_ACC ALPAKA_FN_INLINE bool "
    TPLP = "  template <bool Probe = false, alpaka::concepts::Acc TAcc>\n  ALPAKA_FN_ACC ALPAKA_FN_INLINE bool "
    for fn in ("runQuintupletdBetaCutBBBB", "runQuintupletdBetaCutBBEE", "runQuintupletdBetaCutEEEE",
               "runQuintupletdBetaAlgoSelector", "passT5RZConstraint", "runQuintupletDefaultAlgo"):
        t = sub(t, TPL + fn + "(", TPLP + fn + "(")
    # probe structs in front of the first dBeta function
    structs = ("  // Rung probe of the T4 / T5 builders (env-gated, see RungProbe.h).\n"
               "#ifndef LST_PROBE_REJECT\n#define LST_PROBE_REJECT(bit) \\\n  do {                        \\\n    if constexpr (Probe)      \\\n      probe->fail |= (bit);   \\\n    else                      \\\n      return {};              \\\n  } while (false)\n#endif\n"
               "  enum T5ProbeBit : unsigned int { kT5ProbeFlag = 1, kT5ProbeDnn = 2, kT5ProbeDBeta1 = 4, kT5ProbeDBeta2 = 8, kT5ProbeRz = 16 };\n"
               "  enum T4ProbeBit : unsigned int { kT4ProbeCharge = 1, kT4ProbeDnn = 2, kT4ProbeDBeta = 4, kT4ProbeRz = 8, kT4ProbeFlag = 16 };\n"
               "  struct T5Probe {\n    unsigned int fail = 0;\n    float dBetaCut2[2] = {0.f, 0.f};\n    int rzLinear = 0;\n  };\n"
               "  struct T4Probe {\n    unsigned int fail = 0;\n    float dBetaCut2 = 0.f;\n    int rzLinear = 0;\n  };\n\n")
    t = sub(t, TPLP + "runQuintupletdBetaCutBBBB(", structs + TPLP + "runQuintupletdBetaCutBBBB(")
    # dBeta functions: trailing out pointer
    a = t.index("bool runQuintupletdBetaCutBBBB(")
    b = t.index("bool runQuintupletDefaultAlgo(")
    body = t[a:b]
    assert body.count("float& dBeta,\n") == 4
    import re
    body, n = re.subn(r"(float& dBeta,\n\s+const float ptCut)\) \{", r"\1,\n      float* dBetaCut2Out = nullptr) {", body)
    assert n == 4, n
    old = "    dBeta = betaIn - betaOut;\n    return dBeta * dBeta <= dBetaCut2;"
    assert body.count(old) == 3
    body = body.replace(old, "    dBeta = betaIn - betaOut;\n    if constexpr (Probe)\n      *dBetaCut2Out = dBetaCut2;\n    return dBeta * dBeta <= dBetaCut2;")
    for fn in ("BBBB", "BBEE", "EEEE"):
        body = body.replace("return runQuintupletdBetaCut%s(acc," % fn, "return runQuintupletdBetaCut%s<Probe>(acc," % fn)
    body, n = re.subn(r"(\n\s+dBeta,\n\s+ptCut)\);", r"\1,\n                                       dBetaCut2Out);", body)
    assert n == 5, n
    t = t[:a] + body + t[b:]
    # passT5RZConstraint
    t = sub(t, "                                                         bool& tightCutFlag) {",
            "                                                         bool& tightCutFlag,\n"
            "                                                         int* rzLinearOut = nullptr) {")
    t = sub(t, "      rzChiSquared = 12 * (residual4_linear * residual4_linear + residual5_linear * residual5_linear);\n",
            "      rzChiSquared = 12 * (residual4_linear * residual4_linear + residual5_linear * residual5_linear);\n"
            "      if constexpr (Probe)\n        *rzLinearOut = 1;\n")
    # runQuintupletDefaultAlgo
    a = t.index("bool runQuintupletDefaultAlgo(")
    b = t.index("struct CreateQuintupletsT")
    body = t[a:b]
    body = sub(body, "                                                               const float ptCut) {\n",
               "                                                               const float ptCut,\n"
               "                                                               T5Probe* probe = nullptr) {\n")
    body = sub(body, "    if ((triplets.flags()[innerTripletIndex] & triplets.flags()[outerTripletIndex]) & kT3MdDirectionFail)\n      return false;\n",
               "    if ((triplets.flags()[innerTripletIndex] & triplets.flags()[outerTripletIndex]) & kT3MdDirectionFail)\n      LST_PROBE_REJECT(kT5ProbeFlag);\n"
               "    float* dBetaCut2Out1 = nullptr;\n    float* dBetaCut2Out2 = nullptr;\n    int* rzLinearOut = nullptr;\n"
               "    if constexpr (Probe) {\n      dBetaCut2Out1 = &probe->dBetaCut2[0];\n      dBetaCut2Out2 = &probe->dBetaCut2[1];\n      rzLinearOut = &probe->rzLinear;\n    }\n")
    body = sub(body, "    if (!inference)  // T5-building cut\n      return false;\n", "    if (!inference)  // T5-building cut\n      LST_PROBE_REJECT(kT5ProbeDnn);\n")
    body = sub(body, "    if (not runQuintupletdBetaAlgoSelector(acc,", "    if (not runQuintupletdBetaAlgoSelector<Probe>(acc,", 2)
    body = sub(body, "                                           dBeta1,\n                                           ptCut))\n      return false;\n",
               "                                           dBeta1,\n                                           ptCut,\n                                           dBetaCut2Out1))\n      LST_PROBE_REJECT(kT5ProbeDBeta1);\n")
    body = sub(body, "                                           dBeta2,\n                                           ptCut))\n      return false;\n",
               "                                           dBeta2,\n                                           ptCut,\n                                           dBetaCut2Out2))\n      LST_PROBE_REJECT(kT5ProbeDBeta2);\n")
    body = sub(body, "    if (not passT5RZConstraint(acc,", "    if (not passT5RZConstraint<Probe>(acc,")
    body = sub(body, "                               f,\n                               tightCutFlag))\n      return false;\n",
               "                               f,\n                               tightCutFlag,\n                               rzLinearOut))\n      LST_PROBE_REJECT(kT5ProbeRz);\n")
    k = body.rindex("    return true;\n")
    body = body[:k] + "    if constexpr (Probe)\n      return probe->fail == 0;\n" + body[k:]
    t = t[:a] + body + t[b:]
    open(p, "w").write(t)

    # ---------------- Quadruplet.h
    p = root + "Quadruplet.h"
    t = open(p).read()
    for fn in ("passT4RZConstraint", "runQuadrupletDefaultAlgo"):
        t = sub(t, TPL + fn + "(", TPLP + fn + "(")
    t = sub(t, "                                                         short charge) {",
            "                                                         short charge,\n                                                         int* rzLinearOut = nullptr) {")
    t = sub(t, "      rzChiSquared = 12 * (residual4_linear * residual4_linear);\n",
            "      rzChiSquared = 12 * (residual4_linear * residual4_linear);\n      if constexpr (Probe)\n        *rzLinearOut = 1;\n")
    a = t.index("bool runQuadrupletDefaultAlgo(")
    b = t.index("struct CreateQuadrupletsT")
    body = t[a:b]
    body = sub(body, "                                                               float& fakeScore) {\n",
               "                                                               float& fakeScore,\n                                                               T4Probe* probe = nullptr) {\n")
    body = sub(body, "    if (innerT3charge != outerT3charge)\n      return false;\n",
               "    if (innerT3charge != outerT3charge)\n      LST_PROBE_REJECT(kT4ProbeCharge);\n"
               "    float* dBetaCut2Out = nullptr;\n    int* rzLinearOut = nullptr;\n"
               "    if constexpr (Probe) {\n      dBetaCut2Out = &probe->dBetaCut2;\n      rzLinearOut = &probe->rzLinear;\n    }\n")
    body = sub(body, "    if (!inference) {\n      return false;\n    }\n", "    if (!inference) {\n      LST_PROBE_REJECT(kT4ProbeDnn);\n    }\n")
    body = sub(body, "    if (not runQuintupletdBetaAlgoSelector(acc,", "    if (not runQuintupletdBetaAlgoSelector<Probe>(acc,")
    body = sub(body, "                                           dBeta,\n                                           ptCut))\n      return false;\n",
               "                                           dBeta,\n                                           ptCut,\n                                           dBetaCut2Out))\n      LST_PROBE_REJECT(kT4ProbeDBeta);\n")
    body = sub(body, "    if (not passT4RZConstraint(acc,", "    if (not passT4RZConstraint<Probe>(acc,")
    body = sub(body, "                               inner_circleCenterY,\n                               innerT3charge))\n      return false;\n",
               "                               inner_circleCenterY,\n                               innerT3charge,\n                               rzLinearOut))\n      LST_PROBE_REJECT(kT4ProbeRz);\n")
    body = sub(body, "    if ((triplets.flags()[innerTripletIndex] | triplets.flags()[outerTripletIndex]) & kT3MdDirectionFail)\n      return false;\n",
               "    if ((triplets.flags()[innerTripletIndex] | triplets.flags()[outerTripletIndex]) & kT3MdDirectionFail)\n      LST_PROBE_REJECT(kT4ProbeFlag);\n")
    k = body.rindex("    return true;\n")
    body = body[:k] + "    if constexpr (Probe)\n      return probe->fail == 0;\n" + body[k:]
    t = t[:a] + body + t[b:]
    open(p, "w").write(t)

    # ---------------- RungProbe.h
    p = root + "RungProbe.h"
    t = open(p).read()
    t = sub(t, '#include "Segment.h"\n', '#include "Segment.h"\n#include "Triplet.h"\n#include "Quintuplet.h"\n#include "Quadruplet.h"\n')
    kern = open(sys.argv[0].replace("apply_probe.py", "probe_kernels.inc")).read()
    t = sub(t, "}  // namespace ALPAKA_ACCELERATOR_NAMESPACE::lst\n\n#endif", kern + "\n}  // namespace ALPAKA_ACCELERATOR_NAMESPACE::lst\n\n#endif")
    open(p, "w").write(t)

    # ---------------- LSTEvent.h
    p = root + "LSTEvent.h"
    t = open(p).read()
    t = sub(t, "    void probeSegments();\n", "    void probeSegments();\n    void probeQuintuplets();\n    void probeQuadruplets();\n")
    open(p, "w").write(t)

    # ---------------- LSTEvent.dev.cc
    p = root + "LSTEvent.dev.cc"
    t = open(p).read()
    ev = open(sys.argv[0].replace("apply_probe.py", "probe_event.inc")).read()
    t = sub(t, "void LSTEvent::createTriplets() {\n", ev + "\nvoid LSTEvent::createTriplets() {\n")
    a = t.index("void LSTEvent::createQuintuplets() {")
    b = t.index("void LSTEvent::pixelLineSegmentCleaning(")
    body = t[a:b]
    k = body.rindex("}\n")
    body = body[:k] + "  if (probeMask_ != nullptr)\n    probeQuintuplets();\n" + body[k:]
    t = t[:a] + body + t[b:]
    a = t.index("void LSTEvent::createQuadruplets() {")
    b = t.index("void LSTEvent::addMiniDoubletsToEventExplicit() {")
    body = t[a:b]
    k = body.rindex("}\n")
    body = body[:k] + "  if (probeMask_ != nullptr)\n    probeQuadruplets();\n" + body[k:]
    t = t[:a] + body + t[b:]
    open(p, "w").write(t)
    print("probe applied")


if __name__ == "__main__":
    main()
