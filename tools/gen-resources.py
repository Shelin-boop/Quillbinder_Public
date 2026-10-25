#!/usr/bin/env python3
"""Regenerates resources/resources.qrc from the files in resources/ and docs/.

Run after adding or removing icons, license texts or files of the welcome project:  python3 tools/gen-resources.py
"""
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RES = os.path.join(ROOT, "resources")

entries = []  # (alias, path relative to resources/)
for theme in ("qbicons", "qbicons-dark"):
    base = os.path.join(RES, "icons", theme)
    for dirpath, _, files in sorted(os.walk(base)):
        for f in sorted(files):
            full = os.path.join(dirpath, f)
            rel = os.path.relpath(full, RES)
            entries.append(("icons/" + os.path.relpath(full, os.path.join(RES, "icons")), rel))
entries.append(("app/quillbinder.svg", "app/quillbinder.svg"))
entries.append(("docs/manual.md", "../docs/manual.md"))
entries.append(("docs/developer-guide.md", "../docs/developer-guide.md"))
entries.append(("docs/mindmap-help.md", "../docs/mindmap-help.md"))
entries.append(("docs/quick-start.md", "../docs/quick-start.md"))
# the license texts of the third-party parts (tools/collect-licenses.py) and
# the welcome project (an outline with its Markdown files, src/core/OutlineProject.*)
for sub in ("licenses", "welcome"):
    base = os.path.join(RES, sub)
    for dirpath, _, files in sorted(os.walk(base)):
        for f in sorted(files):
            rel = os.path.relpath(os.path.join(dirpath, f), RES)
            entries.append((rel, rel))

with open(os.path.join(RES, "resources.qrc"), "w") as out:
    out.write("<!DOCTYPE RCC>\n<RCC version=\"1.0\">\n  <qresource prefix=\"/\">\n")
    for alias, path in entries:
        out.write(f"    <file alias=\"{alias}\">{path}</file>\n")
    out.write("  </qresource>\n</RCC>\n")
print(f"{len(entries)} files")
