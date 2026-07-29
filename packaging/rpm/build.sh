#!/usr/bin/env bash
# builds an .rpm from the current source tree. needs rpm-build, cmake, gcc-c++, and
# minnow.spec's BuildRequires
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
# same deal as the deb script - CI sets VERSION, manual runs fall back to the spec file
VERSION="${VERSION:-$(grep -oP '(?<=^Version:)\s*\K[0-9.]+' "$SCRIPT_DIR/minnow.spec")}"

RPMBUILD_DIR="$(mktemp -d)"
trap 'rm -rf "$RPMBUILD_DIR"' EXIT
mkdir -p "$RPMBUILD_DIR"/{SOURCES,SPECS,BUILD,RPMS,SRPMS}

echo "Creating source tarball..."
TAR_DIR="$(mktemp -d)"
trap 'rm -rf "$RPMBUILD_DIR" "$TAR_DIR"' EXIT
cp -a "$PROJECT_ROOT" "$TAR_DIR/minnow-$VERSION"
rm -rf "$TAR_DIR/minnow-$VERSION/build" "$TAR_DIR/minnow-$VERSION/packaging" "$TAR_DIR/minnow-$VERSION/dist"
tar -C "$TAR_DIR" -czf "$RPMBUILD_DIR/SOURCES/minnow-$VERSION.tar.gz" "minnow-$VERSION"

SPEC="$RPMBUILD_DIR/SPECS/minnow.spec"
cp "$SCRIPT_DIR/minnow.spec" "$SPEC"
sed -i "s/^Version:.*/Version:        $VERSION/" "$SPEC"
sed -i "/^%changelog/a * $(date +'%a %b %d %Y') Minnow Contributors <noreply@example.com> - $VERSION-1\n- Release $VERSION.\n" "$SPEC"

rpmbuild --define "_topdir $RPMBUILD_DIR" -ba "$SPEC"

OUT_DIR="$PROJECT_ROOT/dist"
mkdir -p "$OUT_DIR"
find "$RPMBUILD_DIR/RPMS" "$RPMBUILD_DIR/SRPMS" -name '*.rpm' -exec cp {} "$OUT_DIR/" \;
echo "Done. Package(s) copied to $OUT_DIR"
