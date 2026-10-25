#!/usr/bin/env python3
"""Collects the source code of every third-party part the AppImage and the add-ons carry (0.50).

    tools/collect-sources.py <out dir>                 # download and pack everything
    tools/collect-sources.py --list                    # the file names only (nothing downloaded)
    tools/collect-sources.py --name ffmpeg             # the source file of the add-ons' FFmpeg
    tools/collect-sources.py --name ubuntu <file>      # "<source package> <version> <source file>"
                                                       # of a library file of this system

Each part becomes one file <name>.src.tar in <out dir>; the release workflow puts them into the
release "Third-party source code" of the project page (APP_HOMEPAGE in CMakeLists.txt), where they
stay for every version that used them. Help > Third-Party Licenses and the license notes of the
add-ons name these files.

  - every Ubuntu 24.04 source package of resources/licenses/index.json (the AppImage's libraries),
    in the version this system has (tools/collect-licenses.py --refresh keeps the list in step):
    the .dsc, the original upstream archive and Ubuntu's changes, as "apt-get source" fetches them
    (needs deb-src lines in the apt sources);
  - the Ubuntu source packages of the libraries the add-ons bring (ADDON_LIBRARIES below);
  - FFmpeg 6.1.1 of the media add-ons: the original archive plus tools/build-ffmpeg.sh,
    tools/get-ffmpeg.sh and tools/ffmpeg-patches (the file name carries a hash of these, so a
    changed build gets a new file);
  - the AppImage runtime (type2-runtime) at the commit named in index.json, with the archives of
    the libraries its build links in (as its scripts/common/install-dependencies.sh downloads them).

A SOURCES.txt in <out dir> lists the files and what each holds.
"""
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MULTIARCH = "/usr/lib/x86_64-linux-gnu"
FFMPEG_ORIG = "ffmpeg_6.1.1.orig.tar.xz"
FFMPEG_URL = "http://archive.ubuntu.com/ubuntu/pool/universe/f/ffmpeg/" + FFMPEG_ORIG
FFMPEG_SHA256 = "8684f4b00f94b85461884c3719382f1261f0d9eb3d59640a1f4ac0873616f968"
RUNTIME_REPO = "https://github.com/AppImage/type2-runtime"

# library files of this system that add-ons carry (addons/*/stage.sh)
ADDON_LIBRARIES = [
    MULTIARCH + "/libQt6Multimedia.so.6",                              # Media Playback
    MULTIARCH + "/qt6/plugins/multimedia/libffmpegmediaplugin.so",
    MULTIARCH + "/qt6/plugins/multimedia/libgstreamermediaplugin.so",
    MULTIARCH + "/libva.so.2",
    MULTIARCH + "/qt6/plugins/tls/libqopensslbackend.so",              # Web Page Import
]


def sh(*args, cwd=None, check=False):
    r = subprocess.run(args, capture_output=True, text=True, cwd=cwd)
    if check and r.returncode != 0:
        raise RuntimeError("%s failed:\n%s%s" % (" ".join(args), r.stdout, r.stderr))
    return r.stdout


def safe_name(s):
    return re.sub(r"[^A-Za-z0-9._-]", "_", s)


def ubuntu_source_file(src, version):
    return "%s_%s.src.tar" % (safe_name(src), safe_name(version))


def ffmpeg_source_file():
    h = hashlib.sha256()
    files = [os.path.join(ROOT, "tools", "build-ffmpeg.sh")]
    pdir = os.path.join(ROOT, "tools", "ffmpeg-patches")
    files += [os.path.join(pdir, n) for n in sorted(os.listdir(pdir))]
    for f in files:
        h.update(os.path.basename(f).encode() + b"\0")
        with open(f, "rb") as fh:
            h.update(fh.read())
    return "ffmpeg-6.1.1-quillbinder-%s.src.tar" % h.hexdigest()[:8], files


def package_of(path):
    for p in (path, os.path.realpath(path)):
        for line in sh("dpkg", "-S", p).splitlines():
            pkg, _, f = line.partition(": ")
            if f.strip() == p:
                return pkg.split(":")[0]
    return None


def field(pkg, f):
    return sh("dpkg-query", "-W", "-f", "${%s}" % f, pkg).strip()


def ubuntu_of_file(path):
    pkg = package_of(path)
    if not pkg:
        return None
    src = field(pkg, "source:Package") or pkg
    ver = field(pkg, "source:Version") or field(pkg, "Version")
    return src, ver


