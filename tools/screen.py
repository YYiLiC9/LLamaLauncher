"""screen.py - capture a window straight off the screen.

PrintWindow cannot see WebView2 content: the browser composites through
DirectComposition, so a window-DC capture returns only what the host window
itself painted. Grabbing the screen region does capture it.

Usage: python screen.py <out.png> [--class NAME] [--pad N]
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
    pad = 0
    args = sys.argv[2:]
    i = 0
    while i < len(args):
        if args[i] == "--class":
            cls = args[i + 1]; i += 2
        elif args[i] == "--pad":
            pad = int(args[i + 1]); i += 2
        else:
            i += 1

    hwnd = user32.FindWindowW(cls, None)
    if not hwnd:
        sys.exit("window not found")

    user32.ShowWindow(hwnd, 5)

    # Raising the window is not enough: SetForegroundWindow from a background
    # process is blocked by Windows, so whatever happens to be on top still wins
    # the screen grab. Making it topmost for the duration of the capture is the
    # reliable way to guarantee the pixels belong to this window.
    user32.SetWindowPos(hwnd, -1, 0, 0, 0, 0, 0x0001 | 0x0002 | 0x0010)   # HWND_TOPMOST
    time.sleep(1.2)

    cr = RECT()
    if not user32.GetClientRect(hwnd, ctypes.byref(cr)) or cr.r <= 0:
        sys.exit("degenerate client rect")

    pt = wt.POINT(0, 0)
    if not user32.ClientToScreen(hwnd, ctypes.byref(pt)):
        sys.exit("ClientToScreen failed")

    box = (pt.x + pad, pt.y + pad, pt.x + cr.r - pad, pt.y + cr.b - pad)
    img = ImageGrab.grab(bbox=box)
    img.save(out)

    user32.SetWindowPos(hwnd, -2, 0, 0, 0, 0, 0x0001 | 0x0002 | 0x0010)   # HWND_NOTOPMOST
    print(f"OK screen {img.size[0]}x{img.size[1]} -> {out}")


if __name__ == "__main__":
    main()