#!/bin/bash
# replay_kernel_registers.sh [-a sm_89] [-t] [-q] [lib ...]: register report per CUDA kernel of the MkFitAlpaka CUDA
# libraries and plugins of the current CMSSW area (default), from cuobjdump -res-usage (harness lane).
#   -a ARCH  architecture shown in the table (default sm_89 = L40/L4); every architecture is checked for LOCAL
#   -t       also scan the package's CUDA test binaries ($CMSSW_BASE/test/$SCRAM_ARCH/*MkFitAlpaka*|*mkfit*|...Cuda*)
#   -q       only the summary line and the failures
# Per kernel "REG:n STACK:n LOCAL:n SHARED:n" (cuobjdump tokens, grep-able: integ's test/integ_check_registers.sh
# greps 'LOCAL:[1-9]'): REG registers/thread, STACK bytes (CUDA div/sqrt slow paths use 16-48; local arrays too),
# LOCAL bytes (register spills / local arrays), SHARED static shared bytes. Kernel = the alpaka functor name.
# Exit status 1 if any kernel has LOCAL > 0 on any architecture (DESIGN round 3: a spill is a build failure).
# Run inside a CMSSW environment. Wire it after every build: scram b -j 8 && test/replay_kernel_registers.sh -q
ARCH=sm_89; TESTS=0; QUIET=0
while getopts "a:tq" o; do case $o in a) ARCH=$OPTARG ;; t) TESTS=1 ;; q) QUIET=1 ;; *) exit 2 ;; esac; done
shift $((OPTIND - 1))
LIBS="$@"
if [ -z "$LIBS" ]; then
  LIBS=$(ls $CMSSW_BASE/lib/$SCRAM_ARCH/*MkFitAlpaka*CudaAsync*.so 2>/dev/null)
  [ $TESTS = 1 ] && LIBS="$LIBS $(ls $CMSSW_BASE/test/$SCRAM_ARCH/ 2>/dev/null | grep -i 'cuda' | sed "s#^#$CMSSW_BASE/test/$SCRAM_ARCH/#")"
fi
for f in $LIBS; do
  echo "#FILE $(basename $f)"
  cuobjdump -res-usage $f 2>/dev/null
done | python3 -c '
import re, subprocess, sys
arch_show, quiet = sys.argv[1], sys.argv[2] == "1"
rows = []          # (file, arch, kernel, reg, stack, local, shared)
f = arch = fn = None
for line in sys.stdin:
    if line.startswith("#FILE "): f = line.split()[1]; continue
    m = re.match(r"\s*arch = (sm_\d+)", line)
    if m: arch = m.group(1); continue
    m = re.match(r"\s*Function (\S+):", line)
    if m: fn = m.group(1); continue
    if "REG:" in line and fn:
        d = dict(re.findall(r"(\w+(?:\[\d+\])?):(\d+)", line))
        rows.append((f, arch, fn, int(d.get("REG", 0)), int(d.get("STACK", 0)), int(d.get("LOCAL", 0)), int(d.get("SHARED", 0))))
        fn = None
names = {}
mangled = sorted({r[2] for r in rows})
if mangled:
    out = subprocess.run(["c++filt"], input="\n".join(mangled), capture_output=True, text=True).stdout.split("\n")
    for m_, d_ in zip(mangled, out):
        k = re.search(r"gpuKernel<([^,<]+)", d_)
        names[m_] = (k.group(1).replace("alpaka_cuda_async::", "") if k else d_[:80])
fails = [r for r in rows if r[5] > 0]
shown = [r for r in rows if r[1] == arch_show and not names[r[2]].startswith("__cuda")]
seen = set()
if not quiet:
    print("%-40s %-60s %s" % ("file", "kernel (" + arch_show + ")", "usage (bytes; REG = registers per thread)"))
    for r in shown:
        key = (r[0], names[r[2]], r[3], r[4], r[5], r[6])
        if key in seen: continue
        seen.add(key)
        print("%-40s %-60s REG:%d STACK:%d LOCAL:%d SHARED:%d" % (r[0][:40], names[r[2]][:60], r[3], r[4], r[5], r[6]))
archs = sorted({r[1] for r in rows if r[1]})
mx = max(shown, key=lambda r: r[3]) if shown else None
print("[registers] %d kernels x %d archs (%s); max REG on %s: %s; STACK>0: %d; LOCAL>0: %d -> %s" % (
    len({(r[0], r[2]) for r in rows}), len(archs), ",".join(archs), arch_show,
    ("%d (%s)" % (mx[3], names[mx[2]])) if mx else "-", len({(r[0], r[2]) for r in shown if r[4] > 0}),
    len(fails), "FAIL" if fails else "OK"))
for r in fails:
    print("[registers] FAIL LOCAL:%d %s %s %s" % (r[5], r[0], r[1], names[r[2]]))
sys.exit(1 if fails else 0)
' $ARCH $QUIET