def wanted():
    """[(file name, kind, details)] of everything to collect."""
    index = json.load(open(os.path.join(ROOT, "resources", "licenses", "index.json")))
    out, seen = [], set()
    for c in index["components"]:
        if c.get("package"):
            key = (c["package"], c["version"])
            if key not in seen:
                seen.add(key)
                out.append((c["sourceFile"], "ubuntu", {"package": c["package"], "version": c["version"],
                                                       "for": "the AppImage: " + c["where"]}))
    for lib in ADDON_LIBRARIES:
        if not os.path.exists(lib):
            print("note: %s is not installed; its source is left out" % lib, file=sys.stderr)
            continue
        src, ver = ubuntu_of_file(lib)
        if (src, ver) not in seen:
            seen.add((src, ver))
            out.append((ubuntu_source_file(src, ver), "ubuntu", {"package": src, "version": ver,
                                                                 "for": "add-ons: " + os.path.basename(lib)}))
        else:
            for f in out:
                if f[2].get("package") == src and f[2].get("version") == ver:
                    f[2]["for"] += ", add-ons: " + os.path.basename(lib)
    name, _files = ffmpeg_source_file()
    out.append((name, "ffmpeg", {"for": "the add-ons Media Playback and Extra Video and Audio Formats"}))
    rt = index.get("runtime")
    if rt:
        out.append((rt["sourceFile"], "runtime", {"commit": rt["commit"], "for": "the AppImage runtime"}))
    else:
        print("note: index.json names no runtime (tools/collect-licenses.py --refresh <dir> <runtime>)",
              file=sys.stderr)
    return out


def pack(out_file, folder):
    tmp = out_file + ".part"
    with tarfile.open(tmp, "w") as t:
        for n in sorted(os.listdir(folder)):
            t.add(os.path.join(folder, n), arcname=os.path.join(os.path.basename(out_file)[:-len(".src.tar")], n))
    os.replace(tmp, out_file)


def get_ubuntu(d, work):
    sh("apt-get", "source", "--download-only", "-qq", "%s=%s" % (d["package"], d["version"]), cwd=work, check=True)
    if not any(n.endswith(".dsc") for n in os.listdir(work)):
        raise RuntimeError("apt-get source gave no .dsc for %s %s" % (d["package"], d["version"]))


def get_ffmpeg(work):
    cached = os.path.join(ROOT, "build", "ffmpeg-src", FFMPEG_ORIG)
    dst = os.path.join(work, FFMPEG_ORIG)
    if os.path.exists(cached):
        shutil.copy(cached, dst)
    else:
        sh("curl", "-fsSL", "-o", dst, FFMPEG_URL, check=True)
    with open(dst, "rb") as f:
        if hashlib.sha256(f.read()).hexdigest() != FFMPEG_SHA256:
            raise RuntimeError("FFmpeg archive: wrong checksum")
    _name, files = ffmpeg_source_file()
    os.makedirs(os.path.join(work, "quillbinder-build", "ffmpeg-patches"))
    for f in files + [os.path.join(ROOT, "tools", "get-ffmpeg.sh")]:
        sub = "ffmpeg-patches" if os.path.basename(os.path.dirname(f)) == "ffmpeg-patches" else ""
        shutil.copy(f, os.path.join(work, "quillbinder-build", sub, os.path.basename(f)))
    with open(os.path.join(work, "README.txt"), "w") as f:
        f.write("FFmpeg 6.1.1 as built for the Quillbinder add-ons \"Media Playback\" (set \"base\") and\n"
                "\"Extra Video and Audio Formats\" (set \"full\").\n\n"
                "%s  the unchanged FFmpeg 6.1.1 source (Ubuntu 24.04's copy of ffmpeg.org's release)\n"
                "quillbinder-build/                 how it was built: get-ffmpeg.sh unpacks the archive and calls\n"
                "                                   build-ffmpeg.sh, which applies ffmpeg-patches/ and configures\n"
                "                                   each set (the options are in that script)\n" % FFMPEG_ORIG)


