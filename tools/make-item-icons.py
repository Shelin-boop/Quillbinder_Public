#!/usr/bin/env python3
"""Draws the document icons of Documents > Change Icon (0.17).

The set follows the icon menu of Scrivener 3 for Windows (the user's
screenshots): the submenus Book, Characters, Flag, Locations, Notes, To Do,
TV and Way-Station, and the icons below them (Back Matter ... Warning).
They are drawn here, not copied: every picture is this program's own.

The light parts of the pictures are white or nearly white on purpose: with
View > Use Label Color In > Icons the label color shines through them.

Writes resources/icons/qbicons/status/<size>/<id>.svg and the same files into
qbicons-dark (the pictures carry their own colors and work on both).
Run after changing a picture, then tools/update-icon-index.sh:

    python3 tools/make-item-icons.py && tools/update-icon-index.sh
"""
import math
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SIZES = (16, 22, 32, 48, 64)

INK = "#3a3a3a"     # outlines
PAPER = "#ffffff"   # light parts (take the label color)
COLORS = {
    "blue": ("#4a90d9", "#24609f"),
    "green": ("#5cb85c", "#2f7d32"),
    "red": ("#e2574c", "#a3271f"),
    "yellow": ("#f6c844", "#b8860b"),
    "orange": ("#f39237", "#b35c0e"),
    "purple": ("#a569bd", "#6c3483"),
    "brown": ("#b07845", "#6d4321"),
    "white": ("#fafafa", "#8a8a8a"),
    "grey": ("#a3a9ae", "#5f6368"),
    "black": ("#3c3f41", "#111111"),
}

ICONS = {}  # id -> svg body (viewBox 0 0 32 32)


def icon(name):
    def wrap(fn):
        ICONS[name] = fn()
        return fn
    return wrap


def S(w=1.6, color=INK):
    return f'stroke="{color}" stroke-width="{w}" stroke-linejoin="round" stroke-linecap="round"'


# ------------------------------------------------------------------ helpers

def book(fill, dark):
    return (
        # pages (seen at the bottom and the right)
        f'<path d="M9 5.5 H25.5 V27 H9 Z" fill="{PAPER}" {S(1.4)}/>'
        f'<path d="M11 27 V28.6 H26.8 V7 H25.5" fill="{PAPER}" {S(1.4)}/>'
        # cover
        f'<rect x="5.5" y="3.5" width="19" height="22" rx="1.6" fill="{fill}" {S(1.6, dark)}/>'
        f'<rect x="5.5" y="3.5" width="3.6" height="22" rx="1.2" fill="{dark}"/>'
        # label on the cover
        f'<rect x="12" y="8" width="9.5" height="5.5" rx="0.8" fill="{PAPER}" {S(1.1, dark)}/>'
    )


def notebook(fill, dark):
    rings = "".join(f'<rect x="4" y="{y}" width="5" height="2" rx="1" fill="#d6d6d6" {S(1.0)}/>' for y in (6, 11, 16, 21))
    return (
        f'<rect x="7" y="3.5" width="20" height="25" rx="1.8" fill="{fill}" {S(1.6, dark)}/>'
        f'<rect x="12.5" y="8.5" width="11" height="6" rx="0.8" fill="{PAPER}" {S(1.1, dark)}/>'
        + rings
    )


def flag(fill, dark):
    return (
        f'<path d="M8.5 6 C13 3.6 17 8.4 25.5 5.4 V18 C17 21 13 16.2 8.5 18.6 Z" fill="{fill}" {S(1.5, dark)}/>'
        f'<path d="M8 4 V29" fill="none" {S(2.4, "#5b5b5b")}/>'
        f'<circle cx="8" cy="3.6" r="1.6" fill="#d9a400" {S(0.9, "#7a5c00")}/>'
    )


def notepad(fill, dark):
    lines = "".join(f'<path d="M10 {y} H24" fill="none" {S(1.1, dark)}/>' for y in (12, 16, 20, 24))
    rings = "".join(f'<circle cx="{x}" cy="4.6" r="1.4" fill="none" {S(1.1, "#555555")}/>' for x in (11, 15.5, 20, 24.5))
    return (
        f'<rect x="6" y="5" width="21" height="24" rx="1.5" fill="{fill if fill != "#fafafa" else PAPER}" {S(1.6, dark)}/>'
        f'<rect x="6" y="5" width="21" height="3.6" rx="1.2" fill="{dark}"/>'
        + lines + rings
    )


