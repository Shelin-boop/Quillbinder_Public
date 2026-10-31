#!/usr/bin/env python3
"""Checks a source tree before it is published (0.60): names, texts, files and licenses.

    python3 -I tools/legal-check.py [--public] [tree]     (tree: default the repository)

--public adds the checks for the public copy (no private files or private blocks left).
Prints one line per check, "ok" or "FAIL" with the places, and exits with 1 when a check fails.
tools/publish-public.sh runs it with --public on the exported tree; qb_v060_tests runs it on
the source. It guards against known kinds of mistakes; it is not a legal review (the method:
docs/publishing.md).
"""
import hashlib
import os
import re
import sys

TEXT_EXT = {".cpp", ".h", ".hpp", ".c", ".in", ".md", ".txt", ".json", ".sh", ".py", ".cmake", ".qrc",
            ".yml", ".yaml", ".xml", ".desktop", ".html", ".css", ".js", ".ui", ".rtf", ".fountain",
            ".fdx", ".fodt", ".patch", ".theme", ""}
BINARY_EXT = {".png", ".jpg", ".jpeg", ".gif", ".webp", ".bmp", ".ico", ".ttf", ".otf", ".woff",
              ".woff2", ".mp3", ".ogg", ".opus", ".flac", ".wav", ".mp4", ".mkv", ".webm", ".mov",
              ".avi", ".doc", ".docx", ".odt", ".pdf", ".zip", ".epub"}
SCRIVENER_DATA = (".scriv", ".scrivx", ".scap", ".scrivtemplate", ".scrivformat", ".scrivtheme")
# names of files that may name the words they check for
SELF = {"tools/legal-check.py"}

failures = 0


def report(ok, name, places=()):
    global failures
    if ok:
        print("ok    %s" % name)
    else:
        failures += 1
        print("FAIL  %s" % name)
        for p in list(places)[:20]:
            print("        %s" % p)


def walk(tree):
    for d, dirs, files in os.walk(tree):
        dirs[:] = [x for x in dirs if x not in (".git", "build") and not x.startswith("build-")
                   and x != "dist"]
        for f in files:
            full = os.path.join(d, f)
            yield os.path.relpath(full, tree).replace(os.sep, "/"), full


def read_text(full):
    try:
        with open(full, "rb") as fh:
            b = fh.read()
    except OSError:
        return None
    if b"\0" in b[:4096]:
        return None
    return b.decode("utf-8", "replace")


