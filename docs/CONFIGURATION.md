# Configuration Reference

This page documents the configuration files that `codebase-memory-mcp` reads or writes today.

## At a Glance

| Purpose | Path | Format | Notes |
|---|---|---|---|
| Global custom extension mapping | `$XDG_CONFIG_HOME/codebase-memory-mcp/config.json` | JSON | Falls back to `~/.config/codebase-memory-mcp/config.json` when `XDG_CONFIG_HOME` is unset. |
| Per-project custom extension mapping | `{repo_root}/.codebase-memory.json` | JSON | Overrides conflicting global `extra_extensions` entries. |
| CLI-managed runtime settings | `${CBM_CACHE_DIR:-~/.cache/codebase-memory-mcp}/_config.db` | SQLite | Written by `codebase-memory-mcp config set/reset`. |
| UI settings | `${CBM_CACHE_DIR:-~/.cache/codebase-memory-mcp}/config.json` | JSON | Stores `ui_enabled` and `ui_port`. |
| Daemon operation log | `${CBM_CACHE_DIR:-~/.cache/codebase-memory-mcp}/logs/cbm-daemon.log` | Structured log | Durable daemon lifecycle, watcher/indexing, UI, resource, and error events. |
| Admission conflict log | `${CBM_CACHE_DIR:-~/.cache/codebase-memory-mcp}/logs/daemon-conflicts.ndjson` | NDJSON | Exact-build, ABI, and canonical-cache conflicts. |
| Activation log | `${CBM_CACHE_DIR:-~/.cache/codebase-memory-mcp}/logs/activation-events.ndjson` | NDJSON | Install/update/uninstall activation progress and outcomes. |

CBM resolves `CBM_CACHE_DIR` to a canonical per-account path before using any of these locations. The log directory and files are private to the account.

## 1. Custom File Extension Mapping

Two optional JSON files let you map additional file extensions to built-in languages.

### Global config

Default path:

```text
$XDG_CONFIG_HOME/codebase-memory-mcp/config.json
```

Fallback when `XDG_CONFIG_HOME` is unset:

```text
~/.config/codebase-memory-mcp/config.json
```

### Per-project config

Place this file in the repository root:

```text
.codebase-memory.json
```

### Format

```json
{
  "extra_extensions": {
    ".blade.php": "php",
    ".mjs": "javascript",
    ".twig": "html"
  }
}
```

Notes:

- Extension keys must include the leading dot.
- Language names are case-insensitive.
- Unknown language names are skipped.
- Missing files are ignored.
- If the same extension appears in both files, the per-project file wins.

## 2. CLI-Managed Runtime Settings

The `config` subcommand stores runtime settings in a small SQLite database:

```text
${CBM_CACHE_DIR:-~/.cache/codebase-memory-mcp}/_config.db
```

Inspect or change values with the CLI:

```bash
codebase-memory-mcp config list
codebase-memory-mcp config get auto_index
codebase-memory-mcp config set auto_index true
codebase-memory-mcp config set auto_index_limit 50000
codebase-memory-mcp config set watcher_enabled false
codebase-memory-mcp config reset auto_index
```

Current keys:

| Key | Default | Meaning |
|---|---|---|
| `auto_index` | `false` | Automatically index new projects when an MCP session starts. |
| `auto_index_limit` | `50000` | Maximum file count allowed for automatic indexing of a new project. |
| `auto_watch` | `true` | Register the session's project with the background git watcher on connect. Set `false` to keep a session from registering its project (the watcher still runs for other projects). |
| `watcher_enabled` | `true` | Master switch for the background watcher subsystem. Set `false` to stop the watcher from starting at all — no poll thread and no project registration. Reindex manually with `index_repository` when disabled. |

> **`watcher_enabled` vs `auto_watch`.** `watcher_enabled` controls whether the
> watcher *subsystem* starts at all (the background poll thread). `auto_watch` is
> narrower: it only controls whether a connecting session registers *its own*
> project with an already-running watcher. When `watcher_enabled=false`,
> `auto_watch` has no effect — there is no watcher to register with.
>
> They also differ in **when they are read**, which matters because the watcher
> lives in the background daemon, not in your MCP client:
>
> - `auto_watch` is consulted each time a session would register its project, so
>   a change applies to sessions that connect afterwards.
> - `watcher_enabled` is read **once, when the daemon starts**, because it decides
>   whether the watcher is built at all. The daemon is long-lived and outlives
>   individual MCP sessions, so **reconnecting your client is not enough** — retire
>   the daemon so the next one picks the new value up:
>
> ```bash
> codebase-memory-mcp config set watcher_enabled false
> codebase-memory-mcp daemon stop     # next session starts a daemon without the watcher
> codebase-memory-mcp daemon status   # confirm
> ```
>
> Disabling the watcher does not disable anything else: the daemon still starts,
> `auto_index` still runs, and `index_repository` stays available for manual
> reindexing.

## 3. UI Settings

The optional built-in graph UI stores its settings in:

```text
${CBM_CACHE_DIR:-~/.cache/codebase-memory-mcp}/config.json
```

Current format:

