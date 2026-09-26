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


main = u.FindWindowW('LlamaLauncherMainWindow', None)
assert main, 'no main window'
dpi = u.GetDpiForWindow(main)
px = lambda v: v * dpi // 96
r = wt.RECT()
u.GetClientRect(main, ctypes.byref(r))
cw, ch = r.right, r.bottom
T = r'C:\Users\y1974\AppData\Local\Temp'

# select config 1 -> detail view
click(main, px(132), px(48 + 12 + 32 + 6 + 32 + 6 + 29))
time.sleep(1.0)

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
im.save(T + r'\d_full.jpg', quality=95)
# command card region (top of content) and the grouped params below it
oy = 52  # title bar height measured before
im.crop((int(cw * 0.25), oy + px(48 + 24), cw, oy + px(48 + 24 + 240))).save(
    T + r'\d_cmd.jpg', quality=95)
im.crop((int(cw * 0.25), oy + px(48 + 24 + 240), cw, oy + px(48 + 24 + 240 + 420))).save(
    T + r'\d_groups.jpg', quality=95)
print('ok')
