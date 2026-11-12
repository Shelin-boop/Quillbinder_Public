#!/usr/bin/env python3
"""Writes the text of a release on the project page (0.50).

    tools/release-notes.py <dist dir> > notes.md
    tools/release-notes.py <dist dir> --test    # a private test build: the files only (tools/test-release.sh)
    tools/release-notes.py --whats-new          # one line per change, for packaging/linux/build-update.sh

Takes the version from CMakeLists.txt, what is new from docs/CHANGES.md, and names the files of
<dist dir> with what each is for, sorted by system under a short headline each (Linux, Windows, Android, add-ons
for every system, the files for checking; 2026-11-12).
"""
import io
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


def addon_info(path):
    """name, version, and the system the add-on is built for ("" = every system: no program code inside)."""
    def system_of(z, m):
        if m.get("platform", "").startswith("windows"):
            return "windows"
        if m.get("runtime") or any(n.endswith(".so") or ".so." in n for n in z.namelist()):
            return "linux"
        # a bundle: the system of the packages inside it
        for n in z.namelist():
            if n.endswith(".qbpkg"):
                try:
                    with zipfile.ZipFile(io.BytesIO(z.read(n))) as inner:
                        found = system_of(inner, json.loads(inner.read("manifest.json")))
                    if found:
                        return found
                except Exception:
                    pass
        return ""
    try:
        with zipfile.ZipFile(path) as z:
            m = json.loads(z.read("manifest.json"))
            return m.get("name", ""), m.get("version", ""), system_of(z, m)
    except Exception:
        return "", "", ""


def addon_note(f):
    if f.startswith("media-all-formats"):
        return (" **The commonly used audio and video formats, in one package**: installs Media Playback and Extra "
                "Video and Audio Formats together (H.264, H.265, AAC and the other formats licensed under "
                "patents included). Install this one, or only media-playback.qbpkg for the free formats.")
    if f.startswith("media-extra-formats"):
        return (" Plays H.264, H.265, AAC and the other formats licensed under patents; needs Media Playback "
                "1.1. Optional: Media Playback plays the free formats without it.")
    if f.startswith("media-playback"):
        return " Plays audio and video files in free formats (MP3, Ogg, Opus, FLAC, WAV, WebM ...)."
    return ""


def addon_rows(dist, files):
    out = []
    for f in files:
        name, ver, _system = addon_info(os.path.join(dist, f))
        out.append("| %s | Add-on \"%s\" %s (File > Install Update or Add-on).%s |" % (f, name, ver, addon_note(f)))
    return out


