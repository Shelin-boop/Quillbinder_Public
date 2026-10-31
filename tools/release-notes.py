#!/usr/bin/env python3
"""Writes the text of a release on the project page (0.50).

    tools/release-notes.py <dist dir> > notes.md
    tools/release-notes.py --whats-new          # one line per change, for packaging/build-update.sh

Takes the version from CMakeLists.txt, what is new from docs/CHANGES.md, and names the files of
<dist dir> (the AppImage, the program update, the add-ons) with what each is for.
"""
import json
import os
import re
import sys
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def cmake_var(name):
    with open(os.path.join(ROOT, "CMakeLists.txt"), encoding="utf-8") as f:
        m = re.search(r'^set\(%s\s+"([^"]*)"\)' % name, f.read(), re.M)
    return m.group(1) if m else ""


def changes(version):
    with open(os.path.join(ROOT, "docs", "CHANGES.md"), encoding="utf-8") as f:
        text = f.read()
    m = re.search(r"^## %s\b[^\n]*\n(.*?)(?=^## |\Z)" % re.escape(version), text, re.M | re.S)
    return m.group(1).strip() if m else ""


def addon_name(path):
    try:
        with zipfile.ZipFile(path) as z:
            m = json.loads(z.read("manifest.json"))
        return m.get("name", ""), m.get("version", ""), m.get("kind", "")
    except Exception:
        return "", "", ""


def main(dist):
    version = cmake_var("APP_VERSION")
    label = cmake_var("APP_VERSION_LABEL")
    shown = version + (" " + label if label else "")
    home = cmake_var("APP_HOMEPAGE")
    files = sorted(os.listdir(dist))
    appimage = [f for f in files if f.endswith(".AppImage") and "-full-" not in f]
    full = [f for f in files if f.endswith(".AppImage") and "-full-" in f]
    update = [f for f in files if f.endswith("-update.qbpkg")]
    # the one-package media bundle first, so that it is seen before the two add-ons it holds
    addons = sorted((f for f in files if f.endswith(".qbpkg") and f not in update),
                    key=lambda f: (not f.startswith("media-all-formats"), f))
    out = []
    out.append("**Quillbinder %s**: a writing studio for long texts, for Linux (x86_64). "
               "It opens and saves projects in its own format and in Scrivener 3 format (.scriv); it is an "
               "independent program, not made by or connected with Literature & Latte." % shown)
    out.append("")
    if label:
        out.append("> **This is an %s version.** Keep backups of your projects (File > Back Up > Back Up Now; the "
                   "automatic backups are set under File > Options > Backup), and keep a copy of your manuscripts "
                   "outside the program." % label)
        out.append("")
    out.append("### Download and start")
    out.append("")
    if appimage and full:
        out.append("1. Download **%s** (the program), or **%s**: the full version, with all add-ons and "
                   "the libraries they need included." % (appimage[0], full[0]))
    elif appimage:
        out.append("1. Download **%s**." % appimage[0])
    out.append("2. Make it executable: right-click > Properties > Permissions > *Is executable*, or "
               "`chmod +x %s`." % (appimage[0] if appimage else "Quillbinder-*.AppImage"))
    out.append("3. Double-click it. Help > Add to Application Menu puts it into the menu of the desktop.")
    if any(f.startswith("media-all-formats") for f in addons):
        out.append("4. For audio and video in the editor, download **media-all-formats.qbpkg** and install it with "
                   "File > Install Update or Add-on: one package, every format. (The full version has it already.)")
    out.append("")
    out.append("Already have an AppImage of Quillbinder? Replace the file with this one; settings, add-ons and "
               "projects stay. Or install the program update with File > Install Update or Add-on.")
    out.append("")
    out.append("### Files")
    out.append("")
    out.append("| File | What it is |")
    out.append("|------|------------|")
    for f in appimage:
        out.append("| %s | The program (AppImage) |" % f)
    for f in full:
        out.append("| %s | **The full version** (AppImage): the program with all add-ons and the libraries they need "
                   "included: audio and video in every format (also those licensed under patents), the extra themes, "
                   "the project templates, the compile format, the scriptwriting pack, the writing tools and the "
                   "importers. They are installed at the first start; nothing else to download. |" % f)
    for f in update:
        out.append("| %s | Program update for an AppImage of Quillbinder you already have "
                   "(File > Install Update or Add-on) |" % f)
    for f in addons:
        name, ver, _kind = addon_name(os.path.join(dist, f))
        note = ""
        if f.startswith("media-all-formats"):
            note = (" **Audio and video in every format, in one package**: installs Media Playback and Extra "
                    "Video and Audio Formats together (H.264, H.265, AAC and the other formats licensed under "
                    "patents included). Install this one, or only media-playback.qbpkg for the free formats.")
        elif f.startswith("media-extra-formats"):
            note = (" Plays H.264, H.265, AAC and the other formats licensed under patents; needs Media Playback "
                    "1.1. Optional: Media Playback plays the free formats without it.")
        elif f.startswith("media-playback"):
            note = " Plays audio and video files in free formats (MP3, Ogg, Opus, FLAC, WAV, WebM ...)."
        out.append("| %s | Add-on \"%s\" %s (File > Install Update or Add-on).%s |" % (f, name, ver, note))
    for f in files:
        if f.endswith("-symbols.xz"):
            out.append("| %s | The program file before strip, to read crash reports (developers) |" % f)
        if f == "THIRD-PARTY-SOURCES.txt":
            out.append("| %s | Which source files belong to the libraries of this release |" % f)
        if f == "SHA256SUMS.txt":
            out.append("| %s | SHA-256 checksums of the files (`sha256sum -c SHA256SUMS.txt`) |" % f)
    out.append("")
    out.append("### Source code")
    out.append("")
    out.append("Quillbinder's own source code is this repository at the tag of this release (Zero-Clause BSD "
               "license). The source code of every library the AppImage and the add-ons carry, in the versions "
               "used, is in the release [Third-party source code](%s/releases/tag/third-party-sources); "
               "THIRD-PARTY-SOURCES.txt names the files, and Help > Third-Party Licenses shows each library's "
               "license and source file." % home)
    out.append("")
    notes = changes(version)
    if notes:
        out.append("### What is new in %s" % shown)
        out.append("")
        out.append(notes)
        out.append("")
    print("\n".join(out))
    return 0


def whats_new():
    """The changes of this version as short lines: the first sentence of each item."""
    items, cur = [], None
    for line in changes(cmake_var("APP_VERSION")).splitlines():
        if line.startswith("- "):
            cur = [line[2:].strip()]
            items.append(cur)
        elif cur is not None and line.startswith("  "):
            cur.append(line.strip())
        else:
            cur = None
    for parts in items:
        text = re.sub(r"\*\*|`", "", " ".join(parts))
        text = re.split(r"(?<=[a-z0-9)])[.;] ", text)[0].rstrip(".").replace(": ", ": ", 1)
        print(text)
    return 0


if __name__ == "__main__":
    if len(sys.argv) == 2 and sys.argv[1] == "--whats-new":
        sys.exit(whats_new())
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        sys.exit(2)
    sys.exit(main(sys.argv[1]))
