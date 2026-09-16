# Third-party notices

## The tunnelling engine

This client never links or embeds a tunnelling engine. It launches a separate engine program
and communicates with it over loopback sockets (the Clash API and the engine's local mixed
inbound). No engine code is compiled into this application, and no engine source or binary is
kept in this repository — a build made from this tree alone has no engine.

**The release packages do redistribute an engine binary**, at
`/usr/libexec/aibooster/aibooster-core`, so that they work on install. Its **complete
corresponding source** is published at
[AllianceInterStellar/aibooster-engine](https://github.com/AllianceInterStellar/aibooster-engine),
and this repository's release workflow builds the shipped binary from exactly that tree — so
the source is not an offer on paper, it is the thing the binary was made from.

The engine derives from:

- **hiddify-core** — https://github.com/hiddify/hiddify-core — GPL-3.0 with additional
  terms under GPL-3.0 section 7.
- **sing-box** — https://github.com/SagerNet/sing-box — GPL-3.0-or-later.

Full licence text: https://www.gnu.org/licenses/gpl-3.0.html

**Building the engine yourself.** Clone the source repository linked above and build its
`./cmd/main` target; the exact flags, and the two toolchain constraints that bite, are in its
README and in [README.md → The engine](README.md#the-engine). The result is a drop-in
`aibooster-core`.

**Modifications.** AI Booster's engine builds carry changes of our own and are renamed, so
the product does not carry upstream's branding — which upstream's additional terms require of
forks distributed through application stores. They are therefore not a plain rebuild of the
tagged upstream sources, and the changes are listed in the source repository above rather
than left for a reader to diff.

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
