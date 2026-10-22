STATUS: DONE
COMMIT: 254885b4 (cs_lsp_bench), 22526ac6 (lock_registry), on lane/time-assertions in /scratch/agent/wt/cbm-time (base 0695bd0e)
CHANGED: tests/test_cs_lsp_bench.c; tests/test_lock_registry.c; src/foundation/lock_registry.c; src/foundation/lock_registry_internal.h
OPEN: TSan lane (`scripts/test.sh --tsan`) not run on the new seam; the clock pointer is written under the registry lock while idle and read by threads created afterwards.
BLOCKERS: none

---

## 1. tests/test_cs_lsp_bench.c `cslsp_bench_resolution_ratio` (commit 254885b4)

Proxy before: `ASSERT(ms < 200.0)` native, `ASSERT(ms < 2000.0)` under `CBM_SANITIZED`. That is the wall time of one `cbm_extract_file` call, so it measured ~/.claude/references/build-cycle/host CPU contention. Also, the 45% ratio floor sat inside `if (calls >= 20)`, so if extraction lost calls the floor was skipped without failing.

Mechanism now: the test asserts `calls >= 20` (the fixture yields 54), `resolved >= 1`, and `resolved * 100 >= calls * 45` with no guard. The parse time is still measured and printed in the `cs bench:` line and is never asserted. The `foundation/sanitized.h` include, which only the time bound used, is removed.

Seam: none.

30-run result under load: 30/30 PASS. Every run printed `146 lines, 54 calls, 50 resolved (93%), 45 high-conf (83%)`. Parse times ranged from 56.99 ms to 173.79 ms. Load average (1-minute) was 22.92 to 25.41 on 16 cores.

Mutant: in `internal/cbm/lsp/cs_lsp.c`, replaced `recv_type = cs_eval_expr_type(ctx, recv);` with `recv_type = cbm_type_unknown(); /* MUTANT */`. The sed matched both identical lines, in `cs_eval_invocation_type` (line 1299) and `cs_resolve_invocation` (line 2166), so instance-call receivers lose their type. Result:
```
cs bench: 146 lines, 54 calls, 16 resolved (30%), 11 high-conf (20%), 57.81 ms
FAIL tests/test_cs_lsp_bench.c:235: resolved * 100 (1600) not >= calls * 45 (2430)
```
Restored with `git checkout -- internal/cbm/lsp/cs_lsp.c` and rebuilt. Afterwards both suites passed (17 passed).

## 2. tests/test_lock_registry.c `lock_registry_absolute_deadline_survives_repeated_wakes` (commit 22526ac6)

Proxy before: `ASSERT_GTE(elapsed_ms, 150); ASSERT_TRUE(elapsed_ms < 350);`, which timed the tail's `acquire` with a 200 ms deadline while the observer broadcast every 5 ms (`cbm_usleep(5000)`). If the tail thread was descheduled, the measured duration grew past 350 ms even though the deadline logic was correct.

Seam: `lock_registry_should_stop` and `lock_registry_bounded_deadline` called `cbm_now_ms()` directly. They now call `lock_registry_now_ms(registry)`, which returns `cbm_now_ms()` unless a test clock is installed.
- The setter is `cbm_lock_registry_set_clock_for_test(registry, fn, ctx)` in `lock_registry_internal.h`. It follows the same idle-only rule as `cbm_lock_registry_set_stage_hook_for_test`.
- `registry_free` clears the clock.
- Both deadline reads must use the same clock because a deadline is absolute in that clock's domain. Otherwise the native retry bound would mix two domains.
- Condition waits still use the real clock, and production behaviour is unchanged when no clock is installed.

Mechanism now:
- The test clock starts at real time plus one hour, so no condition wait times out on its own and only broadcasts wake the waiters.
- The holder takes the lock shared with `try_acquire`. The head is an exclusive waiter with no deadline, which owns the attempt and parks. The tail is an exclusive waiter with absolute deadline D = clock start + 200, queued behind the head.
- The test wakes the waiters 16 times, at clock values that step evenly up to D − 1. After each wake it waits until `condition_wait_call_count` has grown by at least 2 and `condition_waiter_count == 2`, or until the tail returns. It asserts the tail did not return, that all 16 wakes completed this way, and that `waiter_count == 2`.
- The clock is then set to exactly D and one more broadcast is sent. The test asserts the tail returns `BUSY` with a NULL lease, `waiter_count == 1` (only the head remains), and the head is still waiting.
- Cleanup cancels the head. The test then asserts head `BUSY`, a NULL head lease, and holder release `OK`.
- The only remaining real-time bounds are 5000 ms completion waits. The test never compares elapsed time to a bound.

30-run result under load: 30/30 PASS. Load average (1-minute) was 18.68 to 22.92 on 16 cores.

Mutant: this re-anchors a relative wait budget after every queued wake (the bug class the test guards). It was applied to `src/foundation/lock_registry.c` in `lock_registry_acquire_internal`:
```
631a632
>     uint64_t mutant_budget = deadline_ms == UINT64_MAX ? 0 : deadline_ms - lock_registry_now_ms(registry); /* MUTANT */
670a672
>             if (deadline_ms != UINT64_MAX) deadline_ms = lock_registry_now_ms(registry) + mutant_budget; /* MUTANT */
```
Result: `lock_registry_absolute_deadline_survives_repeated_wakes  FAIL tests/test_lock_registry.c:1213: ASSERT(returned_at_deadline)`. The other 15 tests in the suite passed. Restored by copying back the saved file and rebuilt (17 passed). `grep MUTANT` finds nothing in either source.

## Load

`/scratch/agent/cbm-time-load/load.sh` ran this loop: `rm -rf` of a private build dir, then `make -f Makefile.cbm -j16 BUILD_DIR=/scratch/agent/cbm-time-load/build cbm`. `/scratch/agent/cbm-time-load/loop.sh SUITE TEST 30` ran `build/c/test-runner SUITE` 30 times and recorded `/proc/loadavg` and the verdict for TEST in each run. Both loops ran against the sanitized (ASan+UBSan) runner with the final sources. The load generator was stopped afterwards.

## Commands run green

- `timeout 1500 make -f Makefile.cbm -j16 build/c/test-runner`: exit 0, built with `-Wall -Wextra -Werror`.
- `timeout 1500 build/c/test-runner lock_registry cs_lsp_bench`: 17 passed.
- `timeout 1500 scripts/test.sh --suites lock_registry,cs_lsp_bench`: exit 0, 17 passed, 0 failed. The script also prints a `sort: separator must be exactly one character long` message from its own slowest-suites summary. That output is not part of either suite, and the script was not changed.
- `clang-format --dry-run --Werror` on the four touched files: clean. They were formatted with `clang-format -i` on those files only.
- `make -f Makefile.cbm lint-format`: exit 2. Every finding is in a file this lane did not touch (`src/cli/cli.c` 1, `src/daemon/frontend.c` 2, `src/daemon/runtime.c` 10, `src/daemon/version_cohort.c` 6). None of the touched files appear in its output.

Process note: the first sanitized build was silent for more than 2 minutes while compiling the vendored `zstd.c` and `lz4hc.c`. Inspection showed `cc1` at about 90% CPU, so it was progressing and was left running instead of being killed. It exited 0.

## Orchestrator verification (2026-10-22)

The open item (TSan on the new clock seam) was run before merge: `build/c/test-runner-tsan lock_registry cs_lsp_bench` on the lane tip, 17 passed, 0 ThreadSanitizer warnings. Merged to main at 22526ac6 and pushed.
