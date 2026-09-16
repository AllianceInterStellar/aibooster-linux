# Third-party notices

## The tunnelling engine

This client never links or embeds a tunnelling engine. It launches a separate engine
program and communicates with it over loopback sockets (the Clash API and the engine's
local mixed inbound). No engine code is compiled into this application, and no engine
source or binary is kept in this repository.

**The release packages do redistribute an engine binary**, at
`/usr/libexec/aibooster/aibooster-core`, so that they work out of the box. It is built by
this repository's release workflow from the pinned upstream tag below, unmodified, and
renamed. The corresponding source is that tag and its submodules — the same commit the
workflow clones, so anyone can reproduce the binary from it. A build made from this
repository alone contains no engine.

The engine is derived from:

- **hiddify-core** — https://github.com/hiddify/hiddify-core — GPL-3.0 with additional
  terms under GPL-3.0 section 7.
- **sing-box** — https://github.com/SagerNet/sing-box — GPL-3.0-or-later.

Full licence text: https://www.gnu.org/licenses/gpl-3.0.html

**Building the engine yourself.** Clone the first repository above and build its `./cmd/main`
target; the exact flags, and the two toolchain constraints that bite, are in
[README.md → The engine](README.md#the-engine). The result is a drop-in `aibooster-core`.

**Modifications.** The engine is rebuilt from the sources above at tag **v4.1.0**, which is
what `.github/workflows/release.yml` clones. Its behaviour is unmodified; the build renames
the shipped binary so the product does not carry upstream's branding, which upstream's
additional terms require of forks distributed through application stores. One build tag is
omitted — `with_naive_outbound`, because it links a prebuilt library using relocations the
build runner's binutils cannot read — so the naive protocol is absent. Nothing in this
repository is derived from hiddify-core or sing-box.

## This client

Copyright (C) AllianceInterStellar and contributors.
Licensed under GPL-3.0-or-later — see [LICENSE](LICENSE).

Portions of the user interface and the service integration originate in the cross-platform
AI Booster Qt client and were adapted here for Linux.

## Qt

Built against **Qt 6** (https://www.qt.io) under the LGPL-3.0. Qt is dynamically linked and
is not redistributed in this repository.

## OpenSSL

Uses **OpenSSL** (https://www.openssl.org) under the Apache-2.0 licence, dynamically linked
and not redistributed here.
