# Third-party notices

## The tunnelling engine

This client does not contain, link, or redistribute a tunnelling engine. It launches a
separate engine program and communicates with it over loopback sockets (the Clash API and
the engine's local mixed inbound). No engine code is compiled into this application and no
engine binary is shipped in this repository or in the packages built from it.

The engine AI Booster uses in its official builds is derived from:

- **hiddify-core** — https://github.com/hiddify/hiddify-core — GPL-3.0 with additional
  terms under GPL-3.0 section 7.
- **sing-box** — https://github.com/SagerNet/sing-box — GPL-3.0-or-later.

Full licence text: https://www.gnu.org/licenses/gpl-3.0.html

**Building the engine yourself.** Clone the first repository above and build its `./cmd/main`
target; the exact flags, and the two toolchain constraints that bite, are in
[README.md → The engine](README.md#the-engine). The result is a drop-in `aibooster-core`.

**Modifications.** AI Booster's official engine builds are produced from the projects above
and renamed, so the product does not carry upstream's branding — which upstream's additional
terms require of forks distributed through application stores. Those builds are made from a
tree that carries changes of our own, so they are not a plain rebuild of the tagged upstream
sources; the corresponding source for any engine binary we distribute is available on
request from the address in this file.

Nothing in this repository is derived from hiddify-core or sing-box, and no engine source or
binary is kept here.

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
