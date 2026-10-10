/*
MIT License

Copyright (c) 2026 wisp-code@proton.me

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include "webview.h"

#if defined(WEBVIEW_WEBVIEW2)

#include <windows.h>
#include <objbase.h>
#include <WebView2.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <new>

/* ------------------------------------------------------------------ */
/*  Minimal IUnknown implementation for the three handler interfaces.  */
/* ------------------------------------------------------------------ */

#define IMPL_IUNKNOWN(Iface)                                              \
  ULONG ref_count_ = 1;                                                   \
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) override { \
    if (!ppv) return E_POINTER;                                           \
    if (riid == __uuidof(IUnknown) || riid == __uuidof(Iface)) {          \
      *ppv = static_cast<Iface *>(this);                                  \
      AddRef();                                                           \
      return S_OK;                                                        \
    }                                                                     \
    *ppv = nullptr;                                                       \
    return E_NOINTERFACE;                                                 \
  }                                                                       \
  ULONG STDMETHODCALLTYPE AddRef() override {                             \
    return (ULONG)InterlockedIncrement((LONG *)&ref_count_);              \
  }                                                                       \
  ULONG STDMETHODCALLTYPE Release() override {                            \
    ULONG n = (ULONG)InterlockedDecrement((LONG *)&ref_count_);           \
    if (n == 0) delete this;                                              \
    return n;                                                             \
  }

/* ------------------------------------------------------------------ */
/*  Forward decls                                                      */
/* ------------------------------------------------------------------ */

static HRESULT create_controller_for(struct webview *w);
static void    send_str_to_js(struct webview *w, const char *s);

/* ------------------------------------------------------------------ */
/*  Handler: environment ready                                          */
/* ------------------------------------------------------------------ */

class EnvReady : public ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler {
public:
  IMPL_IUNKNOWN(ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler)

  struct webview *w = nullptr;

  HRESULT STDMETHODCALLTYPE Invoke(HRESULT hr,
                                   ICoreWebView2Environment *env) override {
    if (FAILED(hr) || env == nullptr) {
      fprintf(stderr, "[webview2] environment creation failed: 0x%08lx\n",
              (unsigned long)hr);
      if (w) w->priv.should_exit = 1;
      return S_OK;
    }
    w->priv.env = env;
    env->AddRef();
    create_controller_for(w);
    return S_OK;
  }
};

/* ------------------------------------------------------------------ */
/*  Handler: web message received (JS → host)                           */
/* ------------------------------------------------------------------ */

class WebMsg : public ICoreWebView2WebMessageReceivedEventHandler {
public:
  IMPL_IUNKNOWN(ICoreWebView2WebMessageReceivedEventHandler)

  struct webview *w = nullptr;

  HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2 *sender,
                                   ICoreWebView2WebMessageReceivedEventArgs *args) override {
    (void)sender;
    LPWSTR wide = nullptr;
    if (FAILED(args->TryGetWebMessageAsString(&wide)) || wide == nullptr) {
      return S_OK;
    }
    int n = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (n > 0) {
      char *utf8 = (char *)malloc((size_t)n);
      if (utf8) {
        WideCharToMultiByte(CP_UTF8, 0, wide, -1, utf8, n, nullptr, nullptr);
        if (w && w->external_invoke_cb) {
          w->external_invoke_cb(w, utf8);
        }
        free(utf8);
      }
    }
    CoTaskMemFree(wide);
    return S_OK;
  }
};

/* ------------------------------------------------------------------ */
/*  Handler: controller ready                                           */
/* ------------------------------------------------------------------ */

class CtrlReady : public ICoreWebView2CreateCoreWebView2ControllerCompletedHandler {
public:
  IMPL_IUNKNOWN(ICoreWebView2CreateCoreWebView2ControllerCompletedHandler)

  struct webview *w = nullptr;

