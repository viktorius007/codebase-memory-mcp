# private/

Coordinator working notes, committed and pushed with the repository. The GitHub repository is
public: nothing here may hold a secret or personal data. Every file costs the next session reading
time, so nothing stays without a named reader.

- Bulky or regenerable data never lives here. Corpora, rust-analyzer truth, SCIP indexes and the
  frozen base scores live in `~/Developer/cbm-bench` and are rebuilt by `tools/rust-bench/`
  (`fetch-corpora.sh`, `gen-truth.sh`, `baseline.sh`). Scratch goes under `/tmp`. Agent output
  goes to `rust-run/merge/out/`, which git ignores; a report a later session needs is moved to
  `rust-run/merge/reports/` before it is cited.
- Before you add a file here, name the session step that will read it. If none will, do not write it.
- When a folder's result is on `main` or recorded as a decision in `HANDOFF.md`, `trash` the folder
  in the same session. `rm` is hook-banned.
- At session close, `HANDOFF.md` lists every `private/` path the next session still needs. Trash
  every other path under `private/` that your session or an earlier one created, except paths a
  live agent is writing to.
- Never move a dead folder into `archive/` or `retired/`. Trash it; git history and `HANDOFF.md`
  carry what mattered.
- Housekeeping is the coordinator's standing duty, never a request to wait for: when a lane's report
  is recorded, trash its scratch, remove its worktree and delete its merged branch in the same step;
  run `df -h /System/Volumes/Data` before every dispatch and stop dispatching under 10% free.
