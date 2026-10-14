# Lane itot_firm/stack3 (F1): S2 for timing / memory = #290's cpu timing menu (r22_noasync/cfg/final_cpu.py, aea1b7b7bd2) with
# A-star: noOT, 14 forward skip pairs dropped (43 CA pairs), q5, caps 4194303 doublets / 40,960 + budget 32768 tuples,
# S3 window (mode 3, A = -4, B = 12), Torch-rt = tn_scratch_perbin0.993_cudafix_FP16.pt at 0.5 (torch_noot's per-bin model repackaged for CMSSW's CUDA load; CPU-bitwise identical), Ptc (barrel-terminal 3-hit path, budget 32,768, triplet cleaner, Torch 3-hit bypass, tcpLSTriplets False).
# fillStatistics OFF, Torch preselection log OFF. noT5cc + c2 + the S3 / 3-hit / Torch code are in the CODE: run ONLY in the environment of
# itot_firm/stack3/CMSSW_20_1_0_pre3. The Torch-rt model is also in that area's src (RecoTracker/FinalTrackSelectors/data/...), so no
# CMSSW_SEARCH_PATH change is needed there.
import sys, os
_ITOT_DIR = os.environ.get('ITOT_DIR') or os.path.dirname(os.path.abspath(__file__))
sys.argv = [sys.argv[0]]
exec(open(os.path.join(_ITOT_DIR, 'final_cpu.py')).read())
exec(open(os.path.join(_ITOT_DIR, 'arms_block.py')).read())
exec(open(os.path.join(_ITOT_DIR, 's3_block.py')).read())
_otpairs_variant(process, '', stats=False)
_arms_q5(process)
_arms_caps(process, '4194303', '40960')
_arms_drop_fskip(process)
_s3k_window(process)
_s3k_torch(process, 'tn_scratch_perbin0.993_cudafix_FP16.pt', 0.5)
for _l in _ARMS_HP:
    if hasattr(process, _l):
        getattr(process, _l).logPreselection = False
if 32768 > 0:
    _s3k_ptc(process, 32768)
_ca = process.hltPhase2PixelTracksSoA; _hp = process.hltPhase2PixelTrackTorchHighPuritySelector
print('STACK3 TIMING ARM S2 cpu: pairs', len(_ca.geometry.pairGraph) // 2, 'maxD', _ca.maxNumberOfDoublets.value(), 'maxT', _ca.maxNumberOfTuples.value(),
      'win', _ca.sizeWinMode.value(), 'budget', _ca.maxBarrelTripletTuples.value() if hasattr(_ca, 'maxBarrelTripletTuples') else 0,
      'stats', _ca.fillStatistics.value(), 'torch', _hp.model.value(), _hp.scoreThreshold.value(), 'maxTracks', _hp.maxNumberOfTracks.value(),
      'log', _hp.logPreselection.value())