def sheet(extra, corner=True):
    body = '<path d="M7 3.5 H20 L26 9.5 V28.5 H7 Z" fill="%s" %s/>' % (PAPER, S(1.6))
    if corner:
        body += f'<path d="M20 3.5 V9.5 H26" fill="#e6e6e6" {S(1.4)}/>'
    return body + extra


def person(cx, cy, scale, shirt, hair=None, long_hair=False):
    s = scale
    out = ''
    if long_hair and hair:
        out += f'<path d="M{cx-4.6*s} {cy+2.6*s} C{cx-5.4*s} {cy-5*s} {cx+5.4*s} {cy-5*s} {cx+4.6*s} {cy+2.6*s} Z" fill="{hair}"/>'
    out += f'<path d="M{cx-6.5*s} {cy+11*s} C{cx-6.5*s} {cy+4.5*s} {cx+6.5*s} {cy+4.5*s} {cx+6.5*s} {cy+11*s} Z" fill="{shirt}" {S(1.2*s)}/>'
    out += f'<circle cx="{cx}" cy="{cy}" r="{3.6*s}" fill="#f2c9a0" {S(1.2*s)}/>'
    if hair and not long_hair:
        out += f'<path d="M{cx-3.6*s} {cy-0.4*s} C{cx-3.4*s} {cy-4.6*s} {cx+3.4*s} {cy-4.6*s} {cx+3.6*s} {cy-0.4*s} C{cx+1.5*s} {cy-2*s} {cx-1.5*s} {cy-2*s} {cx-3.6*s} {cy-0.4*s} Z" fill="{hair}"/>'
    if long_hair and hair:
        out += f'<path d="M{cx-3.6*s} {cy-0.2*s} C{cx-3.4*s} {cy-4.8*s} {cx+3.4*s} {cy-4.8*s} {cx+3.6*s} {cy-0.2*s} C{cx+1.2*s} {cy-2.4*s} {cx-1.2*s} {cy-2.4*s} {cx-3.6*s} {cy-0.2*s} Z" fill="{hair}"/>'
    return out


def tv(fill, dark):
    return (
        f'<path d="M12 27.5 H20 M16 23.5 V27.5" fill="none" {S(2.2, "#555555")}/>'
        f'<rect x="3.5" y="6" width="25" height="17.5" rx="2.2" fill="#4d4d4d" {S(1.5, "#2a2a2a")}/>'
        f'<rect x="6" y="8.5" width="20" height="12.5" rx="1" fill="{fill}" {S(1.0, dark)}/>'
        f'<path d="M8 11 L11.5 10.2" fill="none" {S(1.2, "#ffffff")} opacity="0.75"/>'
        f'<path d="M12 6 L9 2.5 M20 6 L23 2.5" fill="none" {S(1.3, "#555555")}/>'
    )


def waystation(fill, dark):
    return (
        f'<path d="M10 7 L26 11.5 L10 16 Z" fill="{fill}" {S(1.5, dark)}/>'
        f'<path d="M9.5 4.5 V27" fill="none" {S(2.4, "#6b4a2b")}/>'
        f'<path d="M5 28.5 H14" fill="none" {S(2.4, "#6b4a2b")}/>'
        f'<circle cx="9.5" cy="4" r="1.6" fill="{PAPER}" {S(1.0)}/>'
    )


# ------------------------------------------------------------------ Book
for c in ("blue", "brown", "green", "red", "yellow"):
    ICONS[f"qb-book-{c}"] = book(*COLORS[c])
for c in ("blue", "green", "orange"):
    ICONS[f"qb-notebook-{c}"] = notebook(*COLORS[c])


@icon("qb-composition")
def _():
    spots = ""
    pts = [(10, 7), (14, 5.5), (19, 7.5), (23, 5.5), (11, 19), (16, 21.5), (21, 18.5), (24, 23.5), (13, 25.5),
           (19, 25.5), (9.5, 14), (25, 14.5), (22, 11)]
    for i, (x, y) in enumerate(pts):
        spots += f'<circle cx="{x}" cy="{y}" r="{0.9 + (i % 3) * 0.35}" fill="#f2f2f2"/>'
    return (
        f'<rect x="6" y="3.5" width="21" height="25" rx="1.4" fill="#262626" {S(1.6, "#111111")}/>'
        + spots +
        f'<rect x="6" y="3.5" width="3.8" height="25" rx="1" fill="#111111"/>'
        f'<rect x="12" y="10" width="11.5" height="6.5" rx="0.6" fill="{PAPER}" {S(1.0, "#111111")}/>'
        f'<path d="M13.6 12.4 H21.9 M13.6 14.4 H19.5" fill="none" {S(0.9, "#777777")}/>'
    )


