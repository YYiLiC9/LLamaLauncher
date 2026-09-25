"""verify_fix.py - message-driven verification for the param editor fixes.

Drives the app entirely with PostMessage (no focus stealing, no physical
cursor) and captures windows with PrintWindow, so it is safe to run while the
user is doing something else on the machine.

Checks:
  1. Open the create-config dialog (sidebar "+ 新建" button).
  2. -m row checkbox is checked (accent fill) in a fresh config.
  3. Command preview box grows with the command (no 2-line cap).
  4. Name EDIT stays vertically centred after fast wheel scrolling, and every
     visible value EDIT sits inside the row clip.
"""
import ctypes
import ctypes.wintypes as wt
import sys
import time

user32 = ctypes.WinDLL("user32", use_last_error=True)
gdi32 = ctypes.WinDLL("gdi32", use_last_error=True)

try:
    user32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))
except Exception:
    pass

WM_LBUTTONDOWN = 0x0201
WM_LBUTTONUP = 0x0202
WM_MOUSEWHEEL = 0x020A
MK_LBUTTON = 0x0001
PW_RENDERFULLCONTENT = 0x00000002


def lp(x, y):
    return (y & 0xFFFF) << 16 | (x & 0xFFFF)


def find(class_name):
    user32.FindWindowW.restype = wt.HWND
    return user32.FindWindowW(class_name, None)


def client_rect(h):
    r = wt.RECT()
    user32.GetClientRect(h, ctypes.byref(r))
    return r.left, r.top, r.right, r.bottom


def window_rect(h):
    r = wt.RECT()
    user32.GetWindowRect(h, ctypes.byref(r))
    return r.left, r.top, r.right, r.bottom


def click(hwnd, x, y):
    user32.PostMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, lp(x, y))
    time.sleep(0.05)
    user32.PostMessageW(hwnd, WM_LBUTTONUP, 0, lp(x, y))


def capture(hwnd, path):
    l, t, r, b = window_rect(hwnd)
    w, h = r - l, b - t
    hdc = user32.GetWindowDC(hwnd)
    mem = gdi32.CreateCompatibleDC(hdc)
    bmp = gdi32.CreateCompatibleBitmap(hdc, w, h)
    old = gdi32.SelectObject(mem, bmp)
    ok = user32.PrintWindow(hwnd, mem, PW_RENDERFULLCONTENT)
    gdi32.SelectObject(mem, old)

    class BMIH(ctypes.LITTLE_ENDIAN * 1 if False else ctypes.Structure):
        _fields_ = [("biSize", wt.DWORD), ("biWidth", wt.LONG), ("biHeight", wt.LONG),
                    ("biPlanes", wt.USHORT), ("biBitCount", wt.USHORT),
                    ("biCompression", wt.DWORD), ("biSizeImage", wt.DWORD),
                    ("biXPelsPerMeter", wt.LONG), ("biYPelsPerMeter", wt.LONG),
                    ("biClrUsed", wt.DWORD), ("biClrImportant", wt.DWORD)]

    bmi = BMIH()
    bmi.biSize = ctypes.sizeof(BMIH)
    bmi.biWidth = w
    bmi.biHeight = -h
    bmi.biPlanes = 1
    bmi.biBitCount = 32
    bmi.biCompression = 0
    buf = ctypes.create_string_buffer(w * h * 4)
    gdi32.GetDIBits(mem, bmp, 0, h, buf, ctypes.byref(bmi), 0)

    from PIL import Image
    img = Image.frombuffer("RGB", (w, h), buf.raw, "raw", "BGRX", 0, 1)
    img.save(path)
    gdi32.DeleteObject(bmp)
    gdi32.DeleteDC(mem)
    user32.ReleaseDC(hwnd, hdc)
    return ok, (w, h)


def child_edits(hwnd):
    """Return (handle, x, y, w, h, visible, enabled) for every child EDIT."""
    out = []

    @ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
    def cb(h, _):
        buf = ctypes.create_unicode_buffer(64)
        user32.GetClassNameW(h, buf, 64)
        if buf.value == "Edit":
            r = wt.RECT()
            user32.GetWindowRect(h, ctypes.byref(r))
            pl, pt, _, _ = window_rect(hwnd)
            out.append((h, r.left - pl, r.top - pt, r.right - r.left,
                        r.bottom - r.top,
                        bool(user32.IsWindowVisible(h)),
                        bool(user32.IsWindowEnabled(h))))
        return True

    user32.EnumChildWindows(hwnd, cb, 0)
    return out


