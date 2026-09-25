"""diag.py - report window geometry and DPI for the running app."""
import ctypes
import ctypes.wintypes as wt
import sys

user32 = ctypes.WinDLL("user32", use_last_error=True)


def make_dpi_aware():
    """Per-monitor DPI awareness, so every rectangle below is real pixels."""
    try:
        if user32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4)):
            return "per-monitor-v2"
    except Exception:
        pass
    try:
        shcore = ctypes.WinDLL("shcore", use_last_error=True)
        if shcore.SetProcessDpiAwareness(2) == 0:
            return "per-monitor"
    except Exception:
        pass
    if user32.SetProcessDPIAware():
        return "system"
    return "unaware"


class RECT(ctypes.Structure):
    _fields_ = [("left", ctypes.c_long), ("top", ctypes.c_long),
                ("right", ctypes.c_long), ("bottom", ctypes.c_long)]


def main():
    aware = make_dpi_aware()

    # HANDLE-returning entry points must be typed, or ctypes truncates them to
    # 32 bits and every handle-derived query returns nonsense.
    user32.GetThreadDpiAwarenessContext.restype = ctypes.c_void_p
    user32.GetAwarenessFromDpiAwarenessContext.argtypes = [ctypes.c_void_p]
    user32.GetAwarenessFromDpiAwarenessContext.restype = ctypes.c_int

    cls = sys.argv[1] if len(sys.argv) > 1 else "LlamaLauncherMainWindow"
    hwnd = user32.FindWindowW(cls, None)
    if not hwnd:
        print("window not found")
        return

    wr, cr = RECT(), RECT()
    user32.GetWindowRect(hwnd, ctypes.byref(wr))
    user32.GetClientRect(hwnd, ctypes.byref(cr))

    dpi = user32.GetDpiForWindow(hwnd)
    gdpi = user32.GetDpiForSystem()
    sm_cx = user32.GetSystemMetrics(0)
    sm_cy = user32.GetSystemMetrics(1)

    ws, hs = wt.RECT(), wt.RECT()
    okw = user32.SystemParametersInfoW(0x0030, 0, ctypes.byref(ws), 0)   # SPI_GETWORKAREA
    work = f"{ws.right - ws.left}x{ws.bottom - ws.top}" if okw else "?"

    ctx = user32.GetThreadDpiAwarenessContext()
    amap = {-1: "INVALID", 0: "UNAWARE", 1: "SYSTEM_AWARE", 2: "PER_MONITOR_AWARE"}
    aware_proc = amap.get(user32.GetAwarenessFromDpiAwarenessContext(ctx), "?")

    print(f"this process   = {aware}")
    print(f"hwnd           = {hwnd}")
    print(f"window rect    = {wr.left},{wr.top} {wr.right - wr.left}x{wr.bottom - wr.top}")
    print(f"client rect    = {cr.right - cr.left}x{cr.bottom - cr.top}")
    print(f"GetDpiForWindow= {dpi}  (exact scale {dpi / 96.0:.4f})")
    print(f"GetDpiForSystem= {gdpi}")
    print(f"screen         = {sm_cx}x{sm_cy}")
    print(f"work area      = {work}")
    print(f"target process = {aware_proc}")


if __name__ == "__main__":
    main()