# ------------------------------------------------------------------ Flag, Notes, TV, Way-Station
for c in ("blue", "green", "orange", "purple", "red", "white", "yellow"):
    ICONS[f"qb-flag-{c}"] = flag(*COLORS[c])
for c in ("blue", "green", "red", "white", "yellow"):
    ICONS[f"qb-notepad-{c}"] = notepad(*COLORS[c])
for c in ("blue", "green", "grey", "red", "yellow"):
    ICONS[f"qb-tv-{c}"] = tv(*COLORS[c])
for c in ("blue", "green", "orange", "purple", "red", "yellow"):
    ICONS[f"qb-waystation-{c}"] = waystation(*COLORS[c])


# ------------------------------------------------------------------ Characters
@icon("qb-character-sheet")
def _():
    lines = "".join(f'<path d="M10 {y} H23" fill="none" {S(1.2, "#8a8a8a")}/>' for y in (21, 24.5))
    return sheet(
        f'<rect x="10" y="7.5" width="8.5" height="9.5" fill="#fbe3c6" {S(1.0)}/>'
        + person(14.25, 11, 0.55, "#e0823d", "#6d4321") + lines)


@icon("qb-character-photo")
def _():
    return (
        f'<rect x="4.5" y="4.5" width="23" height="23" rx="1" fill="{PAPER}" {S(1.6)}/>'
        f'<rect x="7.5" y="7.5" width="17" height="17" fill="#cfe3f5" {S(1.0)}/>'
        f'<clipPath id="ph"><rect x="7.5" y="7.5" width="17" height="17"/></clipPath>'
        f'<g clip-path="url(#ph)">' + person(16, 14.5, 1.0, "#3f7fbf", "#4a3524") + '</g>'
    )


@icon("qb-character-female")
def _():
    return person(16, 11, 1.45, "#c0559a", "#7b4a26", long_hair=True)


@icon("qb-character-male")
def _():
    return person(16, 11, 1.45, "#3f7fbf", "#4a3524")


@icon("qb-character-group")
def _():
    return (person(10.5, 11, 0.95, "#5cb85c", "#3b2a1a") + person(21.5, 11, 0.95, "#c0559a", "#8a5a2b", long_hair=True)
            + person(16, 15.5, 1.05, "#3f7fbf", "#4a3524"))


# ------------------------------------------------------------------ Locations
@icon("qb-location-sheet")
def _():
    lines = "".join(f'<path d="M10 {y} H23" fill="none" {S(1.2, "#8a8a8a")}/>' for y in (21, 24.5))
    return sheet(
        f'<rect x="10" y="7.5" width="11" height="9.5" fill="#bfe0f7" {S(1.0)}/>'
        f'<path d="M10 17 L13.5 12 L16 14.6 L18 12.5 L21 17 Z" fill="#5cb85c" {S(0.9, "#2f7d32")}/>'
        f'<circle cx="18.3" cy="10" r="1.3" fill="#f6c844"/>' + lines)


@icon("qb-location-map")
def _():
    return (
        f'<path d="M3.5 7.5 L11 4.5 L21 7.5 L28.5 4.5 V24.5 L21 27.5 L11 24.5 L3.5 27.5 Z" fill="#f4ecd2" {S(1.5)}/>'
        f'<path d="M11 4.5 V24.5 M21 7.5 V27.5" fill="none" {S(1.1, "#9b8b5e")}/>'
        f'<path d="M3.5 18 C8 15 9 21 14 18 C18 15.5 19 12 24 14 L28.5 13" fill="none" {S(1.3, "#5cb85c")}/>'
        f'<path d="M7 11 L10 13.5 L15 11 L18.5 15.5 L24 19.5" fill="none" stroke="#d9342b" stroke-width="1.5" stroke-dasharray="2 1.6" stroke-linecap="round"/>'
        f'<circle cx="24" cy="19.5" r="1.8" fill="#d9342b" {S(0.8, "#7d1b14")}/>'
    )


@icon("qb-location-globe")
def _():
    return (
        f'<circle cx="16" cy="16" r="12" fill="#4a90d9" {S(1.6, "#24609f")}/>'
        f'<path d="M9 9.5 C12 8 13.5 10.5 12.5 12.5 C11 15 13 16.5 11.5 19 C10.5 21 8 19 7 16 C6.2 13.5 7 11 9 9.5 Z" fill="#5cb85c" {S(0.9, "#2f7d32")}/>'
        f'<path d="M17 6.5 C20 6 23.5 8 25 10.5 C23 12 21 10.5 19.5 12.5 C18.5 14 20.5 15.5 22.5 15 C24.5 14.8 26 17 25 20 C24 23 21 25.5 18.5 24.5 C17 23.8 18.5 21 17 19.5 C15.5 18 15.5 15 16.5 13 C17.5 11 15.5 8 17 6.5 Z" fill="#5cb85c" {S(0.9, "#2f7d32")}/>'
        f'<ellipse cx="12" cy="9.5" rx="3" ry="1.3" fill="#ffffff" opacity="0.35"/>'
    )


