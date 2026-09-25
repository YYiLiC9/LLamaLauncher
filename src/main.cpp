// main.cpp - entry point.
//
// The whole application is a single window plus a handful of modal dialogs, so
// the entry point only has to make sure there is exactly one instance, set up
// the apartment the embedded browser needs, hand control to the App shell, and
// clean up afterwards.
#include <windows.h>

#include <objbase.h>

#include "app.h"
#include "ui/theme.h"

namespace {

// A second copy would fight the first over the same settings and config files,
// so instead of that the running window is brought forward.
bool claimSingleInstance() {
    ::CreateMutexW(nullptr, FALSE, L"Local\\LlamaLauncher.SingleInstance");
    if (::GetLastError() != ERROR_ALREADY_EXISTS) return true;

    HWND existing = ::FindWindowW(app::kWindowClass, nullptr);
    if (existing) {
        if (::IsIconic(existing)) ::ShowWindow(existing, SW_RESTORE);
        ::SetForegroundWindow(existing);
    }
    return false;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR, int) {
    if (!claimSingleInstance()) return 0;

    // Per-monitor DPI awareness is declared in the manifest; this is the belt
    // and braces path for hosts that ignore it.
    ::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // A single-threaded apartment: WebView2 requires it, and the file dialogs
    // and shell APIs used elsewhere are happy with it too.
    HRESULT comHr = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED |
                                                  COINIT_DISABLE_OLE1DDE);

    app::App& shell = app::App::instance();
    if (!shell.init(inst)) {
        ::MessageBoxW(nullptr, L"Failed to create the main window.", L"LlamaLauncher",
                      MB_ICONERROR | MB_OK);
        if (SUCCEEDED(comHr)) ::CoUninitialize();
        return 1;
    }

    int code = shell.run();
    theme::shutdown();
    if (SUCCEEDED(comHr)) ::CoUninitialize();
    return code;
}