#!/usr/bin/env python3
"""Writes resources/licenses-windows/ (Help > Third-Party Licenses of the Windows program; port, W3, 2026-11-08).

The Windows program ships other files than the AppImage: Qt's own build of Qt 6.8.3 for MSVC (with the third-party
code compiled into its DLLs), Hunspell and zlib linked in (vcpkg), Microsoft's C++ runtime DLLs and the spelling
dictionaries. The list has the format of resources/licenses/index.json (tools/collect-licenses.py) and is compiled
into the Windows program under the same resource names (resources/resources-windows.qrc, tools/gen-resources.py).

It is made in a Claude session from these checkouts (the cloud session cannot reach download.qt.io; GitHub works):
  <lic>/qtbase, <lic>/qtsvg           git clone https://github.com/qt/<repo> at tag v6.8.3, sparse: every
                                      qt_attribution.json, /LICENSES/, and the files they name
  <lic>/chromium                      https://github.com/qt/qtwebengine-chromium at 55749ed0af58 (the submodule
                                      commit of qtwebengine v6.8.3, which builds Qt PDF): license files of PDFium
                                      and the parts Qt names in "Qt PDF Licensing" (doc.qt.io/qt-6.8/qtpdf-licensing.html)
  <lic>/hunspell                      https://github.com/hunspell/hunspell at v1.7.5 (the version vcpkg builds)
  <lic>/zlib                          https://github.com/madler/zlib at v1.3.2
  <dict>                              https://github.com/LibreOffice/dictionaries at libreoffice-25.8.7.3 (en/, de/)

  tools/collect-licenses-windows.py <lic> <dict>
"""
import json
import os
import re
import shutil
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "resources", "licenses-windows")
LINUX_TEXTS = os.path.join(ROOT, "resources", "licenses", "texts")
LIC, DICT = sys.argv[1], sys.argv[2]
QT_VERSION = "6.8.3"
SOURCES = "https://github.com/Shelin-boop/Quillbinder_Public/releases/tag/third-party-sources"

QT_MODULES = {
    "qtcore": "Qt6Core.dll",
    "qtgui": "Qt6Gui.dll (and the Windows platform plugin qwindows.dll, image plugins)",
    "qtnetwork": "Qt6Network.dll",
    "qtsvg": "Qt6Svg.dll, qsvg.dll, qsvgicon.dll",
}
# Qt's third-party parts that the Windows build carries (by the Id of their qt_attribution.json);
# left out: build system, Android, Unix-only (forkfd), ARM NEON (pixman), SQL, WebAssembly, XCB,
# macOS, D-Bus and the test library: none of them is in the Windows program
QT_PARTS = [
    "blake2", "doubleconversion", "easing", "md4", "md5", "pcre2", "pcre2-sljit", "rfc6234", "sha1",
    "sha3_endian", "sha3_keccak", "siphash", "tinycbor", "zlib", "tika-mimetypes", "unicode-character-database",
    "unicode-cldr", "d3d12memoryallocator", "vulkanmemoryallocator", "freetype", "freetype-zlib", "freetype-bdf",
    "freetype-pcf", "harfbuzz-ng", "iaccessible2", "icc-srgb-color-profile", "libjpeg", "libpng", "md4c",
    "opengl-headers", "opengl-es2-headers", "grayraster", "smooth-scaling-algorithm", "xserverhelper",
    "rhi-miniengine-d3d12-mipmap", "aglfn", "vulkan-xml-spec", "webgradients", "wintab", "psl-data", "libpsl",
    "xsvg",
]

texts = {}      # file name -> text
components = []


def read(path):
    with open(path, encoding="utf-8", errors="replace") as f:
        return f.read().replace("\r\n", "\n")


def add_text(name, body):
    texts[name] = body.rstrip() + "\n"
    return name


def qt_attributions():
    out = {}
    for repo in ("qtbase", "qtsvg"):
        base = os.path.join(LIC, repo)
        for dirpath, _, files in os.walk(base):
            if "qt_attribution.json" not in files:
                continue
            data = json.loads(read(os.path.join(dirpath, "qt_attribution.json")), strict=False)
            for item in data if isinstance(data, list) else [data]:
                item["_dir"] = dirpath
                item["_repo"] = base
                out[item.get("Id", "")] = item
    return out


def spdx_ids(expr):
    return [t for t in re.split(r"[\s()]+", expr or "") if t and t not in ("AND", "OR", "WITH")]