  HRESULT STDMETHODCALLTYPE Invoke(HRESULT hr,
                                   ICoreWebView2Controller *controller) override {
    if (FAILED(hr) || controller == nullptr) {
      fprintf(stderr, "[webview2] controller creation failed: 0x%08lx\n",
              (unsigned long)hr);
      w->priv.should_exit = 1;
      return S_OK;
    }
    w->priv.controller = controller;
    controller->AddRef();

    ICoreWebView2 *wv = nullptr;
    hr = controller->get_CoreWebView2(&wv);
    if (FAILED(hr) || wv == nullptr) {
      fprintf(stderr, "[webview2] get_CoreWebView2 failed: 0x%08lx\n",
              (unsigned long)hr);
      w->priv.should_exit = 1;
      return S_OK;
    }
    w->priv.webview = wv;
    wv->AddRef();

    /* Fit the webview to the client area and make it visible. */
    RECT rc;
    GetClientRect(w->priv.hwnd, &rc);
    controller->put_Bounds(rc);
    controller->put_IsVisible(TRUE);

    /* Compatibility shim: relay's INJECT_JS talks to the host through
     * window.webkit.messageHandlers.external.postMessage(). Define
     * that on top of the WebView2-native chrome.webview.postMessage. */
    static const wchar_t *compat =
        L"if (window.chrome && window.chrome.webview) {"
        L"  window.webkit = window.webkit || {};"
        L"  window.webkit.messageHandlers = window.webkit.messageHandlers || {};"
        L"  window.webkit.messageHandlers.external = {"
        L"    postMessage: function(x) { window.chrome.webview.postMessage(x); }"
        L"  };"
        L"}";
    wv->AddScriptToExecuteOnDocumentCreated(compat, nullptr);

    /* Register the WebMessageReceived handler so JS → host messages
     * reach external_invoke_cb. Without this, the chooser's Connect
     * button posts into the void. */
    {
      WebMsg *h = new (std::nothrow) WebMsg();
      if (h) {
        h->w = w;
        EventRegistrationToken tok = {};
        wv->add_WebMessageReceived(h, &tok);
        h->Release();
      }
    }

    /* Navigate to the initial URL. Without this the webview stays on
     * about:blank and the window renders as an empty black page. */
    {
      const char *url = (w->url && w->url[0]) ? w->url : DEFAULT_URL;
      int n = MultiByteToWideChar(CP_UTF8, 0, url, -1, nullptr, 0);
      if (n > 0) {
        wchar_t *wide = (wchar_t *)malloc((size_t)n * sizeof(wchar_t));
        if (wide) {
          MultiByteToWideChar(CP_UTF8, 0, url, -1, wide, n);
          fprintf(stderr, "[webview2] navigating to %.80s\n", url);
          wv->Navigate(wide);
          free(wide);
        }
      } else {
        fprintf(stderr, "[webview2] MultiByteToWideChar failed for url\n");
      }
    }

    w->priv.ready = 1;
    return S_OK;
  }
};

/* ------------------------------------------------------------------ */
/*  Window procedure                                                    */
/* ------------------------------------------------------------------ */

static const TCHAR *kClassName = TEXT("RelayWebView2");

static LRESULT CALLBACK wv2_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  struct webview *w =
      (struct webview *)GetWindowLongPtr(hwnd, GWLP_USERDATA);

  switch (msg) {
    case WM_CREATE: {
      CREATESTRUCT *cs = (CREATESTRUCT *)lp;
      w = (struct webview *)cs->lpCreateParams;
      SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)w);
      w->priv.hwnd = hwnd;
      return 0;
    }
    case WM_SIZE: {
      if (w && w->priv.controller) {
        RECT rc = {0, 0, LOWORD(lp), HIWORD(lp)};
        ((ICoreWebView2Controller *)w->priv.controller)->put_Bounds(rc);
      }
      return 0;
    }
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
    case WM_CLOSE:
      if (w) w->priv.should_exit = 1;
      DestroyWindow(hwnd);
      return 0;
  }
  return DefWindowProc(hwnd, msg, wp, lp);
}

/* ------------------------------------------------------------------ */
/*  Pump messages until w->priv.ready is set, or the window dies.       */
/* ------------------------------------------------------------------ */

static void pump_until_ready(struct webview *w) {
  MSG msg;
  while (!w->priv.ready && !w->priv.should_exit) {
    if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
      if (msg.message == WM_QUIT) {
        w->priv.should_exit = 1;
        break;
      }
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    } else {
      Sleep(1);
    }
  }
}

