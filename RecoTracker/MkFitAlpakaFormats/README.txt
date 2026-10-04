RecoTracker/MkFitAlpakaFormats: CUDA/ROCm dictionaries of the RecoTracker/MkFitAlpaka event products.
No device code here, on purpose (the RecoTracker/LST vs LSTCore pattern). Host-type dictionaries stay in
RecoTracker/MkFitAlpaka/src/classes_def.xml (host library, no device code).

Why a second package: with src/alpaka/classes_cuda_def.xml inside RecoTracker/MkFitAlpaka, the CUDA dictionary lands in
libRecoTrackerMkFitAlpakaCudaAsync.so (which holds the package kernels), and then EVERY kernel compiled in the portable
plugin library (plugins/alpaka/*.dev.cc) failed to launch: cudaErrorInvalidResourceHandle at cudaLaunchKernel
(compute-sanitizer: cuKernelGetName invalid handle). Bisected 2026-10-03 (integ, round 3): products commit 286be21
fails the ES CUDA test (3,424,147 mismatches, kernels never ran); the same tree without the CUDA dictionary passes;
the same tree with the dictionary moved here passes. Root cause not traced further.

Git: this directory is its own repository (branch r3/integ), next to the MkFitAlpaka repository. To track it in another
CMSSW area:
  git clone <integ area>/CMSSW/src/RecoTracker/MkFitAlpakaFormats $CMSSW_BASE/src/RecoTracker/MkFitAlpakaFormats
  (later: git -C $CMSSW_BASE/src/RecoTracker/MkFitAlpakaFormats pull)
A product added to MkFitAlpaka needs its device lines here (classes_cuda_def.xml, classes_rocm_def.xml) and its host
lines in MkFitAlpaka/src/classes_def.xml.
