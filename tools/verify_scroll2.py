import ctypes, ctypes.wintypes as wt, time
from PIL import Image

u = ctypes.WinDLL('user32')
u.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))
g = ctypes.WinDLL('gdi32')

main = u.FindWindowW('LlamaLauncherMainWindow', None)
assert main, 'no main window'


def wheel(n):
    lp = ((400 & 0xFFFF) << 16) | (1200 & 0xFFFF)
    for _ in range(n):
        u.PostMessageW(main, 0x020A, ((-120 & 0xFFFF) << 16), lp)
        time.sleep(0.1)


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


wheel(10)
time.sleep(0.5)
# nudge the window size by 1px to force a real WM_SIZE -> full repaint,
# defeating PrintWindow's stale-frame cache
wr = wt.RECT()
u.GetWindowRect(main, ctypes.byref(wr))
u.SetWindowPos(main, None, 0, 0, wr.right - wr.left + 1, wr.bottom - wr.top,
               0x0004 | 0x0001)  # SWP_NOZORDER | SWP_NOMOVE
time.sleep(0.6)
snap(r'C:\Users\y1974\AppData\Local\Temp\d_scrollA.jpg')
u.SetWindowPos(main, None, 0, 0, wr.right - wr.left, wr.bottom - wr.top,
               0x0004 | 0x0001)
time.sleep(0.4)
print('ok')
