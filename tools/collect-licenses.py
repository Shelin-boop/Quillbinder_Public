#!/usr/bin/env python3
"""Collects the license texts of everything an AppImage carries (0.40).

    tools/collect-licenses.py <AppDir or extracted AppImage> <out dir>     # write
    tools/collect-licenses.py --check <AppDir> <licenses dir>              # every library covered?

Every shared library and Qt plugin in the AppDir is looked up in the Ubuntu
24.04 system it was taken from (dpkg -S on the same path below
/usr/lib/x86_64-linux-gnu), and the package's copyright file
(/usr/share/doc/<package>/copyright) is copied, together with every common
license text it refers to (/usr/share/common-licenses/LGPL-3 ...). Packages
built from the same source (libqt6core6t64, libqt6gui6t64 ... from qt6-base)
are listed once. The result, index.json and texts/*.txt, goes into the
program's resources (Help > Third-Party Licenses) and into the AppImage
(usr/share/doc/quillbinder/licenses).

The program's own files (usr/bin/quillbinder, usr/lib/quillbinder/...) are
not listed here; the parts that are not Ubuntu packages (Breeze icons, the
name lists, the AppImage runtime) come from tools/licenses-extra.json.
"""
import hashlib
import json
import os
import re
import subprocess
import sys

MULTIARCH = "/usr/lib/x86_64-linux-gnu"


def sh(*args):
    return subprocess.run(args, capture_output=True, text=True).stdout


def system_path(appdir, rel):
    """Where the file of the AppDir came from on the build system."""
    parts = rel.split("/")
    if rel.startswith("usr/plugins/"):
        return os.path.join(MULTIARCH, "qt6/plugins", *parts[2:])
    if rel.startswith("usr/lib/"):
        return os.path.join(MULTIARCH, parts[-1])
    return None