/* ------------------------------------------------------------------ */
/*  Internal helpers                                                    */
/* ------------------------------------------------------------------ */

static HRESULT create_controller_for(struct webview *w) {
  ICoreWebView2Environment *env = (ICoreWebView2Environment *)w->priv.env;
  CtrlReady *h = new (std::nothrow) CtrlReady();
  if (!h) return E_OUTOFMEMORY;
  h->w = w;
  HRESULT hr = env->CreateCoreWebView2Controller(w->priv.hwnd, h);
  h->Release();
  return hr;
}

static void send_str_to_js(struct webview *w, const char *s) {
  if (!w->priv.webview) return;
  ICoreWebView2 *wv = (ICoreWebView2 *)w->priv.webview;
  int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
  if (n <= 0) return;
  wchar_t *wide = (wchar_t *)malloc((size_t)n * sizeof(wchar_t));
  if (!wide) return;
  MultiByteToWideChar(CP_UTF8, 0, s, -1, wide, n);
  wv->ExecuteScript(wide, nullptr);
  free(wide);
}

/* ------------------------------------------------------------------ */
/*  Public API                                                          */
/* ------------------------------------------------------------------ */

WEBVIEW_API int webview_init(struct webview *w) {
  w->priv.ready = 0;
  w->priv.should_exit = 0;
  w->priv.env = nullptr;
  w->priv.controller = nullptr;
  w->priv.webview = nullptr;

  HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
    fprintf(stderr, "[webview2] CoInitializeEx failed: 0x%08lx\n",
            (unsigned long)hr);
    return -1;
  }

  HINSTANCE hInst = GetModuleHandle(nullptr);

  WNDCLASSEX wc = {0};
  wc.cbSize = sizeof(wc);
  wc.hInstance = hInst;
  wc.lpfnWndProc = wv2_wndproc;
  wc.lpszClassName = kClassName;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
  RegisterClassEx(&wc);

  DWORD style = WS_OVERLAPPEDWINDOW;
  if (!w->resizable) {
    style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
  }

  RECT rc = {0, 0, w->width, w->height};
  AdjustWindowRect(&rc, style, FALSE);

  HWND hwnd = CreateWindowEx(
      0, kClassName, w->title ? w->title : "relay", style,
      CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top,
      nullptr, nullptr, hInst, (void *)w);
  if (!hwnd) {
    fprintf(stderr, "[webview2] CreateWindowEx failed: %lu\n", GetLastError());
    return -1;
  }

  ShowWindow(hwnd, SW_SHOWDEFAULT);
  UpdateWindow(hwnd);

  EnvReady *h = new (std::nothrow) EnvReady();
  if (!h) return -1;
  h->w = w;

  wchar_t user_data[MAX_PATH];
  wchar_t local_app[MAX_PATH];
  if (GetEnvironmentVariableW(L"LOCALAPPDATA", local_app, MAX_PATH)) {
    swprintf(user_data, MAX_PATH, L"%ls\\wisp-relay\\WebView2", local_app);
  } else {
    wcscpy(user_data, L"wisp-relay-WebView2");
  }

  hr = CreateCoreWebView2EnvironmentWithOptions(
      nullptr, user_data, nullptr, h);
  h->Release();

  if (FAILED(hr)) {
    fprintf(stderr, "[webview2] CreateCoreWebView2EnvironmentWithOptions "
                    "failed: 0x%08lx\n", (unsigned long)hr);
    fprintf(stderr, "[webview2] Is the WebView2 Runtime installed? "
                    "See https://developer.microsoft.com/microsoft-edge/webview2/\n");
    return -1;
  }

  pump_until_ready(w);

  return w->priv.should_exit ? -1 : 0;
}

