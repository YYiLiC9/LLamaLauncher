"""shot.py - capture a window by class name or handle, using ctypes + Pillow.

Usage:
    python shot.py <out.png> [--hwnd N] [--class NAME] [--front] [--click X,Y]

The window is grabbed with PrintWindow so the capture works even when the
window is partially covered; we fall back to a GDI BitBlt of the screen rect
if PrintWindow refuses.
"""
import ctypes
import ctypes.wintypes as wt
import sys
import time

user32 = ctypes.WinDLL("user32", use_last_error=True)
gdi32 = ctypes.WinDLL("gdi32", use_last_error=True)

PW_RENDERFULLCONTENT = 0x00000002
SRCCOPY = 0x00CC0020


def make_dpi_aware():
    """Become per-monitor DPI aware.

    Without this, Windows virtualises every rectangle this process sees, so a
    window on a 175%-scaled display reports as ~57% of its real size and the
    capture comes out cropped and mislabelled.
    """
    try:
        # DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 == (HANDLE)-4
        if user32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4)):
            return "per-monitor-v2"
    except Exception:
        pass
    try:
        shcore = ctypes.WinDLL("shcore", use_last_error=True)
        if shcore.SetProcessDpiAwareness(2) == 0:   # PROCESS_PER_MONITOR_DPI_AWARE
            return "per-monitor"
    except Exception:
        pass
    if user32.SetProcessDPIAware():
        return "system"
    return "unaware"


class RECT(ctypes.Structure):
    _fields_ = [("left", ctypes.c_long), ("top", ctypes.c_long),
                ("right", ctypes.c_long), ("bottom", ctypes.c_long)]


def find_window(class_name=None, title=None):
    hwnd = user32.FindWindowW(class_name, title)
    return hwnd or None


def window_rect(hwnd):
    r = RECT()
    if not user32.GetWindowRect(hwnd, ctypes.byref(r)):
        raise OSError("GetWindowRect failed")
    return r


def capture(hwnd, out_path):
    r = window_rect(hwnd)
    w, h = r.right - r.left, r.bottom - r.top
    if w <= 0 or h <= 0:
        raise OSError(f"degenerate window size {w}x{h}")

    hdc_win = user32.GetWindowDC(hwnd)
    hdc_mem = gdi32.CreateCompatibleDC(hdc_win)
    hbmp = gdi32.CreateCompatibleBitmap(hdc_win, w, h)
    gdi32.SelectObject(hdc_mem, hbmp)

    ok = user32.PrintWindow(hwnd, hdc_mem, PW_RENDERFULLCONTENT)

    from PIL import Image
    if ok:
        # Pull the pixels out of the DIB we just rendered into.
        class BITMAPINFOHEADER(ctypes.Structure):
            _fields_ = [("biSize", wt.DWORD), ("biWidth", ctypes.c_long),
                        ("biHeight", ctypes.c_long), ("biPlanes", wt.WORD),
                        ("biBitCount", wt.WORD), ("biCompression", wt.DWORD),
                        ("biSizeImage", wt.DWORD), ("biXPelsPerMeter", ctypes.c_long),
                        ("biYPelsPerMeter", ctypes.c_long), ("biClrUsed", wt.DWORD),
                        ("biClrImportant", wt.DWORD)]

        bmi = BITMAPINFOHEADER()
        bmi.biSize = ctypes.sizeof(BITMAPINFOHEADER)
        bmi.biWidth = w
        bmi.biHeight = -h          # negative => top-down rows
        bmi.biPlanes = 1
        bmi.biBitCount = 32
        bmi.biCompression = 0      # BI_RGB

        buf = ctypes.create_string_buffer(w * h * 4)
        got = gdi32.GetDIBits(hdc_mem, hbmp, 0, h, buf, ctypes.byref(bmi), 0)
        if got:
            img = Image.frombuffer("RGBA", (w, h), buf, "raw", "BGRA", 0, 1)
            img.convert("RGB").save(out_path)
            gdi32.DeleteObject(hbmp)
            gdi32.DeleteDC(hdc_mem)
            user32.ReleaseDC(hwnd, hdc_win)
            return "printwindow", w, h

    # Fallback: blit straight off the screen.
    hdc_screen = user32.GetDC(0)
    gdi32.BitBlt(hdc_mem, 0, 0, w, h, hdc_screen, r.left, r.top, SRCCOPY)
    user32.ReleaseDC(0, hdc_screen)

    buf = ctypes.create_string_buffer(w * h * 4)
    # Re-read through GetDIBits with the same top-down header.
    class BIH(ctypes.Structure):
        _fields_ = [("biSize", wt.DWORD), ("biWidth", ctypes.c_long),
                    ("biHeight", ctypes.c_long), ("biPlanes", wt.WORD),
                    ("biBitCount", wt.WORD), ("biCompression", wt.DWORD),
                    ("biSizeImage", wt.DWORD), ("biXPelsPerMeter", ctypes.c_long),
                    ("biYPelsPerMeter", ctypes.c_long), ("biClrUsed", wt.DWORD),
                    ("biClrImportant", wt.DWORD)]

    bmi = BIH()
    bmi.biSize = ctypes.sizeof(BIH)
    bmi.biWidth = w
    bmi.biHeight = -h
    bmi.biPlanes = 1
    bmi.biBitCount = 32
    gdi32.GetDIBits(hdc_mem, hbmp, 0, h, buf, ctypes.byref(bmi), 0)

    from PIL import Image
    img = Image.frombuffer("RGBA", (w, h), buf, "raw", "BGRA", 0, 1)
    img.convert("RGB").save(out_path)

    gdi32.DeleteObject(hbmp)
    gdi32.DeleteDC(hdc_mem)
    user32.ReleaseDC(hwnd, hdc_win)
    return "bitblt", w, h


def main():
    aware = make_dpi_aware()
    out = sys.argv[1]
    hwnd = 0
    cls = "LlamaLauncherMainWindow"
    front = False
    click = None

    args = sys.argv[2:]
    i = 0
    while i < len(args):
        if args[i] == "--hwnd":
            hwnd = int(args[i + 1]); i += 2
        elif args[i] == "--class":
            cls = args[i + 1]; i += 2
        elif args[i] == "--front":
            front = True; i += 1
        elif args[i] == "--click":
            click = tuple(int(v) for v in args[i + 1].split(",")); i += 2
        else:
            i += 1

    if not hwnd:
        hwnd = find_window(cls, None)
    if not hwnd:
        sys.exit(f"window not found: class={cls} hwnd={hwnd}")

    if front:
        user32.ShowWindow(hwnd, 5)   # SW_SHOW
        user32.SetForegroundWindow(hwnd)
        time.sleep(1.2)

    if click:
        x, y = click
        # Post the messages directly: this avoids fighting the foreground lock.
        lparam = (y << 16) | (x & 0xFFFF)
        user32.PostMessageW(hwnd, 0x0200, 0, lparam)   # WM_MOUSEMOVE
        time.sleep(0.15)
        user32.PostMessageW(hwnd, 0x0201, 1, lparam)   # WM_LBUTTONDOWN
        time.sleep(0.08)
        user32.PostMessageW(hwnd, 0x0202, 0, lparam)   # WM_LBUTTONUP
        time.sleep(0.9)

    how, w, h = capture(hwnd, out)
    print(f"OK {how} {w}x{h} -> {out} (dpi awareness: {aware})")


if __name__ == "__main__":
    main()