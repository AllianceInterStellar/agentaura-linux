# Notices

## This client

The source code in this repository is released under the [MIT License](LICENSE).

## Qt

AgentAura is built with the [Qt](https://www.qt.io) framework (Qt Widgets and Qt Network),
used under the terms of the
[GNU Lesser General Public License v3](https://www.gnu.org/licenses/lgpl-3.0.html).

The release packages do **not** contain Qt. They link dynamically against the Qt libraries
your distribution provides, so you can replace or rebuild those libraries independently of
this application. The source for Qt is available from
[code.qt.io](https://code.qt.io) and from your distribution.

## OpenSSL

At runtime Qt Network loads the system [OpenSSL](https://www.openssl.org) library for TLS.
OpenSSL 3 is licensed under the [Apache License 2.0](https://www.openssl.org/source/license.html).
It is not included in the release packages.

## Name and icon

"AgentAura" and the AgentAura icon identify AllianceInterStellar's product. The MIT License
covers the source code; it does not grant rights to use the name or the icon to identify a
different product. If you distribute a modified build, please give it a different name and
icon.
