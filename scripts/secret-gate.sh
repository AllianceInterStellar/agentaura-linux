#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# Fails if the tree contains something shaped like a live credential, a personal address or
# a developer's machine path. Run in CI on every push; run it yourself before publishing.
set -euo pipefail
cd "$(dirname "$0")/.."

# The file list. In a checkout it is what git tracks; outside one (a fresh copy about to be
# published) it is every file. Never an empty list: a gate that scans nothing passes.
list_files() {
    if git rev-parse --is-inside-work-tree >/dev/null 2>&1 && [ -n "$(git ls-files | head -1)" ]; then
        git ls-files -z
    else
        find . -type f -not -path './.git/*' -not -path './build*' -not -path './dist/*' -print0
    fi
}
count="$(list_files | tr -cd '\0' | wc -c | tr -d ' ')"
if [ "$count" -eq 0 ]; then
    echo "::error::no files to scan" >&2
    exit 1
fi
echo "Scanning $count files."

fail=0
check() {
    local label="$1" pattern="$2"
    local hits
    hits="$(list_files | xargs -0 grep -nIE -e "$pattern" -- 2>/dev/null \
            | grep -v -e 'scripts/secret-gate.sh:' || true)"
    if [ -n "$hits" ]; then
        echo "::error::$label"
        echo "$hits"
        fail=1
    fi
}

check "private key material"            '-----BEGIN [A-Z ]*PRIVATE KEY-----'
check "GitHub token"                    'gh[pousr]_[A-Za-z0-9]{36,}'
check "Anthropic / OpenAI key"          'sk-(ant-)?[A-Za-z0-9_-]*[A-Z0-9][A-Za-z0-9_-]{20,}'
check "Google OAuth client secret"      'GOCSPX-[A-Za-z0-9_-]{20,}'
check "AWS access key"                  'AKIA[0-9A-Z]{16}'
check "long hex token"                  '["'"'"'][0-9a-f]{40,}["'"'"']'
check "a developer's home directory"    '/Users/[a-z]'
check "personal e-mail address"         '[A-Za-z0-9._%+-]+@(gmail|qq|163|outlook|hotmail|icloud|privaterelay\.appleid)\.com'

if [ "$fail" -ne 0 ]; then
    echo "Credential-shape gate failed. Move the value to a build-time setting (see README)." >&2
    exit 1
fi
echo "Credential-shape gate: clean."
