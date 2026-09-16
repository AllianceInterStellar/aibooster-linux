#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Assembles a .deb from an already-installed staging tree.
#
#   cmake -B build -DCMAKE_BUILD_TYPE=Release
#   cmake --build build --parallel
#   DESTDIR="$PWD/pkgroot" cmake --install build --prefix /usr
#   packaging/build-deb.sh "$PWD/pkgroot"
#
# The package deliberately does NOT depend on, contain, or download the tunnelling engine.
# The engine is a separate program under a different licence; see README.md.

set -euo pipefail

PKGROOT="${1:?usage: build-deb.sh <staging-dir>}"
OUTDIR="${2:-dist}"

VERSION="$(sed -n 's/^project(AiBoosterLinux VERSION \([0-9.]*\).*/\1/p' \
    "$(dirname "$0")/../CMakeLists.txt")"
if [[ -z "$VERSION" ]]; then
    echo "Could not read the version out of CMakeLists.txt" >&2
    exit 1
fi

ARCH="$(dpkg --print-architecture)"
STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT

cp -a "$PKGROOT/." "$STAGE/"
mkdir -p "$STAGE/DEBIAN"

# Let dpkg-shlibdeps work out the real Qt/OpenSSL dependencies from the binary rather than
# hand-maintaining a list that silently rots when Qt is upgraded.
DEPENDS="$(cd "$STAGE" && dpkg-shlibdeps -O --ignore-missing-info usr/bin/aibooster 2>/dev/null \
    | sed 's/^shlibs:Depends=//')"
# The QML modules are loaded by name at runtime, so no linker records them and
# dpkg-shlibdeps cannot see them. Missing these produces a window that opens blank.
QML_DEPENDS="qml6-module-qtquick, qml6-module-qtquick-controls, qml6-module-qtquick-layouts, qml6-module-qtquick-window, qml6-module-qtquick-templates"

cat > "$STAGE/DEBIAN/control" <<CONTROL
Package: aibooster
Version: ${VERSION}
Section: net
Priority: optional
Architecture: ${ARCH}
Depends: ${DEPENDS:+${DEPENDS}, }${QML_DEPENDS}
Recommends: gsettings-desktop-schemas
Maintainer: AllianceInterStellar <support@allianceinterstellar.com>
Homepage: https://allianceinterstellar.com
Description: AI Booster VPN client (user interface)
 Desktop client for the AI Booster network: subscriptions and profiles, server
 selection, live traffic and logs, and desktop proxy integration.
 .
 This package contains the user interface only. The tunnelling engine is a
 separate program that the client launches and communicates with over loopback
 sockets; it is not linked into this application and ships separately.
CONTROL

mkdir -p "$OUTDIR"
DEB="$OUTDIR/aibooster_${VERSION}_${ARCH}.deb"
dpkg-deb --build --root-owner-group "$STAGE" "$DEB"
echo "Built $DEB"
dpkg-deb --info "$DEB"
