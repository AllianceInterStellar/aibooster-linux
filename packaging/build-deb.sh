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

# Derive the shared-library dependencies from the binary rather than hand-maintaining a
# list that silently rots when Qt is upgraded.
#
# Not dpkg-shlibdeps: it insists on a debian/control relative to the working directory and
# exits 25 without one, which is not a shape that fits building from a staging tree. Asking
# the dynamic linker what the binary actually loads, then asking dpkg which package owns
# each of those files, gets the same answer from the same source of truth.
declare -a PKGS=()
while read -r lib; do
    [ -n "$lib" ] || continue
    resolved="$(readlink -f "$lib")"
    owner="$(dpkg -S "$resolved" 2>/dev/null | head -1 | cut -d: -f1)"
    [ -n "$owner" ] && PKGS+=("$owner")
done < <(ldd "$STAGE/usr/bin/aibooster" | awk '/=>/ && $3 ~ /^\// {print $3}')

if [ ${#PKGS[@]} -eq 0 ]; then
    echo "Could not resolve any library dependencies for usr/bin/aibooster" >&2
    exit 1
fi

# Package names without version bounds: the build machine's exact versions would be far
# too tight a floor, and the Qt 6 ABI is stable across the 6.x a given release ships.
# paste -d takes a *cycling list* of delimiters, so -d', ' alternates comma and space and
# produces "a,b c,d" — which dpkg rejects as a syntax error. One delimiter, then space it.
DEPENDS="$(printf '%s\n' "${PKGS[@]}" | sort -u | paste -sd, - | sed 's/,/, /g')"
# QML modules are resolved by name at runtime, so they appear in no linker record and the
# ldd walk above cannot see them. Missing these produces a window that opens blank.
# libqt6svg6 carries the SVG image-format plugin. The navigation icons are SVG, and a
# missing plugin renders every one of them as a blank square.
QML_DEPENDS="libqt6svg6, qml6-module-qtquick, qml6-module-qtquick-controls, qml6-module-qtquick-layouts, qml6-module-qtquick-window, qml6-module-qtquick-templates, qml6-module-qtqml-workerscript"

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
