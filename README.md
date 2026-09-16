# AI Booster — Linux client

The Linux desktop client for the [AI Booster](https://allianceinterstellar.com) network,
written in Qt 6 / QML.

**This repository is the user interface only.** The tunnelling engine is a separate program
under a different licence; its source is not here and it is never linked into this
application — the client launches it as a child process and talks to it over loopback
sockets.

The **release packages** include a prebuilt engine, so they work on install. A build made
from this source tree does not; you supply the engine yourself, as described below. The
engine's own source is at
[aibooster-engine](https://github.com/AllianceInterStellar/aibooster-engine); see
[NOTICE.md](NOTICE.md) for the licensing.

---

## What it does

- Subscriptions and profiles — import by URL, store, switch between them
- Server list with latency, plus the engine's own selector/urltest groups
- Live traffic and connection counts from the engine's Clash API
- Engine log stream in-app
- Routing, DNS, inbound, TLS-tricks and WARP settings
- **Desktop proxy integration** for GNOME (gsettings) and KDE Plasma (kioslaverc), with
  crash recovery — see below

## Architecture

```
┌────────────────────────┐          ┌──────────────────────────┐
│  aibooster (this repo) │          │  aibooster-core          │
│  Qt 6 / QML            │          │  (separate program,      │
│                        │          │   GPL-3.0, not included) │
│  CoreProcess ──────────┼─ spawn ─▶│                          │
│  ClashApi ─────────────┼─ HTTP ──▶│  127.0.0.1:18756         │
│  SystemProxy           │          │  mixed inbound  :2334    │
└───────────┬────────────┘          └──────────────────────────┘
            │ gsettings / kioslaverc
            ▼
    desktop proxy settings
```

The boundary is deliberate. The engine is GPL-3.0 and is a separate work: this client never
`dlopen()`s it and never links it. Earlier cross-platform builds of this client did load the
engine in-process; that is gone. Besides the licensing boundary, out-of-process means an
engine that wedges or crashes surfaces as an error in the UI instead of taking the UI down
with it.

### About the system proxy

The client redirects the desktop's proxy settings at the engine's local inbound. That is
**machine-wide state that outlives the process**: if the client were killed while the proxy
was redirected, every application on the desktop would keep trying to reach a port with
nothing behind it — which presents as "the machine lost internet" with no obvious cause.

So [`SystemProxy`](src/platform/SystemProxy.h) writes the previous settings to
`$XDG_STATE_HOME/aibooster/system-proxy-restore.json` *before* changing anything, and
replays that file on the next launch. `SIGINT`/`SIGTERM`/`SIGHUP` are handled so an orderly
shutdown reverts immediately. A `SIGKILL` is recoverable: the proxy stays wrong only until
the app is next started.

If you ever need to undo it by hand:

```bash
gsettings set org.gnome.system.proxy mode none          # GNOME, Cinnamon, MATE…
kwriteconfig6 --file kioslaverc --group 'Proxy Settings' --key ProxyType 0   # KDE Plasma
```

## Building

Requires Qt 6.2+, OpenSSL and a C++17 compiler. Built and run on Ubuntu 22.04 (Qt 6.2)
and Ubuntu 24.04 (Qt 6.4).

On Ubuntu 22.04, `libqt6opengl6-dev` is required explicitly — 24.04 pulls it in for you, and
without it CMake reports Qt6Quick as missing while its config file plainly exists.

```bash
# Debian / Ubuntu
sudo apt install build-essential cmake ninja-build \
    qt6-base-dev qt6-declarative-dev libqt6opengl6-dev libgl1-mesa-dev \
    libqt6svg6 libssl-dev

# Fedora
sudo dnf install gcc-c++ cmake ninja-build \
    qt6-qtbase-devel qt6-qtdeclarative-devel openssl-devel

cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/aibooster
```

### Deployment configuration

The client is the front end for a subscription service. The endpoints and the payload
decryption key identify **one operator** and are deliberately not in this source tree —
publishing the key would hand that operator's paid subscription data to anyone who cloned
the repository.

A default build compiles and runs. It cannot talk to a service until you supply your own,
and it says so plainly rather than failing obscurely:

```bash
cmake -B build -G Ninja \
  -DAIBOOSTER_PREMIUM_AES_KEY_B64=<base64 AES-256 key> \
  -DAIBOOSTER_PREMIUM_URL=https://…/?iso=1 \
  -DAIBOOSTER_PREMIUM_BACKUP_URL=https://…/?iso=1 \
  -DAIBOOSTER_FREE_URL=https://…/?type=free \
  -DAIBOOSTER_FREE_BACKUP_URL=https://…/?type=free \
  -DAIBOOSTER_API_BASE_URL=https://… \
  -DAIBOOSTER_FIREBASE_API_KEY=<firebase web api key>
```

(The Firebase Web API key is a public project identifier, not a credential — access is
governed by Firebase security rules. It is parameterised so a fork points at its own
project.)

### The engine

The client needs an `aibooster-core` binary at runtime and looks for it, in order, at:

1. `$AIBOOSTER_CORE`
2. `<app dir>/aibooster-core`
3. `<app dir>/../libexec/aibooster/aibooster-core`
4. `/usr/libexec/aibooster/aibooster-core`, `/usr/lib/aibooster/…`, `/usr/local/lib/aibooster/…`
5. `$PATH`

If it is missing, the client says so and lists exactly where it looked. The official engine
build ships with the [AI Booster downloads](https://allianceinterstellar.com); you can also
build one yourself from the engine's upstream source — [NOTICE.md](NOTICE.md) says where
that is and what it is licensed under. The client drives it through its standard
`run -c <config> -d <settings>` interface:

```bash
# Go 1.26.1 specifically: on older toolchains a vendored TLS package asserts the layout of
# crypto/tls.ConnectionState at init and panics before main().
go build -trimpath -ldflags="-w -s -checklinkname=0 -buildid=" \
  -tags "with_gvisor,with_quic,with_wireguard,with_utls,with_clash_api,with_grpc,with_awg,\
tfogo_checklinkname0,with_conntrack,with_dhcp" \
  -o aibooster-core ./cmd/main
```

`with_naive_outbound` is omitted above: it links a prebuilt `libcronet.a` that uses CREL
relocations, which binutils older than 2.43 (Ubuntu 22.04 ships 2.38) cannot read. Add it
back on a newer toolchain if you need the naive protocol.

**Note on the engine's control API.** The client reads live traffic from it. Two things had
to be true for that to work, and neither was:

- The engine opened that listener only after downloading an optional web UI, so on a network
  where the download stalled the API never existed. Fixed upstream of the binaries we ship —
  it listens first now.
- The engine does not always bind the port we ask for; with a full sing-box config it used
  its own default. The client no longer assumes: it reads the port out of the line the engine
  prints when it binds.

The connection itself is not gated on any of this. Readiness is the proxy inbound being
open, which is the port traffic actually goes through.

## Packaging

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
DESTDIR="$PWD/pkgroot" cmake --install build --prefix /usr
packaging/build-deb.sh "$PWD/pkgroot"
```

Installs a `.desktop` entry, a scalable icon and AppStream metainfo under the usual XDG
locations. The package does not depend on, contain, or download the engine.

## Licence

GPL-3.0-or-later — see [LICENSE](LICENSE) and [NOTICE.md](NOTICE.md).
