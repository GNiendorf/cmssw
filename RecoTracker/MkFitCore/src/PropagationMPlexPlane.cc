#include "PropagationMPlex.h"

//#define DEBUG
#include "Debug.h"

namespace mkfit {

  template void portable::helixAtPlane<NN>(const MPlexLV<NN>&,
                                           const MPlexQI<NN>&,
                                           const MPlexHV<NN>&,
                                           const MPlexHV<NN>&,
                                           MPlexQF<NN>&,
                                           MPlexLV<NN>&,
                                           MPlexLL<NN>&,
                                           MPlexQI<NN>&,
                                           const int,
                                           const PropagationFlags&);
  template void portable::propagateHelixToPlaneMPlex<NN>(const MPlexLS<NN>&,
                                                         const MPlexLV<NN>&,
                                                         const MPlexQI<NN>&,
                                                         const MPlexHV<NN>&,
                                                         const MPlexHV<NN>&,
                                                         MPlexLS<NN>&,
                                                         MPlexLV<NN>&,
                                                         MPlexQI<NN>&,
                                                         const int,
                                                         const PropagationFlags&,
                                                         const MPlexQI<NN>*,
                                                         const MPlexQF<NN>*,
                                                         const MPlexQF<NN>*);
  template void portable::propagateHelixToPlaneSubStepMPlex<NN>(const MPlexLS<NN>&,
                                                                const MPlexLV<NN>&,
                                                                const MPlexQI<NN>&,
                                                                const MPlexHV<NN>&,
                                                                const MPlexHV<NN>&,
                                                                MPlexLS<NN>&,
                                                                MPlexLV<NN>&,
                                                                MPlexQI<NN>&,
                                                                const int,
                                                                const PropagationFlags&,
                                                                const int,
                                                                const bool*,
                                                                const MPlexQI<NN>*,
                                                                const MPlexQF<NN>*,
                                                                const MPlexQF<NN>*);

}  // namespace mkfit
