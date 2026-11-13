#!/usr/bin/env python3
"""Writes the download page of a release (2026-11-12): one button per system for the full version (the program with
all add-ons), the steps to start it, and links to all files of the release. Since 0.90.2 (the writer's wish) each
system also lists its other downloads: the program alone (the small AppImage), the program update and the audio and
video add-ons. The release workflow publishes it on the
public repository's GitHub Pages (job download-page of .github/workflows/release.yml).

    tools/download-page.py <folder with the release's files (their names are enough)> <tag> <out dir>

A system without a file in the release shows "comes later". The visitor's own system comes first (the page's
script reads the browser's user agent; without script the order is Windows, Linux, Android).
"""
import html
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def cmake_var(name):
    with open(os.path.join(ROOT, "CMakeLists.txt"), encoding="utf-8") as f:
        m = re.search(r'^set\(%s\s+"([^"]*)"\)' % name, f.read(), re.M)
    return m.group(1) if m else ""


def pick(files):
    """The full version of each system ("" when the release has none)."""
    win = lambda f: "windows" in f.lower()
    linux = next((f for f in files if f.endswith(".AppImage") and "-full-" in f), "")
    windows = next((f for f in files if f.endswith(".zip") and win(f) and "-full-" in f), "")
    android = next((f for f in files if f.endswith(".apk")), "")
    return {"linux": linux, "windows": windows, "android": android}


def others(files):
    """The other downloads of each system (0.90.2): [(file, what it is, how to use it)], only those the release has."""
    win = lambda f: "windows" in f.lower()
    first = lambda test: next((f for f in files if test(f)), "")
    linux = [
        (first(lambda f: f.endswith(".AppImage") and "-full-" not in f),
         "The program alone (the small AppImage)", "without the add-ons; they can be installed later, one by one"),
        (first(lambda f: f.endswith("-update.qbpkg") and not win(f)),
         "Program update", "for a Quillbinder you already have (File &gt; Install Update or Add-on)"),
        (first(lambda f: f == "media-all-formats.qbpkg"),
         "Audio and video add-ons", "sound and video in all common formats (File &gt; Install Update or Add-on)"),
    ]
    windows = [
        (first(lambda f: f.endswith(".zip") and win(f) and "-full-" not in f and "symbols" not in f),
         "The program alone", "without the add-ons; they can be installed later, one by one"),
        (first(lambda f: f.endswith("-update.qbpkg") and win(f)),
         "Program update", "for a Quillbinder you already have (File &gt; Install Update or Add-on)"),
        (first(lambda f: f.startswith("media-all-formats") and win(f) and f.endswith(".qbpkg")),
         "Audio and video add-ons", "sound and video in all common formats (File &gt; Install Update or Add-on)"),
    ]
    keep = lambda rows: [r for r in rows if r[0]]
    return {"linux": keep(linux), "windows": keep(windows), "android": []}


