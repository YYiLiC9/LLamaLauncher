// ui/webview.cpp - WebView2 host window.
//
// The controller bounds are expressed relative to the host window's client area
// (the window handed to CreateCoreWebView2Controller), so the browser always
// exactly fills the pane no matter where the pane itself sits.
#include "ui/webview.h"

#include <objbase.h>
#include <wrl.h>

#include <string>

#include "WebView2.h"
#include "WebView2EnvironmentOptions.h"
#include "core/i18n.h"
#include "core/paths.h"
#include "core/util.h"
#include "ui/shell.h"
#include "ui/theme.h"

using Microsoft::WRL::Callback;
using Microsoft::WRL::Make;

namespace web {

namespace {

// Fallback location for the browser's profile. The app passes the real one
// (under its data root) so everything stays in a single folder.
std::wstring defaultUserDataDir() {
    return util::joinPath(paths::dataRoot(), L"webview");
}

// Appends one line to logs\webview.log.
//
// WebView2 fails quietly: a bad user-data folder or a browser that refuses to
// start just means the pane stays blank, with nothing on screen to say why.
// This is the one place that can say what actually happened.
void wvLog(const std::wstring& line) {
    static int entries = 0;
    if (entries > 200) return;   // do not let a failure loop fill the disk
    ++entries;

    std::wstring dir = paths::logDir();
    util::ensureDir(dir);
    std::wstring stamped =
        util::format(L"[%s] %s\r\n", util::timestampForDisplay().c_str(), line.c_str());
    std::wstring existing = util::readTextFile(util::joinPath(dir, L"webview.log"));
    // Keep the tail so the file cannot grow without bound.
    const size_t kMax = 8000;
    if (existing.size() > kMax) existing.erase(0, existing.size() - kMax);
    util::writeTextFile(util::joinPath(dir, L"webview.log"), existing + stamped);
}

// The browser arguments passed to every WebView2 environment.
//
// The crash reporter is switched off deliberately. Chromium's crashpad handler
// spawns CrashSender.exe, and when it fails - which it does whenever the app
// was killed rather than closed, leaving a stale crashpad database behind - it
// puts up a modal "error launching CrashSender.exe" box on top of the window.
// The user cannot act on that box and it blocks the app, so the reporter is
// simply never started.
constexpr wchar_t kBrowserArgs[] =
    L"--disable-crash-reporter --disable-breakpad --noerrdialogs";

// Posted to the host once the controller handle exists; the actual
// initialisation runs from that message (see WebView::completeInit).
constexpr UINT kInitMessage = WM_APP + 0x2C4;

}  // namespace

const wchar_t* WebView::hostClass() { return L"LlamaLauncherWebHost"; }

WebView::~WebView() { destroy(); }

bool WebView::runtimeAvailable() {
    // WebView2 publishes the installed runtime version under this client id.
    // Its absence is the signal that the app must fall back to system browser.
    static const wchar_t* kKeys[] = {
        L"SOFTWARE\\WOW6432Node\\Microsoft\\EdgeUpdate\\Clients\\"
        L"{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}",
        L"SOFTWARE\\Microsoft\\EdgeUpdate\\Clients\\"
        L"{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}",
    };
    for (const wchar_t* key : kKeys) {
        HKEY h = nullptr;
        if (::RegOpenKeyExW(HKEY_LOCAL_MACHINE, key, 0, KEY_READ, &h) == ERROR_SUCCESS) {
            wchar_t version[64]{};
            DWORD size = sizeof(version);
            DWORD type = 0;
            LSTATUS st = ::RegQueryValueExW(h, L"pv", nullptr, &type, (BYTE*)version, &size);
            ::RegCloseKey(h);
            if (st == ERROR_SUCCESS && version[0] != L'\0') return true;
        }
    }
    return false;
}

bool WebView::create(HWND parent, const std::wstring& userDataDir, const std::wstring& startUrl) {
    if (host_) {
        // Re-entering the chat view: reuse the live browser and just retarget it.
        navigate(startUrl);
        return true;
    }
    parent_ = parent;
    pendingUrl_ = startUrl;
    failed_ = false;

    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = &WebView::HostProc;
        wc.hInstance = ::GetModuleHandleW(nullptr);
        wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;
        wc.lpszClassName = hostClass();
        if (!::RegisterClassExW(&wc)) return false;
        registered = true;
    }

    host_ = ::CreateWindowExW(0, hostClass(), L"", WS_CHILD | WS_CLIPCHILDREN, 0, 0, 10, 10,
                             parent_, nullptr, ::GetModuleHandleW(nullptr), this);
    if (!host_) return false;

    if (!runtimeAvailable()) {
        failed_ = true;
        wvLog(L"WebView2 runtime not present in the registry");
        notifyOwner();
        return true;   // the pane exists so the caller can show the fallback text
    }

    std::wstring dataDir = userDataDir.empty() ? defaultUserDataDir() : userDataDir;
    util::ensureDir(dataDir);

