#!/usr/bin/env python3
"""val5k scheduler of the MkFitAlpaka menu lane (round 5, D5-f) = lanes/pre2_4way/scripts/scheduler4.py adapted to 2 arms, ttbar 5k only.
Arms: stock = the existing pre2_4way vA outputs of the SAME job.sh command line (linked into runs/stock, vA unchanged since they were
made); port = cp -a vA + MkFitAlpaka + MkFitAlpakaFormats (main heads) + --customise customizeHLTforMkFitAlpaka; stockrep = vA rerun on
the first 2 local files (reproducibility check of the reused stock outputs). All arms registered up front.
List: hltqcd/val5k/files_ttbar.txt (50 files: 5 local, 45 staged with xrdcp into stage/, deleted once all arms ran on them).
A job of an arm starts only when <arm>.READY and GO exist, fewer than maxj (file, default 2) jobs run and MemAvailable > 80 GB.
Staging: <= nfetch (file, default 2) transfers; a transfer starts only if stage bytes on disk + growth_frac x (remaining bytes in flight
+ new file) <= stage_gb (files, defaults 18 / 1.0) AND disk free - remaining bytes in flight - new file >= 45 GB (strict disk floor,
lane change vs scheduler4 which checked the free space only at the start). STOP file: no new jobs or transfers, drain and exit.
State lines every 10 min in sched.log; chain.sh writes PROGRESS.txt.
STREAM mode (file 'stream' exists; lane addition after the disk fell below the floor with one staged file): remote files are not
staged; the job reads root://<redirector>/<lfn> directly (xrootd, X509 proxy in the job env). Same events, no disk."""
import os, subprocess, time, collections

L = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/mem_ref/lanes/mkfit_alpaka/r5_menu/val5k"
VL = "/mnt/data1/gsn27/here/hltqcd/val5k"
STAGE = f"{L}/stage"; ARMS = ["port", "stockrep"]
ONLY = {"stockrep": 2}  # arm -> run only on the first N local files
MEM_GB, DISK_MIN = 80, 45
EST = {"ttbar": 12.7e9}
REDIR = ["root://cmsxrootd.fnal.gov/", "root://cms-xrd-global.cern.ch/"]


def log(m):
    with open(f"{L}/sched.log", "a") as fh: fh.write(time.strftime("%I:%M:%S %p ") + m + "\n")


def mem():
    for line in open("/proc/meminfo"):
        if line.startswith("MemAvailable"): return int(line.split()[1]) / 1e6
    return 0


def disk():
    st = os.statvfs("/mnt/data1"); return st.f_bavail * st.f_frsize / 1e9


def ctl(name, default):
    try: return float(open(f"{L}/{name}").read().strip())
    except Exception: return default


def fsize(p):
    try: return os.path.getsize(p)
    except OSError: return 0