def page(files, tag):
    home = cmake_var("APP_HOMEPAGE") or "https://github.com/Shelin-boop/Quillbinder_Public"
    shown = tag[1:].replace("-", " ") if tag.startswith("v") else tag
    full = pick(files)
    more = others(files)
    url = lambda f: "%s/releases/download/%s/%s" % (home, tag, f)
    systems = [
        ("windows", "Windows 11", "64-bit",
         ["Unpack the zip file completely into a folder of your own, for example "
          "<code>C:\\Users\\&lt;your name&gt;\\Quillbinder</code> (not into Program Files).",
          "Start <b>quillbinder.exe</b> in it. Windows may warn because the program is not signed: "
          "&ldquo;More info&rdquo;, then &ldquo;Run anyway&rdquo;.",
          "A guide at the first start explains where the program keeps its files."]),
        ("linux", "Linux", "x86_64, an AppImage",
         ["Make the file executable: right-click &gt; Properties &gt; Permissions &gt; <i>Is executable</i>.",
          "Double-click it. The add-ons are installed at the first start.",
          "Help &gt; Add to Application Menu puts it into the menu of the desktop."]),
        ("android", "Android", "tablets",
         ["Download the APK file on the tablet and open it to install."]),
    ]
    cards = []
    for key, name, detail, steps in systems:
        f = full[key]
        if f:
            body = ('<a class="button" href="%s">Download for %s</a>\n'
                    '      <p class="file">%s</p>\n      <ol>%s</ol>'
                    % (html.escape(url(f)), html.escape(name), html.escape(f),
                       "".join("<li>%s</li>" % s for s in steps)))
        else:
            body = '<p class="later">Comes later.</p>'
        if more[key]:
            rows = "".join('\n        <li><a href="%s">%s</a>: %s <span class="file">%s</span></li>'
                           % (html.escape(url(of)), what, how, html.escape(of)) for of, what, how in more[key])
            body += ('\n      <h3>Other downloads</h3>\n      <ul class="others">%s\n      </ul>' % rows)
        cards.append('    <section class="system" data-system="%s">\n      <h2>%s <span>%s</span></h2>\n      %s\n    </section>'
                     % (key, html.escape(name), html.escape(detail), body))
    return """<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Quillbinder Download</title>
<style>
  :root { --bg: #fbfaf7; --fg: #1d1d1b; --muted: #5d5b55; --card: #ffffff; --line: #dedbd2; --accent: #2f5d8a; --accent-fg: #ffffff; }
  @media (prefers-color-scheme: dark) {
    :root { --bg: #1b1c1e; --fg: #ecebe7; --muted: #a9a6a0; --card: #242528; --line: #3a3b3f; --accent: #7fb0e0; --accent-fg: #10161c; }
  }
  * { box-sizing: border-box; }
  body { margin: 0; background: var(--bg); color: var(--fg); font: 17px/1.55 system-ui, -apple-system, "Segoe UI", sans-serif; }
  main { max-width: 760px; margin: 0 auto; padding: 32px 16px 48px; }
  h1 { font-size: 30px; margin: 0 0 4px; }
  .lead { color: var(--muted); margin: 0 0 28px; }
  .system { background: var(--card); border: 1px solid var(--line); border-radius: 12px; padding: 20px 22px; margin: 0 0 16px; }
  .system h2 { font-size: 21px; margin: 0 0 12px; }
  .system h2 span { font-size: 15px; font-weight: normal; color: var(--muted); margin-left: 6px; }
  .button { display: inline-block; background: var(--accent); color: var(--accent-fg); text-decoration: none; font-weight: 600;
            padding: 11px 20px; border-radius: 8px; }
  .button:focus-visible { outline: 3px solid var(--fg); outline-offset: 2px; }
  .file { color: var(--muted); font-size: 14px; margin: 8px 0 0; word-break: break-all; }
  ol { margin: 12px 0 0; padding-left: 22px; }
  h3 { font-size: 16px; margin: 18px 0 6px; }
  .others { margin: 0; padding-left: 22px; }
  .others li { margin: 4px 0; }
  .others a { color: var(--accent); font-weight: 600; }
  .others .file { display: block; margin: 0; }
  .later { color: var(--muted); margin: 0; }
  .more { margin-top: 28px; }
  .more a { color: var(--accent); }
  .note { color: var(--muted); font-size: 14px; margin-top: 28px; }
  code { font-size: 15px; }
</style>
</head>
<body>
<main>
  <h1>Quillbinder %(shown)s</h1>
  <p class="lead">A writing studio for long texts. The full version: the program with all add-ons, one download.</p>
  <div id="systems">
%(cards)s
  </div>
  <p class="more"><a href="%(release)s">All files of this version</a> (single add-ons, the symbols for crash reports,
  checksums) &middot; <a href="%(home)s/releases">Earlier versions</a> &middot;
  <a href="%(home)s">Source code</a></p>
  <p class="note">Quillbinder is an independent program. It is not made by or connected with Literature &amp; Latte,
  the maker of Scrivener; it reads and writes Scrivener 3 projects. This is %(article)s %(label)s version: keep backups of
  your projects and a copy of your manuscripts outside the program.</p>
</main>
<script>
  // the visitor's own system first
  (function () {
    var ua = navigator.userAgent || "";
    var own = /Android/i.test(ua) ? "android" : /Windows/i.test(ua) ? "windows" : /Linux|X11/i.test(ua) ? "linux" : "";
    var box = document.getElementById("systems");
    var card = own && box.querySelector('[data-system="' + own + '"]');
    if (card) box.insertBefore(card, box.firstChild);
  })();
</script>
</body>
</html>
""" % {"shown": html.escape(shown), "cards": "\n".join(cards), "release": html.escape("%s/releases/tag/%s" % (home, tag)),
       "home": html.escape(home), "label": html.escape(cmake_var("APP_VERSION_LABEL") or "test"),
       "article": "an" if (cmake_var("APP_VERSION_LABEL")[:1].lower() in "aeiou" and cmake_var("APP_VERSION_LABEL")) else "a"}


def main(argv):
    if len(argv) != 4:
        print(__doc__, file=sys.stderr)
        return 2
    files = sorted(os.listdir(argv[1]))
    os.makedirs(argv[3], exist_ok=True)
    with open(os.path.join(argv[3], "index.html"), "w", encoding="utf-8") as f:
        f.write(page(files, argv[2]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
