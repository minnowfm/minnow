#!/usr/bin/env bash
# builds a .deb from the current source tree. needs dpkg-dev, debhelper, cmake,
# and whatever's in control's Build-Depends
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="$(mktemp -d)"
trap 'rm -rf "$BUILD_DIR"' EXIT

# CI sets VERSION from the tag; for a manual run just fall back to the changelog's top entry
VERSION="${VERSION:-$(dpkg-parsechangelog -l "$SCRIPT_DIR/changelog" -S Version | sed 's/-[^-]*$//')}"
export DEB_VERSION="$VERSION"

echo "Assembling source tree in $BUILD_DIR..."
cp -a "$PROJECT_ROOT/." "$BUILD_DIR/minnow"
rm -rf "$BUILD_DIR/minnow/build" "$BUILD_DIR/minnow/packaging"
mkdir -p "$BUILD_DIR/minnow/debian"
cp "$SCRIPT_DIR/control" "$SCRIPT_DIR/rules" "$SCRIPT_DIR/changelog" "$SCRIPT_DIR/copyright" \
   "$BUILD_DIR/minnow/debian/"
chmod +x "$BUILD_DIR/minnow/debian/rules"

NEW_ENTRY="minnow (${VERSION}-1) unstable; urgency=medium

  * Release ${VERSION}.

 -- Minnow Contributors <noreply@example.com>  $(date -R)

"
printf '%s' "$NEW_ENTRY" | cat - "$BUILD_DIR/minnow/debian/changelog" > "$BUILD_DIR/changelog.new"
mv "$BUILD_DIR/changelog.new" "$BUILD_DIR/minnow/debian/changelog"

cd "$BUILD_DIR/minnow"
dpkg-buildpackage -us -uc -b

OUT_DIR="$PROJECT_ROOT/dist"
mkdir -p "$OUT_DIR"
cp "$BUILD_DIR"/*.deb "$OUT_DIR/" 2>/dev/null || true
echo "Done. Package(s) copied to $OUT_DIR"