def qt_part_text(item):
    lines = [item.get("Name", item["Id"])]
    if item.get("Version"):
        lines.append("Version: " + item["Version"])
    if item.get("Homepage"):
        lines.append("Homepage: " + item["Homepage"])
    if item.get("Description"):
        lines.append(item["Description"].replace("\n", " "))
    copyright = item.get("Copyright")
    if isinstance(copyright, list):
        copyright = "\n".join(copyright)
    if copyright:
        lines += ["", copyright]
    if item.get("CopyrightFile"):
        lines += ["", read(os.path.normpath(os.path.join(item["_dir"], item["CopyrightFile"])))]
    lines.append("")
    files = item.get("LicenseFiles") or ([item["LicenseFile"]] if item.get("LicenseFile") else [])
    if files:
        for f in files:
            lines += [read(os.path.normpath(os.path.join(item["_dir"], f))), ""]
    else:
        for spdx in spdx_ids(item.get("LicenseId")):
            p = os.path.join(item["_repo"], "LICENSES", spdx + ".txt")
            if os.path.exists(p):
                lines += [read(p), ""]
    return "\n".join(lines)


def comp(name, version, license, where, text, source="", sourceFile=""):
    c = {"name": name, "version": version, "license": license, "where": where, "text": text}
    if source:
        c["source"] = source
    if sourceFile:
        c["sourceFile"] = sourceFile
    components.append(c)


