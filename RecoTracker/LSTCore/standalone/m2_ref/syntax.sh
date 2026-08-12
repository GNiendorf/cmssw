#!/bin/bash
# -fsyntax-only preflight of the serial-backend LSTEvent TU (discipline rule 6).
S=/mnt/data1/gsn27/here/gpu_wt/g4/src/RecoTracker/LSTCore/standalone
cd $S && source setup.sh > /dev/null 2>&1 && eval $(scramv1 runtime -sh) > /dev/null 2>&1 && source setup.sh > /dev/null 2>&1
cd $S/LST
CMSSWINCLUDE="-I${TRACKLOOPERDIR}/../../../ -I${CMSSW_BASE}/src -I${FMT_ROOT}/include -I${CMSSW_RELEASE_BASE}/src"
g++ -fsyntax-only -march=native -mtune=native -Ofast -fno-reciprocal-math -fopenmp-simd -g -Wall \
  -Woverloaded-virtual -fPIC -fopenmp -I.. -I$ROOT_ROOT/include -DLST_STANDALONE \
  -Werror=return-type -Werror=sign-compare -Werror=unused-but-set-variable -Werror=unused-variable \
  -Wextra -Wno-unused-parameter -Wno-unused-local-typedefs -Wno-attributes \
  -I${ALPAKA_ROOT}/include -I/${BOOST_ROOT}/include -std=c++20 $CMSSWINCLUDE \
  -DALPAKA_ACC_CPU_B_SEQ_T_SEQ_ENABLED -DALPAKA_DISABLE_VENDOR_RNG -DALPAKA_DEFAULT_HOST_MEMORY_ALIGNMENT=128 \
  ../../src/alpaka/LSTEvent.dev.cc 2>&1 | head -60
echo "SYNTAX RC=${PIPESTATUS[0]}"