@icon("qb-location-house")
def _():
    return (
        f'<rect x="7" y="14" width="18" height="14" fill="{PAPER}" {S(1.6)}/>'
        f'<path d="M4 15.5 L16 4.5 L28 15.5" fill="#d9534f" {S(1.6, "#8f2a23")}/>'
        f'<rect x="13.5" y="19" width="5" height="9" fill="#b07845" {S(1.1, "#6d4321")}/>'
        f'<rect x="8.5" y="17" width="3.5" height="3.5" fill="#bfe0f7" {S(0.9)}/>'
        f'<rect x="20" y="17" width="3.5" height="3.5" fill="#bfe0f7" {S(0.9)}/>'
    )


# ------------------------------------------------------------------ To Do
@icon("qb-checked")
def _():
    return (f'<rect x="4.5" y="4.5" width="23" height="23" rx="3.5" fill="{PAPER}" {S(2.0)}/>'
            f'<path d="M9.5 16.5 L14 21 L23 10.5" fill="none" {S(3.2, "#2a2a2a")}/>')


@icon("qb-unchecked")
def _():
    return f'<rect x="4.5" y="4.5" width="23" height="23" rx="3.5" fill="{PAPER}" {S(2.0)}/>'


@icon("qb-pass")
def _():
    return f'<path d="M5 17 L12.5 24.5 L27 8" fill="none" {S(5.0, "#27ae60")}/>'


@icon("qb-fail")
def _():
    return f'<path d="M7 7 L25 25 M25 7 L7 25" fill="none" {S(5.0, "#da4453")}/>'


@icon("qb-unclear")
def _():
    return f'<circle cx="16" cy="16" r="11.5" fill="#f67400" {S(1.5, "#a84f00")}/>'


# ------------------------------------------------------------------ the icons below the submenus
@icon("qb-back-matter")
def _():
    return (f'<path d="M6 4.5 H22 L26 8 V28 H6 Z" fill="{PAPER}" {S(1.6)}/>'
            f'<path d="M21.5 4.5 V28" fill="none" {S(1.2)}/><rect x="21.5" y="4.5" width="4.5" height="23.5" fill="#9aa0a6" {S(1.2)}/>'
            + "".join(f'<path d="M9.5 {y} H18" fill="none" {S(1.2, "#8a8a8a")}/>' for y in (10, 14, 18, 22)))


@icon("qb-front-matter")
def _():
    return (f'<path d="M6 4.5 H26 V28 H6 Z" fill="{PAPER}" {S(1.6)}/>'
            f'<rect x="6" y="4.5" width="4.5" height="23.5" fill="#9aa0a6" {S(1.2)}/>'
            + "".join(f'<path d="M14 {y} H22.5" fill="none" {S(1.2, "#8a8a8a")}/>' for y in (10, 14, 18, 22)))


@icon("qb-blackboard")
def _():
    return (f'<path d="M9 25 L6.5 30 M23 25 L25.5 30" fill="none" {S(1.8, "#6d4321")}/>'
            f'<rect x="3" y="5" width="26" height="20" rx="1" fill="#2f4f3a" {S(2.2, "#8a5a2b")}/>'
            f'<path d="M7 11 C9 9 11 12 13 10 M7 15.5 H17 M7 19.5 H13" fill="none" {S(1.3, "#f0f0f0")}/>'
            f'<path d="M20.5 22 H25" fill="none" {S(1.6, "#ffffff")}/>')


@icon("qb-calendar")
def _():
    grid = ""
    for r, y in enumerate((14.5, 18.5, 22.5)):
        for x in (9, 13.5, 18, 22.5):
            grid += f'<rect x="{x - 1.4}" y="{y - 1.4}" width="2.8" height="2.6" fill="{"#e2574c" if (r, x) == (1, 18) else "#9aa0a6"}"/>'
    return (f'<rect x="4.5" y="5.5" width="23" height="22" rx="2" fill="{PAPER}" {S(1.6)}/>'
            f'<path d="M4.5 10.5 V7.5 A2 2 0 0 1 6.5 5.5 H25.5 A2 2 0 0 1 27.5 7.5 V10.5 Z" fill="#e2574c" {S(1.6)}/>'
            f'<path d="M10 3 V7.5 M22 3 V7.5" fill="none" {S(2.0)}/>' + grid)


