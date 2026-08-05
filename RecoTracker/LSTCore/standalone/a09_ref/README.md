# A09 -- SEED-RETIREMENT UNIFICATION -- artifact index

| file | what it is |
|------|------------|
| `STATUS.md` | the running milestone log. Read this first. |
| `MECHANISM_MAP.md` | the structural map: the five mechanisms, the two release mechanisms, the ordering analysis, and what the structure says can be deleted. Written from the source before any run. |
| `a09_scoreboard.txt` | every measured configuration with full metrics, plus the two empirical laws. |
| `TRANSFERABLE.md` | what should carry into the combined config and the known conflict surface. |
| `VERDICT_DRAFT.md` | the verdict as it firmed up. |
| `protoA09_vs_protoFIN.diff` | the COMPLETE source delta against the Baseline agent's workspace. |
| `a09_run.sh` | the runner. Carries the frozen prefix AND the assembled baseline internally; overrides come after and win. |
| `batch2.sh`, `batch3.sh` | the launched batches, exactly as run. |
| `a09_tab.py` / `a09_tab2.py` | overall+displaced-band / per-region tables. Fall back to fin_ref, xc_ref, rebase_ref so sibling tags can be quoted in the same table. |
| `a09_led.py` | pulls the retirement ledger lines out of the run logs. |
| `a09_laws.py` | the two-laws table: truth partition vs measured eff/dup, with the fit. |
| `a09_front.py` | interpolates each mode's -XCT curve to a target duplicate rate. |
| `r_<TAG>.{root,json,log,cmd}` | one run each. `.cmd` records the exact overrides and the wall time. |

Binary: `protoA09/bin/chainproto_a09`, md5 fc636849f3bffa4f159e426af3d79408.
`protoA09/bin/chainproto` is deliberately left as the UNMODIFIED protoFIN binary
(md5 519b0abc34a28cd1e803d6b9407ef224) that batch 1 ran on, so the pre-edit and post-edit
gates can both be reproduced from this workspace.