def main(argv):
    public = "--public" in argv
    args = [a for a in argv if a != "--public"]
    tree = os.path.abspath(args[0]) if args else os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    files = dict(walk(tree))
    texts = {}
    for rel, full in files.items():
        if os.path.splitext(rel)[1].lower() in TEXT_EXT:
            t = read_text(full)
            if t is not None:
                texts[rel] = t

    def text(rel):
        return texts.get(rel, "")

    # 1. Quillbinder's own license: 0BSD, no person named
    lic = text("LICENSE")
    report("Permission to use, copy, modify, and/or distribute this software for any" in lic
           and "Quillbinder authors" in lic and "MIT" not in lic,
           "LICENSE is the Zero-Clause BSD license and names no person")

    # 2. the program says it is independent of Literature & Latte where it is presented
    places = ["README.md", "packaging/quillbinder.metainfo.xml", "src/ui/MainWindow.cpp", "tools/release-notes.py"]
    def plain(t):  # C++ string pieces joined, white space folded
        return re.sub(r"\s+", " ", re.sub(r'"\s*\n\s*"', "", t).replace("&amp;", "&"))
    missing = [p for p in places if "not made by or connected with Literature" not in plain(text(p))]
    report(not missing, "README, AppStream text, About and the release text say it is not made by or "
                        "connected with Literature & Latte", missing)

    # 3. no wording that suggests a copy of Scrivener, a link with its maker, or a replacement
    bad_phrases = [
        r"Scrivener[- ]?(clone|replacement|alternative|substitute)",
        r"(clone|replacement|substitute|imitation) (of|for) Scrivener",
        r"alternative to Scrivener",
        r"Scrivener for Linux",
        r"Scrivener[- ]like",
        r"\blike Scrivener\b",
        r"\bofficial(ly)?\b[^.\n]{0,30}\b(Scrivener|Scapple|Literature)",
        r"(endorsed|approved|certified|licensed|authori[sz]ed) by Literature",
        r"\bScrivenings\b",
        r"Linguistic Focus",
        r"Imaginary Tool Tutorial",
    ]
    rx = re.compile("|".join("(?:%s)" % p for p in bad_phrases), re.I)
    allowed_scrivenings = {"CLAUDE.md", "tests/v040_tests.cpp"}  # the rule itself and its check
    hits = []
    for rel, t in texts.items():
        if rel in SELF or rel == "docs/status.md":
            continue
        for m in rx.finditer(t):
            if m.group(0).lower() == "scrivenings" and (rel in allowed_scrivenings or m.group(0)[0] == "s"):
                continue  # the settings keys of older versions ("scriveningsTitles") are read
            line = t.count("\n", 0, m.start()) + 1
            hits.append("%s:%d: %s" % (rel, line, m.group(0)))
    report(not hits, "no wording that suggests a copy of, a replacement for, or a link with Scrivener "
                     "or Literature & Latte", hits)

    # 4. nothing of the old tutorial; the pen name of earlier versions nowhere (looked for by the
    #    SHA-256 of three words, an initial in the middle, so that this file does not name it)
    old_command = "Interactive " "Tutorial"  # in two pieces, so that this file does not hold the name
    tut = [rel for rel, t in texts.items() if old_command in t
           and rel not in ("docs/status.md", "tests/v040_tests.cpp") and rel not in SELF]
    tut += [p for p in ("docs/tutorial.txt", "addons/tutorial") if os.path.exists(os.path.join(tree, p))]
    report(not tut, "nothing of the old tutorial is left", tut)
    pen = bytes.fromhex("7fdbda2a60c9b936f620bb2eb0524ea26a80de567309a56231848816ebc59a7f")
    named = []
    for rel, t in texts.items():
        words = []
        for w in t.split():
            w = re.sub(r"^[^A-Za-z]+", "", w)
            if len(w) > 2:
                w = re.sub(r"[^A-Za-z]+$", "", w)
            elif len(w) == 2 and not w[1].isalpha() and w[1] != ".":
                w = w[:1]
            words.append(w)
        for i in range(len(words) - 2):
            if len(words[i + 1]) == 2 and words[i + 1].endswith(".") and \
                    hashlib.sha256(("%s %s %s" % tuple(words[i:i + 3])).encode()).digest() == pen:
                named.append(rel)
                break
    report(not named, "the pen name of earlier versions is in no file", named)

    # 5. no project, board or template files of Scrivener or Scapple, no pictures named after them
    data = [rel for rel in files if any(seg.lower().endswith(SCRIVENER_DATA) for seg in rel.split("/"))]
    data += [rel for rel in files if os.path.splitext(rel)[1].lower() in BINARY_EXT
             and re.search(r"scrivener|scapple|literature", rel, re.I)]
    report(not data, "no Scrivener or Scapple files and no pictures named after them", data)

    # 6. every picture, sound, video, font and document file says where it comes from: a README,
    #    LICENSE or COPYING file in its folder or a folder above it (not the top folder); project
    #    templates of add-ons are made from the outlines beside them
    def has_note(rel):
        d = os.path.dirname(rel)
        while d:
            for f in os.listdir(os.path.join(tree, d)):
                if re.match(r"(README|LICENSE|COPYING|LICENCE)", f, re.I):
                    return True
            d = os.path.dirname(d)
        return False

    orphans = []
    for rel in files:
        if os.path.splitext(rel)[1].lower() not in BINARY_EXT:
            continue
        m = re.match(r"addons/([^/]+)/payload/templates/[^/]+\.zip$", rel)
        if m:
            base = os.path.join(tree, "addons", m.group(1))
            if os.path.exists(os.path.join(base, "outline.json")) or os.path.isdir(os.path.join(base, "outlines")):
                continue
        if not has_note(rel):
            orphans.append(rel)
    report(not orphans, "every picture, sound, video, font and document file has a note on where it comes from",
           orphans)

    # 7. fonts: a license file beside every font
    fonts = [rel for rel in files if os.path.splitext(rel)[1].lower() in (".ttf", ".otf", ".woff", ".woff2")]
    nolic = [rel for rel in fonts if not any(re.match(r"(LICENSE|OFL|COPYING)", f, re.I)
                                             for f in os.listdir(os.path.dirname(os.path.join(tree, rel))))]
    report(not nolic, "every font has its license file beside it (%d fonts)" % len(fonts), nolic)

    # 8. FFmpeg: built under the LGPL (no GPL or non-free parts), and named with its license
    ff = text("tools/build-ffmpeg.sh")
    opts = re.findall(r"--enable-(gpl|nonfree|version3|libx264|libx265|libfdk[-_]aac)\b", ff)
    about = text("src/ui/MainWindow.cpp")
    readmes = [text("addons/media-playback/stage.sh"), text("addons/media-extra-formats/stage.sh")]
    report(bool(ff) and not opts and "FFmpeg" in about and "LGPL" in about and all("LGPL" in r for r in readmes),
           "FFmpeg is built without GPL or non-free parts; About and the add-ons name it with its LGPL", opts)

    # 9. THIRD-PARTY.md names Quillbinder's license and the main third-party parts
    third = text("THIRD-PARTY.md")
    need = ["0BSD", "Qt", "FFmpeg", "Breeze", "third-party-sources"]
    report(all(n in third for n in need), "THIRD-PARTY.md names 0BSD, Qt, FFmpeg, Breeze and the source release",
           [n for n in need if n not in third])

    if public:
        # 10. nothing private in the public copy
        private = [rel for rel in files if rel == "docs/status.md" or rel.startswith("claude/")
                   or rel == "tools/put-asset.sh"]
        marker = re.compile(r"^[ \t]*<!-- private:(begin|end) -->", re.M)
        private += [rel for rel, t in texts.items() if marker.search(t)]
        report(not private, "no private files or private blocks in the public copy", private)

    print("\n%d failure(s)" % failures)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
