#!/usr/bin/env bash
# Exports the versioned, checksum-verified protocol contract bundle.
# Usage: tools/export_protocol.sh [output-dir]
set -euo pipefail
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROTO="$REPO_ROOT/protocol"
OUT="${1:-$REPO_ROOT/build/protocol-bundle}"
rm -rf "$OUT"
mkdir -p "$OUT/test-vectors"
cp "$PROTO/remote_windows.proto" "$PROTO/VERSION" "$PROTO/README.md" "$OUT/"
# All golden test vectors, including subdirectories (e.g. media/).
while IFS= read -r f; do
  mkdir -p "$OUT/$(dirname "$f")"
  cp "$PROTO/$f" "$OUT/$f"
done < <(cd "$PROTO" && find test-vectors -type f -name '*.json' | LC_ALL=C sort)
( cd "$OUT" && { echo "README.md"; echo "VERSION"; echo "remote_windows.proto";
                 find test-vectors -type f -name '*.json' | LC_ALL=C sort; } \
    | xargs sha256sum > SHA256SUMS )
echo "Contract bundle exported to $OUT (version $(cat "$PROTO/VERSION" | tr -d '[:space:]'))"
