import ctypes, ctypes.wintypes as wt, time
from PIL import Image

u = ctypes.WinDLL('user32')
u.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))
g = ctypes.WinDLL('gdi32')


def click(h, x, y):
    lp = ((y & 0xFFFF) << 16) | (x & 0xFFFF)
    u.PostMessageW(h, 0x0201, 1, lp)
    time.sleep(0.05)
    u.PostMessageW(h, 0x0202, 0, lp)


def snap(path, box=None):
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
    im = Image.frombuffer('RGB', (w, hh), bytes(buf), 'raw', 'BGRX', 0, 1).convert('RGB')
    if box:
        im = im.crop(box)
    im.save(path, quality=95)


main = u.FindWindowW('LlamaLauncherMainWindow', None)
assert main, 'no main window'
dpi = u.GetDpiForWindow(main)
px = lambda v: v * dpi // 96
r = wt.RECT()
u.GetClientRect(main, ctypes.byref(r))
cw, ch = r.right, r.bottom
T = r'C:\Users\y1974\AppData\Local\Temp'

# detail page: command card + only-enabled sections
click(main, px(132), px(48 + 12 + 32 + 6 + 32 + 6 + 29))
time.sleep(0.8)
snap(T + r'\q_detail.jpg', box=(int(cw * 0.25), 0, cw, int(ch * 0.65)))

# stop latency: enter Running via ball, then SendMessage-click the stop button
click(main, cw - px(50), ch - px(30) - px(50))
time.sleep(1.0)
row_y = int(px(48 + 24 + 88 - 18 - 17))
chat_x2 = cw - px(24) - px(18)
stop_cx = int(chat_x2 - px(10) - px(50 + 5 * 37) - px(10) - (px(50) + 4 * 37) // 2)
t0 = time.perf_counter()
lp = ((row_y & 0xFFFF) << 16) | (stop_cx & 0xFFFF)
u.SendMessageW(main, 0x0201, 1, lp)
u.SendMessageW(main, 0x0202, 0, lp)
dt = (time.perf_counter() - t0) * 1000
print(f'stop dispatch took {dt:.0f} ms')
time.sleep(0.6)
snap(T + r'\q_after_stop.jpg', box=(int(cw * 0.55), 0, cw, int(ch * 0.28)))
print('alive:', bool(u.IsWindow(main)))
