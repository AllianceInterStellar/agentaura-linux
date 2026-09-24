#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# Installs a built .deb into clean container images and checks that it actually runs there.
#   scripts/package-test.sh dist/agentaura_1.0.0_amd64.deb ubuntu:22.04 ubuntu:24.04
set -euo pipefail

DEB="${1:?usage: package-test.sh <deb> <image>...}"
shift
[ $# -gt 0 ] || { echo "no images given" >&2; exit 1; }

for image in "$@"; do
    echo "::group::$image"
    docker run --rm -v "$PWD/$(dirname "$DEB")":/pkg -e DEBIAN_FRONTEND=noninteractive "$image" bash -euo pipefail -c '
        apt-get update -qq
        # apt resolves the Depends: line — this is the whole point of the test.
        if ! apt-get install -y /pkg/'"$(basename "$DEB")"' xvfb xauth > /tmp/apt.log 2>&1; then
            cat /tmp/apt.log
            apt-cache policy $(dpkg-deb -f /pkg/'"$(basename "$DEB")"' Depends | tr "|," "\n\n" | awk "{print \$1}" | sort -u) 2>/dev/null | grep -B1 "Candidate: (none)" || true
            exit 1
        fi
        agentaura --version
        # TLS: Qt loads OpenSSL at runtime, so a missing libssl only shows up here.
        QT_QPA_PLATFORM=offscreen agentaura --self-test
        # The real X11 platform plugin, under a virtual display.
        set +e
        xvfb-run -a timeout 10 agentaura > /tmp/smoke.log 2>&1
        code=$?
        set -e
        cat /tmp/smoke.log
        if [ "$code" -ne 124 ]; then
            echo "agentaura exited with $code under X11; expected it to be waiting at sign-in" >&2
            exit 1
        fi
        test -f /usr/share/applications/io.allianceinterstellar.AgentAura.desktop
        test -f /usr/share/icons/hicolor/256x256/apps/io.allianceinterstellar.AgentAura.png
        echo "OK on $(. /etc/os-release; echo "$PRETTY_NAME")"
    '
    echo "::endgroup::"
done