def main(dist, test=False):
    """The release text, the files sorted by system under a short headline each (2026-11-12, the writer's wish:
    the files as before, sorted by system)."""
    version = cmake_var("APP_VERSION")
    label = cmake_var("APP_VERSION_LABEL")
    shown = version + (" " + label if label else "")
    home = cmake_var("APP_HOMEPAGE")
    files = sorted(os.listdir(dist))
    is_win = lambda f: "windows" in f.lower()
    pkgs = [f for f in files if f.endswith(".qbpkg")]
    # Linux
    appimage = [f for f in files if f.endswith(".AppImage") and "-full-" not in f]
    full = [f for f in files if f.endswith(".AppImage") and "-full-" in f]
    update = [f for f in pkgs if f.endswith("-update.qbpkg") and not is_win(f)]
    # Windows
    win_zip = [f for f in files if f.endswith(".zip") and is_win(f) and "-full-" not in f and "symbols" not in f]
    win_full = [f for f in files if f.endswith(".zip") and is_win(f) and "-full-" in f]
    win_update = [f for f in pkgs if is_win(f) and f.endswith("-update.qbpkg")]
    win_symbols = [f for f in files if f.endswith(".zip") and is_win(f) and "symbols" in f]
    # Android
    apk = [f for f in files if f.endswith(".apk")]
    # add-ons: for every system (no program code inside), for Linux, for Windows; the one-package media bundle first,
    # so that it is seen before the two add-ons it holds
    addons = [f for f in pkgs if not f.endswith("-update.qbpkg")]
    order = lambda f: (not f.startswith("media-all-formats"), f)
    systems = {f: addon_info(os.path.join(dist, f))[2] for f in addons}
    for f in addons:
        if not systems[f] and is_win(f):
            systems[f] = "windows"
    common = sorted((f for f in addons if not systems[f]), key=order)
    linux_addons = sorted((f for f in addons if systems[f] == "linux"), key=order)
    win_addons = sorted((f for f in addons if systems[f] == "windows"), key=order)
    has_linux = bool(appimage or full or update or linux_addons)
    has_win = bool(win_zip or win_full or win_update or win_addons)
    has_android = bool(apk)
    names = [n for n, on in (("Linux (x86_64)", has_linux), ("Windows 11 (64-bit)", has_win), ("Android", has_android)) if on]
    out = []
    out.append("**Quillbinder %s**: a writing studio for long texts, for %s. "
               "It opens and saves projects in its own format and in Scrivener 3 format (.scriv); it is an "
               "independent program, not made by or connected with Literature & Latte."
               % (shown, " and ".join(names) if names else "Linux (x86_64)"))
    out.append("")
    if label:
        article = "an" if label[:1].lower() in "aeiou" else "a"
        out.append("> **This is " + article + " %s version.** Keep backups of your projects (File > Back Up > Back Up Now; the "
                   "automatic backups are set under File > Options > Backup), and keep a copy of your manuscripts "
                   "outside the program." % label)
        out.append("")

    if has_linux:
        out.append("## Linux")
        out.append("")
        if appimage and full:
            out.append("1. Download **%s** (the program), or **%s**: the full version, with all add-ons and "
                       "the libraries they need included." % (appimage[0], full[0]))
        elif appimage:
            out.append("1. Download **%s**." % appimage[0])
        elif full:
            out.append("1. Download **%s**: the full version, with all add-ons and the libraries they need included." % full[0])
        out.append("2. Make it executable: right-click > Properties > Permissions > *Is executable*, or "
                   "`chmod +x %s`." % ((appimage or full)[0] if (appimage or full) else "Quillbinder-*.AppImage"))
        out.append("3. Double-click it. Help > Add to Application Menu puts it into the menu of the desktop.")
        if any(f.startswith("media-all-formats") for f in linux_addons + common):
            out.append("4. For audio and video in the editor, download **media-all-formats.qbpkg** and install it with "
                       "File > Install Update or Add-on: one package for the commonly used formats. (The full version has it already.)")
        out.append("")
        out.append("Already have an AppImage of Quillbinder? Replace the file with this one; settings, add-ons and "
                   "projects stay. Or install the program update with File > Install Update or Add-on.")
        out.append("")
        out.append("| File | What it is |")
        out.append("|------|------------|")
        for f in appimage:
            out.append("| %s | The program (AppImage) |" % f)
        for f in full:
            out.append("| %s | **The full version** (AppImage): the program with all add-ons and the libraries they need "
                       "included: the commonly used audio and video formats (also those licensed under patents), the extra themes, "
                       "the project templates, the compile format, the scriptwriting pack, the writing tools and the "
                       "importers. They are installed at the first start; nothing else to download. |" % f)
        for f in update:
            out.append("| %s | Program update for an AppImage of Quillbinder you already have "
                       "(File > Install Update or Add-on) |" % f)
        out.extend(addon_rows(dist, linux_addons))
        for f in files:
            if f.endswith("-symbols.xz"):
                out.append("| %s | The program file before strip, to read crash reports (developers) |" % f)
        out.append("")

    if has_win:
        out.append("## Windows")
        out.append("")
        first = (win_zip or win_full)[0] if (win_zip or win_full) else "the zip file"
        out.append("1. Download **%s**%s." % (first, (", or **%s**: the full version, with all add-ons included" % win_full[0])
                                              if win_zip and win_full else ""))
        out.append("2. Unpack it completely into a folder of your own, for example C:\\Users\\<your name>\\Quillbinder "
                   "(not into Program Files: the program keeps its settings in its own folder).")
        out.append("3. Start **quillbinder.exe** in it. Windows may warn because the program is not signed "
                   "(SmartScreen: \"More info\", then \"Run anyway\"). A guide at the first start explains the rest.")
        out.append("")
        out.append("| File | What it is |")
        out.append("|------|------------|")
        for f in win_zip:
            out.append("| %s | The program for Windows 11 (64-bit): unpack and start; spelling dictionaries for English and "
                       "German included |" % f)
        for f in win_full:
            out.append("| %s | **The full version** for Windows: the program with every add-on, installed at its first "
                       "start |" % f)
        for f in win_update:
            out.append("| %s | Program update and all add-ons for a Quillbinder for Windows you already have "
                       "(File > Install Update or Add-on) |" % f)
        out.extend(addon_rows(dist, win_addons))
        for f in win_symbols:
            out.append("| %s | The program's debug symbols, to read crash reports (developers) |" % f)
        out.append("")

    if has_android:
        out.append("## Android")
        out.append("")
        out.append("| File | What it is |")
        out.append("|------|------------|")
        for f in apk:
            out.append("| %s | The app for Android tablets (install the APK file) |" % f)
        out.append("")

    if common:
        out.append("## Add-ons for every system")
        out.append("")
        out.append("| File | What it is |")
        out.append("|------|------------|")
        out.extend(addon_rows(dist, common))
        out.append("")

    checks = [f for f in files if f in ("SHA256SUMS.txt", "SHA256SUMS.txt.sig", "THIRD-PARTY-SOURCES.txt")
              or f.endswith("-public.pem")]
    if checks:
        out.append("## Checking the files (not needed to use the program)")
        out.append("")
        out.append("| File | What it is |")
        out.append("|------|------------|")
        for f in checks:
            if f == "SHA256SUMS.txt":
                out.append("| %s | SHA-256 checksums of the files (`sha256sum -c SHA256SUMS.txt`) |" % f)
            elif f == "SHA256SUMS.txt.sig":
                out.append("| %s | The signature of the checksum list, made with the project's key |" % f)
            elif f.endswith("-public.pem"):
                out.append("| %s | The project's public key, to check the signature |" % f)
            else:
                out.append("| %s | Which source files belong to the libraries of this release |" % f)
        out.append("")
    if test:
        # a private test build (tools/test-release.sh): no source code note, no list of changes
        print("\n".join(out))
        return 0
    out.append("## Source code")
    out.append("")
    out.append("Quillbinder's own source code is this repository at the tag of this release (Apache License 2.0). "
               "The source code of every library the program and the add-ons carry, in the versions "
               "used, is in the release [Third-party source code](%s/releases/tag/third-party-sources); "
               "THIRD-PARTY-SOURCES.txt names the files, and Help > Third-Party Licenses shows each library's "
               "license and source file." % home)
    out.append("")
    notes = changes(version)
    if notes:
        out.append("## What is new in %s" % shown)
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
    if len(sys.argv) == 3 and sys.argv[2] == "--test":
        sys.exit(main(sys.argv[1], test=True))
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        sys.exit(2)
    sys.exit(main(sys.argv[1]))
