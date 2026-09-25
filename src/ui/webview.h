// ui/webview.h - an embedded browser pane for the llama-server chat page.
//
// The chat UI that ships with llama-server is a modern web app, so the only
// reasonable way to show it inside the launcher is the system WebView2 runtime
// (Edge). It is created lazily: nothing is spawned until the chat view is
// actually opened, and the browser processes are torn down with the window, so
// the launcher keeps its "zero cost when idle" property.
//
// If the runtime is missing the pane reports failure and the caller falls back
// to opening the page in the default browser.
#pragma once

#include <windows.h>

#include <string>

namespace web {

class WebView {
public:
    WebView() = default;
    ~WebView();

    WebView(const WebView&) = delete;
    WebView& operator=(const WebView&) = delete;

    // True when the WebView2 runtime is installed (cheap registry-ish probe).
    static bool runtimeAvailable();

    // Creates the host window as a child of `parent` and starts WebView2.
    // Initialisation is asynchronous; poll ready()/failed() to follow it.
    bool create(HWND parent, const std::wstring& userDataDir, const std::wstring& startUrl);

    void destroy();

    // Moves the host window; the browser follows automatically via WM_SIZE.
    void setBounds(const RECT& bounds);

    void navigate(const std::wstring& url);
    void reload();

    // WebView2 only presents while its controller is marked visible, which does
    // not follow the parent window automatically. Call this after showing or
    // hiding the pane.
    void setVisible(bool visible);

    bool ready() const { return core_ != nullptr; }
    bool failed() const { return failed_; }
    // Set while an initialisation is in flight but not finished.
    bool pending() const { return host_ != nullptr && !ready() && !failed_; }
    // Failure detail, for the status line. 0 when nothing failed.
    HRESULT lastError() const { return hr_; }

    HWND host() const { return host_; }

    // Called whenever the state changes so the app can repaint (loading text,
    // failure message, or the page itself).
    using Notify = void (*)(void* context);
    void setNotify(Notify notify, void* context) {
        notify_ = notify;
        notifyContext_ = context;
    }

private:
    void onHostMessage(UINT msg, WPARAM wp, LPARAM lp, LRESULT& out);
    void applyBounds();
    void settings();
    void completeInit();
    void notifyOwner();

    static LRESULT CALLBACK HostProc(HWND, UINT, WPARAM, LPARAM);
    static const wchar_t* hostClass();

    HWND host_ = nullptr;
    HWND parent_ = nullptr;

    // WebView2 objects. Held as void* so this header stays free of the SDK.
    void* environment_ = nullptr;
    void* controller_ = nullptr;
    void* core_ = nullptr;

    std::wstring pendingUrl_;
    bool failed_ = false;
    bool initialised_ = false;
    bool visible_ = false;
    // Last failure HRESULT, shown in the chat bar so a blank pane is diagnosable.
    HRESULT hr_ = S_OK;

    Notify notify_ = nullptr;
    void* notifyContext_ = nullptr;
};

}  // namespace web