WEBVIEW_API int webview_loop(struct webview *w, int blocking) {
  MSG msg;
  if (blocking) {
    /* Bounded wait instead of GetMessage. Windows stops posting to a
     * minimized / fully occluded window's queue, so a bare GetMessage
     * would block this thread forever and starve the caller's
     * pump_pending_prompt() — the relay would never call ExecuteScript
     * on the shim until the user clicked the window. MsgWaitForMultiple
     * Objects with a 20 ms cap wakes on any input or timer message, and
     * otherwise returns on the timeout so the caller can run its own
     * cross-thread work. */
    DWORD r = MsgWaitForMultipleObjects(0, nullptr, FALSE, 20, QS_ALLINPUT);
    if (r == WAIT_OBJECT_0) {
      while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) return -1;
        TranslateMessage(&msg);
        DispatchMessage(&msg);
      }
    }
    /* WAIT_TIMEOUT: fall through and let the caller pump. */
  } else {
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
      if (msg.message == WM_QUIT) return -1;
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    }
  }
  return w->priv.should_exit ? -1 : 0;
}


WEBVIEW_API int webview_eval(struct webview *w, const char *js) {
  if (!w->priv.ready) pump_until_ready(w);
  if (!w->priv.webview) return -1;
  send_str_to_js(w, js);
  return 0;
}

WEBVIEW_API int webview_add_init_script(struct webview *w, const char *js) {
  if (!w->priv.ready) pump_until_ready(w);
  if (!w->priv.webview) return -1;
  ICoreWebView2 *wv = (ICoreWebView2 *)w->priv.webview;
  int n = MultiByteToWideChar(CP_UTF8, 0, js, -1, nullptr, 0);
  if (n <= 0) return -1;
  wchar_t *wide = (wchar_t *)malloc((size_t)n * sizeof(wchar_t));
  if (!wide) return -1;
  MultiByteToWideChar(CP_UTF8, 0, js, -1, wide, n);
  HRESULT hr = wv->AddScriptToExecuteOnDocumentCreated(wide, nullptr);
  free(wide);
  return SUCCEEDED(hr) ? 0 : -1;
}

WEBVIEW_API int webview_inject_css(struct webview *w, const char *css) {
  char buf[4096];
  snprintf(buf, sizeof(buf),
           "(function(){var s=document.createElement('style');"
           "s.textContent=%s;document.head.appendChild(s);})()",
           css);
  return webview_eval(w, buf);
}

WEBVIEW_API void webview_set_title(struct webview *w, const char *title) {
  SetWindowTextA(w->priv.hwnd, title);
}

WEBVIEW_API void webview_set_fullscreen(struct webview *w, int fullscreen) {
  (void)w; (void)fullscreen;
}

WEBVIEW_API void webview_set_color(struct webview *w, uint8_t r, uint8_t g,
                                   uint8_t b, uint8_t a) {
  (void)w; (void)r; (void)g; (void)b; (void)a;
}

WEBVIEW_API void webview_dialog(struct webview *w,
                                enum webview_dialog_type dlgtype, int flags,
                                const char *title, const char *arg,
                                char *result, size_t resultsz) {
  (void)w; (void)dlgtype; (void)flags;
  if (result && resultsz) result[0] = '\0';
  MessageBoxA(w->priv.hwnd, arg, title, MB_OK);
}

WEBVIEW_API void webview_dispatch(struct webview *w, webview_dispatch_fn fn,
                                  void *arg) {
  (void)w; (void)fn; (void)arg;
}

WEBVIEW_API void webview_terminate(struct webview *w) {
  w->priv.should_exit = 1;
  PostQuitMessage(0);
}

WEBVIEW_API void webview_exit(struct webview *w) {
  if (w->priv.webview)    ((ICoreWebView2 *)w->priv.webview)->Release();
  if (w->priv.controller) ((ICoreWebView2Controller *)w->priv.controller)->Release();
  if (w->priv.env)        ((ICoreWebView2Environment *)w->priv.env)->Release();
  w->priv.webview = w->priv.controller = w->priv.env = nullptr;
  CoUninitialize();
}

WEBVIEW_API void webview_print_log(const char *s) {
  OutputDebugStringA(s);
}

WEBVIEW_API void webview_debug(const char *fmt, ...) {
  char buf[2048];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  OutputDebugStringA(buf);
}

#endif  /* WEBVIEW_WEBVIEW2 */