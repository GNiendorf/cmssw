#!/bin/bash
# integ: register CI check (review H5, DESIGN "Registers"). Runs the harness register report
# (test/replay_kernel_registers.sh) on the package's CUDA libraries and FAILS if any kernel has LOCAL > 0 (spills or
# local arrays in local memory). The report is printed in full. Used by 'scram b runtests' (test
# testMkFitAlpakaKernelRegisters). The build only WARNS on a spill (ptxas -warn-spills in the package BuildFiles,
# no -Werror since review M5): this test is the gate. Exit 0 = no kernel with LOCAL > 0; 2 = no CUDA library (skip).
here=$(dirname $(readlink -f $0))
LIBS="$@"
[ -z "$LIBS" ] && LIBS=$(ls $CMSSW_BASE/lib/$SCRAM_ARCH/*MkFitAlpaka*CudaAsync*.so 2>/dev/null)
[ -z "$LIBS" ] && LIBS=$(ls $CMSSW_RELEASE_BASE/lib/$SCRAM_ARCH/*MkFitAlpaka*CudaAsync*.so 2>/dev/null)
if [ -z "$LIBS" ]; then echo "no MkFitAlpaka CUDA library: skipped"; exit 2; fi
out=$(bash $here/replay_kernel_registers.sh $LIBS)
echo "$out"
bad=$(echo "$out" | grep -E 'LOCAL:[1-9]' | wc -l)
nk=$(echo "$out" | grep -c 'REG:')
echo "kernels: $nk   with LOCAL > 0: $bad"
[ "$bad" -eq 0 ] || { echo "FAIL: kernels with LOCAL > 0:"; echo "$out" | grep -E 'LOCAL:[1-9]'; exit 1; }
echo PASS