@icon("qb-clapper")
def _():
    return (f'<rect x="4" y="12" width="24" height="15.5" rx="1" fill="#3c3f41" {S(1.4, "#111111")}/>'
            f'<path d="M7 17.5 H25 M7 21.5 H20" fill="none" {S(1.2, "#f0f0f0")}/>'
            f'<g transform="rotate(-14 4 12)"><rect x="4" y="5.5" width="24" height="6" fill="{PAPER}" {S(1.4, "#111111")}/>'
            f'<path d="M7 5.5 L10 11.5 M13 5.5 L16 11.5 M19 5.5 L22 11.5 M25 5.5 L28 11.5" fill="none" {S(2.4, "#111111")}/></g>')


@icon("qb-clock")
def _():
    ticks = ""
    for i in range(12):
        a = math.radians(i * 30)
        r1, r2 = (9.2, 11) if i % 3 else (8.2, 11)
        ticks += f'<path d="M{16 + r1 * math.sin(a):.2f} {16 - r1 * math.cos(a):.2f} L{16 + r2 * math.sin(a):.2f} {16 - r2 * math.cos(a):.2f}" fill="none" {S(1.1, "#555555")}/>'
    return (f'<circle cx="16" cy="16" r="13" fill="{PAPER}" {S(2.0)}/>' + ticks +
            f'<path d="M16 16 V8.5 M16 16 L21 19" fill="none" {S(1.9, "#2a2a2a")}/><circle cx="16" cy="16" r="1.4" fill="#e2574c"/>')


@icon("qb-cog")
def _():
    teeth = []
    n = 8
    for i in range(n * 2):
        a = math.radians(i * 180 / n)
        r = 13 if i % 2 == 0 else 10
        a1, a2 = a - math.radians(7), a + math.radians(7)
        teeth.append(f"{16 + r * math.cos(a1):.2f},{16 + r * math.sin(a1):.2f}")
        teeth.append(f"{16 + r * math.cos(a2):.2f},{16 + r * math.sin(a2):.2f}")
    return (f'<polygon points="{" ".join(teeth)}" fill="#a3a9ae" {S(1.4, "#4f5459")}/>'
            f'<circle cx="16" cy="16" r="4.2" fill="{PAPER}" {S(1.4, "#4f5459")}/>')


@icon("qb-eye")
def _():
    return (f'<path d="M2.5 16 C8 7 24 7 29.5 16 C24 25 8 25 2.5 16 Z" fill="{PAPER}" {S(1.8)}/>'
            f'<circle cx="16" cy="16" r="5.6" fill="#4a90d9" {S(1.2, "#24609f")}/>'
            f'<circle cx="16" cy="16" r="2.6" fill="#1a1a1a"/><circle cx="17.6" cy="14.4" r="1.1" fill="#ffffff"/>')


@icon("qb-figures")
def _():
    return (f'<rect x="4" y="4.5" width="24" height="23" rx="1.5" fill="{PAPER}" {S(1.6)}/>'
            f'<path d="M8 8 V23.5 H25" fill="none" {S(1.4)}/>'
            f'<path d="M9.5 20 L13.5 15 L17 17.5 L23.5 9.5" fill="none" {S(1.9, "#4a90d9")}/>'
            f'<path d="M9.5 22 L14 19 L18.5 21 L23.5 16" fill="none" {S(1.6, "#e2574c")}/>')


@icon("qb-filing-cabinet")
def _():
    drawers = ""
    for y in (4.5, 12.5, 20.5):
        drawers += (f'<rect x="8.5" y="{y + 1.4}" width="15" height="5.2" rx="0.6" fill="#d9dcdf" {S(1.1)}/>'
                    f'<path d="M13.5 {y + 4} H18.5" fill="none" {S(1.6, "#4f5459")}/>')
    return f'<rect x="6.5" y="3" width="19" height="26.5" rx="1.2" fill="#a3a9ae" {S(1.6, "#4f5459")}/>' + drawers


@icon("qb-information")
def _():
    return (f'<path d="M16 3.5 L29 27 H3 Z" fill="#4aa3df" {S(1.8, "#1f6f9f")}/>'
            f'<circle cx="16" cy="12.2" r="1.7" fill="#ffffff"/><path d="M16 16 V23.5" fill="none" {S(2.8, "#ffffff")}/>')


