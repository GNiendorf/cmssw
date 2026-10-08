#ifndef RecoTracker_MkFitCore_src_KalmanUtilsMPlex_h
#define RecoTracker_MkFitCore_src_KalmanUtilsMPlex_h

#include "RecoTracker/MkFitCore/interface/Track.h"
#include "Matrix.h"
#include "RecoTracker/MkFitCore/interface/portable/KalmanUtilsMPlex.h"

namespace mkfit {

  using LocalStatesOut = portable::LocalStatesOut<NN>;

  using portable::kalmanCheckChargeFlip;
  using portable::kalmanComputeChi2;
  using portable::kalmanComputeChi2Endcap;
  using portable::kalmanComputeChi2Plane;
  using portable::kalmanOperation;
  using portable::kalmanOperationEndcap;
  using portable::kalmanOperationPlane;
  using portable::kalmanOperationPlaneLocal;
  using portable::kalmanPropagateAndComputeChi2;
  using portable::kalmanPropagateAndComputeChi2Endcap;
  using portable::kalmanPropagateAndComputeChi2Plane;
  using portable::kalmanPropagateAndUpdate;
  using portable::kalmanPropagateAndUpdateAndChi2Plane;
  using portable::kalmanPropagateAndUpdateEndcap;
  using portable::kalmanPropagateAndUpdatePlane;
  using portable::kalmanUpdate;
  using portable::kalmanUpdateEndcap;
  using portable::kalmanUpdatePlane;
  using portable::smoothLocalStatesPlane;