def sha(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        h.update(f.read())
    return h.hexdigest()


def own_file(rel):
    return rel.startswith("usr/bin/") or rel.startswith("usr/lib/quillbinder/")


def libraries(appdir):
    out = []
    for root, _dirs, files in os.walk(appdir):
        for n in files:
            if ".so" not in n:
                continue
            full = os.path.join(root, n)
            if os.path.islink(full):
                continue
            rel = os.path.relpath(full, appdir)
            if not own_file(rel):
                out.append(rel)
    return sorted(out)


def package_of(path):
    for line in sh("dpkg", "-S", path).splitlines():
        pkg, _, p = line.partition(": ")
        if p.strip() == path:
            return pkg.split(":")[0]
    # the file may be reached through a symlink (usr-merge, .so.6 -> .so.6.4.2)
    real = os.path.realpath(path)
    if real != path:
        return package_of(real)
    return None


def field(pkg, f):
    return sh("dpkg-query", "-W", "-f", "${%s}" % f, pkg).strip()


def copyright_file(pkg):
    for p in (pkg, field(pkg, "source:Package")):
        c = "/usr/share/doc/%s/copyright" % p
        if os.path.exists(c):
            return c
    return None


COMMON = re.compile(r"/usr/share/common-licenses/([A-Za-z0-9.+_-]+)")


def collect(appdir):
    sources = {}  # source package -> entry
    problems = []
    for rel in libraries(appdir):
        sp = system_path(appdir, rel)
        if not sp or not os.path.exists(sp):
            problems.append("%s: not found on this system (%s)" % (rel, sp))
            continue
        # (not compared byte for byte: linuxdeploy strips the libraries and sets their search path)
        pkg = package_of(sp)
        if not pkg:
            problems.append("%s: no package owns %s" % (rel, sp))
            continue
        src = field(pkg, "source:Package") or pkg
        e = sources.setdefault(src, {"source": src, "version": field(pkg, "source:Version") or field(pkg, "Version"),
                                     "packages": [], "files": [], "copyright": None})
        if pkg not in e["packages"]:
            e["packages"].append(pkg)
        e["files"].append(rel)
        if not e["copyright"]:
            e["copyright"] = copyright_file(pkg)
            if not e["copyright"]:
                problems.append("%s: package %s has no copyright file" % (rel, pkg))
    return sources, problems


# copyright files in the old free-form layout have no License: lines
SUMMARY = {
    "gcc-14": "GPL-3.0-or-later with the GCC Runtime Library Exception (libgomp)",
    "libxcb": "MIT (X11 style)",
    "libxkbcommon": "MIT (X11 style)",
}


def summary(text, src=""):
    """The license names a Debian copyright file states (License: lines)."""
    if src in SUMMARY:
        return SUMMARY[src]
    names = []
    for m in re.finditer(r"^License:\s*(.+)$", text, re.M):
        n = m.group(1).strip()
        if n and n not in names:
            names.append(n)
    return ", ".join(names[:8]) + (" ..." if len(names) > 8 else "")


def write(appdir, out):
    sources, problems = collect(appdir)
    if problems:
        print("\n".join(problems), file=sys.stderr)
        return 1
    os.makedirs(os.path.join(out, "texts"), exist_ok=True)
    for f in os.listdir(os.path.join(out, "texts")):
        os.remove(os.path.join(out, "texts", f))
    index = {"note": "Made by tools/collect-licenses.py from the Ubuntu 24.04 packages the AppImage carries.",
             "components": [], "commonLicenses": []}
    common = set()
    extra_file = os.path.join(os.path.dirname(os.path.abspath(__file__)), "licenses-extra.json")
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    for x in json.load(open(extra_file)):
        name = x["file"]
        with open(os.path.join(root, x["source"]), encoding="utf-8", errors="replace") as f:
            text = f.read()
        with open(os.path.join(out, "texts", name), "w", encoding="utf-8") as f:
            f.write(text)
        common.update(COMMON.findall(text))
        common.update(x.get("common", []))
        index["components"].append({"name": x["name"], "version": x.get("version", ""), "license": x["license"],
                                    "where": x["where"], "text": name, "source": x.get("url", "")})
    for src in sorted(sources, key=str.lower):
        e = sources[src]
        with open(e["copyright"], encoding="utf-8", errors="replace") as f:
            text = f.read()
        name = "ubuntu-%s.txt" % src
        with open(os.path.join(out, "texts", name), "w", encoding="utf-8") as f:
            f.write(text)
        common.update(COMMON.findall(text))
        index["components"].append({
            "name": src, "version": e["version"], "license": summary(text, src),
            "where": ", ".join(sorted(os.path.basename(x) for x in e["files"])),
            "text": name,
            "source": "Ubuntu 24.04 source package %s %s (apt-get source %s=%s; https://launchpad.net/ubuntu/+source/%s/%s)"
                      % (src, e["version"], src, e["version"], src, e["version"])})
    for c in sorted(common):
        path = "/usr/share/common-licenses/" + c
        if os.path.isdir(path) or not os.path.exists(path):
            continue
        name = "common-%s.txt" % c
        with open(path, encoding="utf-8", errors="replace") as f, \
                open(os.path.join(out, "texts", name), "w", encoding="utf-8") as g:
            g.write(f.read())
        index["commonLicenses"].append({"name": c, "text": name})
    with open(os.path.join(out, "index.json"), "w", encoding="utf-8") as f:
        json.dump(index, f, indent=1, ensure_ascii=False)
        f.write("\n")
    print("%d components, %d common license texts -> %s" % (len(index["components"]), len(index["commonLicenses"]), out))
    return 0


def check(appdir, licdir):
    index = json.load(open(os.path.join(licdir, "index.json")))
    covered = set()
    for c in index["components"]:
        covered.update(x.strip() for x in c["where"].split(","))
    missing = [rel for rel in libraries(appdir) if os.path.basename(rel) not in covered]
    for c in index["components"] + index["commonLicenses"]:
        if not os.path.exists(os.path.join(licdir, "texts", c["text"])):
            missing.append("license text " + c["text"])
    if missing:
        print("not covered by %s:\n  %s" % (licdir, "\n  ".join(missing)), file=sys.stderr)
        return 1
    print("every library of the AppDir has its license text")
    return 0


if __name__ == "__main__":
    if len(sys.argv) == 4 and sys.argv[1] == "--check":
        sys.exit(check(sys.argv[2], sys.argv[3]))
    if len(sys.argv) != 3:
        print(__doc__, file=sys.stderr)
        sys.exit(2)
    sys.exit(write(sys.argv[1], sys.argv[2]))
