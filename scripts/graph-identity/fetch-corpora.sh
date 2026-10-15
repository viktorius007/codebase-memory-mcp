#!/usr/bin/env bash
# fetch-corpora.sh <dest-dir>: materialise the C/C++ graph-identity corpora from their pinned
# upstream archives. Each archive's sha256 is checked before extraction, so <dest-dir> is
# byte-identical on every machine. Skips a corpus whose directory already exists.
set -euo pipefail
DEST="${1:?usage: $0 <dest-dir>}"
mkdir -p "$DEST"
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT

fetch() { # <name> <url> <sha256> <extract-fn>
  local name="$1" url="$2" sum="$3" extract="$4" a="$TMP/$1.archive"
  if [ -d "$DEST/$name" ]; then echo "SKIP: $name (exists)"; return; fi
  curl -sSL -o "$a" "$url"
  echo "$sum  $a" | shasum -a 256 -c - >/dev/null || { echo "FAIL: $name checksum" >&2; exit 1; }
  mkdir -p "$TMP/$name" && tar -xzf "$a" -C "$TMP/$name"
  "$extract" "$TMP/$name" "$DEST/$name"
  echo "OK: $name $(find "$DEST/$name" -type f | wc -l | tr -d ' ') files"
}
whole_tree()   { mv "$1"/*/ "$2"; }
rn_native()    { mkdir -p "$2"; mv "$1/package/ReactCommon" "$1/package/ReactAndroid" "$2/"; }

fetch bitcoin-v27 \
  https://codeload.github.com/bitcoin/bitcoin/tar.gz/refs/tags/v27.0 \
  d6230128637dcf0ebfac5efdde6ad86b5afb876dbd24bccbcdd98c28bac7c034 whole_tree
fetch react-native-0.88 \
  https://registry.npmjs.org/react-native/-/react-native-0.88.0-rc.3.tgz \
  620745b48686013930cab8f9465eb11e27eb3b4bac8b79962d55afff5c63241d rn_native
