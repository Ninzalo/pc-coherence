#!/usr/bin/env bash
# Syncs the pinned protocol contract snapshot from the Host tree.
#
# Single-repository mode (root docs/decisions/ADR-0008-single-repo-layout.md):
# the canonical schema lives in ../remote-windows-host/protocol relative to
# this script. The snapshot under Protocol/Contract/ is never hand-edited;
# update it ONLY by running this tool, then review the diff.
#
# Usage: tools/sync_protocol_from_host.sh
set -euo pipefail

CLIENT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
HOST_PROTO="$(cd "$CLIENT_ROOT/../remote-windows-host/protocol" 2>/dev/null && pwd || true)"
if [ -z "${HOST_PROTO:-}" ] || [ ! -f "$HOST_PROTO/remote_windows.proto" ]; then
  echo "error: host protocol directory not found (expected sibling 'remote-windows-host/protocol')" >&2
  exit 1
fi

CONTRACT="$CLIENT_ROOT/Protocol/Contract"
VERSION="$(tr -d '[:space:]' < "$HOST_PROTO/VERSION")"

rm -rf "$CONTRACT"
mkdir -p "$CONTRACT/test-vectors"
cp "$HOST_PROTO/remote_windows.proto" "$HOST_PROTO/VERSION" "$HOST_PROTO/README.md" "$CONTRACT/"
# All golden test vectors, including subdirectories (e.g. media/).
while IFS= read -r f; do
  mkdir -p "$CONTRACT/$(dirname "$f")"
  cp "$HOST_PROTO/$f" "$CONTRACT/$f"
done < <(cd "$HOST_PROTO" && find test-vectors -type f -name '*.json' | LC_ALL=C sort)

( cd "$CONTRACT" && {
    echo "README.md"
    echo "VERSION"
    echo "remote_windows.proto"
    find test-vectors -type f -name '*.json' | LC_ALL=C sort
  } | xargs shasum -a 256 > SHA256SUMS )

COMMIT="$(git -C "$CLIENT_ROOT" rev-parse HEAD 2>/dev/null || echo unknown)"
cat > "$CONTRACT/PROVENANCE" <<EOF
source = remote-windows-host/protocol (single repository, root ADR-0008)
host_protocol_version = $VERSION
host_repo_commit = single-repo:$COMMIT
synced = $(date -u +%Y-%m-%dT%H:%M:%SZ)
EOF

echo "Contract snapshot synced: $CONTRACT (protocol v$VERSION, commit $COMMIT)"
echo "Verify: (cd \"$CONTRACT\" && shasum -a 256 -c SHA256SUMS)"
