# AgentAura — desktop client for Linux

[![build](https://github.com/AllianceInterStellar/agentaura-linux/actions/workflows/build.yml/badge.svg)](https://github.com/AllianceInterStellar/agentaura-linux/actions/workflows/build.yml)
[![Discord](https://img.shields.io/badge/Discord-join%20the%20community-5865F2?logo=discord&logoColor=white)](https://discord.gg/PkqfnYSmZB)

The desktop client for [AgentAura](https://allianceinterstellar.com/en/agentaura): install a
private AI agent (Claude Code or Codex) on a Linux server you control, then manage it and
chat with it from your desktop. Written in C++ with Qt 6 Widgets.

## Install

Download the latest release from
**[Releases](https://github.com/AllianceInterStellar/agentaura-linux/releases)**. Every
release has x86-64 and ARM64 builds.

**Ubuntu or Debian (22.04 or newer)**

```sh
sudo apt install ./agentaura_*_amd64.deb      # or _arm64.deb
```

apt installs Qt and OpenSSL for you. AgentAura then appears in your application menu, or
run `agentaura` from a terminal.

**Other distributions**

The `.tar.gz` contains the same binary without package metadata. Install Qt 6.2 or newer
(Widgets and Network) and OpenSSL 3 from your distribution, then:

```sh
tar xzf agentaura-*-linux-amd64.tar.gz       # or -arm64
./agentaura-*/agentaura
```

**Check that it can reach the service**

Every request the client makes is HTTPS or WSS, and Qt loads its TLS library at runtime. A
missing OpenSSL does not stop the app from starting — it makes every request fail. This
command checks for that:

```sh
agentaura --self-test
```

## What it does

- **Sign in** with an email one-time code (no password), the same account as the web, iOS
  and Android apps.
- **Deploy** an agent to a server: pick the engine (Claude Code or Codex), the provider,
  plan and region.
- **Agents** — see every agent you own, its status while it provisions, restarts or
  migrates, and act on it.
- **Chat** with an agent. Replies stream from the agent's gateway over a WebSocket.
- **Account** — plan and subscription status.

## Build from source

Ubuntu 22.04 or newer:

```sh
sudo apt install build-essential cmake ninja-build qt6-base-dev libgl1-mesa-dev
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/agentaura
```

The code also builds on macOS and Windows with Qt 6.2 or newer; the release packages here
are Linux only.

To build the Debian package the way the release pipeline does:

```sh
DESTDIR="$PWD/pkgroot" cmake --install build --prefix /usr
packaging/build-deb.sh "$PWD/pkgroot"
```

### Pointing a fork at your own backend

The client talks to AgentAura's public service by default. Both endpoints are configure-time
settings:

```sh
cmake -B build \
  -DAGENTAURA_API_BASE_URL=https://api.example.com \
  -DAGENTAURA_FIREBASE_API_KEY=<your Firebase Web API key>
```

The Firebase Web API key in `CMakeLists.txt` is not a secret. It identifies a Firebase project,
ships inside every client, and grants nothing by itself — access is enforced by Firebase Auth
and the service's own checks.

## Project layout

```
src/
  main.cpp            entry point, --version and --self-test
  MainWindow.*        navigation between the screens
  screens/            Agents, Deploy, Chat, Config, Account, sign-in dialog
  services/           ApiClient (REST), FirebaseAuth, ChatService,
                      WebSocketClient (RFC 6455 over QSslSocket, no QtWebSockets)
  models/  widgets/  theme/
packaging/            .desktop entry, AppStream metadata, icons, build-deb.sh
scripts/              package-test.sh (clean-image install test), secret-gate.sh
.github/workflows/    build (every push) and release (on a v* tag)
```

## Continuous integration

Every push builds on Ubuntu 22.04 (Qt 6.2) and 24.04 (Qt 6.4), runs `--self-test`, and
starts the app headless — a healthy start is a process still waiting at the sign-in dialog
when the test stops it. The package test builds the `.deb`, installs it on clean
`ubuntu:22.04` and `ubuntu:24.04` images, and runs it under a real X11 display; that is what
proves the package's dependency list is complete. A credential-shape gate rejects anything
that looks like a token, a private key, a personal e-mail address or a developer's home
directory.

## Security

Please report security issues privately to **support@allianceinterstellar.com** rather than
in a public issue.

## License

[MIT](LICENSE). Qt is used under the LGPL v3 and linked dynamically; see
[NOTICE.md](NOTICE.md), which also covers the AgentAura name and icon.
