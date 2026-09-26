import ctypes, ctypes.wintypes as wt, time

u = ctypes.WinDLL('user32')
u.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))

main = u.FindWindowW('LlamaLauncherMainWindow', None)
assert main, 'no main window'
dpi = u.GetDpiForWindow(main)
px = lambda v: v * dpi // 96


def click(h, x, y):
    lp = ((y & 0xFFFF) << 16) | (x & 0xFFFF)
    u.PostMessageW(h, 0x0201, 1, lp)
    time.sleep(0.06)
    u.PostMessageW(h, 0x0202, 0, lp)


# select config 1 -> detail
click(main, px(132), px(48 + 12 + 32 + 6 + 32 + 6 + 29))
time.sleep(0.8)
# narrow the window (same as the screenshot the button coords came from)
wr = wt.RECT()
u.GetWindowRect(main, ctypes.byref(wr))
u.SetWindowPos(main, None, wr.left, wr.top, int(1150 * dpi / 96), wr.bottom - wr.top, 0x0004)
time.sleep(0.8)
# copy button centre, from the narrow screenshot (window px 990,236 -> client)
click(main, 982, 184)
time.sleep(0.5)

if u.OpenClipboard(0):
    u.GetClipboardData.restype = ctypes.c_void_p
    h = u.GetClipboardData(13)
    if h:
        k = ctypes.WinDLL('kernel32')
        k.GlobalLock.restype = ctypes.c_void_p
        k.GlobalLock.argtypes = [ctypes.c_void_p]
        k.GlobalUnlock.argtypes = [ctypes.c_void_p]
        p = k.GlobalLock(ctypes.c_void_p(h))
        s = ctypes.wstring_at(p) if p else ''
        k.GlobalUnlock(h)
        print('LEN:', len(s))
        print('CMD:', s)
    else:
        print('no text on clipboard')
    u.CloseClipboard()
else:
    print('clipboard open failed')
