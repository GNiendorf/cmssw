#!/usr/bin/env python3
"""Rolling-window HLT validation: stage RAW files with xrdcp, run every arm on each, delete the staged copy.
Concurrency is read from val5k/maxj each loop (default 6); a job starts only if MemAvailable > MEM_GB and disk
free > DISK_GB. Arms are taken from val5k/arms.txt (name per line) and only once val5k/<arm>.READY exists.
State lines go to val5k/sched.log; per-job results to val5k/jobs.status."""
import os, subprocess, time, collections

V = "/mnt/data1/gsn27/here/hltqcd/val5k"
STAGE = f"{V}/stage"
MEM_GB, DISK_GB, STAGE_MAX = 100, 80, 6
REDIR = ["root://cmsxrootd.fnal.gov/", "root://cms-xrd-global.cern.ch/"]


def log(msg):
    with open(f"{V}/sched_5k.log", "a") as fh:
        fh.write(time.strftime("%H:%M:%S ") + msg + "\n")


def mem_avail_gb():
    for line in open("/proc/meminfo"):
        if line.startswith("MemAvailable"):
            return int(line.split()[1]) / 1e6
    return 0


def disk_free_gb():
    st = os.statvfs("/mnt/data1")
    return st.f_bavail * st.f_frsize / 1e9


def maxj():
    try:
        return int(open(f"{V}/maxj_5k").read().strip())
    except Exception:
        return 6


def arms():
    return [a.strip() for a in open(f"{V}/arms_5k.txt") if a.strip()]


def main():
    env = dict(os.environ, X509_USER_PROXY="/mnt/data1/gsn27/here/hltqcd/x509_proxy")
    os.makedirs(STAGE, exist_ok=True)
    files = []  # interleave samples so both progress
    lists = {s: [l.split() for l in open(f"{V}/files_{s}.txt") if l.strip()] for s in ("ttbar", "qcd")}
    for i in range(max(len(v) for v in lists.values())):
        for s in ("ttbar", "qcd"):
            if i < len(lists[s]):
                lfn = lists[s][i][0]; loc = lists[s][i][1] if len(lists[s][i]) > 1 else ""
                files.append((s, lfn, loc))
    path = {}        # lfn -> local path once available
    staged = set()   # lfns we copied (to delete later)
    fetching = {}    # lfn -> Popen
    running = {}     # (arm, lfn) -> (Popen, tries)
    done = collections.defaultdict(set)
    failed = collections.defaultdict(int)
    for s, lfn, loc in files:
        if loc:
            path[lfn] = loc
    log(f"start: {len(files)} files")
    while True:
        A = [a for a in arms() if os.path.exists(f"{V}/{a}.READY")]
        # finished fetches
        for lfn, p in list(fetching.items()):
            if p.poll() is not None:
                del fetching[lfn]
                dst = f"{STAGE}/{os.path.basename(lfn)}"
                if p.returncode == 0 and os.path.getsize(dst) > 0:
                    path[lfn] = dst; staged.add(lfn); log(f"staged {os.path.basename(lfn)}")
                else:
                    failed[("fetch", lfn)] += 1; log(f"FETCH FAIL {lfn} rc={p.returncode}")
                    if failed[("fetch", lfn)] >= 3:
                        done[lfn].update(arms()); log(f"GAVE UP fetching {lfn}")
                    if os.path.exists(dst): os.remove(dst)
        # finished jobs
        for key, (p, tries) in list(running.items()):
            if p is not None and p.poll() is not None:
                del running[key]; arm, lfn = key
                tag = os.path.basename(lfn)[:8]
                with open(f"{V}/jobs_5k.status", "a") as fh:
                    fh.write(f"{time.strftime('%H:%M:%S')} rc={p.returncode} {arm} {tag}\n")
                if p.returncode == 0:
                    done[lfn].add(arm)
                elif tries < 2:
                    failed[key] += 1; log(f"retry {arm} {tag}")
                    running[key] = (None, tries + 1)  # placeholder, relaunched below
                else:
                    done[lfn].add(arm); log(f"GAVE UP {arm} {tag}")
        # delete staged files whose arms are all done
        full = set(arms())
        for lfn in list(staged):
            if full <= done[lfn] and not any(k[1] == lfn for k in running):
                os.remove(path[lfn]); staged.discard(lfn); del path[lfn]; log(f"deleted {os.path.basename(lfn)}")
        # stage more
        pending_remote = [(s, l) for s, l, loc in files if not loc and l not in path and l not in fetching
                          and not full <= done[l] and failed[("fetch", l)] < 3]
        while pending_remote and len(staged) + len(fetching) < STAGE_MAX and disk_free_gb() > DISK_GB + 15 and len(fetching) < 3:
            s, lfn = pending_remote.pop(0)
            dst = f"{STAGE}/{os.path.basename(lfn)}"
            red = REDIR[failed[("fetch", lfn)] % 2]
            fetching[lfn] = subprocess.Popen(["xrdcp", "-f", "--nopbar", red + lfn, dst], env=env,
                                             stdout=subprocess.DEVNULL, stderr=open(f"{V}/xrdcp.err", "a"))
        # launch jobs
        nrun = sum(1 for p, _ in running.values() if p is not None)
        cands = [(s, lfn, arm) for s, lfn, loc in files if lfn in path for arm in A
                 if arm not in done[lfn] and ((arm, lfn) not in running or running[(arm, lfn)][0] is None)]
        for s, lfn, arm in cands:
            if nrun >= maxj() or mem_avail_gb() < MEM_GB or disk_free_gb() < DISK_GB:
                break
            tries = running[(arm, lfn)][1] if (arm, lfn) in running else 0
            tag = os.path.basename(lfn)[:8]
            p = subprocess.Popen(["bash", f"{V}/run_val.sh", arm, s, path[lfn], tag],
                                 stdout=open(f"{V}/run_val_5k.out", "a"), stderr=subprocess.STDOUT)
            running[(arm, lfn)] = (p, tries); nrun += 1
            log(f"launch {arm} {s} {tag} (running {nrun}, mem {mem_avail_gb():.0f} GB, disk {disk_free_gb():.0f} GB)")
            time.sleep(20)  # let memory settle before the next launch decision
        if all(full <= done[l] for _, l, _ in files) and not running and not fetching:
            log("ALL DONE"); break
        time.sleep(15)


if __name__ == "__main__":
    main()
