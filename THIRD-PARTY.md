# Third-party parts

Quillbinder's own source code and files are under the Zero-Clause BSD license (0BSD, `LICENSE`):
anyone may use, copy, change and pass them on, with or without fee, without conditions.

The program uses, and its AppImage carries, parts of other projects under their own licenses. None
of them is changed by the 0BSD license of Quillbinder. The full license texts of everything the
AppImage carries are in `resources/licenses` (made by `tools/collect-licenses.py` from the Ubuntu
24.04 packages the libraries come from), in the AppImage under `usr/share/doc/quillbinder/licenses`,
and in the program under **Help > Third-Party Licenses** (also a button of **Help > About**).

| Part | Where | License |
|------|-------|---------|
| Qt 6.4 (Core, Gui, Widgets, Network, PrintSupport, Pdf, PdfWidgets, Svg, Xml, DBus, OpenGL, Wayland and XCB platform parts, image format plugins; as shipped by Ubuntu 24.04) | linked dynamically; carried by the AppImage | LGPL-3.0 (also available under GPL-2.0 / GPL-3.0); Qt Pdf is part of the qt6-webengine source package |
| Qt Multimedia | the add-on "Media Playback" | LGPL-3.0 |
| Hunspell (spell checking) | linked dynamically; carried by the AppImage | tri-license MPL-1.1 / GPL-2.0-or-later / LGPL-2.1-or-later |
| ICU, libpng, libjpeg-turbo, libwebp, OpenJPEG, PCRE2, double-conversion, md4c, graphite2, libb2, libgomp (GCC runtime), libxkbcommon, Wayland and XCB libraries | carried by the AppImage (taken from Ubuntu 24.04 by linuxdeploy) | their own licenses (MIT, BSD, Zlib, LGPL and others; GCC Runtime Library Exception for libgomp); each with its Ubuntu copyright file in `resources/licenses` |
| AppImage type2-runtime (the start code of the AppImage file), with musl libc, libfuse, squashfuse, zstd and zlib linked into it | the beginning of the AppImage file | MIT; musl MIT, libfuse LGPL-2.1, squashfuse BSD-2-Clause, zstd BSD-3-Clause or GPL-2.0, zlib Zlib (`packaging/licenses`) |
| Breeze icons (a subset) | `resources/icons/` | LGPL-3.0-or-later, see `resources/icons/COPYING-BREEZE` |
| FFmpeg 6.1.1 (decoding only, built without GPL parts) | the add-on "Media Playback" (`addons/media-playback`, built by `tools/build-ffmpeg.sh`) | LGPL-2.1-or-later; the add-on carries its license texts |
| Name lists of the Name Generator | the add-on "Name Generator", made from the Python package Faker by `tools/make-name-lists.py` | MIT (`names/LICENSE-Faker.txt` in the add-on) |
| linuxdeploy, linuxdeploy-plugin-qt, appimagetool | used to build the AppImage, not part of the program | MIT |

The source code of the Ubuntu libraries is available from Ubuntu: the name and version of each source
package are listed in Help > Third-Party Licenses (`apt-get source <package>=<version>`, or
https://launchpad.net/ubuntu/+source/<package>/<version>). The libraries inside the AppImage may be
replaced with other builds of the same version (extract the AppImage with `--appimage-extract`).

Scrivener and Scapple are names of products of Literature & Latte. Quillbinder is an independent
program, not made by or connected with Literature & Latte. It reads and writes their file formats so
that projects can move between the programs; it contains no program code of them.
