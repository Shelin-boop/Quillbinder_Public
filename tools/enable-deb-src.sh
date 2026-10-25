#!/bin/bash
# Lets apt fetch source packages of Ubuntu 24.04 ("apt-get source"), as tools/collect-sources.py
# needs them (0.50). Run as root; harmless when run again.
set -euo pipefail
cat > /etc/apt/sources.list.d/quillbinder-deb-src.sources <<'TEXT'
Types: deb-src
URIs: http://archive.ubuntu.com/ubuntu
Suites: noble noble-updates noble-security
Components: main restricted universe multiverse
Signed-By: /usr/share/keyrings/ubuntu-archive-keyring.gpg
TEXT
apt-get update -qq
echo "deb-src enabled"
