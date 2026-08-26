#!/bin/sh
set -eu

binary=${1:?binary path is required}
version_output=$("$binary" --version)
reported=${version_output##*build=}

case "$reported" in
    *[!0-9a-f]*|'')
        echo "FAIL: --version did not report a hexadecimal build identifier" >&2
        exit 1
        ;;
esac

if command -v sha256sum >/dev/null 2>&1; then
    expected=$(sha256sum "$binary" | awk '{print $1}')
else
    expected=$(shasum -a 256 "$binary" | awk '{print $1}')
fi

if [ "$reported" != "$expected" ]; then
    echo "FAIL: --version build identifier does not fingerprint the executable" >&2
    exit 1
fi

echo "build identifier contract passed"
