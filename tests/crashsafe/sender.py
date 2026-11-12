#!/usr/bin/env python3
"""Outside key sender of the system test (typing gates, section 3.1 of the procedure).

Sends key presses and releases through the X server (XTest), at the rhythm of a typist profile
with its rollover (the next key pressed before the last is released), into whatever window has
the keyboard focus, and writes the monotonic time of every press (the clock Qt and
std::chrono::steady_clock use on Linux), so the program side can measure key-to-screen.

  sender.py --text FILE --log FILE --profile slow|fast|wpmN --seconds N [--seed N] [--burst N]
            [--start-delay S]

Profiles from Dhakal et al., "Observations on Typing from 136 Million Keystrokes" (CHI 2018):
slow = the slowest 10 % (mean gap 481 ms, SD 123 ms, rollover about 8 % of keys); fast = the
fastest 10 % (mean gap 122 ms, SD 12 ms, rollover about 50 %, overlapping about 30 ms, up to
100 ms). A gap is never shorter than 60 ms. After the profile, --burst N keys are sent as fast as
the X server takes them (throughput).

Log lines: "k <index> <ns>" per press of the profile run, "b <index> <ns>" per press of the
burst, "x <index> <char code>" for a character the keyboard layout cannot type (skipped),
"end <sent>" at the end. <index> is the character's place in the text file.
"""
import argparse
import ctypes
import random
import os
import signal
import sys
import time

XK_SHIFT_L = 0xFFE1
XK_RETURN = 0xFF0D

PROFILES = {
    # mean gap, SD (seconds), rollover share, overlap mean and SD (seconds)
    "slow": (0.481, 0.123, 0.08, 0.030, 0.015),
    "fast": (0.122, 0.012, 0.50, 0.030, 0.015),
}


def load_x():
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
    x11.XCloseDisplay.argtypes = [ctypes.c_void_p]
    x11.XAutoRepeatOff.argtypes = [ctypes.c_void_p]
    x11.XChangeKeyboardMapping.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int,
                                           ctypes.POINTER(ctypes.c_ulong), ctypes.c_int]
    xtst.XTestFakeKeyEvent.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_int, ctypes.c_ulong]
    return x11, xtst


def keymap(x11, dpy):
    """keysym -> (keycode, shift) for the keys of group 1 at level 1 or 2 (no AltGr)."""
    out = {}
    for level in (0, 1):
        for kc in range(8, 256):
            ks = x11.XkbKeycodeToKeysym(dpy, kc, 0, level)
            if ks and ks not in out:
                out[ks] = (kc, level == 1)
    return out


def bind_missing(x11, dpy, kmap, text):
    """Gives each character of the text that no key types (ae, oe, ue, sz on a virtual display that
    keeps the US layout: setxkbmap has no effect on the session's Xvfb) a free key code of its own,
    with its capital on the shift level, once before typing, as xdotool does. Returns the count."""
    free = [kc for kc in range(255, 7, -1)
            if all(not x11.XkbKeycodeToKeysym(dpy, kc, 0, lv) for lv in range(4))]
    done = 0
    for ch in sorted(set(text)):
        if ch == "\n" or keysym_of(ch) in kmap or not free:
            continue
        low, up = ch.lower(), ch.upper()
        if len(low) != 1 or len(up) != 1:
            low = up = ch
        kc = free.pop(0)
        syms = (ctypes.c_ulong * 2)(keysym_of(low), keysym_of(up))
        x11.XChangeKeyboardMapping(dpy, kc, 2, syms, 1)
        kmap.setdefault(keysym_of(low), (kc, False))
        kmap.setdefault(keysym_of(up), (kc, True))
        done += 1
    if done:
        x11.XSync(dpy, 0)
        time.sleep(0.3)   # the programs on the display take the new key table
    return done


