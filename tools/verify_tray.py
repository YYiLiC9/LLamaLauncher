import ctypes, ctypes.wintypes as wt, time, glob, os, subprocess
from PIL import Image

u = ctypes.WinDLL('user32')
u.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))
g = ctypes.WinDLL('gdi32')
T = r'C:\Users\y1974\AppData\Local\Temp'

# ---- 0. extract the icon from the exe ----
k = ctypes.WinDLL('kernel32')
exe = r'D:\Documents\workbuddy\llamacpp-gui-project\build\bin\LlamaLauncher.exe'
u.PrivateExtractIconsW.restype = ctypes.c_uint
u.PrivateExtractIconsW.argtypes = [wt.LPCWSTR, ctypes.c_int, ctypes.c_int, ctypes.c_int,
                                   ctypes.POINTER(ctypes.c_void_p), ctypes.POINTER(ctypes.c_uint),
                                   ctypes.c_uint, ctypes.c_uint]
hicon = (ctypes.c_void_p * 1)()
nid = (ctypes.c_uint * 1)()
n = u.PrivateExtractIconsW(exe, 0, 48, 48, hicon, nid, 1, 0)
if n:
    # render the icon onto a bitmap via DrawIconEx
    mem = g.CreateCompatibleDC(None)
    bmp = g.CreateCompatibleBitmap(g.CreateDCW('DISPLAY', None, None, None), 48, 48)
    g.SelectObject(mem, bmp)
    g.PatBlt(mem, 0, 0, 48, 48, 0x00F00021)  # whiteness
    u.DrawIconEx(mem, 0, 0, hicon[0], 48, 48, 0, None, 3)
    class BMIH(ctypes.Structure):
        _fields_ = [("s", wt.DWORD), ("w", wt.LONG), ("h2", wt.LONG), ("p", wt.USHORT),
                    ("b", wt.USHORT), ("c", wt.DWORD), ("i", wt.DWORD), ("x", wt.LONG),
                    ("y", wt.LONG), ("cu", wt.DWORD), ("ci", wt.DWORD)]
    bmi = BMIH(); bmi.s = ctypes.sizeof(BMIH); bmi.w = 48; bmi.h2 = -48; bmi.p = 1; bmi.b = 32
    buf = ctypes.create_string_buffer(48 * 48 * 4)
    g.GetDIBits(mem, bmp, 0, 48, buf, ctypes.byref(bmi), 0)
    Image.frombuffer('RGB', (48, 48), bytes(buf), 'raw', 'BGRX', 0, 1).save(T + r'\exe_icon.png')
    print('icon extracted, n =', n)
else:
    print('icon extraction FAILED')

main = u.FindWindowW('LlamaLauncherMainWindow', None)
assert main, 'no main window'
dpi = u.GetDpiForWindow(main)
px = lambda v: v * dpi // 96


def click(h, x, y):
    lp = ((y & 0xFFFF) << 16) | (x & 0xFFFF)
    u.PostMessageW(h, 0x0201, 1, lp)
    time.sleep(0.06)
    u.PostMessageW(h, 0x0202, 0, lp)


def snap_main(path, box=None):
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
    bmi = BMIH(); bmi.s = ctypes.sizeof(BMIH); bmi.w = w; bmi.h2 = -hh; bmi.p = 1; bmi.b = 32
    buf = ctypes.create_string_buffer(w * hh * 4)
    g.GetDIBits(mem, bmp, 0, hh, buf, ctypes.byref(bmi), 0)
    g.SelectObject(mem, old); g.DeleteObject(bmp); g.DeleteDC(mem); u.ReleaseDC(main, hdc)
    im = Image.frombuffer('RGB', (w, hh), bytes(buf), 'raw', 'BGRX', 0, 1).convert('RGB')
    if box:
        im = im.crop(box)
    im.save(path, quality=95)


