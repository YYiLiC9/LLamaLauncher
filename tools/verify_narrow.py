import ctypes, ctypes.wintypes as wt, time
from PIL import Image

u = ctypes.WinDLL('user32')
u.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))
g = ctypes.WinDLL('gdi32')

main = u.FindWindowW('LlamaLauncherMainWindow', None)
assert main, 'no main window'
dpi = u.GetDpiForWindow(main)
px = lambda v: v * dpi // 96


def click(h, x, y):
    lp = ((y & 0xFFFF) << 16) | (x & 0xFFFF)
    u.PostMessageW(h, 0x0201, 1, lp)
    time.sleep(0.06)
    u.PostMessageW(h, 0x0202, 0, lp)


def snap(path):
    wr = wt.RECT()
    u.GetWindowRect(main, ctypes.byref(wr))
    w, hh = wr.right - wr.left, wr.bottom - wr.top
    hdc = u.GetWindowDC(main)
    mem = g.CreateCompatibleDC(hdc)
    bmp = g.CreateCompatibleBitmap(hdc, w, hh)
    old = g.SelectObject(mem, bmp)
    u.PrintWindow(main, mem, 2)

    class BMIH(ctypes.Structure):
        _fields_ = [("s", wt.DWORD), ("w", wt.LONG), ("h2", wt.LONG), ("p", wt.USHORT),
                    ("b", wt.USHORT), ("c", wt.DWORD), ("i", wt.DWORD), ("x", wt.LONG),
                    ("y", wt.LONG), ("cu", wt.DWORD), ("ci", wt.DWORD)]

    bmi = BMIH()
    bmi.s = ctypes.sizeof(BMIH)
    bmi.w = w
    bmi.h2 = -hh
    bmi.p = 1
    bmi.b = 32
    buf = ctypes.create_string_buffer(w * hh * 4)
    g.GetDIBits(mem, bmp, 0, hh, buf, ctypes.byref(bmi), 0)
    g.SelectObject(mem, old)
    g.DeleteObject(bmp)
    g.DeleteDC(mem)
    u.ReleaseDC(main, hdc)
    Image.frombuffer('RGB', (w, hh), bytes(buf), 'raw', 'BGRX', 0, 1).convert('RGB').save(path, quality=95)


# select config 1 -> detail
click(main, px(132), px(48 + 12 + 32 + 6 + 32 + 6 + 29))
time.sleep(0.8)
# shrink the window to ~1150 logical px wide: the command must re-wrap into
# many lines; the card height adapts at paint time (DT_CALCRECT)
wr = wt.RECT()
u.GetWindowRect(main, ctypes.byref(wr))
u.SetWindowPos(main, None, wr.left, wr.top, int(1150 * dpi / 96), wr.bottom - wr.top,
               0x0004)  # SWP_NOZORDER
time.sleep(0.8)
snap(r'C:\Users\y1974\AppData\Local\Temp\n narrow.jpg'.replace(' ', ''))
u.SetWindowPos(main, None, wr.left, wr.top, wr.right - wr.left, wr.bottom - wr.top, 0x0004)
time.sleep(0.4)
print('ok')
