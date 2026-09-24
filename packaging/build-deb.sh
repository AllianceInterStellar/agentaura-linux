#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# Assembles a .deb from an already-installed staging tree.
#
#   cmake -B build -DCMAKE_BUILD_TYPE=Release
#   cmake --build build --parallel
#   DESTDIR="$PWD/pkgroot" cmake --install build --prefix /usr
#   packaging/build-deb.sh "$PWD/pkgroot"

set -euo pipefail

PKGROOT="${1:?usage: build-deb.sh <staging-dir> [outdir]}"
OUTDIR="${2:-dist}"

VERSION="$(sed -n 's/^project(AgentAura VERSION \([0-9.]*\).*/\1/p' \
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

# Shared-library dependencies: the libraries the binary itself links (its ELF NEEDED
# entries), mapped to the packages that own them — not a hand-kept list that rots when Qt
# moves.
#
# Only the DIRECT ones. `ldd` also lists everything those libraries pull in (Qt's libicu70,
# libdouble-conversion3, ...), and those names are specific to the release we build on:
# Ubuntu 24.04 has libicu74, not libicu70, so a package that names libicu70 can never be
# installed there. The Qt packages declare their own dependencies; we only name Qt.
BIN="$STAGE/usr/bin/agentaura"
declare -A DIRECT=()
while read -r soname; do
    DIRECT["$soname"]=1
done < <(objdump -p "$BIN" | awk '/^ *NEEDED/ {print $2}')
if [ ${#DIRECT[@]} -eq 0 ]; then
    echo "objdump found no NEEDED entries in $BIN" >&2
    exit 1
fi

declare -a PKGS=()
while read -r lib; do
    [ -n "$lib" ] || continue
    owner=""
    # Query the path as reported AND as resolved: on Ubuntu 22.04 /lib is a symlink and
    # dpkg only knows the spelling it recorded. `|| true` because dpkg -S exits 1 for an
    # unknown path, which under `set -e` + pipefail would end the script silently.
    for candidate in "$lib" "$(readlink -f "$lib")"; do
        owner="$(dpkg -S "$candidate" 2>/dev/null | head -1 | cut -d: -f1 || true)"
        [ -n "$owner" ] && break
    done
    if [ -n "$owner" ]; then
        PKGS+=("$owner")
    else
        echo "note: no package owns $lib; not adding a dependency for it" >&2
    fi
done < <(ldd "$BIN" | awk '/=>/ && $3 ~ /^\// {print $1, $3}' | while read -r name path; do
    [ -n "${DIRECT[$name]:-}" ] && echo "$path"
done)

if [ ${#PKGS[@]} -eq 0 ]; then
    echo "Could not resolve any library dependencies for usr/bin/agentaura" >&2
    exit 1
fi

# Each library also accepts its "t64" successor: Ubuntu 24.04 renamed them for 64-bit
# time_t, and a package built on 22.04 would otherwise be uninstallable there.
# (paste -d takes a cycling list of delimiters, so use one and space it afterwards.)
declare -a DEPLIST=()
while read -r pkg; do
    case "$pkg" in
        lib*[0-9]) DEPLIST+=("$pkg | ${pkg}t64") ;;
        *)         DEPLIST+=("$pkg") ;;
    esac
done < <(printf '%s\n' "${PKGS[@]}" | sort -u)
DEPENDS="$(printf '%s\n' "${DEPLIST[@]}" | paste -sd, - | sed 's/,/, /g')"

# Loaded at runtime, so invisible to ldd:
#  - qt6-qpa-plugins carries the platform plugins (xcb, wayland shell, offscreen). Without
#    it the app dies at start with "could not find the Qt platform plugin".
#  - libssl3 is dlopen'ed by Qt's TLS backend. Without it the app starts fine and then
#    every HTTPS/WSS request fails — which, for a client that only talks HTTPS, is all of them.
RUNTIME_DEPENDS="qt6-qpa-plugins, libssl3 | libssl3t64"

cat > "$STAGE/DEBIAN/control" <<CONTROL
Package: agentaura
Version: ${VERSION}
Section: net
Priority: optional
Architecture: ${ARCH}
Depends: ${DEPENDS}, ${RUNTIME_DEPENDS}
Recommends: qt6-wayland
Maintainer: AllianceInterStellar <support@allianceinterstellar.com>
Homepage: https://allianceinterstellar.com/en/agentaura
Description: AgentAura desktop client
 Deploy a private AI agent on a Linux server you control, then manage it and
 chat with it from the desktop.
CONTROL

mkdir -p "$OUTDIR"
DEB="$OUTDIR/agentaura_${VERSION}_${ARCH}.deb"
dpkg-deb --build --root-owner-group "$STAGE" "$DEB"
echo "Built $DEB"
dpkg-deb --info "$DEB"
