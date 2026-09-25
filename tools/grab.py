"""grab.py - capture a window exactly as it appears, without touching it.

Every other capture helper here nudges the window first (topmost, foreground,
frame-changed). Those nudges force DWM to re-composite, which *repairs* the very
kind of first-paint bug this file exists to catch - so a screenshot taken that
way can look fine while the user sees a broken window.

This one only reads: find the window, wait, grab the screen rectangle.

Usage: python grab.py <out.png> [--class NAME] [--wait SECONDS]
"""
import ctypes
import ctypes.wintypes as wt
import sys
import time

from PIL import ImageGrab

user32 = ctypes.WinDLL("user32", use_last_error=True)


class RECT(ctypes.Structure):
    _fields_ = [("l", ctypes.c_long), ("t", ctypes.c_long),
                ("r", ctypes.c_long), ("b", ctypes.c_long)]


def main():
    try:
        user32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))
    except Exception:
        pass

    out = sys.argv[1]
    cls = "LlamaLauncherMainWindow"
    wait = 3.0
    args = sys.argv[2:]
    i = 0
    while i < len(args):
        if args[i] == "--class":
            cls = args[i + 1]; i += 2
        elif args[i] == "--wait":
            wait = float(args[i + 1]); i += 2
        else:
            i += 1

    time.sleep(wait)

    hwnd = user32.FindWindowW(cls, None)
    if not hwnd:
        sys.exit("window not found")

    fg = user32.GetForegroundWindow()
    r = RECT()
    user32.GetWindowRect(hwnd, ctypes.byref(r))

    # Read-only from here on: no ShowWindow, no SetWindowPos, no foreground.
    img = ImageGrab.grab(bbox=(r.l, r.t, r.r, r.b))
    img.save(out)
    print(f"OK grab {img.size[0]}x{img.size[1]} -> {out} "
          f"(foreground={'yes' if fg == hwnd else 'no'})")


if __name__ == "__main__":
    main()