```json
{
  "ui_enabled": false,
  "ui_port": 9749
}
```

Notes:

- If a UI-enabled binary finds its verified external asset pack and no UI config file exists yet, the UI auto-enables on first run. Missing or invalid assets leave the MCP/daemon service available and keep the UI disabled.
- `CBM_CACHE_DIR` changes both the UI config location and the runtime settings database location.
- CBM resolves `CBM_CACHE_DIR` to one canonical per-account cache root. A process configured with a different root fails while any CBM session or command is active; close them before switching roots.

## 4. Environment Variables

These environment variables affect runtime behavior:

| Variable | Default | Description |
|---|---|---|
| `CBM_ALLOWED_ROOT` | *(unset)* | Confine `index_repository` to paths within this directory. When set, a `repo_path` that resolves (after symlink / `..` resolution) outside this root is refused, and the same check now applies to the graph UI's `POST /api/index` route rather than only to the MCP tool. Unset imposes no *containment* restriction — but see the always-on limits below, which apply whether or not this is set. Useful when the server may be driven by an untrusted caller, e.g. agentic or multi-tenant deployments. |
| `CBM_CACHE_DIR` | `~/.cache/codebase-memory-mcp` | Override the cache directory used for indexes, `_config.db`, and UI `config.json`. |
| `CBM_DIAGNOSTICS` | `false` | Enable periodic `snapshot.json` and retained `trajectory.ndjson` below a fresh owner-private directory in the system temp directory. The daemon records the randomized paths in the `diagnostics.start` discovery record (a single JSON line) in `${CBM_CACHE_DIR}/logs/cbm-daemon.log`; that one record is emitted even when `CBM_LOG_LEVEL` suppresses ordinary logging, so the paths always remain discoverable. |
| `CBM_DOWNLOAD_URL` | GitHub releases | Override the update download URL. |
| `CBM_LOG_LEVEL` | role-aware | Set the log level to `debug`, `info`, `warn`, `error`, or `none` (or `0`-`4`). Thin MCP/CLI/hook frontends default to `warn`; the detached daemon and supervised index workers default to `info`. Physical workers retain INFO liveness records because their private logs drive the supervisor's no-progress timeout. Frontend messages use that session's stderr; detached daemon events use `${CBM_CACHE_DIR}/logs/cbm-daemon.log`. |
| `CBM_RUNTIME_DIR` | `%LOCALAPPDATA%` (Windows), `/private/tmp` (macOS), `/tmp` (other) | Parent directory for the daemon/CLI rendezvous directory, which CBM creates inside it as `cbm-daemon-<uid>` (`cbm-daemon-<key>` on Windows). Set it when the default ancestry cannot pass the private-directory check — see below. `CBM_CACHE_DIR` does **not** move the rendezvous. |
| `CBM_WORKERS` | auto-detected | Override the indexing worker count. |

### Relocating the daemon rendezvous directory

Before it is used, the rendezvous directory and every ancestor of it are checked:
each ancestor must be owned by you or by root, must not be world-writable (unless
it is the standard root-owned sticky directory such as `/tmp`), and must carry no
allow-ACL — on Windows, no ACE granting mutation rights to another identity. The
rendezvous directory itself is then forced to owner-only (`0700`, no extended ACL
/ an owner-only DACL).

That ancestry is not always acceptable in the default location. A Windows profile
that has acquired a capability-SID ACE with `WRITE_DAC` / `WRITE_OWNER` / `DELETE`
on `%LOCALAPPDATA%` — something an installed packaged app can add — fails the walk,
and so can an unusual `/tmp` or home directory on POSIX. When that happens *every*
command fails, `config list` included, so the settings surface cannot be reached
either:

```text
codebase-memory-mcp: secure daemon endpoint could not be created
```

`CBM_RUNTIME_DIR` points the rendezvous at an ancestry you choose:

```bash
export CBM_RUNTIME_DIR="$HOME/cbm-runtime"   # any directory you own
```

```powershell
$env:CBM_RUNTIME_DIR = "D:\cbm-runtime"
```

The check is not relaxed for the directory you name: it goes through exactly the
same validation as the default, and a value that fails it is refused rather than
silently ignored. Because the rendezvous is how sessions find each other, every
process that should share one daemon must see the same value — set it in the
environment of your MCP client and your shell alike, or a CLI invocation without
it will coordinate through the default location instead.

Environment used by daemon-owned components—such as diagnostics, daemon logging, and process-wide indexing resource limits—is captured from the first daemon-backed session that starts the daemon. Later sessions join the existing process and cannot replace those values. To change them, close every daemon-backed session, update the relevant agent configurations consistently, and restart a session. `CBM_ALLOWED_ROOT` remains session-specific, a conflicting `CBM_CACHE_DIR` is rejected, and CLI clients inherit their current environment and execute commands through the shared daemon.

### CLI use inside a filesystem sandbox

On POSIX, an existing cache owned by the current user with mode `0700` and no
extended ACL is validated without creating directories or rewriting its
permissions. If permissions or an ACL need repair, CBM still requires that repair
to succeed. A refusal includes the failing check and path after `cache-private`.