    // The SDK's own options object: it fills in the required browser-version
    // match (which a hand-rolled implementation gets wrong and the loader then
    // rejects with E_INVALIDARG) and lets us add browser arguments on top.
    auto options = Make<CoreWebView2EnvironmentOptions>();
    options->put_AdditionalBrowserArguments(kBrowserArgs);
    // Language follows the UI so pages asking for it match the rest of the app.
    options->put_Language(i18n::current() == Lang::Zh ? L"zh-CN" : L"en-US");

    HRESULT hr = ::CreateCoreWebView2EnvironmentWithOptions(
        nullptr, dataDir.c_str(), options.Get(),
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [this](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
                if (FAILED(result) || !env) {
                    failed_ = true;
                    hr_ = result;
                    wvLog(util::format(L"environment creation failed, hr=0x%08X",
                                       (unsigned)result));
                    notifyOwner();
                    return S_OK;
                }
                wvLog(L"environment created");
                // Invoke() does NOT give us a reference: the object is only
                // guaranteed alive for the duration of the callback. Storing
                // the raw pointer without AddRef left a dangling pointer, and
                // every later call through it (put_Bounds, put_IsVisible, ...)
                // was use-after-free - the chat-view crash.
                env->AddRef();
                environment_ = env;

                return env->CreateCoreWebView2Controller(
                    host_,
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [this](HRESULT r2, ICoreWebView2Controller* controller) -> HRESULT {
                            if (FAILED(r2) || !controller) {
                                failed_ = true;
                                hr_ = r2;
                                wvLog(util::format(
                                    L"controller creation failed, hr=0x%08X", (unsigned)r2));
                                notifyOwner();
                                return S_OK;
                            }
                            wvLog(L"controller created");
                            // Same ownership rule as the environment above.
                            controller_ = controller;
                            controller->AddRef();
                            // The controller is NOT touched here. Calling
                            // put_IsVisible / put_Bounds / Navigate while the
                            // callback is still completing crashed with an
                            // access violation inside WebView::setBounds
                            // whenever the UI resized or left the chat view in
                            // that window (the whole chat open/close path
                            // calls setBounds). The actual initialisation
                            // runs from a posted message instead - normal
                            // message-loop context, after the callback has
                            // fully returned.
                            if (host_)
                                ::PostMessageW(host_, kInitMessage, 0, 0);
                            return S_OK;
                        })
                        .Get());
            })
            .Get());

    if (FAILED(hr)) {
        failed_ = true;
        hr_ = hr;
        wvLog(util::format(L"CreateCoreWebView2EnvironmentWithOptions returned 0x%08X",
                           (unsigned)hr));
        notifyOwner();
    }
    return true;
}

void WebView::settings() {
    if (!core_) return;
    ICoreWebView2* core = (ICoreWebView2*)core_;

    ICoreWebView2Settings* s = nullptr;
    if (SUCCEEDED(core->get_Settings(&s)) && s) {
        s->put_IsScriptEnabled(TRUE);
        s->put_AreDefaultContextMenusEnabled(FALSE);
        s->put_IsStatusBarEnabled(FALSE);
        s->put_AreDevToolsEnabled(FALSE);
        s->put_IsZoomControlEnabled(TRUE);
        s->Release();
    }

    // The app is per-monitor DPI aware, so the browser has to be told the scale
    // explicitly or it renders at 100% on a scaled display.
    if (controller_) {
        ICoreWebView2Controller3* c3 = nullptr;
        if (SUCCEEDED(((ICoreWebView2Controller*)controller_)
                          ->QueryInterface(IID_ICoreWebView2Controller3, (void**)&c3)) &&
            c3) {
            double scale = theme::M.scale();
            c3->put_RasterizationScale(scale > 0 ? scale : 1.0);
            c3->put_ShouldDetectMonitorScaleChanges(FALSE);
            c3->Release();
        }
    }
}

void WebView::applyBounds() {
    // Gate on initialised_: before the deferred init ran, the controller
    // object exists but is not safe to call into (that call is exactly where
    // the chat open/close crash happened).
    if (!host_ || !controller_ || !initialised_) return;
    RECT rc{};
    ::GetClientRect(host_, &rc);
    // Bounds are relative to the window that owns the controller, so they are
    // simply the host's own client rectangle.
    ((ICoreWebView2Controller*)controller_)->put_Bounds(rc);
}

void WebView::setBounds(const RECT& bounds) {
    if (!host_) return;
    ::SetWindowPos(host_, nullptr, bounds.left, bounds.top, bounds.right - bounds.left,
                   bounds.bottom - bounds.top, SWP_NOZORDER | SWP_NOACTIVATE);
    applyBounds();
}

void WebView::setVisible(bool visible) {
    visible_ = visible;
    // The controller's IsVisible is only ever switched ON, and only once the
    // deferred init is done. On/off for the user is done by showing/hiding the
    // host window, which takes the browser's child windows with it and is
    // safe at any point of the initialisation.
    if (visible && controller_ && initialised_)
        ((ICoreWebView2Controller*)controller_)->put_IsVisible(TRUE);
    if (host_) {
        ::ShowWindow(host_, visible ? SW_SHOW : SW_HIDE);
        ::InvalidateRect(host_, nullptr, FALSE);
    }
}

