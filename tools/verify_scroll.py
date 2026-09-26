import ctypes, ctypes.wintypes as wt, time
from PIL import Image

u = ctypes.WinDLL('user32')
u.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))
g = ctypes.WinDLL('gdi32')

main = u.FindWindowW('LlamaLauncherMainWindow', None)
assert main, 'no main window'
dpi = u.GetDpiForWindow(main)
px = lambda v: v * dpi // 96


def wheel(delta, n=1):
    lp = ((300 & 0xFFFF) << 16) | (1000 & 0xFFFF)
    for _ in range(n):
        u.PostMessageW(main, 0x020A, ((delta & 0xFFFF) << 16), lp)
        time.sleep(0.12)


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
    Image.frombuffer('RGB', (w, hh), bytes(buf), 'raw', 'BGRX', 0, 1).convert('RGB').save(
        path, quality=95)


T = r'C:\Users\y1974\AppData\Local\Temp'
# scroll down to the GPU group (detail view scroll)
wheel(-120, 6)
time.sleep(0.6)
snap(T + r'\d_scroll1.jpg')
wheel(-120, 6)
time.sleep(0.6)
snap(T + r'\d_scroll2.jpg')
print('ok')