@icon("qb-lectern")
def _():
    return (f'<path d="M12 14 L13.5 26 H18.5 L20 14 Z" fill="#b07845" {S(1.4, "#6d4321")}/>'
            f'<path d="M9 29 H23" fill="none" {S(2.4, "#6d4321")}/>'
            f'<path d="M5 13 L8 6 H24 L27 13 Z" fill="#b07845" {S(1.4, "#6d4321")}/>'
            f'<path d="M9.5 11.5 L11 7.5 H21 L22.5 11.5 Z" fill="{PAPER}" {S(1.0)}/>')


@icon("qb-light-bulb")
def _():
    return (f'<path d="M16 3 C9.5 3 6.5 8.5 8 13.5 C9 16.5 12 18 12 21.5 H20 C20 18 23 16.5 24 13.5 C25.5 8.5 22.5 3 16 3 Z" fill="#f9d649" {S(1.6, "#a88400")}/>'
            f'<path d="M13.5 15 C14.5 12 17.5 12 18.5 15" fill="none" {S(1.1, "#c49a00")}/>'
            f'<rect x="12" y="21.5" width="8" height="5" rx="1" fill="#a3a9ae" {S(1.3)}/>'
            f'<path d="M12 23.6 H20" fill="none" {S(1.0)}/><path d="M14 28.5 H18" fill="none" {S(1.8)}/>'
            f'<path d="M11 7.5 C12 6 13.5 5.4 15 5.3" fill="none" {S(1.4, "#ffffff")}/>')


@icon("qb-magnifier")
def _():
    return (f'<path d="M19.5 19.5 L27.5 27.5" fill="none" {S(4.2, "#4d3826")}/>'
            f'<circle cx="13" cy="13" r="9" fill="#e6f2fb" {S(2.4, "#555555")}/>'
            f'<path d="M8.5 10.5 C9.5 8.5 11 7.6 13 7.4" fill="none" {S(1.5, "#ffffff")}/>')


@icon("qb-music")
def _():
    return (f'<path d="M11 23 V7.5 L26 4.5 V20" fill="none" {S(2.2, "#24609f")}/>'
            f'<path d="M11 7.5 L26 4.5 V8.5 L11 11.5 Z" fill="#24609f"/>'
            f'<ellipse cx="8" cy="23.5" rx="3.6" ry="2.8" fill="#4a90d9" {S(1.2, "#24609f")} transform="rotate(-18 8 23.5)"/>'
            f'<ellipse cx="23" cy="20.5" rx="3.6" ry="2.8" fill="#4a90d9" {S(1.2, "#24609f")} transform="rotate(-18 23 20.5)"/>')


@icon("qb-musical-notes")
def _():
    return (f'<path d="M3.5 22 H28.5 M3.5 26 H28.5" fill="none" {S(0.9, "#9aa0a6")}/>'
            f'<path d="M10.5 22 V6 C12.5 8 14.5 8.5 15.5 11.5" fill="none" {S(1.9, "#24609f")}/>'
            f'<ellipse cx="7.8" cy="22.3" rx="3.2" ry="2.4" fill="#4a90d9" {S(1.1, "#24609f")} transform="rotate(-18 7.8 22.3)"/>'
            f'<path d="M24 19 V4.5 C26 6.5 28 7 29 10" fill="none" {S(1.9, "#24609f")}/>'
            f'<ellipse cx="21.3" cy="19.3" rx="3.2" ry="2.4" fill="#4a90d9" {S(1.1, "#24609f")} transform="rotate(-18 21.3 19.3)"/>')


@icon("qb-outline")
def _():
    rows = ""
    for i, (x, y) in enumerate(((9, 9.5), (12.5, 13.5), (12.5, 17.5), (9, 21.5), (12.5, 25))):
        rows += f'<circle cx="{x}" cy="{y}" r="1.1" fill="#4a90d9"/><path d="M{x + 2.6} {y} H23.5" fill="none" {S(1.3, "#7a7a7a")}/>'
    return f'<rect x="5" y="4" width="22" height="25" rx="1.4" fill="{PAPER}" {S(1.6)}/>' + rows


@icon("qb-pencil")
def _():
    return ('<g transform="rotate(45 16 16)">'
            f'<rect x="12.5" y="1.5" width="7" height="5" rx="1.4" fill="#f19aa8" {S(1.3)}/>'
            f'<rect x="12.5" y="6" width="7" height="2.4" fill="#a3a9ae" {S(1.1)}/>'
            f'<rect x="12.5" y="8.4" width="7" height="15" fill="#f6c844" {S(1.3, "#9c7000")}/>'
            f'<path d="M12.5 23.4 L16 30 L19.5 23.4 Z" fill="#f2d3a8" {S(1.2)}/>'
            f'<path d="M15 28 L16 30 L17 28 Z" fill="#2a2a2a"/>'
            '</g>')