void WebView::navigate(const std::wstring& url) {
    pendingUrl_ = url;
    // Before the init step the browser must not be touched; completeInit
    // navigates to pendingUrl_ once it is safe.
    if (core_ && initialised_) ((ICoreWebView2*)core_)->Navigate(url.c_str());
}

void WebView::reload() {
    if (core_ && initialised_) ((ICoreWebView2*)core_)->Reload();
}

// Runs from the posted init message: the controller callback has fully
// returned, so the object can now be called into.
void WebView::completeInit() {
    if (!controller_ || initialised_) return;
    ((ICoreWebView2Controller*)controller_)->get_CoreWebView2((ICoreWebView2**)&core_);
    initialised_ = true;
    // Presentation is always on once the controller exists; on/off for the
    // user is host-window visibility (see setVisible).
    ((ICoreWebView2Controller*)controller_)->put_IsVisible(TRUE);
    settings();
    applyBounds();
    if (!pendingUrl_.empty() && core_)
        ((ICoreWebView2*)core_)->Navigate(pendingUrl_.c_str());
    wvLog(L"controller initialised");
    notifyOwner();
}

void WebView::destroy() {
    // Close the controller first so the browser's child processes exit cleanly
    // instead of lingering as orphans.
    if (controller_) {
        ((ICoreWebView2Controller*)controller_)->Close();
        ((IUnknown*)controller_)->Release();
        controller_ = nullptr;
    }
    if (core_) {
        ((IUnknown*)core_)->Release();
        core_ = nullptr;
    }
    if (environment_) {
        ((IUnknown*)environment_)->Release();
        environment_ = nullptr;
    }
    if (host_) {
        HWND h = host_;
        host_ = nullptr;
        ::DestroyWindow(h);
    }
    parent_ = nullptr;
    initialised_ = false;
}

void WebView::notifyOwner() {
    if (host_) ::InvalidateRect(host_, nullptr, FALSE);
    if (notify_) notify_(notifyContext_);
}

LRESULT CALLBACK WebView::HostProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    WebView* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = (CREATESTRUCTW*)lp;
        self = (WebView*)cs->lpCreateParams;
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
        if (self) self->host_ = hwnd;
    } else {
        self = (WebView*)::GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    }
    if (self) {
        LRESULT out = 0;
        self->onHostMessage(msg, wp, lp, out);
        if (out) return out;
    }
    return ::DefWindowProcW(hwnd, msg, wp, lp);
}

void WebView::onHostMessage(UINT msg, WPARAM wp, LPARAM lp, LRESULT& out) {
    (void)wp;
    (void)lp;
    out = 0;
    switch (msg) {
        case kInitMessage:
            // Deferred controller initialisation - see the creation callback.
            completeInit();
            return;

        case WM_ERASEBKGND:
            out = 1;
            return;

        case WM_PAINT: {
            // Only ever visible before the browser paints, or when it could not
            // start at all - it shows the loading / failure notice.
            PAINTSTRUCT ps{};
            HDC dc = ::BeginPaint(host_, &ps);
            RECT rc{};
            ::GetClientRect(host_, &rc);
            if (rc.right > rc.left && rc.bottom > rc.top) {
                HDC mem = ::CreateCompatibleDC(dc);
                HBITMAP bmp = ::CreateCompatibleBitmap(dc, rc.right, rc.bottom);
                HGDIOBJ oldBmp = ::SelectObject(mem, bmp);

                HBRUSH bg = ::CreateSolidBrush(theme::LayerBg);
                ::FillRect(mem, &rc, bg);
                ::DeleteObject(bg);

                std::wstring text;
                COLORREF col = theme::TextSecondary;
                if (failed_) {
                    text = T(Str::WebViewFailed);
                    col = theme::Warning;
                } else {
                    text = T(Str::WebViewLoading);
                }

                // Centred both ways: this is a placeholder, not body copy, so it
                // should read as a state rather than as content.
                int boxW = std::min<long>(rc.right - theme::M.px(48), theme::M.px(420));
                int boxH = theme::M.px(60);
                RECT textRc{(rc.right - boxW) / 2, (rc.bottom - boxH) / 2,
                            (rc.right - boxW) / 2 + boxW, (rc.bottom - boxH) / 2 + boxH};
                ::SetBkMode(mem, TRANSPARENT);
                ::SetTextColor(mem, col);
                HGDIOBJ oldFont = ::SelectObject(mem, theme::fontBody());
                ::DrawTextW(mem, text.c_str(), (int)text.size(), &textRc,
                            DT_CENTER | DT_VCENTER | DT_WORDBREAK | DT_NOPREFIX);

                ::BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
                ::SelectObject(mem, oldFont);
                ::SelectObject(mem, oldBmp);
                ::DeleteObject(bmp);
                ::DeleteDC(mem);
            }
            ::EndPaint(host_, &ps);
            out = 1;
            return;
        }

        case WM_SIZE:
            applyBounds();
            return;

        default:
            return;
    }
}

}  // namespace web
