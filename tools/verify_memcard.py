"""verify_memcard.py - message-driven check of the two-bar capacity card.

Drives the app with PostMessage only (no focus stealing, no physical cursor)
and captures with PrintWindow, so it is safe to run while the machine is in
use. Expects a fake llama-server to be configured (tools/fake_server.cpp) and
a single configuration left in the store so the first sidebar row is the one
under test.

Steps: select the first configuration -> press 启动 -> wait for the log line
and a few monitor ticks -> screenshot the running view.
"""
import ctypes
import ctypes.wintypes as wt
import os
import subprocess
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
PW_RENDERFULLCONTENT = 0x00000002
MK_LBUTTON = 0x0001

EXE = r"D:\Documents\workbuddy\llamacpp-gui-project\build\bin\LlamaLauncher.exe"
SHOT = os.path.join(os.environ.get("TEMP", r"C:\Windows\Temp"), "memcard.png")


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

    class BMIH(ctypes.Structure):
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


def main():
    main_h = find("LlamaLauncherMainWindow")
    if not main_h:
        print("starting launcher")
        subprocess.Popen([EXE])
        for _ in range(40):
            time.sleep(0.5)
            main_h = find("LlamaLauncherMainWindow")
            if main_h:
                break
    if not main_h:
        print("FAIL: main window not found")
        return 1
    time.sleep(1.5)

    _, _, cw, ch = client_rect(main_h)
    dpi = user32.GetDpiForWindow(main_h)
    px = lambda v: v * dpi // 96
    print(f"main={main_h} client={cw}x{ch} dpi={dpi}")

    # sidebar: first configuration row
    # list.y = topBar(48) + padding(12) + 32 + 6 + 32 + 6 = px(136)
    click(main_h, px(132), px(136) + px(29))
    time.sleep(0.8)

    # detail header: 启动 button, right-aligned in the header card.
    # Right-to-left: 删除(96) gap(8) 修改(96) gap(8) 启动(104), inset 18.
    start_x = cw - px(24) - px(18) - px(96) - px(8) - px(96) - px(8) - px(104) + px(52)
    start_y = px(114)
    print(f"click start at ({start_x},{start_y})")
    click(main_h, start_x, start_y)
    # Real models need time to reach the GPU; give the monitor a few ticks too.
    time.sleep(float(os.environ.get("WAIT", "150")))

    ok, size = capture(main_h, SHOT)
    print(f"capture {SHOT} ok={ok} size={size}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
