#!/usr/bin/env bash
# Contract: every vendored input is content-pinned, including opaque blobs and
# the generated Tree-sitter sources that are compiled into release binaries.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
FIXTURE="$(mktemp -d "${TMPDIR:-/tmp}/cbm-vendored-integrity.XXXXXX")"
trap 'rm -rf "$FIXTURE"' EXIT

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

make_fixture() {
  local destination="$1"
  mkdir -p "$destination/scripts" "$destination/vendored/yyjson"
  cp "$ROOT/scripts/security-vendored.sh" "$destination/scripts/security-vendored.sh"
  cp "$ROOT/scripts/vendor-grammar.sh" "$destination/scripts/vendor-grammar.sh"
  : > "$destination/vendored/yyjson/safe.c"
  printf '%s  %s\n' \
    'e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855' \
    'vendored/yyjson/safe.c' > "$destination/scripts/vendored-checksums.txt"
}

opaque="$FIXTURE/opaque"
make_fixture "$opaque"
printf '\230\237\000\000opaque-vector-bytes\n' > "$opaque/vendored/yyjson/code_vectors.bin"
if (cd "$opaque" && bash scripts/security-vendored.sh >/dev/null 2>&1); then
  fail "vendored integrity gate accepted an unmanifested opaque binary blob"
fi

grammar="$FIXTURE/grammar"
make_fixture "$grammar"
mkdir -p "$grammar/internal/cbm/vendored/grammars/example"
printf '%s\n' 'int tree_sitter_example(void) { return 1; }' \
  > "$grammar/internal/cbm/vendored/grammars/example/parser.c"
if (cd "$grammar" && bash scripts/security-vendored.sh >/dev/null 2>&1); then
  fail "vendored integrity gate accepted an unmanifested generated grammar source"
fi

exact="$FIXTURE/exact"
make_fixture "$exact"
mkdir -p "$exact/internal/cbm/vendored/grammars" "$exact/upstream/src"
printf '%s\n' \
  '# Vendored tree-sitter Grammar Manifest' \
  '' \
  '## Local source patches (applied atop pinned upstream)' \
  '' \
  '| grammar | location | patch | reason |' \
  '|---|---|---|---|' \
  '| example | `example.patch` | fixture overlay | must remain patch metadata |' \
  '' \
  '## Vendored from verified upstream' \
  '' \
  '| grammar | cur ABI | upstream repo | pinned commit | verdict | LICENSE |' \
  '|---|:---:|---|---|---|:---:|' \
  '| example | 14 | old/example | `000000000000` | STALE | ✅ |' \
  > "$exact/internal/cbm/vendored/grammars/MANIFEST.md"
printf '%s\n' \
  '#include <stdint.h>' \
  '#define LANGUAGE_VERSION 15' \
  'int tree_sitter_example(void) { return 1; }' \
  > "$exact/upstream/src/parser.c"
printf '%s\n' 'example license' > "$exact/upstream/LICENSE"
git -C "$exact/upstream" init --quiet
git -C "$exact/upstream" config user.name 'Vendoring Contract'
git -C "$exact/upstream" config user.email 'vendoring-contract@example.invalid'
git -C "$exact/upstream" add src/parser.c LICENSE
git -C "$exact/upstream" commit --quiet -m 'fixture parser'
exact_ref="$(git -C "$exact/upstream" rev-parse HEAD)"
moving_ref="$(git -C "$exact/upstream" symbolic-ref --short HEAD)"

printf '%s\n' \
  'diff --git a/src/parser.c b/src/parser.c' \
  'index 6242653..07d9339 100644' \
  '--- a/src/parser.c' \
  '+++ b/src/parser.c' \
  '@@ -1,3 +1,3 @@' \
  ' #include <stdint.h>' \
  ' #define LANGUAGE_VERSION 15' \
  '-int tree_sitter_example(void) { return 1; }' \
  '+int tree_sitter_example(void) { return 2; }' \
  > "$exact/example.patch"

if (cd "$exact" && bash scripts/vendor-grammar.sh \
    --repo "$exact/upstream" \
    --ref "$exact_ref" \
    --name example \
    --patch example.patch \
    --verdict PINNED-PATCHED >/dev/null); then
  :
else
  fail "exact grammar vendoring contract failed"
fi
grep -Fq 'return 2' "$exact/internal/cbm/vendored/grammars/example/parser.c" ||
  fail "local grammar patch was not applied before vendoring"
grep -Fq "| example | 15 | $exact/upstream | \`${exact_ref:0:12}\` | PINNED-PATCHED | ✅ |" \
  "$exact/internal/cbm/vendored/grammars/MANIFEST.md" ||
  fail "grammar manifest was not reconciled to the exact source revision"
grep -Fq '| example | `example.patch` | fixture overlay | must remain patch metadata |' \
  "$exact/internal/cbm/vendored/grammars/MANIFEST.md" ||
  fail "grammar manifest reconciliation overwrote local patch provenance"
grep -Fq 'internal/cbm/vendored/grammars/example/parser.c' \
  "$exact/scripts/vendored-checksums.txt" ||
  fail "generated parser was not added to the checksum inventory"

if (cd "$exact" && bash scripts/vendor-grammar.sh \
    --repo "$exact/upstream" --ref "$moving_ref" --name example >/dev/null 2>&1); then
  fail "grammar vendoring accepted a moving branch instead of an exact commit"
fi
if (cd "$exact" && bash scripts/vendor-grammar.sh \
    --repo "$exact/upstream" --ref "$exact_ref" --name example \
    --generate-version 0.26.7 >/dev/null 2>&1); then
  fail "grammar regeneration accepted a generator version without an ABI"
fi

echo "vendored integrity contract: OK"