@icon("qb-proofread")
def _():
    return (f'<rect x="2.5" y="4.5" width="27" height="23" rx="3" fill="{PAPER}" {S(1.8)}/>'
            f'<text x="16" y="21.8" font-family="DejaVu Sans, Liberation Sans, Arial, sans-serif" font-size="14.5" font-weight="bold" font-style="italic" text-anchor="middle" fill="#1a1a1a">PR</text>')


@icon("qb-question")
def _():
    return (f'<circle cx="16" cy="16" r="13" fill="#f39c3a" {S(1.6, "#b35c0e")}/>'
            f'<path d="M11.8 12.2 A4.3 4.3 0 1 1 17.6 16.2 C16.5 16.9 16 17.6 16 19.2" fill="none" {S(3.0, "#ffffff")}/>'
            f'<circle cx="16" cy="23.6" r="1.9" fill="#ffffff"/>')


@icon("qb-reviewer")
def _():
    pts = []
    for i in range(10):
        a = math.radians(-90 + i * 36)
        r = 13 if i % 2 == 0 else 5.6
        pts.append(f"{16 + r * math.cos(a):.2f},{16.8 + r * math.sin(a):.2f}")
    return f'<polygon points="{" ".join(pts)}" fill="#f9d649" {S(1.6, "#c27c0e")}/>'


@icon("qb-scissors")
def _():
    return (f'<path d="M10 4 L21 21 M22 4 L11 21" fill="none" {S(3.0, "#8a9096")}/>'
            f'<path d="M10 4 L21 21 M22 4 L11 21" fill="none" {S(1.0, "#d9dcdf")}/>'
            f'<circle cx="9" cy="24.5" r="4" fill="{PAPER}" {S(2.6, "#c0392b")}/>'
            f'<circle cx="23" cy="24.5" r="4" fill="{PAPER}" {S(2.6, "#c0392b")}/>'
            f'<circle cx="16" cy="12.6" r="1.2" fill="#4f5459"/>')


@icon("qb-speech-bubble")
def _():
    return (f'<path d="M16 4 C8 4 3.5 8.5 3.5 13.5 C3.5 17.5 6.5 20.8 11 22.2 L9 28 L16.5 23 C24.5 23 28.5 18.5 28.5 13.5 C28.5 8.5 24 4 16 4 Z" fill="#f9d649" {S(1.6, "#a88400")}/>')


@icon("qb-speech-marks")
def _():
    return (f'<path d="M5 18.5 C5 11 8 7.5 13 6.5 L13.5 9 C10.5 10 9.3 11.8 9.3 14 H13.5 V24.5 H5 Z" fill="#6b6f73"/>'
            f'<path d="M18 18.5 C18 11 21 7.5 26 6.5 L26.5 9 C23.5 10 22.3 11.8 22.3 14 H26.5 V24.5 H18 Z" fill="#6b6f73"/>')


@icon("qb-statistics")
def _():
    return (f'<path d="M4 28 H28" fill="none" {S(1.6)}/>'
            f'<rect x="6" y="17" width="5" height="11" fill="#f39237" {S(1.3, "#b35c0e")}/>'
            f'<rect x="13.5" y="10" width="5" height="18" fill="#f39237" {S(1.3, "#b35c0e")}/>'
            f'<rect x="21" y="5" width="5" height="23" fill="#f39237" {S(1.3, "#b35c0e")}/>')


@icon("qb-sync-folder")
def _():
    return (f'<path d="M3.5 8 V26.5 H28.5 V10.5 H15 L12.5 7.5 H5 A1.5 1.5 0 0 0 3.5 8 Z" fill="#f6b450" {S(1.5, "#b36b00")}/>'
            f'<path d="M3.5 12.5 H28.5" fill="none" {S(1.2, "#b36b00")}/>'
            f'<path d="M11.5 19.5 A4.6 4.6 0 0 1 19.8 16.6" fill="none" {S(1.9, "#ffffff")}/><path d="M20.8 14.2 L20.3 17.6 L17 16.9" fill="none" {S(1.6, "#ffffff")}/>'
            f'<path d="M20.5 19.8 A4.6 4.6 0 0 1 12.2 22.7" fill="none" {S(1.9, "#ffffff")}/><path d="M11.2 25.1 L11.7 21.7 L15 22.4" fill="none" {S(1.6, "#ffffff")}/>')


