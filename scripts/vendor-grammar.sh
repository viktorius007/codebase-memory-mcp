#!/usr/bin/env bash
# Vendor one tree-sitter grammar from an exact upstream commit.
#
# Usage:
#   scripts/vendor-grammar.sh --repo URL --ref 40_HEX_COMMIT --name NAME \
#     [--subdir DIR] [--patch FILE] \
#     [--generate-version VERSION --abi ABI] [--verdict VERDICT]
#
# A local grammar patch is applied before optional source regeneration. When
# regeneration is requested, the exact tree-sitter CLI version and ABI are
# mandatory. The generated sources, provenance row, and vendored checksum
# inventory are updated as one reviewed operation.

set -euo pipefail

usage() {
    sed -n '2,11p' "$0" >&2
    exit 2
}

REPO_URL=""
REF=""
NAME=""
SUBDIR=""
PATCH_FILE=""
GENERATOR_VERSION=""
ABI=""
VERDICT="PINNED-EXACT"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --repo)
            [[ $# -ge 2 ]] || usage
            REPO_URL="$2"
            shift 2
            ;;
        --ref)
            [[ $# -ge 2 ]] || usage
            REF="$2"
            shift 2
            ;;
        --name)
            [[ $# -ge 2 ]] || usage
            NAME="$2"
            shift 2
            ;;
        --subdir)
            [[ $# -ge 2 ]] || usage
            SUBDIR="$2"
            shift 2
            ;;
        --patch)
            [[ $# -ge 2 ]] || usage
            PATCH_FILE="$2"
            shift 2
            ;;
        --generate-version)
            [[ $# -ge 2 ]] || usage
            GENERATOR_VERSION="$2"
            shift 2
            ;;
        --abi)
            [[ $# -ge 2 ]] || usage
            ABI="$2"
            shift 2
            ;;
        --verdict)
            [[ $# -ge 2 ]] || usage
            VERDICT="$2"
            shift 2
            ;;
        *)
            echo "ERROR: unknown argument: $1" >&2
            usage
            ;;
    esac
done

if [[ -z "$REPO_URL" || -z "$REF" || -z "$NAME" ]]; then
    echo "ERROR: --repo, --ref, and --name are required" >&2
    usage
fi
if [[ ! "$REF" =~ ^[0-9a-fA-F]{40}$ ]]; then
    echo "ERROR: --ref must be an exact 40-character hexadecimal commit" >&2
    exit 2
fi
if [[ ! "$NAME" =~ ^[a-zA-Z0-9_+-]+$ ]]; then
    echo "ERROR: invalid grammar name: $NAME" >&2
    exit 2
fi
if [[ -n "$GENERATOR_VERSION" || -n "$ABI" ]]; then
    if [[ -z "$GENERATOR_VERSION" || ! "$ABI" =~ ^[0-9]+$ ]]; then
        echo "ERROR: --generate-version and numeric --abi must be supplied together" >&2
        exit 2
    fi
fi

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd -P)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
GRAMMAR_DIR="$PROJECT_DIR/internal/cbm/vendored/grammars/$NAME"
MANIFEST="$PROJECT_DIR/internal/cbm/vendored/grammars/MANIFEST.md"
WORK_DIR="$(mktemp -d "${TMPDIR:-/tmp}/cbm-vendor-grammar.XXXXXX")"

cleanup() {
    rm -rf "$WORK_DIR"
}
trap cleanup EXIT

if [[ -n "$PATCH_FILE" ]]; then
    if [[ "$PATCH_FILE" != /* ]]; then
        PATCH_FILE="$PROJECT_DIR/$PATCH_FILE"
    fi
    if [[ ! -f "$PATCH_FILE" || -L "$PATCH_FILE" ]]; then
        echo "ERROR: patch must be a regular, non-symlink file: $PATCH_FILE" >&2
        exit 1
    fi
fi
if [[ ! -f "$MANIFEST" || -L "$MANIFEST" ]]; then
    echo "ERROR: grammar manifest must be a regular, non-symlink file: $MANIFEST" >&2
    exit 1
fi

echo "Vendoring $NAME from exact commit $REF at $REPO_URL"
git init --quiet "$WORK_DIR/repo"
git -C "$WORK_DIR/repo" remote add origin "$REPO_URL"
git -C "$WORK_DIR/repo" fetch --quiet --depth 1 origin "$REF"
git -C "$WORK_DIR/repo" checkout --quiet --detach FETCH_HEAD
ACTUAL_REF="$(git -C "$WORK_DIR/repo" rev-parse HEAD)"
if [[ "$ACTUAL_REF" != "$REF" ]]; then
    echo "ERROR: fetched commit $ACTUAL_REF does not match requested $REF" >&2
    exit 1
fi

GRAMMAR_ROOT="$WORK_DIR/repo"
if [[ -n "$SUBDIR" ]]; then
    GRAMMAR_ROOT="$GRAMMAR_ROOT/$SUBDIR"
fi
if [[ ! -d "$GRAMMAR_ROOT" || -L "$GRAMMAR_ROOT" ]]; then
    echo "ERROR: grammar source root not found: $GRAMMAR_ROOT" >&2
    exit 1
fi

if [[ -n "$PATCH_FILE" ]]; then
    git -C "$WORK_DIR/repo" apply --check "$PATCH_FILE"
    git -C "$WORK_DIR/repo" apply "$PATCH_FILE"
fi

if [[ -n "$GENERATOR_VERSION" ]]; then
    if ! command -v pnpm >/dev/null 2>&1; then
        echo "ERROR: pnpm is required to run the pinned tree-sitter generator" >&2
        exit 1
    fi
    (
        cd "$GRAMMAR_ROOT"
        pnpm dlx "tree-sitter-cli@$GENERATOR_VERSION" generate --abi "$ABI"
    )
fi

SRC_DIR="$GRAMMAR_ROOT/src"
if [[ ! -f "$SRC_DIR/parser.c" || -L "$SRC_DIR/parser.c" ]]; then
    echo "ERROR: generated parser is missing or unsafe: $SRC_DIR/parser.c" >&2
    exit 1
fi

mkdir -p "$GRAMMAR_DIR/tree_sitter"
cp "$SRC_DIR/parser.c" "$GRAMMAR_DIR/parser.c"

if [[ -f "$SRC_DIR/scanner.c" && ! -L "$SRC_DIR/scanner.c" ]]; then
    cp "$SRC_DIR/scanner.c" "$GRAMMAR_DIR/scanner.c"
elif [[ -f "$GRAMMAR_DIR/scanner.c" ]]; then
    echo "ERROR: upstream no longer supplies scanner.c; refusing to retain a stale copy" >&2
    exit 1
fi
if [[ -f "$SRC_DIR/scanner.cc" ]]; then
    echo "ERROR: $NAME has a C++ scanner; configure its build before vendoring" >&2
    exit 1
fi

if [[ -d "$SRC_DIR/tree_sitter" ]]; then
    for header in "$SRC_DIR/tree_sitter/"*.h; do
        [[ -f "$header" ]] && cp "$header" "$GRAMMAR_DIR/tree_sitter/"
    done
fi
for extra in "$SRC_DIR/"*.h "$SRC_DIR/"*.inc; do
    [[ -f "$extra" ]] && cp "$extra" "$GRAMMAR_DIR/"
done
if [[ -d "$SRC_DIR/common" ]]; then
    cp -R "$SRC_DIR/common" "$GRAMMAR_DIR/"
fi

LICENSE_SOURCE=""
for candidate in "$GRAMMAR_ROOT/LICENSE" "$WORK_DIR/repo/LICENSE" \
                 "$GRAMMAR_ROOT/LICENSE.md" "$WORK_DIR/repo/LICENSE.md" \
                 "$GRAMMAR_ROOT/COPYING" "$WORK_DIR/repo/COPYING"; do
    if [[ -f "$candidate" && ! -L "$candidate" ]]; then
        LICENSE_SOURCE="$candidate"
        break
    fi
done
if [[ -z "$LICENSE_SOURCE" ]]; then
    echo "ERROR: no regular upstream license file found" >&2
    exit 1
fi
cp "$LICENSE_SOURCE" "$GRAMMAR_DIR/LICENSE"

PARSER_ABI="$(awk '/^#define LANGUAGE_VERSION / { print $3; exit }' "$SRC_DIR/parser.c")"
if [[ ! "$PARSER_ABI" =~ ^[0-9]+$ ]]; then
    echo "ERROR: generated parser does not declare a numeric LANGUAGE_VERSION" >&2
    exit 1
fi
if [[ -n "$ABI" && "$PARSER_ABI" != "$ABI" ]]; then
    echo "ERROR: generated parser ABI $PARSER_ABI does not match requested ABI $ABI" >&2
    exit 1
fi

REPO_LABEL="${REPO_URL%.git}"
REPO_LABEL="${REPO_LABEL#https://github.com/}"
REPO_LABEL="${REPO_LABEL#git@github.com:}"
SHORT_REF="${REF:0:12}"
MANIFEST_TMP="$(mktemp "$MANIFEST.tmp.XXXXXX")"
if ! awk -v name="$NAME" -v abi="$PARSER_ABI" -v repo="$REPO_LABEL" \
    -v ref="$SHORT_REF" -v verdict="$VERDICT" '
        $0 == "## Vendored from verified upstream" { in_vendored_table = 1 }
        in_vendored_table && $0 ~ /^## / && $0 != "## Vendored from verified upstream" {
            in_vendored_table = 0
        }
        in_vendored_table && $0 ~ "^\\| " name "[[:space:]]*\\|" {
            print "| " name " | " abi " | " repo " | `" ref "` | " verdict " | ✅ |"
            found++
            next
        }
        { print }
        END { if (found != 1) exit 3 }
    ' "$MANIFEST" > "$MANIFEST_TMP"; then
    echo "ERROR: grammar manifest has no unique row for $NAME" >&2
    exit 1
fi
mv -f "$MANIFEST_TMP" "$MANIFEST"

(cd "$PROJECT_DIR" && scripts/security-vendored.sh --update)

echo "Vendored $NAME at $REF (ABI $PARSER_ABI) to $GRAMMAR_DIR"
