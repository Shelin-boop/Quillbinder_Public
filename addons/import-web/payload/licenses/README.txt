Files in this add-on ("Web Page Import" 1.0)
============================================

plugins/libqb-import-web.so is part of Quillbinder (fetching, the article finder, the window).

qtplugins/tls/libqopensslbackend.so
    Qt's TLS plugin for OpenSSL (Qt Network 6.4.2), as built by Ubuntu 24.04 (package
    libqt6network6t64 6.4.2+dfsg-21.1build5). It lets the program fetch secure (https) pages; the
    AppImage does not carry it. It uses the system's OpenSSL 3 (libssl.so.3, libcrypto.so.3).
    Licence: GNU LGPL 3.0 (Qt-LGPL-3.0.txt, which refers to GPL-3.0.txt).
    Source: https://download.qt.io/archive/qt/6.4/6.4.2/submodules/ or "apt-get source qt6-base"
    on Ubuntu 24.04. It can be replaced by another build of the same Qt version.