def get_runtime(d, work):
    repo = os.path.join(work, "..", "type2-runtime-git")
    if not os.path.exists(repo):
        sh("git", "clone", "-q", "--filter=blob:none", RUNTIME_REPO, repo, check=True)
    full = sh("git", "rev-parse", d["commit"] + "^{commit}", cwd=repo, check=True).strip()
    sh("git", "archive", "--format=tar.gz", "--prefix=type2-runtime-%s/" % d["commit"],
       "-o", os.path.join(work, "type2-runtime-%s.tar.gz" % d["commit"]), full, cwd=repo, check=True)
    script = sh("git", "show", full + ":scripts/common/install-dependencies.sh", cwd=repo, check=True)
    urls = re.findall(r"wget\s+\"?(https://[^\s\"]+)", script)
    sums = dict((n, h) for h, n in re.findall(r"echo\s+\"([0-9a-f]{64})\s+([^\"]+)\"\s*\|\s*sha256sum", script))
    os.makedirs(os.path.join(work, "deps"))
    notes = []
    for u in urls:
        name = u.rsplit("/", 1)[1]
        dst = os.path.join(work, "deps", name)
        r = subprocess.run(["curl", "-fsSL", "-o", dst, u], capture_output=True, text=True)
        m = re.match(r"https://github\.com/([^/]+)/([^/]+)/archive/(.+)\.tar\.gz$", u)
        if r.returncode != 0 and m:
            # GitHub's archive service unreachable: the same tag through git
            owner, repo_name, ref = m.groups()
            clone = os.path.join(work, "..", "git-" + repo_name)
            sh("git", "clone", "-q", "--depth", "1", "--branch", ref,
               "https://github.com/%s/%s" % (owner, repo_name), clone, check=True)
            sh("git", "archive", "--format=tar.gz", "--prefix=%s-%s/" % (repo_name, ref), "-o", dst, "HEAD",
               cwd=clone, check=True)
            notes.append("%s was made with git archive from the tag %s (%s could not be fetched)" % (name, ref, u))
            continue
        if r.returncode != 0:
            raise RuntimeError("curl %s failed: %s" % (u, r.stderr))
        if name in sums:
            with open(dst, "rb") as f:
                if hashlib.sha256(f.read()).hexdigest() != sums[name]:
                    raise RuntimeError("%s: wrong checksum" % name)
    with open(os.path.join(work, "README.txt"), "w") as f:
        f.write("The AppImage runtime (the start code at the beginning of the Quillbinder AppImage file):\n"
                "type2-runtime at commit %s (%s/commit/%s), MIT license.\n\n"
                "deps/ holds the archives its build downloads and links in statically (see\n"
                "scripts/common/install-dependencies.sh inside the type2-runtime archive), among them\n"
                "libfuse (LGPL 2.1) and squashfuse (BSD). The other parts it links in (musl, zstd, zlib,\n"
                "mimalloc) come from Alpine Linux packages of the release named in its build scripts.\n"
                "Build: scripts/chroot/build.sh or scripts/docker/build-with-docker.sh of type2-runtime.\n"
                % (full, RUNTIME_REPO, full))
        f.write("\nFiles in deps/:\n" + "".join("  %s  (%s)\n" % (u.rsplit("/", 1)[1], u) for u in urls))
        for n in notes:
            f.write("Note: %s.\n" % n)


def main(argv):
    if len(argv) == 2 and argv[1] == "--list":
        for name, kind, d in wanted():
            print(name)
        return 0
    if len(argv) == 3 and argv[1] == "--name" and argv[2] == "ffmpeg":
        print(ffmpeg_source_file()[0])
        return 0
    if len(argv) == 4 and argv[1] == "--name" and argv[2] == "ubuntu":
        r = ubuntu_of_file(argv[3])
        if not r:
            print("no package owns %s" % argv[3], file=sys.stderr)
            return 1
        print(r[0], r[1], ubuntu_source_file(*r))
        return 0
    if len(argv) != 2 or argv[1].startswith("-"):
        print(__doc__, file=sys.stderr)
        return 2
    out = os.path.abspath(argv[1])
    os.makedirs(out, exist_ok=True)
    items = wanted()
    lines = []
    tmp_root = tempfile.mkdtemp(prefix="qb-sources-")
    try:
        for name, kind, d in items:
            target = os.path.join(out, name)
            if kind == "ubuntu":
                what = "Ubuntu 24.04 source package %s %s (for %s)" % (d["package"], d["version"], d["for"])
            elif kind == "ffmpeg":
                what = "FFmpeg 6.1.1 and the Quillbinder build of it (for %s)" % d["for"]
            else:
                what = "AppImage runtime type2-runtime %s with its libraries (for %s)" % (d["commit"], d["for"])
            lines.append("%s\n    %s\n" % (name, what))
            if os.path.exists(target):
                print("have %s" % name)
                continue
            work = os.path.join(tmp_root, name)
            os.makedirs(work)
            print("collecting %s" % name, flush=True)
            if kind == "ubuntu":
                get_ubuntu(d, work)
            elif kind == "ffmpeg":
                get_ffmpeg(work)
            else:
                get_runtime(d, work)
            pack(target, work)
            shutil.rmtree(work)
    finally:
        shutil.rmtree(tmp_root, ignore_errors=True)
    with open(os.path.join(out, "SOURCES.txt"), "w") as f:
        f.write("Source code of the third-party parts of Quillbinder's AppImage and add-ons\n"
                "=========================================================================\n\n"
                "Each file is a tar archive (tar xf <file>); the license texts are in the program under\n"
                "Help > Third-Party Licenses and in the add-ons' licenses folders.\n\n")
        f.write("\n".join(lines))
    print("%d source files in %s" % (len(items), out))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