@icon("qb-tables")
def _():
    return (f'<rect x="4" y="5" width="24" height="22" rx="1" fill="{PAPER}" {S(1.6)}/>'
            f'<rect x="4" y="5" width="24" height="5.5" fill="#4a90d9" {S(1.4)}/>'
            f'<path d="M4 16 H28 M4 21.5 H28 M12 5 V27 M20 5 V27" fill="none" {S(1.2)}/>')


@icon("qb-test-tube")
def _():
    return ('<g transform="rotate(30 16 16)">'
            f'<path d="M12.5 3.5 V24 A3.5 3.5 0 0 0 19.5 24 V3.5" fill="{PAPER}" {S(1.6)}/>'
            f'<path d="M12.5 14 H19.5 V24 A3.5 3.5 0 0 1 12.5 24 Z" fill="#a569bd" {S(1.3, "#6c3483")}/>'
            f'<path d="M11 3.5 H21" fill="none" {S(2.0)}/>'
            f'<circle cx="15" cy="18" r="1" fill="#ffffff" opacity="0.8"/><circle cx="17" cy="21.5" r="0.8" fill="#ffffff" opacity="0.8"/>'
            '</g>')


@icon("qb-thought-bubble")
def _():
    return (f'<path d="M8 19 C3.5 19 2.5 13.5 6.2 12.2 C5.2 7.6 10.5 5.2 13.2 7.5 C14.8 3.8 21 3.8 22.2 7.6 C26 6.2 29.6 10.2 27.2 13.5 C30 15.8 27.5 20.5 23.8 19.5 C22.5 22.5 17.5 22.8 16 20.2 C14 22.5 9.4 22 8 19 Z" fill="{PAPER}" {S(1.7)}/>'
            f'<circle cx="8.5" cy="24" r="2" fill="{PAPER}" {S(1.4)}/><circle cx="5" cy="28" r="1.3" fill="{PAPER}" {S(1.2)}/>')


@icon("qb-warning")
def _():
    return (f'<path d="M16 3.5 L29 27 H3 Z" fill="#f9d649" {S(1.8, "#a88400")}/>'
            f'<path d="M16 11 V19" fill="none" {S(3.0, "#2a2a2a")}/><circle cx="16" cy="23" r="1.7" fill="#2a2a2a"/>')


@icon("qb-mindmap")
def _():
    # the button of the inspector's Mind Map tab: notes joined by lines
    return (f'<path d="M16 16 L6 7 M16 16 L26 8 M16 16 L7 26 M16 16 L25 25" fill="none" stroke="#5f6368" stroke-width="1.6" stroke-dasharray="2.6 1.8"/>'
            f'<rect x="9.5" y="12.5" width="13" height="7" rx="2.5" fill="#f6c844" {S(1.4, "#a88400")}/>'
            f'<rect x="1.5" y="3" width="9" height="5.5" rx="2" fill="{PAPER}" {S(1.3)}/>'
            f'<rect x="21.5" y="4" width="9" height="5.5" rx="2" fill="#bfe0f7" {S(1.3, "#24609f")}/>'
            f'<rect x="2.5" y="23.5" width="9" height="5.5" rx="2" fill="#d0f0c0" {S(1.3, "#2f7d32")}/>'
            f'<rect x="20.5" y="22.5" width="9" height="5.5" rx="2" fill="{PAPER}" {S(1.3)}/>')


@icon("qb-mm-shape")
def _():
    # a background shape with two notes on it (mind map: New Background Shape)
    return (f'<rect x="3" y="5" width="26" height="22" rx="4" fill="#e3e6e8" {S(1.5, "#5f6368")}/>'
            f'<rect x="7" y="9" width="11" height="5" rx="1.5" fill="{PAPER}" {S(1.1)}/>'
            f'<rect x="13" y="18" width="11" height="5" rx="1.5" fill="{PAPER}" {S(1.1)}/>')


@icon("qb-mm-menu")
def _():
    return f'<path d="M6 9 H26 M6 16 H26 M6 23 H26" fill="none" {S(2.6, "#7f8487")}/>'


def write():
    for theme in ("qbicons", "qbicons-dark"):
        for size in SIZES:
            d = os.path.join(ROOT, "resources", "icons", theme, "status", str(size))
            os.makedirs(d, exist_ok=True)
            for name, body in ICONS.items():
                with open(os.path.join(d, name + ".svg"), "w") as f:
                    f.write(f'<svg xmlns="http://www.w3.org/2000/svg" width="{size}" height="{size}" viewBox="0 0 32 32">\n{body}\n</svg>\n')
    print(f"{len(ICONS)} icons, {len(SIZES)} sizes, 2 themes")


if __name__ == "__main__":
    write()
