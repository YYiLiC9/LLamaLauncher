import ctypes, ctypes.wintypes as wt, time

u = ctypes.WinDLL('user32')
u.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))

main = u.FindWindowW('LlamaLauncherMainWindow', None)
assert main, 'no main window'
dpi = u.GetDpiForWindow(main)
px = lambda v: v * dpi // 96
r = wt.RECT()
u.GetClientRect(main, ctypes.byref(r))
cw = r.right


def click(h, x, y):
    lp = ((y & 0xFFFF) << 16) | (x & 0xFFFF)
    u.PostMessageW(h, 0x0201, 1, lp)
    time.sleep(0.06)
    u.PostMessageW(h, 0x0202, 0, lp)


# select config 1 -> detail
click(main, px(132), px(48 + 12 + 32 + 6 + 32 + 6 + 29))
time.sleep(0.8)
# copy button: top-right of the command card. The header (name card) is ~ px(118)
# tall starting at content top px(48+24); the command card's copy button sits at
# its title row. From the screenshots: title row of 命令行 card ~ y=490 client.
# Measure precisely: scan for it is overkill; use the screenshot-derived position.
click(main, int(cw - px(24) - px(60)), int(px(48 + 24 + 118 + 14)))
time.sleep(0.5)

if u.OpenClipboard(0):
    h = u.GetClipboardData(13)  # CF_UNICODETEXT
    if h:
        p = u.GlobalLock(h)
        s = ctypes.wstring_at(p) if p else ''
        u.GlobalUnlock(h)
        print('CLIPBOARD LEN:', len(s))
        print('CLIPBOARD:', s)
    else:
        print('no text on clipboard')
    u.CloseClipboard()
else:
    print('clipboard open failed')
