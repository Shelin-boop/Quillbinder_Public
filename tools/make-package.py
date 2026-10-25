#!/usr/bin/env python3
"""Builds .qbpkg packages (developer tool; users never need this).

  make-package.py addon  <folder> <out.qbpkg>
      <folder>/manifest.json   id, name, version, description, author, whatsNew, minApp
      <folder>/payload/...     themes/, templates/, compile-formats/, plugins/, dictionaries/

  make-package.py core   --binary <program> --version X.Y.Z --runtime <id>
                         [--plugins <dir>] [--whats-new "text" ...] <out.qbpkg>

  make-package.py bundle --id <id> --name <name> --version X.Y [--description "text"]
                         <package.qbpkg> ... <out.qbpkg>

The file list with SHA-256 sums is written into the manifest here.
"""
import argparse, hashlib, json, os, sys, zipfile


def file_entry(path, rel, executable=False):
    data = open(path, "rb").read()
    e = {"path": rel, "sha256": hashlib.sha256(data).hexdigest(), "size": len(data)}
    if executable:
        e["executable"] = True
    return e, data


def write_package(out, manifest, payload, packages=()):
    if os.path.exists(out):
        os.remove(out)
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        z.writestr("manifest.json", json.dumps(manifest, indent=2, ensure_ascii=False))
        for rel, data in payload:
            z.writestr("payload/" + rel, data)
        for name, data in packages:
            z.writestr("packages/" + name, data, compress_type=zipfile.ZIP_STORED)
    print(f"{out}  ({os.path.getsize(out) / 1e6:.2f} MB)")


def cmd_addon(a):
    manifest = json.load(open(os.path.join(a.folder, "manifest.json"), encoding="utf-8"))
    manifest.setdefault("format", 1)
    manifest["kind"] = "addon"
    files, payload = [], []
    root = os.path.join(a.folder, "payload")
    for d, _, names in sorted(os.walk(root)):
        for n in sorted(names):
            p = os.path.join(d, n)
            rel = os.path.relpath(p, root).replace(os.sep, "/")
            e, data = file_entry(p, rel, executable=n.endswith(".so"))
            files.append(e)
            payload.append((rel, data))
    manifest["files"] = files
    write_package(a.out, manifest, payload)


def cmd_core(a):
    files, payload = [], []
    e, data = file_entry(a.binary, os.path.basename(a.binary), executable=True)
    files.append(e)
    payload.append((e["path"], data))
    if a.plugins:
        for n in sorted(os.listdir(a.plugins)):
            p = os.path.join(a.plugins, n)
            if os.path.isfile(p) and (n.endswith(".so") or n.endswith(".json")):
                e2, d2 = file_entry(p, "plugins/" + n, executable=n.endswith(".so"))
                files.append(e2)
                payload.append((e2["path"], d2))
    manifest = {"format": 1, "id": "core", "name": a.name, "version": a.version, "kind": "core",
                "runtime": a.runtime, "binary": os.path.basename(a.binary),
                "description": a.description, "whatsNew": a.whats_new or [], "files": files}
    write_package(a.out, manifest, payload)


def cmd_bundle(a):
    packages = [(os.path.basename(p), open(p, "rb").read()) for p in a.packages]
    manifest = {"format": 1, "id": a.id, "name": a.name, "version": a.version, "kind": "bundle",
                "description": a.description, "whatsNew": a.whats_new or [],
                "packages": [n for n, _ in packages]}
    write_package(a.out, manifest, [], packages)


ap = argparse.ArgumentParser()
sub = ap.add_subparsers(dest="cmd", required=True)
s = sub.add_parser("addon"); s.add_argument("folder"); s.add_argument("out"); s.set_defaults(fn=cmd_addon)
s = sub.add_parser("core")
s.add_argument("--binary", required=True); s.add_argument("--version", required=True)
s.add_argument("--runtime", required=True); s.add_argument("--plugins")
s.add_argument("--name", default="Program update"); s.add_argument("--description", default="")
s.add_argument("--whats-new", action="append"); s.add_argument("out"); s.set_defaults(fn=cmd_core)
s = sub.add_parser("bundle")
s.add_argument("--id", required=True); s.add_argument("--name", required=True)
s.add_argument("--version", required=True); s.add_argument("--description", default="")
s.add_argument("--whats-new", action="append")
s.add_argument("packages", nargs="+"); s.add_argument("out"); s.set_defaults(fn=cmd_bundle)
args = ap.parse_args()
args.fn(args)