def grab_dlg(dlg, path):
    wr = wt.RECT()
    u.GetWindowRect(dlg, ctypes.byref(wr))
    w, hh = wr.right - wr.left, wr.bottom - wr.top
    hdc = u.GetWindowDC(dlg)
    mem = g.CreateCompatibleDC(hdc)
    bmp = g.CreateCompatibleBitmap(hdc, w, hh)
    old = g.SelectObject(mem, bmp)
    u.PrintWindow(dlg, mem, 2)
    class BMIH(ctypes.Structure):
        _fields_ = [("s", wt.DWORD), ("w", wt.LONG), ("h2", wt.LONG), ("p", wt.USHORT),
                    ("b", wt.USHORT), ("c", wt.DWORD), ("i", wt.DWORD), ("x", wt.LONG),
                    ("y", wt.LONG), ("cu", wt.DWORD), ("ci", wt.DWORD)]
    bmi = BMIH(); bmi.s = ctypes.sizeof(BMIH); bmi.w = w; bmi.h2 = -hh; bmi.p = 1; bmi.b = 32
    buf = ctypes.create_string_buffer(w * hh * 4)
    g.GetDIBits(mem, bmp, 0, hh, buf, ctypes.byref(bmi), 0)
    g.SelectObject(mem, old); g.DeleteObject(bmp); g.DeleteDC(mem); u.ReleaseDC(dlg, hdc)
    Image.frombuffer('RGB', (w, hh), bytes(buf), 'raw', 'BGRX', 0, 1).convert('RGB').save(path, quality=95)


def close_dlg():
    dlg = u.FindWindowW('LlamaLauncherDialog', None)
    if dlg:
        u.PostMessageW(dlg, 0x0010, 0, 0)
        time.sleep(0.5)


# ---- 1. settings dialog: the new tray toggle ----
r = wt.RECT()
u.GetClientRect(main, ctypes.byref(r))
cw, ch = r.right, r.bottom
# gear button = leftmost of the top-bar tool group
bw, gap = px(84), px(4)
total = 4 * bw + 3 * gap
x_gear = cw - px(12) - total + bw // 2
click(main, x_gear, px(24))
time.sleep(1.0)
dlg = u.FindWindowW('LlamaLauncherDialog', None)
if dlg:
    grab_dlg(dlg, T + r'\settings_toggle.jpg')
    close_dlg()

# ---- 2. select the test config (last sidebar row), start the service ----
base = px(48 + 12 + 32 + 6 + 32 + 6 + 29)
# find the row whose caption is 长行换行验证: it was created last -> try the
# bottom-most visible rows; click each and check the header name via screenshot
# (cheaper: click the 8th row directly and verify by the log that appears)
click(main, px(132), base + px(61) * 7)
time.sleep(0.8)
click(main, 1512, 199)   # Start
time.sleep(3.0)

# adopt/service state: enter Running via the ball
click(main, cw - px(50), ch - px(30) - px(50))
time.sleep(1.0)

# ---- 3. open the log dialog: expect the 500-char line wrapped ----
# adaptive widths: min(最小化) log(服务日志) stop stop... with server running:
# row = [最小化][服务日志][停止服务][打开对话页面], right-aligned
chat_w = px(50 + 5 * 29)
stop_w = px(50 + 4 * 29)
log_w = px(50 + 4 * 29)
min_w = px(50 + 3 * 29)
row_y = int(px(48 + 24 + 88 - 18 - 17))
chat_x2 = cw - px(24) - px(18)
log_cx = int(chat_x2 - px(10) - chat_w - px(10) - stop_w - px(10) - log_w // 2)
click(main, log_cx, row_y)
time.sleep(1.5)
dlg = u.FindWindowW('LlamaLauncherDialog', None)
print('log dialog:', bool(dlg))
if dlg:
    grab_dlg(dlg, T + r'\log_wrapped.jpg')
    close_dlg()
time.sleep(0.5)

# ---- 4. WM_CLOSE with close_to_tray=true: window hides, process lives ----
u.PostMessageW(main, 0x0010, 0, 0)  # WM_CLOSE
time.sleep(0.8)
print('after WM_CLOSE: IsWindow =', bool(u.IsWindow(main)),
      '| visible =', bool(u.IsWindowVisible(main)))
out = subprocess.run(['tasklist'], capture_output=True).stdout.decode(errors='replace')
print('process alive:', 'LlamaLauncher.exe' in out)

# ---- 5. restore like the tray click / second instance would ----
main2 = u.FindWindowW('LlamaLauncherMainWindow', None)
u.ShowWindow(main2, 5)  # SW_SHOW
print('restored visible:', bool(u.IsWindowVisible(main2)))