def main():
    if os.path.isdir(OUT):
        shutil.rmtree(OUT)
    os.makedirs(os.path.join(OUT, "texts"))

    # ---- the program itself and its icons (as in the Linux list)
    comp("Quillbinder", "", "0BSD (Zero-Clause BSD)", "the program itself",
         add_text("quillbinder-0BSD.txt", read(os.path.join(LINUX_TEXTS, "quillbinder-0BSD.txt"))),
         "https://github.com/Shelin-boop/Quillbinder_Public")
    comp("Breeze icons (a subset)", "", "LGPL-3.0-or-later", "icons of the menus and toolbars",
         add_text("breeze-icons.txt", read(os.path.join(LINUX_TEXTS, "breeze-icons.txt"))),
         "the SVG files themselves, in resources/icons of the Quillbinder source "
         "(https://github.com/Shelin-boop/Quillbinder_Public); upstream: https://invent.kde.org/frameworks/breeze-icons")

    # ---- Qt
    qtsrc = f"qt-everywhere-src-{QT_VERSION}.tar.xz"
    comp(f"Qt {QT_VERSION}", QT_VERSION, "LGPL-3.0-only",
         "Qt6Core, Qt6Gui, Qt6Widgets, Qt6Network, Qt6Xml, Qt6Svg, Qt6PrintSupport, Qt6Pdf, Qt6PdfWidgets (DLLs beside "
         "the program) and Qt's plugins (platforms, styles, imageformats, iconengines, tls, networkinformation, generic). "
         "The DLLs are The Qt Company's own build for Windows (MSVC 2022, 64 bit), unchanged; each can be replaced "
         "by another build of the same version.",
         add_text("qt.txt", "Qt " + QT_VERSION + "\nCopyright (C) The Qt Company Ltd. and other contributors.\n"
                  "https://www.qt.io\n\nQt is used under the GNU Lesser General Public License version 3 (below); "
                  "the GNU General Public License version 3 it refers to is listed under the full texts of common "
                  "licenses.\n\n" + read(os.path.join(LIC, "qtbase", "LICENSES", "LGPL-3.0-only.txt"))),
         f"{SOURCES}, file {qtsrc} (the Qt Project's source release; also https://download.qt.io/official_releases/"
         f"qt/6.8/{QT_VERSION}/single/ and the tag v{QT_VERSION} of https://code.qt.io)", qtsrc)

    attr = qt_attributions()
    for pid in QT_PARTS:
        item = attr[pid]
        module = item.get("QDocModule", "")
        where = QT_MODULES.get(module, "Qt")
        comp(f"{item.get('Name', pid)} (in Qt)", item.get("Version", ""), item.get("License", item.get("LicenseId", "")),
             where, add_text(f"qt-{pid}.txt", qt_part_text(item)),
             f"the Qt source, file {qtsrc}", qtsrc)

    # ---- Qt PDF (PDFium and the parts it builds in; Qt's list in "Qt PDF Licensing" plus PDFium's own parts)
    ch = os.path.join(LIC, "chromium", "chromium")
    pdf_where = "Qt6Pdf.dll (showing PDF files)"

    def pdf(name, version, license, files, header=""):
        body = header + "".join(read(os.path.join(ch, f)) + "\n" for f in files)
        key = "qtpdf-" + re.sub(r"[^a-z0-9]+", "-", name.lower()).strip("-") + ".txt"
        comp(f"{name} (in Qt PDF)", version, license, pdf_where, add_text(key, body), f"the Qt source, file {qtsrc}", qtsrc)

    pdf("PDFium", "", "BSD-3-Clause", ["third_party/pdfium/LICENSE"])
    pdf("The Chromium Project", "", "BSD-3-Clause", ["LICENSE"])
    pdf("Abseil", "", "Apache-2.0", ["third_party/abseil-cpp/LICENSE"])
    pdf("FreeType", "", "FTL (FreeType License)", ["third_party/freetype/src/docs/FTL.TXT"])
    pdf("ICU", "", "Unicode-3.0 (ICU License)", ["third_party/icu/LICENSE"])
    pdf("libjpeg-turbo", "", "IJG, BSD-3-Clause, Zlib", ["third_party/libjpeg_turbo/LICENSE.md", "third_party/libjpeg_turbo/README.ijg"])
    pdf("libpng", "", "Libpng-2.0, Libpng", ["third_party/libpng/LICENSE"])
    pdf("zlib", "", "Zlib", ["third_party/zlib/LICENSE"])
    pdf("OpenJPEG", "2.5.0", "BSD-2-Clause", [],
        "\n".join(l[3:] if l.startswith(" * ") else l.lstrip(" *") for l in
                  read(os.path.join(ch, "third_party/pdfium/third_party/libopenjpeg/opj_includes.h")).split("*/")[0].split("\n")) + "\n")
    lcms_head = read(os.path.join(ch, "third_party/pdfium/third_party/lcms/src/cmscnvrt.c")).split("//---------------------------------------------------------------------------------")[1]
    pdf("Little CMS", "2.15", "MIT", [], "\n".join(l[2:] for l in lcms_head.strip().split("\n")) + "\n")
    agg = read(os.path.join(ch, "third_party/pdfium/third_party/agg23/agg_basics.h")).split("#ifndef")[0]
    pdf("Anti-Grain Geometry", "2.3", "MIT-like (permission notice below)", [], "\n".join(l[3:] for l in agg.strip().split("\n")) + "\n")
    pdf("BigInt", "", "Public domain (notice below)", ["third_party/pdfium/third_party/bigint/LICENSE"])
    pdf("FP16", "", "MIT", ["third_party/pdfium/third_party/fp16/LICENSE"])
    pdf("fast_float", "", "MIT", [],
        "Copyright (c) 2021 The fast_float authors\n\n" + read(os.path.join(LIC, "qtbase", "LICENSES", "MIT.txt")))

    # ---- linked into the program (vcpkg, static)
    hs = os.path.join(LIC, "hunspell")
    comp("Hunspell", "1.7.5", "MPL-1.1 or GPL-2.0-or-later or LGPL-2.1-or-later (used under MPL-1.1)",
         "spelling (linked into quillbinder.exe)",
         add_text("hunspell.txt", "".join(read(os.path.join(hs, f)) + "\n" for f in
                                          ("COPYING", "license.hunspell", "license.myspell", "COPYING.MPL"))),
         f"{SOURCES}, file hunspell-1.7.5.tar.gz (also https://github.com/hunspell/hunspell, tag v1.7.5)",
         "hunspell-1.7.5.tar.gz")
    comp("zlib", "1.3.2", "Zlib", "reading and writing packed files (linked into quillbinder.exe)",
         add_text("zlib.txt", read(os.path.join(LIC, "zlib", "LICENSE"))), "https://github.com/madler/zlib, tag v1.3.2")
    # 0.65.0: the check of package signatures, compiled in from the Quillbinder source (src/core/third_party)
    comp("Monocypher", "4.0.3", "BSD-2-Clause (dual-licensed: BSD-2-Clause or CC0-1.0)",
         "the check of package signatures (Ed25519), compiled into quillbinder.exe",
         add_text("monocypher.txt", read(os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                                                       "src", "core", "third_party", "monocypher", "LICENCE.md"))),
         "src/core/third_party/monocypher of the Quillbinder source; upstream: https://github.com/LoupVaillant/Monocypher, tag 4.0.3")

    # ---- Microsoft's C++ runtime
    comp("Microsoft Visual C++ Runtime", "14.x", "Microsoft Software License Terms (redistributable code of Visual Studio)",
         "msvcp140*.dll, vcruntime140*.dll, concrt140.dll, vccorlib140.dll beside the program",
         add_text("msvc-runtime.txt",
                  "Microsoft Visual C++ Runtime\nCopyright (c) Microsoft Corporation. All rights reserved.\n\n"
                  "These DLL files are Microsoft's redistributable C++ runtime, copied unchanged from Visual Studio by\n"
                  "Qt's deployment tool. Microsoft allows them to be distributed with programs built with Visual Studio\n"
                  "under the terms for \"Distributable Code\" of the Microsoft Software License Terms of Visual Studio\n"
                  "(https://visualstudio.microsoft.com/license-terms/, and the file redist.txt of Visual Studio).\n"
                  "They are not covered by Quillbinder's license, may not be changed, and are used only as part of\n"
                  "this program.\n"))

    # ---- spelling dictionaries (LibreOffice's, unchanged; the German ones renamed from de_XX_frami)
    lo = "https://github.com/LibreOffice/dictionaries, tag libreoffice-25.8.7.3"
    en = os.path.join(DICT, "en")
    de = os.path.join(DICT, "de")
    comp("English (United States) spelling dictionary", "2020.12.07", "SCOWL license (BSD-like, permissive)",
         "dictionaries/en_US.aff, en_US.dic", add_text("dict-en_US.txt", read(os.path.join(en, "README_en_US.txt"))),
         f"the files themselves (Hunspell dictionaries are their own source); {lo}, folder en")
    comp("English (United Kingdom) spelling dictionary", "", "GNU LGPL (as stated by its authors, below)",
         "dictionaries/en_GB.aff, en_GB.dic",
         add_text("dict-en_GB.txt", read(os.path.join(en, "README_en_GB.txt")) + "\n" + read(os.path.join(en, "README.txt"))),
         f"the files themselves; {lo}, folder en", "libreoffice-dictionaries-25.8.7.3-en.tar")
    comp("German (Germany, Austria, Switzerland) spelling dictionaries (frami)", "", "GPL-2.0 or GPL-3.0",
         "dictionaries/de_DE, de_AT, de_CH (.aff, .dic)",
         # the README is Latin-1
         add_text("dict-de-frami.txt", open(os.path.join(de, "README_de_DE_frami.txt"), encoding="latin-1").read()
                  + "\n" + read(os.path.join(de, "COPYING_GPLv2")) + "\n" + read(os.path.join(de, "COPYING_GPLv3"))),
         f"{SOURCES}, file libreoffice-dictionaries-25.8.7.3-de.tar (the dictionaries in the form LibreOffice and Ubuntu "
         f"distribute and maintain them; {lo}, folder de)", "libreoffice-dictionaries-25.8.7.3-de.tar")

    # ---- full texts the licenses above refer to
    common = []
    for name, path in (("LGPL-3.0", ("qtbase", "LGPL-3.0-only.txt")), ("GPL-3.0", ("qtbase", "GPL-3.0-only.txt")),
                       ("GPL-2.0", ("qtbase", "GPL-2.0-only.txt")), ("MPL-2.0", ("qtbase", "MPL-2.0.txt")),
                       ("Apache-2.0", ("qtbase", "Apache-2.0.txt")), ("Unicode-3.0", ("qtbase", "Unicode-3.0.txt"))):
        common.append({"name": name, "text": add_text(f"common-{name}.txt", read(os.path.join(LIC, path[0], "LICENSES", path[1])))})
    common.append({"name": "LGPL-2.1", "text": add_text("common-LGPL-2.1.txt", read(os.path.join(DICT, "de", "COPYING_LGPL_v2.1.txt")))})
    common.append({"name": "LGPL-2.0", "text": add_text("common-LGPL-2.0.txt", read(os.path.join(DICT, "de", "COPYING_LGPL_v2.0.txt")))})
    common.append({"name": "GPL-2.0 (as shipped with the English dictionaries)", "text": add_text("common-en-license.txt", read(os.path.join(en, "license.txt")))})

    index = {
        "note": "Made by tools/collect-licenses-windows.py from Qt " + QT_VERSION + "'s attribution files, the license files of "
                "Qt PDF's parts, Hunspell 1.7.5, zlib 1.3.2 and LibreOffice's dictionaries (sources pinned in the script).",
        "components": components,
        "commonLicenses": common,
    }
    for name, body in texts.items():
        with open(os.path.join(OUT, "texts", name), "w", encoding="utf-8", newline="\n") as f:
            f.write(body)
    with open(os.path.join(OUT, "index.json"), "w", encoding="utf-8", newline="\n") as f:
        json.dump(index, f, indent=1, ensure_ascii=False)
        f.write("\n")
    print(f"{len(components)} components, {len(common)} common licenses, {len(texts)} text files")


main()