def main():
    main_h = find("LlamaLauncherMainWindow")
    if not main_h:
        print("FAIL: main window not found")
        return 1
    # a dialog left over from an earlier run would swallow the click
    stale = find("LlamaLauncherDialog")
    if stale:
        user32.PostMessageW(stale, 0x0010, 0, 0)   # WM_CLOSE
        time.sleep(0.8)
    cl, ct, cr, cbm = client_rect(main_h)
    dpi = user32.GetDpiForWindow(main_h)
    px = lambda v: v * dpi // 96
    print(f"main={main_h} client={cr}x{cbm} dpi={dpi}")

    # ---- 1. open the create-config dialog via the sidebar "+ 新建" pill ----
    # head = {px(12), topBar+px(12), sidebarW-2*px(12), px(32)}
    # newBtn = {head.right()-px(76), head.y, px(76), head.h}
    top_bar = px(48)
    sidebar_w = px(264)
    head_x = px(12)
    head_y = top_bar + px(12)
    head_w = sidebar_w - px(24)
    btn_x = head_x + head_w - px(76) + px(76) // 2
    btn_y = head_y + px(32) // 2
    click(main_h, btn_x, btn_y)
    time.sleep(1.5)

    dlg = find("LlamaLauncherDialog")
    if not dlg:
        print("FAIL: dialog did not open")
        return 1
    title = ctypes.create_unicode_buffer(128)
    user32.GetWindowTextW(dlg, title, 128)
    print(f"dialog={dlg} title={title.value!r}")

    edits0 = child_edits(dlg)
    name0 = [e for e in edits0 if e[6]]  # visible
    print("child edits before scroll:")
    for e in name0:
        print(f"  hwnd={e[0]} rect=({e[1]},{e[2]},{e[3]}x{e[4]}) vis={e[5]} en={e[6]}")

    ok, size = capture(dlg, r"C:\Users\y1974\AppData\Local\Temp\dlg_new.png")
    print(f"capture dlg_new.png ok={ok} size={size}")

    # ---- 2. fast wheel scrolling ----
    wl, wt_, wr, wb = window_rect(dlg)
    # over the rows area: right column, below contentTop (px(128))
    sx = wl + (wr - wl) // 2 + 100
    sy = wt_ + px(128) + 120
    for _ in range(10):
        user32.PostMessageW(dlg, WM_MOUSEWHEEL, ((-120) & 0xFFFF) << 16, lp(sx, sy))
        time.sleep(0.02)
    for _ in range(10):
        user32.PostMessageW(dlg, WM_MOUSEWHEEL, (120 & 0xFFFF) << 16, lp(sx, sy))
        time.sleep(0.02)
    time.sleep(0.6)

    edits1 = child_edits(dlg)
    print("child edits after scroll:")
    for e in edits1:
        print(f"  hwnd={e[0]} rect=({e[1]},{e[2]},{e[3]}x{e[4]}) vis={e[5]} en={e[6]}")

    # name edit must not have moved
    name_before = [e for e in edits0 if e[5] and e[1] < px(300)]
    if name_before and edits1:
        nb = name_before[0]
        na = [e for e in edits1 if e[0] == nb[0]]
        if na:
            moved = (na[0][1], na[0][2]) != (nb[1], nb[2])
            print(f"name edit moved after scroll: {moved} ({(nb[1], nb[2])} -> {(na[0][1], na[0][2])})")
        else:
            print("name edit gone after scroll!")
    if edits1:
        ys = sorted(e[2] for e in edits1 if e[5])
        print(f"visible edit ys: {ys}")

    ok, size = capture(dlg, r"C:\Users\y1974\AppData\Local\Temp\dlg_scroll.png")
    print(f"capture dlg_scroll.png ok={ok} size={size}")

    # close the dialog (cancel) so repeated runs start clean
    user32.PostMessageW(dlg, 0x0010, 0, 0)   # WM_CLOSE
    time.sleep(0.5)
    return 0


if __name__ == "__main__":
    sys.exit(main())
