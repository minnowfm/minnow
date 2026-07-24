#!/usr/bin/env bash
# Builds an .rpm package from the current source tree.
# Requires: rpm-build, cmake, gcc-c++, and the BuildRequires listed in minnow.spec.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
VERSION="0.1.0"

RPMBUILD_DIR="$(mktemp -d)"
trap 'rm -rf "$RPMBUILD_DIR"' EXIT
mkdir -p "$RPMBUILD_DIR"/{SOURCES,SPECS,BUILD,RPMS,SRPMS}

echo "Creating source tarball..."
TAR_DIR="$(mktemp -d)"
trap 'rm -rf "$RPMBUILD_DIR" "$TAR_DIR"' EXIT
cp -a "$PROJECT_ROOT" "$TAR_DIR/minnow-$VERSION"
rm -rf "$TAR_DIR/minnow-$VERSION/build" "$TAR_DIR/minnow-$VERSION/packaging" "$TAR_DIR/minnow-$VERSION/dist"
tar -C "$TAR_DIR" -czf "$RPMBUILD_DIR/SOURCES/minnow-$VERSION.tar.gz" "minnow-$VERSION"

cp "$SCRIPT_DIR/minnow.spec" "$RPMBUILD_DIR/SPECS/"

rpmbuild --define "_topdir $RPMBUILD_DIR" -ba "$RPMBUILD_DIR/SPECS/minnow.spec"

OUT_DIR="$PROJECT_ROOT/dist"
mkdir -p "$OUT_DIR"
find "$RPMBUILD_DIR/RPMS" "$RPMBUILD_DIR/SRPMS" -name '*.rpm' -exec cp {} "$OUT_DIR/" \;
echo "Done. Package(s) copied to $OUT_DIR"
