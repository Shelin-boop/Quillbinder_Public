#!/usr/bin/env python3
"""Every key of the keyboard layout once, through the X server (XTest): case S-W17 of the system test.

  keys.py --log FILE

Sends each key of the main block (key codes 10 to 61 and 94) alone, with Shift and with AltGr
(ISO_Level3_Shift) where the layout gives a character there; then each dead key followed by a, e, i,
o, u and Space; then a line that checks the movement and deletion keys. The expected text is worked
out from the layout's own key table of the X server (XkbKeycodeToKeysym), not from Qt, and written to
FILE.expected; FILE gets one line per key sent ("s <keycode> <level> <keysym hex>") and the keys left
out ("x ...", no character for that keysym).
"""
import argparse
import ctypes
import time
import unicodedata

DEAD = {0xFE50: "̀", 0xFE51: "́", 0xFE52: "̂", 0xFE53: "̃", 0xFE57: "̈"}
SPACING = {0xFE50: "`", 0xFE51: "´", 0xFE52: "^", 0xFE53: "~", 0xFE57: "¨"}
NAMED = {0x20AC: "€", 0xFF09: "\t"}


def char_of(ks):
    if 0x20 <= ks <= 0x7E or 0xA0 <= ks <= 0xFF:
        return chr(ks)
    if 0x01000100 <= ks <= 0x0110FFFF:
        return chr(ks - 0x01000000)
    return NAMED.get(ks)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--log", required=True)
    ap.add_argument("--gap", type=float, default=0.06)
    a = ap.parse_args()
    x11 = ctypes.CDLL("libX11.so.6")
    xtst = ctypes.CDLL("libXtst.so.6")
    x11.XOpenDisplay.restype = ctypes.c_void_p
    x11.XOpenDisplay.argtypes = [ctypes.c_char_p]
    x11.XKeysymToKeycode.restype = ctypes.c_ubyte
    x11.XKeysymToKeycode.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
    x11.XkbKeycodeToKeysym.restype = ctypes.c_ulong
    x11.XkbKeycodeToKeysym.argtypes = [ctypes.c_void_p, ctypes.c_ubyte, ctypes.c_int, ctypes.c_int]
    x11.XFlush.argtypes = [ctypes.c_void_p]
    x11.XSync.argtypes = [ctypes.c_void_p, ctypes.c_int]
    x11.XAutoRepeatOff.argtypes = [ctypes.c_void_p]
    xtst.XTestFakeKeyEvent.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_int, ctypes.c_ulong]
    dpy = x11.XOpenDisplay(None)
    if not dpy:
        raise SystemExit("no display")
    x11.XAutoRepeatOff(dpy)
    shift = x11.XKeysymToKeycode(dpy, 0xFFE1)
    altgr = x11.XKeysymToKeycode(dpy, 0xFE03)
    log = open(a.log, "w", buffering=1)
    expected = []

    def key(kc, down):
        xtst.XTestFakeKeyEvent(dpy, kc, 1 if down else 0, 0)
        x11.XFlush(dpy)

    def tap(kc, mods=()):
        for m in mods:
            key(m, True)
        key(kc, True)
        key(kc, False)
        for m in reversed(mods):
            key(m, False)
        x11.XSync(dpy, 0)
        time.sleep(a.gap)

    def by_sym(ks):
        return x11.XKeysymToKeycode(dpy, ks)

    time.sleep(0.5)
    dead_keys = []
    for kc in list(range(10, 62)) + [94]:
        for level, mods in ((0, ()), (1, (shift,)), (2, (altgr,) if altgr else None)):
            if mods is None:
                continue
            ks = x11.XkbKeycodeToKeysym(dpy, kc, 0, level)
            if ks == 0:
                continue
            if ks in DEAD:
                dead_keys.append((kc, mods, ks))
                continue
            ch = char_of(ks)
            if ch is None:
                log.write("x %d %d %x\n" % (kc, level, ks))
                continue
            if level == 2 and ch == char_of(x11.XkbKeycodeToKeysym(dpy, kc, 0, 0)):
                continue  # AltGr gives the same as the plain key
            tap(kc, mods)
            expected.append(ch)
            log.write("s %d %d %x\n" % (kc, level, ks))
    ret = by_sym(0xFF0D)
    tap(ret)
    expected.append("\n")
    # the dead keys, each followed by the vowels and Space
    for kc, mods, ks in dead_keys:
        for v in "aeiou ":
            tap(kc, mods)
            tap(by_sym(ord(v)))
            expected.append(SPACING[ks] if v == " " else unicodedata.normalize("NFC", v + DEAD[ks]))
            log.write("d %d %x %s\n" % (kc, ks, v))
        tap(ret)
        expected.append("\n")
    # movement and deletion: "abcd", Left Left BackSpace Delete End "!" -> "ad!"
    for ch in "abcd":
        tap(by_sym(ord(ch)))
    tap(by_sym(0xFF51))
    tap(by_sym(0xFF51))
    tap(by_sym(0xFF08))
    tap(by_sym(0xFFFF))
    tap(by_sym(0xFF57))
    tap(by_sym(ord("!")), (shift,) if x11.XkbKeycodeToKeysym(dpy, by_sym(ord("!")), 0, 0) != ord("!") else ())
    expected.append("ad!")
    # Home and Tab: "xy", Home, Tab -> "\txy"
    tap(ret)
    expected.append("\n")
    tap(by_sym(ord("x")))
    tap(by_sym(ord("y")))
    tap(by_sym(0xFF50))
    tap(by_sym(0xFF09))
    expected.append("\txy")
    log.write("end\n")
    with open(a.log + ".expected", "w", encoding="utf-8") as f:
        f.write("".join(expected))


if __name__ == "__main__":
    main()
