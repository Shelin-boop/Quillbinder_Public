#!/usr/bin/env python3
"""Builds .qbpkg packages (developer tool; users never need this).

  make-package.py addon  [--platform windows-x64] <folder> <out.qbpkg>
      <folder>/manifest.json   id, name, version, description, author, whatsNew, minApp
      --platform: the system the add-on's libraries were built for (Windows add-ons, port W2;
      Linux add-ons name none)
      <folder>/payload/...     themes/, templates/, compile-formats/, plugins/, dictionaries/

  make-package.py core   --binary <program> --version X.Y.Z --runtime <id>
                         [--plugins <dir>] [--whats-new "text" ...] <out.qbpkg>

  make-package.py bundle --id <id> --name <name> --version X.Y [--description "text"]
                         <package.qbpkg> ... <out.qbpkg>

  make-package.py sign <package.qbpkg> ...
      signs finished packages in place (0.70.0, audit A S-03: the release workflow builds without the key
      and signs in a job of its own that runs no third-party code); a bundle's parts are signed too

The file list with SHA-256 sums is written into the manifest here.

Signatures (0.65.0): with QB_SIGNING_KEY_FILE=<Ed25519 private key, PEM> every package gets manifest.sig,
the key's signature of manifest.json (made with openssl). The release workflow sets it from the
secret QB_SIGNING_KEY; without it packages are made unsigned (the program then warns, and refuses
unsigned program updates). A bundle's parts carry their own signatures.
"""
import argparse, base64, hashlib, json, os, subprocess, sys, tempfile, zipfile


def file_entry(path, rel, executable=False):
    data = open(path, "rb").read()
    e = {"path": rel, "sha256": hashlib.sha256(data).hexdigest(), "size": len(data)}
    if executable:
        e["executable"] = True
    return e, data


def signature(data):
    """manifest.sig for these bytes, or None without QB_SIGNING_KEY_FILE"""
    key = os.environ.get("QB_SIGNING_KEY_FILE")
    if not key:
        return None
    with tempfile.TemporaryDirectory() as t:
        msg, sig = os.path.join(t, "manifest.json"), os.path.join(t, "manifest.sig")
        with open(msg, "wb") as f:
            f.write(data)
        subprocess.run(["openssl", "pkeyutl", "-sign", "-rawin", "-inkey", key, "-in", msg, "-out", sig], check=True)
        raw = open(sig, "rb").read()
        pub = subprocess.run(["openssl", "pkey", "-in", key, "-pubout", "-outform", "DER"], check=True,
                             capture_output=True).stdout[-32:]
    if len(raw) != 64 or len(pub) != 32:
        sys.exit("make-package.py: the key in QB_SIGNING_KEY_FILE is not an Ed25519 key")
    return json.dumps({"algorithm": "ed25519", "key": hashlib.sha256(pub).hexdigest()[:16],
                       "signature": base64.b64encode(raw).decode(), "signs": "manifest.json"}, indent=2)


def write_package(out, manifest, payload, packages=()):
    if os.path.exists(out):
        os.remove(out)
    mdata = json.dumps(manifest, indent=2, ensure_ascii=False).encode("utf-8")
    sig = signature(mdata)
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        z.writestr("manifest.json", mdata)
        if sig:
            z.writestr("manifest.sig", sig)
        for rel, data in payload:
            z.writestr("payload/" + rel, data)
        for name, data in packages:
            z.writestr("packages/" + name, data, compress_type=zipfile.ZIP_STORED)
    print(f"{out}  ({os.path.getsize(out) / 1e6:.2f} MB)")


def cmd_addon(a):
    manifest = json.load(open(os.path.join(a.folder, "manifest.json"), encoding="utf-8"))
    manifest.setdefault("format", 1)
    manifest["kind"] = "addon"
    if a.platform:
        manifest["platform"] = a.platform
    files, payload = [], []
    root = os.path.join(a.folder, "payload")
    for d, _, names in sorted(os.walk(root)):
        for n in sorted(names):
            p = os.path.join(d, n)
            rel = os.path.relpath(p, root).replace(os.sep, "/")
            e, data = file_entry(p, rel, executable=n.endswith(".so") or n.lower().endswith(".dll"))
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
            if os.path.isfile(p) and (n.endswith(".so") or n.lower().endswith(".dll") or n.endswith(".json")):
                e2, d2 = file_entry(p, "plugins/" + n, executable=n.endswith(".so") or n.lower().endswith(".dll"))
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


def signed_bytes(data):
    """the package (bytes of a .qbpkg) with manifest.sig made anew, its parts (a bundle's packages) as well"""
    import io
    src = zipfile.ZipFile(io.BytesIO(data))
    manifest = src.read("manifest.json")
    sig = signature(manifest)
    if not sig:
        sys.exit("make-package.py sign: QB_SIGNING_KEY_FILE is not set")
    out = io.BytesIO()
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        z.writestr("manifest.json", manifest)
        z.writestr("manifest.sig", sig)
        for info in src.infolist():
            if info.filename in ("manifest.json", "manifest.sig"):
                continue
            body = src.read(info.filename)
            if info.filename.startswith("packages/") and info.filename.endswith(".qbpkg"):
                z.writestr(info.filename, signed_bytes(body), compress_type=zipfile.ZIP_STORED)
            else:
                z.writestr(info, body, compress_type=info.compress_type)
    return out.getvalue()


def cmd_sign(a):
    for p in a.packages:
        data = signed_bytes(open(p, "rb").read())
        tmp = p + ".signing"
        with open(tmp, "wb") as f:
            f.write(data)
        os.replace(tmp, p)
        print(f"{p}  signed ({len(data) / 1e6:.2f} MB)")


ap = argparse.ArgumentParser()
sub = ap.add_subparsers(dest="cmd", required=True)
s = sub.add_parser("addon"); s.add_argument("--platform"); s.add_argument("folder"); s.add_argument("out")
s.set_defaults(fn=cmd_addon)
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
s = sub.add_parser("sign")
s.add_argument("packages", nargs="+"); s.set_defaults(fn=cmd_sign)
args = ap.parse_args()
args.fn(args)