  // Instantiated for N = NN in KalmanUtilsMPlex.cc only.
  extern template void portable::kalmanUpdate<NN>(const MPlexLS<NN>& psErr,
                                                  const MPlexLV<NN>& psPar,
                                                  const MPlexHS<NN>& msErr,
                                                  const MPlexHV<NN>& msPar,
                                                  MPlexLS<NN>& outErr,
                                                  MPlexLV<NN>& outPar,
                                                  const int N_proc);
  extern template void portable::kalmanPropagateAndUpdate<NN>(const MPlexLS<NN>& psErr,
                                                              const MPlexLV<NN>& psPar,
                                                              MPlexQI<NN>& Chg,
                                                              const MPlexHS<NN>& msErr,
                                                              const MPlexHV<NN>& msPar,
                                                              MPlexLS<NN>& outErr,
                                                              MPlexLV<NN>& outPar,
                                                              MPlexQI<NN>& outFailFlag,
                                                              const int N_proc,
                                                              const PropagationFlags& propFlags,
                                                              const bool propToHit);
  extern template void portable::kalmanComputeChi2<NN>(const MPlexLS<NN>& psErr,
                                                       const MPlexLV<NN>& psPar,
                                                       const MPlexQI<NN>& inChg,
                                                       const MPlexHS<NN>& msErr,
                                                       const MPlexHV<NN>& msPar,
                                                       MPlexQF<NN>& outChi2,
                                                       const int N_proc);
  extern template void portable::kalmanPropagateAndComputeChi2<NN>(const MPlexLS<NN>& psErr,
                                                                   const MPlexLV<NN>& psPar,
                                                                   const MPlexQI<NN>& inChg,
                                                                   const MPlexHS<NN>& msErr,
                                                                   const MPlexHV<NN>& msPar,
                                                                   MPlexQF<NN>& outChi2,
                                                                   MPlexLV<NN>& propPar,
                                                                   MPlexQI<NN>& outFailFlag,
                                                                   const int N_proc,
                                                                   const PropagationFlags& propFlags,
                                                                   const bool propToHit);
  extern template void portable::kalmanOperation<NN>(const int kfOp,
                                                     const MPlexLS<NN>& psErr,
                                                     const MPlexLV<NN>& psPar,
                                                     const MPlexHS<NN>& msErr,
                                                     const MPlexHV<NN>& msPar,
                                                     MPlexLS<NN>& outErr,
                                                     MPlexLV<NN>& outPar,
                                                     MPlexQF<NN>& outChi2,
                                                     const int N_proc);
  extern template void portable::kalmanUpdateEndcap<NN>(const MPlexLS<NN>& psErr,
                                                        const MPlexLV<NN>& psPar,
                                                        const MPlexHS<NN>& msErr,
                                                        const MPlexHV<NN>& msPar,
                                                        MPlexLS<NN>& outErr,
                                                        MPlexLV<NN>& outPar,
                                                        const int N_proc);
  extern template void portable::kalmanPropagateAndUpdateEndcap<NN>(const MPlexLS<NN>& psErr,
                                                                    const MPlexLV<NN>& psPar,
                                                                    MPlexQI<NN>& Chg,
                                                                    const MPlexHS<NN>& msErr,
                                                                    const MPlexHV<NN>& msPar,
                                                                    MPlexLS<NN>& outErr,
                                                                    MPlexLV<NN>& outPar,
                                                                    MPlexQI<NN>& outFailFlag,
                                                                    const int N_proc,
                                                                    const PropagationFlags& propFlags,
                                                                    const bool propToHit);
  extern template void portable::kalmanComputeChi2Endcap<NN>(const MPlexLS<NN>& psErr,
                                                             const MPlexLV<NN>& psPar,
                                                             const MPlexQI<NN>& inChg,
                                                             const MPlexHS<NN>& msErr,
                                                             const MPlexHV<NN>& msPar,
                                                             MPlexQF<NN>& outChi2,
                                                             const int N_proc);
  extern template void portable::kalmanPropagateAndComputeChi2Endcap<NN>(const MPlexLS<NN>& psErr,
                                                                         const MPlexLV<NN>& psPar,
                                                                         const MPlexQI<NN>& inChg,
                                                                         const MPlexHS<NN>& msErr,
                                                                         const MPlexHV<NN>& msPar,
                                                                         MPlexQF<NN>& outChi2,
                                                                         MPlexLV<NN>& propPar,
                                                                         MPlexQI<NN>& outFailFlag,
                                                                         const int N_proc,
                                                                         const PropagationFlags& propFlags,
                                                                         const bool propToHit);
  extern template void portable::kalmanOperationEndcap<NN>(const int kfOp,
                                                           const MPlexLS<NN>& psErr,
                                                           const MPlexLV<NN>& psPar,
                                                           const MPlexHS<NN>& msErr,
                                                           const MPlexHV<NN>& msPar,
                                                           MPlexLS<NN>& outErr,
                                                           MPlexLV<NN>& outPar,
                                                           MPlexQF<NN>& outChi2,
                                                           const int N_proc);
  extern template void portable::kalmanUpdatePlane<NN>(const MPlexLS<NN>& psErr,
                                                       const MPlexLV<NN>& psPar,
                                                       const MPlexQI<NN>& Chg,
                                                       const MPlexHS<NN>& msErr,
                                                       const MPlexHV<NN>& msPar,
                                                       const MPlexHV<NN>& plNrm,
                                                       const MPlexHV<NN>& plDir,
                                                       const MPlexHV<NN>& plPnt,
                                                       MPlexLS<NN>& outErr,
                                                       MPlexLV<NN>& outPar,
                                                       const int N_proc);
  extern template void portable::kalmanPropagateAndUpdatePlane<NN>(const MPlexLS<NN>& psErr,
                                                                   const MPlexLV<NN>& psPar,
                                                                   MPlexQI<NN>& Chg,
                                                                   const MPlexHS<NN>& msErr,
                                                                   const MPlexHV<NN>& msPar,
                                                                   const MPlexHV<NN>& plNrm,
                                                                   const MPlexHV<NN>& plDir,
                                                                   const MPlexHV<NN>& plPnt,
                                                                   MPlexLS<NN>& outErr,
                                                                   MPlexLV<NN>& outPar,
                                                                   MPlexQI<NN>& outFailFlag,
                                                                   const int N_proc,
                                                                   const PropagationFlags& propFlags,
                                                                   const bool propToHit);
  extern template void portable::kalmanPropagateAndUpdateAndChi2Plane<NN, cpe_func>(
      const MPlexLS<NN>& psErr,
      const MPlexLV<NN>& psPar,
      MPlexQI<NN>& Chg,
      const MPlexHS<NN>& msErr,
      const MPlexHV<NN>& msPar,
      const MPlexHV<NN>& plNrm,
      const MPlexHV<NN>& plDir,
      const MPlexHV<NN>& plPnt,
      MPlexLS<NN>& outErr,
      MPlexLV<NN>& outPar,
      MPlexQI<NN>& outFailFlag,
      MPlexQF<NN>& outChi2,
      const int N_proc,
      const PropagationFlags& propFlags,
      const bool propToHit,
      const MPlexQI<NN>* noMatEffPtr,
      const MPlexQI<NN>* doCPE,
      cpe_func cpe_corr_func,
      const MPlexQF<NN>* matRadl,
      const MPlexQF<NN>* matBbxi,
      const LocalStatesOut<NN>* localStates);
  extern template void portable::kalmanComputeChi2Plane<NN>(const MPlexLS<NN>& psErr,
                                                            const MPlexLV<NN>& psPar,
                                                            const MPlexQI<NN>& inChg,
                                                            const MPlexHS<NN>& msErr,
                                                            const MPlexHV<NN>& msPar,
                                                            const MPlexHV<NN>& plNrm,
                                                            const MPlexHV<NN>& plDir,
                                                            const MPlexHV<NN>& plPnt,
                                                            MPlexQF<NN>& outChi2,
                                                            const int N_proc);
  extern template void portable::kalmanPropagateAndComputeChi2Plane<NN>(const MPlexLS<NN>& psErr,
                                                                        const MPlexLV<NN>& psPar,
                                                                        const MPlexQI<NN>& inChg,
                                                                        const MPlexHS<NN>& msErr,
                                                                        const MPlexHV<NN>& msPar,
                                                                        const MPlexHV<NN>& plNrm,
                                                                        const MPlexHV<NN>& plDir,
                                                                        const MPlexHV<NN>& plPnt,
                                                                        MPlexQF<NN>& outChi2,
                                                                        MPlexLV<NN>& propPar,
                                                                        MPlexQI<NN>& outFailFlag,
                                                                        const int N_proc,
                                                                        const PropagationFlags& propFlags,
                                                                        const bool propToHit);
  extern template void portable::kalmanOperationPlane<NN>(const int kfOp,
                                                          const MPlexLS<NN>& psErr,
                                                          const MPlexLV<NN>& psPar,
                                                          const MPlexQI<NN>& Chg,
                                                          const MPlexHS<NN>& msErr,
                                                          const MPlexHV<NN>& msPar,
                                                          const MPlexHV<NN>& plNrm,
                                                          const MPlexHV<NN>& plDir,
                                                          const MPlexHV<NN>& plPnt,
                                                          MPlexLS<NN>& outErr,
                                                          MPlexLV<NN>& outPar,
                                                          MPlexQF<NN>& outChi2,
                                                          const int N_proc);
  extern template void portable::kalmanOperationPlaneLocal<NN, cpe_func>(const int kfOp,
                                                                         const MPlexLS<NN>& psErr,
                                                                         const MPlexLV<NN>& psPar,
                                                                         const MPlexQI<NN>& Chg,
                                                                         const MPlexHS<NN>& msErr,
                                                                         const MPlexHV<NN>& msPar,
                                                                         const MPlexHV<NN>& plNrm,
                                                                         const MPlexHV<NN>& plDir,
                                                                         const MPlexHV<NN>& plPnt,
                                                                         MPlexLS<NN>& outErr,
                                                                         MPlexLV<NN>& outPar,
                                                                         MPlexQF<NN>& outChi2,
                                                                         const int N_proc,
                                                                         const MPlexQI<NN>* doCPE,
                                                                         cpe_func cpe_corr_func,
                                                                         const PropagationEnv* fieldEnv,
                                                                         const LocalStatesOut<NN>* localStates);
  extern template void portable::smoothLocalStatesPlane<NN>(const MPlex5V<NN>& xf,
                                                            const MPlex5S<NN>& cf,
                                                            const MPlex5V<NN>& xb,
                                                            const MPlex5S<NN>& cb,
                                                            MPlex5V<NN>& xs,
                                                            MPlex5S<NN>& cs,
                                                            MPlexQI<NN>& ok,
                                                            const int N_proc);

}  // end namespace mkfit
#endif