def keysym_of(ch):
    if ch == "\n":
        return XK_RETURN
    o = ord(ch)
    if 0x20 <= o <= 0x7E or 0xA0 <= o <= 0xFF:
        return o
    return 0x01000000 + o


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--text", required=True)
    ap.add_argument("--log", required=True)
    ap.add_argument("--profile", required=True, help="slow, fast, or wpmN (N words a minute, the fast profile's rollover)")
    ap.add_argument("--seconds", type=float, default=60)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--burst", type=int, default=200)
    ap.add_argument("--start-delay", type=float, default=1.0)
    a = ap.parse_args()

    with open(a.text, encoding="utf-8") as f:
        text = f.read()
    x11, xtst = load_x()
    dpy = x11.XOpenDisplay(None)
    if not dpy:
        print("no X display", file=sys.stderr)
        return 2
    # no key repeat on the virtual display: when this process is held up between a press and its
    # release (a busy machine), the X server would repeat the key (first run, 2026-11-08: "quuuiet")
    x11.XAutoRepeatOff(dpy)
    x11.XSync(dpy, 0)
    kmap = keymap(x11, dpy)
    bound = bind_missing(x11, dpy, kmap, text)
    shift_kc = x11.XKeysymToKeycode(dpy, XK_SHIFT_L)
    rnd = random.Random(a.seed)
    min_gap = 0.015 if a.profile.startswith("wpm") else 0.060
    if a.profile.startswith("wpm"):
        gap = 60.0 / (float(a.profile[3:]) * 5.0)   # five characters a word
        mean, sd, roll, ov_mean, ov_sd = (gap, gap * 0.1, 0.50, min(0.030, gap * 0.4), 0.010)
    else:
        mean, sd, roll, ov_mean, ov_sd = PROFILES[a.profile]

    # the keys: (index, keycode, shift)
    keys = []
    skipped = []
    for i, ch in enumerate(text):
        m = kmap.get(keysym_of(ch))
        if m is None:
            skipped.append((i, ord(ch)))
            continue
        keys.append((i, m[0], m[1]))

    # line by line: X-02 kills this process while it types, and the record must hold every key sent
    log = open(a.log, "w", encoding="utf-8", buffering=1)
    log.write("m %d\n" % bound)   # characters given a free key code (bind_missing)
    for i, code in skipped:
        log.write("x %d %d\n" % (i, code))

    # 0.64.2: the keys that are down now; ended with SIGTERM (X-02 ends the sender when it kills the program), they
    # are released first. A key left down in the X server ate the first press of the same key in the next round
    # (a letter lost about 1 s into the typing, only in rounds after a kill: finding 8 of the first run).
    down = set()

    def press(kc, d):
        xtst.XTestFakeKeyEvent(dpy, kc, 1 if d else 0, 0)
        if d:
            down.add(kc)
        else:
            down.discard(kc)

    def release_all(signum=None, frame=None):
        for kc in list(down):
            xtst.XTestFakeKeyEvent(dpy, kc, 0, 0)
        down.clear()
        x11.XFlush(dpy)
        if signum is not None:
            try:
                log.write("released on signal\n")
                log.close()
            except Exception:
                pass
            os._exit(0)

    signal.signal(signal.SIGTERM, release_all)

    def wait_until(t):
        while True:
            d = t - time.monotonic()
            if d <= 0:
                return
            time.sleep(d - 0.002 if d > 0.004 else 0)

    time.sleep(a.start_delay)
    # the profile run: an event list (time, order, action) built key by key
    t0 = time.monotonic()
    end = t0 + a.seconds
    t = t0
    shift_down = False
    held = None          # (keycode, release time) of a key held into the next one (rollover)
    n = 0
    k = 0
    while k < len(keys) and t < end:
        idx, kc, sh = keys[k]
        nxt = keys[k + 1] if k + 1 < len(keys) else None
        gap = max(min_gap, rnd.gauss(mean, sd))
        # shift: pressed before the first shifted key of a run, released after the last
        if sh != shift_down:
            if held is not None:
                wait_until(min(held[1], t - 0.010))
                press(held[0], False)
                x11.XFlush(dpy)
                held = None
            wait_until(t - 0.015)
            press(shift_kc, sh)
            x11.XFlush(dpy)
            shift_down = sh
        wait_until(t)
        press(kc, True)
        x11.XFlush(dpy)
        log.write("k %d %d\n" % (idx, time.monotonic_ns()))
        n += 1
        if held is not None:          # the previous key released while this one is down
            wait_until(held[1])
            press(held[0], False)
            x11.XFlush(dpy)
            held = None
        rollover = nxt is not None and nxt[2] == sh and nxt[1] != kc and rnd.random() < roll
        if rollover:
            overlap = min(0.100, max(0.005, rnd.gauss(ov_mean, ov_sd)))
            held = (kc, t + gap + overlap)
        else:
            hold = min(0.080, gap * 0.5)
            wait_until(t + hold)
            press(kc, False)
            x11.XFlush(dpy)
        t += gap
        k += 1
    if held is not None:
        wait_until(held[1])
        press(held[0], False)
    if shift_down:
        press(shift_kc, False)
        shift_down = False
    x11.XFlush(dpy)

    # the burst: as fast as the X server takes the keys
    time.sleep(1.5)
    b = 0
    while k < len(keys) and b < a.burst:
        idx, kc, sh = keys[k]
        if sh != shift_down:
            press(shift_kc, sh)
            shift_down = sh
        press(kc, True)
        x11.XFlush(dpy)
        log.write("b %d %d\n" % (idx, time.monotonic_ns()))
        press(kc, False)
        x11.XFlush(dpy)
        b += 1
        k += 1
    if shift_down:
        press(shift_kc, False)
    x11.XSync(dpy, 0)
    log.write("end %d\n" % (n + b))
    log.close()
    x11.XCloseDisplay(dpy)
    return 0


if __name__ == "__main__":
    sys.exit(main())