Configure the sandbox to allow reading the cache and its ancestors, and writing
the coordination files under the shared rendezvous directory. The CLI executes
commands through the shared daemon. To deny all cache writes to the CLI process,
start that daemon outside the restriction with `codebase-memory-mcp daemon start`
before launching the sandboxed client. Starting a new daemon inside the sandbox
requires cache write access for its logs and other state; indexing also requires
the daemon to be able to write the store. A read-only cache does not make the CLI
independent of its coordination locks.

If the default locations are outside the sandbox's allowed paths, choose private
directories within those paths using `CBM_CACHE_DIR` and `CBM_RUNTIME_DIR`.
Create the runtime parent before launching CBM. Close active CBM processes before
changing either location, then use the same settings in every MCP client and CLI
environment so they continue to share indexes and coordination. Changing the
cache location selects a different store; it does not copy existing indexes.

The macOS regression test exercises `cli list_projects` against a running daemon
with all client writes denied to an existing private cache and an allowed runtime
directory. Other sandbox policies may additionally restrict runtime locks,
process execution, or reads; the command still needs those permissions for its
normal operation.

Run that regression from an unsandboxed host shell. macOS refuses to apply its
test profile inside an existing sandbox (`sandbox_apply: Operation not
permitted`), so a nested invocation exits before CBM runs. The test still launches
the CLI inside the profile it defines and verifies that the denied write control
fails with `Operation not permitted`.

#### Codex exec on macOS

Verified with Codex CLI `0.152.0`: the default `workspace-write` sandbox also
denies the Unix socket connection to CBM's daemon. A warm daemon and a private
cache alone are therefore insufficient. With `read-only`, coordination-file
writes are denied as well. CBM currently reports these as a daemon connection
timeout or an exact-build admission failure.

Use the patched CBM binary for both client and daemon, start the daemon outside
the sandbox, and locate its socket:

```bash
codebase-memory-mcp daemon start
ls "${CBM_RUNTIME_DIR:-/private/tmp}/cbm-daemon-$(id -u)"/*.sock
```

Codex supports a named permissions profile with that exact socket allowed. This
configuration replaces the legacy `sandbox_mode` and `[sandbox_workspace_write]`
settings; do not combine them or pass `-s` when selecting the named profile.
Replace the placeholder socket path with the path printed above:

```toml
default_permissions = "cbm"

[features.network_proxy]
enabled = true

[permissions.cbm]
extends = ":workspace"

[permissions.cbm.network]
enabled = true

[permissions.cbm.network.unix_sockets]
"/private/tmp/cbm-daemon-<uid>/cbm-<key>.sock" = "allow"
```

Both the network proxy and the profile's network setting are required in the
tested Codex version: the socket entry alone did not permit the connection.
Direct TCP connections remain blocked: a control connection to an unlisted
endpoint was denied while `cli list_projects` succeeded. See
the [Codex configuration reference](https://learn.chatgpt.com/docs/config-file/config-reference)
for permissions profiles and Unix socket rules.

For a read-only filesystem profile, change `extends` to `":read-only"` and add
only the daemon's coordination directory as writable:

```toml
[permissions.cbm.filesystem]
"/private/tmp/cbm-daemon-<uid>" = "write"
```

Use the actual runtime location if `CBM_RUNTIME_DIR` is set. A `:workspace`
profile also needs this write rule when that location lies outside its writable
workspace and temporary directories. Keep that value and
`CBM_CACHE_DIR` identical between the daemon and the Codex agent. These
permissions do not remove CBM's owner, ACL, peer-identity, or build checks.

The optional integration test uses the installed `codex sandbox` command and a
temporary daemon to check default socket denial, scoped access, and blocked
unlisted networking:

```bash
CBM_TEST_CODEX_SANDBOX=1 bash tests/test_cli_codex_sandbox.sh
```


### Roots that are always refused

Independently of `CBM_ALLOWED_ROOT`, some directories are refused as an indexing
root because they are too broad or too sensitive to index as a unit:

- a filesystem root, a Windows drive root, or a UNC share root;
- a top-level system tree — `/etc`, `/var`, `/usr`, `/home`, `/Users`, and on
  Windows `C:\Windows`, `C:\Users`, `C:\ProgramData`, `C:\Program Files`;
- your home directory itself (directories *below* it are fine);
- a credential directory at any depth — `.ssh`, `.aws`, `.gnupg`, `.kube`,
  `.docker`, `.netrc`, `.git-credentials`, `.password-store`, macOS `Keychains`.

Two limits are worth stating plainly. This constrains *scope*, not
*sensitivity*: inside a root that is allowed, every file the process can read may
be indexed and later returned. And the credential list is a denylist, so it
raises the cost of a mistake rather than closing the class — a directory it does
not name is permitted.

## 5. Agent and Editor Integration Files

The `install` command can also write MCP entries and instruction blocks into agent/editor config files such as Claude Code, Codex, Gemini, VS Code, Cursor, Zed, and others.

Those target paths vary by tool and platform, so the easiest way to inspect the exact files for your machine is:

```bash
codebase-memory-mcp install --dry-run
```

That prints the specific config files the installer would modify without writing anything.
