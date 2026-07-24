#!/usr/bin/env bash
# Builds a .deb package from the current source tree.
# Requires: dpkg-dev, debhelper, cmake, and the Build-Depends listed in control.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="$(mktemp -d)"
trap 'rm -rf "$BUILD_DIR"' EXIT

echo "Assembling source tree in $BUILD_DIR..."
cp -a "$PROJECT_ROOT/." "$BUILD_DIR/minnow"
rm -rf "$BUILD_DIR/minnow/build" "$BUILD_DIR/minnow/packaging"
mkdir -p "$BUILD_DIR/minnow/debian"
cp "$SCRIPT_DIR/control" "$SCRIPT_DIR/rules" "$SCRIPT_DIR/changelog" "$SCRIPT_DIR/copyright" \
   "$BUILD_DIR/minnow/debian/"
chmod +x "$BUILD_DIR/minnow/debian/rules"

cd "$BUILD_DIR/minnow"
dpkg-buildpackage -us -uc -b

OUT_DIR="$PROJECT_ROOT/dist"
mkdir -p "$OUT_DIR"
cp "$BUILD_DIR"/*.deb "$OUT_DIR/" 2>/dev/null || true
echo "Done. Package(s) copied to $OUT_DIR"