def main():
    env = dict(os.environ, X509_USER_PROXY="/mnt/data1/gsn27/here/hltqcd/x509_proxy")
    os.makedirs(STAGE, exist_ok=True); os.makedirs(f"{L}/logs", exist_ok=True)
    files = [("ttbar", f[0], f[1] if len(f) > 1 else "") for f in (l.split() for l in open(f"{VL}/files_ttbar.txt") if l.strip())]
    files.sort(key=lambda f: 0 if f[2] else 1)  # local first, then the list order
    smp = {lfn: s for s, lfn, _ in files}
    tag = lambda lfn: os.path.basename(lfn)[:8]
    path, staged, fetching, running = {}, set(), {}, {}
    done = collections.defaultdict(set); failed = collections.defaultdict(int)
    for i, (s, lfn, lp) in enumerate(files):
        if lp: path[lfn] = lp
        for a in ARMS:
            if a in ONLY and not (lp and i < ONLY[a]): done[lfn].add(a)  # arm restricted to the first local files
            if fsize(f"{L}/runs/{a}/{s}/{tag(lfn)}/DQM_{tag(lfn)}.root") > 1000000: done[lfn].add(a)
    byname = {os.path.basename(lfn): lfn for _, lfn, _ in files}
    for s, lfn, lp in files:  # resume: complete staged copies
        dst = f"{STAGE}/{os.path.basename(lfn)}"
        if not lp and os.path.exists(dst) and os.path.exists(dst + ".ok"): path[lfn] = dst; staged.add(lfn)
    for f in os.listdir(STAGE):  # partial files of killed transfers
        lfn = byname.get(f)
        if lfn and lfn not in path and not f.endswith(".ok"): os.remove(f"{STAGE}/{f}"); log(f"removed partial {f}")
    log(f"start: {len(files)} files, {len(path)} available, {sum(1 for _, l, _ in files if set(ARMS) <= done[l])} done")
    last_state = 0
    while True:
        A = [a for a in ARMS if os.path.exists(f"{L}/{a}.READY")] if os.path.exists(f"{L}/GO") else []
        for lfn, p in list(fetching.items()):
            if p.poll() is not None:
                del fetching[lfn]; dst = f"{STAGE}/{os.path.basename(lfn)}"
                if p.returncode == 0 and fsize(dst) > 0:
                    open(dst + ".ok", "w").close(); path[lfn] = dst; staged.add(lfn); log(f"staged {smp[lfn]} {tag(lfn)} ({fsize(dst)/1e9:.1f} GB)")
                else:
                    failed[("fetch", lfn)] += 1; log(f"FETCH FAIL {lfn} rc={p.returncode}")
                    if failed[("fetch", lfn)] >= 4: done[lfn].update(ARMS); log(f"GAVE UP fetching {lfn}")
                    if os.path.exists(dst): os.remove(dst)
        for key, (p, tries) in list(running.items()):
            if p is not None and p.poll() is not None:
                del running[key]; arm, lfn = key
                with open(f"{L}/jobs.status", "a") as fh: fh.write(f"{time.strftime('%I:%M:%S %p')} rc={p.returncode} {arm} {smp[lfn]} {tag(lfn)}\n")
                if p.returncode == 0: done[lfn].add(arm)
                elif tries < 1: log(f"retry {arm} {smp[lfn]} {tag(lfn)}"); running[key] = (None, tries + 1)
                else: done[lfn].add(arm); log(f"GAVE UP {arm} {smp[lfn]} {tag(lfn)}")
                if p.returncode != 0 and path.get(lfn, "").startswith("root://"): failed[("job", lfn)] += 1; del path[lfn]  # retry via the other redirector
        for lfn in list(staged):
            if set(ARMS) <= done[lfn] and not any(k[1] == lfn for k in running):
                os.remove(path[lfn]); os.remove(path[lfn] + ".ok"); staged.discard(lfn); del path[lfn]; log(f"deleted {smp[lfn]} {tag(lfn)}")
        inflight = list(fetching)
        ondisk = sum(fsize(f"{STAGE}/{f}") for f in os.listdir(STAGE))
        remain = sum(max(0.0, EST[smp[l]] - fsize(f"{STAGE}/{os.path.basename(l)}")) for l in inflight)
        budget, g = ctl("stage_gb", 18) * 1e9, ctl("growth_frac", 1.0)
        pend = [] if os.path.exists(f"{L}/stream") else [l for _, l, lp in files if not lp and l not in path and l not in fetching
                and not set(ARMS) <= done[l] and failed[("fetch", l)] < 4]
        for lfn in pend:
            if len(inflight) >= ctl("nfetch", 2) or os.path.exists(f"{L}/STOP") or ondisk > budget: break
            if disk() * 1e9 - remain - EST[smp[lfn]] < DISK_MIN * 1e9: break  # strict disk floor incl. bytes still to arrive
            if ondisk + g * (remain + EST[smp[lfn]]) > budget: continue
            dst = f"{STAGE}/{os.path.basename(lfn)}"
            red = REDIR[failed[("fetch", lfn)] % 2]
            fetching[lfn] = subprocess.Popen(["xrdcp", "-f", "--nopbar", red + lfn, dst], env=env,
                                             stdout=subprocess.DEVNULL, stderr=open(f"{L}/logs/xrdcp.err", "a"))
            inflight.append(lfn); remain += EST[smp[lfn]]
            log(f"fetch {smp[lfn]} {tag(lfn)} (disk {disk():.0f} GB, stage on disk {ondisk/1e9:.0f} GB, in flight {len(inflight)}, remaining {remain/1e9:.0f} GB)")
        if os.path.exists(f"{L}/stream"):
            for _, l, lp in files:
                if not lp and l not in path and l not in fetching: path[l] = REDIR[failed[("job", l)] % 2] + l
        nrun = sum(1 for p, _ in running.values() if p is not None)
        if A and not os.path.exists(f"{L}/STOP"):
            order = [f for f in files if f[1] in path]
            order.sort(key=lambda f: 0 if f[1] in staged else 1)  # staged first: frees stage space soonest
            cands = [(s, lfn, arm) for s, lfn, _ in order for arm in A
                     if arm not in done[lfn] and ((arm, lfn) not in running or running[(arm, lfn)][0] is None)]
            for s, lfn, arm in cands:
                if nrun >= ctl("maxj", 2) or mem() < MEM_GB: break
                tries = running[(arm, lfn)][1] if (arm, lfn) in running else 0
                p = subprocess.Popen(["bash", f"{L}/scripts/job.sh", arm, s, path[lfn], tag(lfn)],
                                     stdout=open(f"{L}/logs/run.out", "a"), stderr=subprocess.STDOUT, start_new_session=True, env=env)
                running[(arm, lfn)] = (p, tries); nrun += 1
                with open(f"{L}/pids.txt", "a") as fh: fh.write(f"{p.pid} {arm} {s} {tag(lfn)}\n")
                log(f"launch {arm} {s} {tag(lfn)} pid {p.pid} (running {nrun}, mem {mem():.0f} GB, disk {disk():.0f} GB)")
                time.sleep(20)
        if time.time() - last_state > 600:
            last_state = time.time()
            nd = sum(1 for _, l, _ in files if set(ARMS) <= done[l])
            na = {a: sum(1 for _, l, _ in files if a in done[l]) for a in ARMS}
            log(f"state: files done (all arms) ttbar {nd}/{len(files)}; per arm {na}; running {nrun} fetching {len(fetching)} staged {len(staged)} disk {disk():.0f} GB mem {mem():.0f} GB")
        if all(set(ARMS) <= done[l] for _, l, _ in files) and not running and not fetching:
            log("ALL DONE"); break
        if os.path.exists(f"{L}/STOP") and not any(p is not None and p.poll() is None for p, _ in running.values()):
            for p in fetching.values(): p.kill()
            log("STOPPED"); break
        time.sleep(15)


if __name__ == "__main__":
    main()
