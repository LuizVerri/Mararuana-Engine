#include "Mararuana/Window.h"
#include <windowsx.h>   // GET_X_LPARAM/GET_Y_LPARAM — correctly sign-extend for multi-monitor negative coords
#include <mutex>
#include <algorithm>
#include <cstring>
#include <cstddef>

// Some SDKs / older MinGW headers don't declare the per-monitor-v2 DPI API
// surface even though it's resolved dynamically at runtime below. Only the
// *type* and the *sentinel constant* are needed at compile time (the actual
// function is never called through a static prototype); guard so this is a
// no-op wherever the real SDK already provides them.
#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
DECLARE_HANDLE(DPI_AWARENESS_CONTEXT);
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((DPI_AWARENESS_CONTEXT)-4)
#endif

namespace Mar {

    namespace {
        void EnableDPIAwareness() noexcept {
            static std::once_flag onceFlag;
            std::call_once(onceFlag, []() {
                HMODULE user32 = GetModuleHandleW(L"user32.dll");
                if (user32) {
                    using SetProcessDpiAwarenessContextFunc = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
                    auto pSetContext = reinterpret_cast<SetProcessDpiAwarenessContextFunc>(
                        reinterpret_cast<void*>(GetProcAddress(user32, "SetProcessDpiAwarenessContext")));
                    if (pSetContext && pSetContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
                        return;
                    }
                }
                SetProcessDPIAware();
                });
        }

        std::mutex g_ClassMutex;

        // Optional Windows 10+ per-window/per-system DPI API, resolved once
        // per process (function-local static init is thread-safe in C++11+)
        // rather than once per Window instance. Falls back gracefully to 96
        // DPI / AdjustWindowRect on older systems.
        struct DpiApi {
            using GetDpiForSystemFunc = UINT(WINAPI*)(VOID);
            using AdjustWindowRectExForDpiFunc = BOOL(WINAPI*)(LPRECT, DWORD, BOOL, DWORD, UINT);
            using GetDpiForWindowFunc = UINT(WINAPI*)(HWND);

            GetDpiForSystemFunc pGetDpiForSystem = nullptr;
            AdjustWindowRectExForDpiFunc pAdjustWindowRectExForDpi = nullptr;
            GetDpiForWindowFunc pGetDpiForWindow = nullptr;

            DpiApi() noexcept {
                if (HMODULE user32 = GetModuleHandleW(L"user32.dll")) {
                    pGetDpiForSystem = reinterpret_cast<GetDpiForSystemFunc>(
                        reinterpret_cast<void*>(GetProcAddress(user32, "GetDpiForSystem")));
                    pAdjustWindowRectExForDpi = reinterpret_cast<AdjustWindowRectExForDpiFunc>(
                        reinterpret_cast<void*>(GetProcAddress(user32, "AdjustWindowRectExForDpi")));
                    pGetDpiForWindow = reinterpret_cast<GetDpiForWindowFunc>(
                        reinterpret_cast<void*>(GetProcAddress(user32, "GetDpiForWindow")));
                }
            }
        };

        const DpiApi& GetDpiApi() noexcept {
            static const DpiApi api;
            return api;
        }
    }

    std::atomic<int> Window::s_ClassRefCount{ 0 };

    Window::Window(uint32_t width, uint32_t height, const wchar_t* title)
        : m_hInstance(GetModuleHandleW(nullptr))
        , m_Data(width, height)
        , m_CreationThreadId(GetCurrentThreadId())
    {
        EnableDPIAwareness();
        Init();

        if (m_hWnd && title) {
            SetWindowTextW(m_hWnd, title);
            // Initialize virtual cursor to center of window
            m_VirtualMouseX.store(static_cast<float>(width) / 2.0f, std::memory_order_relaxed);
            m_VirtualMouseY.store(static_cast<float>(height) / 2.0f, std::memory_order_relaxed);
        }
    }

    Window::~Window() { Close(); }

    bool Window::RegisterWindowClass(HINSTANCE hInstance) noexcept {
        std::lock_guard<std::mutex> lock(g_ClassMutex);
        if (s_ClassRefCount.load(std::memory_order_relaxed) == 0) {
            WNDCLASSEXW wcex = {};
            wcex.cbSize = sizeof(WNDCLASSEXW);
            wcex.lpfnWndProc = WndProc;
            wcex.hInstance = hInstance;
            wcex.lpszClassName = kClassName;
            wcex.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
            wcex.hIcon = LoadIconW(nullptr, reinterpret_cast<LPCWSTR>(IDI_WINLOGO));
            wcex.hIconSm = LoadIconW(nullptr, reinterpret_cast<LPCWSTR>(IDI_WINLOGO));
            wcex.hCursor = LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_ARROW));

            const ATOM classAtom = RegisterClassExW(&wcex);
            const bool ok = classAtom != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
            MAR_CORE_ASSERT(ok, "Window::RegisterWindowClass: RegisterClassExW failed.");
            if (!ok) return false;
        }
        s_ClassRefCount.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    void Window::UnregisterWindowClass(HINSTANCE hInstance) noexcept {
        std::lock_guard<std::mutex> lock(g_ClassMutex);
        if (s_ClassRefCount.fetch_sub(1, std::memory_order_relaxed) == 1) {
            UnregisterClassW(kClassName, hInstance);
        }
    }

    void Window::Init() noexcept {
        if (!RegisterWindowClass(m_hInstance)) return;

        constexpr DWORD style = WS_OVERLAPPEDWINDOW;
        constexpr DWORD exStyle = WS_EX_APPWINDOW | WS_EX_NOREDIRECTIONBITMAP;

        RECT rect;
        rect.left = 250;
        rect.top = 250;
        rect.right = rect.left + static_cast<LONG>(m_Data.width.load());
        rect.bottom = rect.top + static_cast<LONG>(m_Data.height.load());

        const DpiApi& dpiApi = GetDpiApi();
        bool adjusted = false;
        if (dpiApi.pGetDpiForSystem && dpiApi.pAdjustWindowRectExForDpi) {
            const UINT dpi = dpiApi.pGetDpiForSystem();
            dpiApi.pAdjustWindowRectExForDpi(&rect, style, FALSE, exStyle, dpi);
            adjusted = true;
        }
        if (!adjusted) {
            AdjustWindowRect(&rect, style, FALSE);
        }

        m_hWnd = CreateWindowExW(
            exStyle, kClassName, L"", style,
            rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
            nullptr, nullptr, m_hInstance, this
        );

        MAR_CORE_ASSERT(m_hWnd != nullptr, "Window::Init: CreateWindowExW failed.");
        if (!m_hWnd) return;

        m_Dpi.store(dpiApi.pGetDpiForWindow ? dpiApi.pGetDpiForWindow(m_hWnd) : 96, std::memory_order_relaxed);

        RegisterRawInput();
        DisableIME();
        ShowWindow(m_hWnd, SW_SHOW);
        // Cache the standard arrow cursor to avoid repeated LoadCursorW calls
        m_HCursor = LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_ARROW));
    }

    void Window::RegisterRawInput() const noexcept {
        RAWINPUTDEVICE rid = {};
        rid.usUsagePage = 0x01;
        rid.usUsage = 0x02;
        rid.dwFlags = 0; // foreground-only: standard, correct choice for gameplay input
        rid.hwndTarget = m_hWnd;
        const BOOL ok = RegisterRawInputDevices(&rid, 1, sizeof(rid));
        MAR_CORE_ASSERT(ok, "Window::RegisterRawInput: RegisterRawInputDevices failed.");
        (void)ok;
    }

    void Window::UnregisterRawInput() const noexcept {
        RAWINPUTDEVICE rid = {};
        rid.usUsagePage = 0x01;
        rid.usUsage = 0x02;
        rid.dwFlags = RIDEV_REMOVE;
        rid.hwndTarget = nullptr; // must be null for RIDEV_REMOVE
        RegisterRawInputDevices(&rid, 1, sizeof(rid));
    }

    void Window::DisableIME() const noexcept {
        if (HMODULE imm32 = LoadLibraryW(L"imm32.dll")) {
            using ImmDisableIMEFunc = BOOL(WINAPI*)(DWORD);
            auto pImmDisableIME = reinterpret_cast<ImmDisableIMEFunc>(
                reinterpret_cast<void*>(GetProcAddress(imm32, "ImmDisableIME")));
            if (pImmDisableIME) pImmDisableIME(static_cast<DWORD>(-1));
            FreeLibrary(imm32);
        }
    }

    void Window::ClipCursorToClient() noexcept {
        if (!m_hWnd) return;
        RECT rect;
        if (GetClientRect(m_hWnd, &rect)) {
            POINT tl{ rect.left, rect.top };
            POINT br{ rect.right, rect.bottom };
            ClientToScreen(m_hWnd, &tl);
            ClientToScreen(m_hWnd, &br);
            RECT screenRect{ tl.x, tl.y, br.x, br.y };
            ClipCursor(&screenRect);
        }
    }

    void Window::UpdateCursorClip() noexcept {
        // Disabled mode confines the cursor to the client rect, but only
        // while focused — Windows releases any clip automatically on focus
        // loss anyway, and re-clipping an unfocused window would fight the
        // window the user just switched to.
        if (m_CursorMode.load(std::memory_order_relaxed) == CursorMode::Disabled &&
            m_Data.isFocused.load(std::memory_order_relaxed)) {
            ClipCursorToClient();
        }
        else {
            ClipCursor(nullptr);
        }
    }

    void Window::SetCursorMode(CursorMode mode) noexcept {
        if (m_CursorMode.exchange(mode, std::memory_order_relaxed) == mode) return;

        // ShowCursor is a process-wide reference counter, not a boolean — call
        // it only on an actual transition, or repeated calls desync the count
        // and leave the cursor stuck shown or hidden.
        const bool wantHidden = (mode != CursorMode::Normal);
        if (wantHidden != m_CursorHiddenApplied) {
            ShowCursor(wantHidden ? FALSE : TRUE);
            m_CursorHiddenApplied = wantHidden;
        }
        UpdateCursorClip();
    }

    void Window::SetTitle(const wchar_t* title) noexcept {
        if (m_hWnd && title) {
            SetWindowTextW(m_hWnd, title);
        }
    }

    void Window::SetPosition(int x, int y) noexcept {
        if (m_hWnd) {
            SetWindowPos(m_hWnd, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }

    void Window::SetMinimumSize(uint32_t width, uint32_t height) noexcept {
        m_MinWidth.store(width, std::memory_order_relaxed);
        m_MinHeight.store(height, std::memory_order_relaxed);
    }

    void Window::SetMaximumSize(uint32_t width, uint32_t height) noexcept {
        m_MaxWidth.store(width, std::memory_order_relaxed);
        m_MaxHeight.store(height, std::memory_order_relaxed);
    }

    bool Window::SetFullscreen(bool enable) noexcept {
        if (!m_hWnd || enable == IsFullscreen()) return m_hWnd != nullptr;

        if (enable) {
            m_SavedPlacement.length = sizeof(WINDOWPLACEMENT);
            GetWindowPlacement(m_hWnd, &m_SavedPlacement);
            m_SavedStyle = GetWindowLongPtrW(m_hWnd, GWL_STYLE);
            m_SavedExStyle = GetWindowLongPtrW(m_hWnd, GWL_EXSTYLE);

            HMONITOR monitor = MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTONEAREST);
            MONITORINFO mi{};
            mi.cbSize = sizeof(MONITORINFO);
            if (!GetMonitorInfoW(monitor, &mi)) return false;

            SetWindowLongPtrW(m_hWnd, GWL_STYLE, m_SavedStyle & ~WS_OVERLAPPEDWINDOW);
            SetWindowLongPtrW(m_hWnd, GWL_EXSTYLE,
                m_SavedExStyle & ~(WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE));

            SetWindowPos(m_hWnd, HWND_TOP,
                mi.rcMonitor.left, mi.rcMonitor.top,
                mi.rcMonitor.right - mi.rcMonitor.left,
                mi.rcMonitor.bottom - mi.rcMonitor.top,
                SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        }
        else {
            SetWindowLongPtrW(m_hWnd, GWL_STYLE, m_SavedStyle);
            SetWindowLongPtrW(m_hWnd, GWL_EXSTYLE, m_SavedExStyle);
            SetWindowPos(m_hWnd, nullptr, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
            SetWindowPlacement(m_hWnd, &m_SavedPlacement);
        }

        m_Fullscreen.store(enable, std::memory_order_relaxed);
        UpdateCursorClip();
        return true;
    }

    void Window::Close() noexcept {
        // exchange() makes this safe to call concurrently (e.g. destructor on
        // one thread racing an explicit shutdown call on another) — only the
        // caller that actually flips closed false->true proceeds.
        if (m_Data.closed.exchange(true, std::memory_order_acq_rel)) return;
        m_Data.shouldClose.store(true, std::memory_order_relaxed);

        if (m_CursorHiddenApplied) {
            ClipCursor(nullptr);
            ShowCursor(TRUE);
            m_CursorHiddenApplied = false;
        }
        UnregisterRawInput();

        if (m_hWnd) {
            DestroyWindow(m_hWnd);
            m_hWnd = nullptr;
        }
        UnregisterWindowClass(m_hInstance);
    }

    bool Window::ShouldClose() const noexcept { return m_Data.shouldClose.load(std::memory_order_relaxed); }

    void Window::PollEvents() noexcept {
        MAR_CORE_ASSERT(GetCurrentThreadId() == m_CreationThreadId,
            "Window::PollEvents: must be called from the thread that created the window.");

        EventManager* evtMgr = EventManager::GetInstance();
        if (!evtMgr) return;

        // ==========================================
        // 0. AUTHORITATIVE CURSOR POSITION
        // ==========================================
        // One syscall per frame. Always correct — independent of relative/absolute
        // mode, DPI, monitor layout, or how many raw packets arrived this frame.
        // This replaces the old delta-integration + clamp, which is what was
        // getting stuck at 0 and losing sync with the real cursor.
        // Only query the authoritative cursor position when the window is focused
        POINT cursorPt;
        if (m_Data.isFocused.load(std::memory_order_relaxed) &&
            GetCursorPos(&cursorPt) && ScreenToClient(m_hWnd, &cursorPt)) {
            const float newX = static_cast<float>(cursorPt.x);
            const float newY = static_cast<float>(cursorPt.y);
            if (newX != m_VirtualMouseX.load(std::memory_order_relaxed) ||
                newY != m_VirtualMouseY.load(std::memory_order_relaxed)) {
                m_MouseMoved = true;
            }
            m_VirtualMouseX.store(newX, std::memory_order_relaxed);
            m_VirtualMouseY.store(newY, std::memory_order_relaxed);
        }

        // ==========================================
// 1. UNIFIED BATCH RAW INPUT (buttons + wheel + look-deltas)
// ==========================================
// Loop até esvaziar a fila: em mice de 4000/8000Hz + um frame mais lento,
// pode chegar mais pacote do que cabe numa chamada só. Se não drenar tudo
// aqui, o resto só é visto no próximo PollEvents — ou seja, 1 frame de
// latência extra no look/aim, variável com a taxa do mouse.
        const float mouseX = m_VirtualMouseX.load(std::memory_order_relaxed);
        const float mouseY = m_VirtualMouseY.load(std::memory_order_relaxed);

        for (;;) {
            UINT bufferSize = sizeof(m_RawInputBuffer);
            const UINT rawInputCount = GetRawInputBuffer(
                reinterpret_cast<RAWINPUT*>(m_RawInputBuffer),
                &bufferSize,
                sizeof(RAWINPUTHEADER)
            );
            if (rawInputCount == 0 || rawInputCount == (UINT)-1) break;

            std::byte* current = m_RawInputBuffer;
            std::byte* bufferEnd = m_RawInputBuffer + sizeof(m_RawInputBuffer);

            for (UINT i = 0; i < rawInputCount; ++i) {
                if (current + sizeof(RAWINPUTHEADER) > bufferEnd) break;
                auto* raw = reinterpret_cast<RAWINPUT*>(current);

                if (raw->header.dwType == RIM_TYPEMOUSE) {
                    USHORT flags = raw->data.mouse.usButtonFlags;

                    if (flags & RI_MOUSE_LEFT_BUTTON_DOWN)   evtMgr->Push(MouseButtonPressedEvent(0, mouseX, mouseY));
                    if (flags & RI_MOUSE_LEFT_BUTTON_UP)     evtMgr->Push(MouseButtonReleasedEvent(0, mouseX, mouseY));
                    if (flags & RI_MOUSE_RIGHT_BUTTON_DOWN)  evtMgr->Push(MouseButtonPressedEvent(1, mouseX, mouseY));
                    if (flags & RI_MOUSE_RIGHT_BUTTON_UP)    evtMgr->Push(MouseButtonReleasedEvent(1, mouseX, mouseY));
                    if (flags & RI_MOUSE_MIDDLE_BUTTON_DOWN) evtMgr->Push(MouseButtonPressedEvent(2, mouseX, mouseY));
                    if (flags & RI_MOUSE_MIDDLE_BUTTON_UP)   evtMgr->Push(MouseButtonReleasedEvent(2, mouseX, mouseY));
                    if (flags & RI_MOUSE_BUTTON_4_DOWN)      evtMgr->Push(MouseButtonPressedEvent(3, mouseX, mouseY));
                    if (flags & RI_MOUSE_BUTTON_4_UP)        evtMgr->Push(MouseButtonReleasedEvent(3, mouseX, mouseY));
                    if (flags & RI_MOUSE_BUTTON_5_DOWN)      evtMgr->Push(MouseButtonPressedEvent(4, mouseX, mouseY));
                    if (flags & RI_MOUSE_BUTTON_5_UP)        evtMgr->Push(MouseButtonReleasedEvent(4, mouseX, mouseY));

                    // Wheel notches are instantaneous impulses, not state —
                   // WHEEL_DELTA (120) is one notch on a standard detent wheel.
                   /*if (flags & RI_MOUSE_WHEEL) {
                       const float notches = static_cast<float>(static_cast<SHORT>(raw->data.mouse.usButtonData)) / static_cast<float>(WHEEL_DELTA);
                       evtMgr->Push(MouseScrolledEvent(0.0f, notches));
                   }
                   if (flags & RI_MOUSE_HWHEEL) {
                       const float notches = static_cast<float>(static_cast<SHORT>(raw->data.mouse.usButtonData)) / static_cast<float>(WHEEL_DELTA);
                       evtMgr->Push(MouseScrolledEvent(notches, 0.0f));
                   }*/


                    if ((raw->data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE) == 0) {
                        const long dx = raw->data.mouse.lLastX;
                        const long dy = raw->data.mouse.lLastY;
                        if (dx != 0 || dy != 0) {
                            m_AccumDeltaX += static_cast<float>(dx);
                            m_AccumDeltaY += static_cast<float>(dy);
                            m_MouseMoved = true;
                        }
                    }
                }



                // Travessia igual à semântica de NEXTRAWINPUTBLOCK: cada bloco é
                // alinhado a 4 bytes (DWORD), e dwSize sozinho não garante isso.
                // Avançar por dwSize cru só quebra quando há >1 pacote no buffer —
                // ou seja, só aparece sob mouse de alto polling, e passa
                // despercebido em teste com mouse comum.
                UINT structSize;
                std::memcpy(&structSize, &raw->header.dwSize, sizeof(DWORD));
                if (structSize == 0) break;
                const UINT aligned = (structSize + sizeof(DWORD) - 1) & ~(sizeof(DWORD) - 1);
                if (current + aligned > bufferEnd) break;
                current += aligned;
            }
        }

        // ==========================================
        // 2. STANDARD MESSAGE PUMP
        // ==========================================
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            DispatchMessageW(&msg);
        }

        // ==========================================
        // 3. AGGLUTINATED MOTION DISPATCH
        // ==========================================
        if (m_MouseMoved) {
            m_LastDeltaX.store(m_AccumDeltaX, std::memory_order_relaxed);
            m_LastDeltaY.store(m_AccumDeltaY, std::memory_order_relaxed);

            // Sensitivity-scaled, double-precision accumulation — this is
            // where fractional ("sub-pixel") precision actually comes from.
            // Raw hardware deltas are always integers; multiplying by a
            // fractional sensitivity and summing in double (no truncation)
            // is what game engines do to get smooth, precise look/aim even
            // at very low sensitivities.
            const double sx = static_cast<double>(m_SensitivityX.load(std::memory_order_relaxed));
            const double sy = static_cast<double>(m_SensitivityY.load(std::memory_order_relaxed));
            const double prevRawX = m_RawMouseX.load(std::memory_order_relaxed);
            const double prevRawY = m_RawMouseY.load(std::memory_order_relaxed);
            m_RawMouseX.store(prevRawX + static_cast<double>(m_AccumDeltaX) * sx, std::memory_order_relaxed);
            m_RawMouseY.store(prevRawY + static_cast<double>(m_AccumDeltaY) * sy, std::memory_order_relaxed);

            evtMgr->Push(MouseMotionEvent(
                m_VirtualMouseX.load(std::memory_order_relaxed),
                m_VirtualMouseY.load(std::memory_order_relaxed),
                m_AccumDeltaX, m_AccumDeltaY));
            m_AccumDeltaX = 0.0f;
            m_AccumDeltaY = 0.0f;
            m_MouseMoved = false;
        }
        else {
            m_LastDeltaX.store(0.0f, std::memory_order_relaxed);
            m_LastDeltaY.store(0.0f, std::memory_order_relaxed);
        }
    }

    void Window::RefreshScreenMetrics() noexcept {
        m_ScreenMetrics.width = GetSystemMetrics(SM_CXSCREEN);
        m_ScreenMetrics.height = GetSystemMetrics(SM_CYSCREEN);
        m_ScreenMetrics.vdWidth = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        m_ScreenMetrics.vdHeight = GetSystemMetrics(SM_CYVIRTUALSCREEN);
        m_ScreenMetrics.vdOriginX = GetSystemMetrics(SM_XVIRTUALSCREEN);
        m_ScreenMetrics.vdOriginY = GetSystemMetrics(SM_YVIRTUALSCREEN);
        m_ScreenMetrics.valid = true;
    }

    const Window::ScreenMetricsCache& Window::GetScreenMetrics() noexcept {
        if (!m_ScreenMetrics.valid) {
            RefreshScreenMetrics();
        }
        return m_ScreenMetrics;
    }

    LRESULT CALLBACK Window::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) noexcept {
        if (msg == WM_NCCREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            auto* self = static_cast<Window*>(cs->lpCreateParams);
            self->m_hWnd = hWnd;
            SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            return DefWindowProcW(hWnd, msg, wParam, lParam);
        }
        auto* self = reinterpret_cast<Window*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
        return self ? self->HandleMessage(hWnd, msg, wParam, lParam)
            : DefWindowProcW(hWnd, msg, wParam, lParam);
    }

    LRESULT Window::HandleMessage(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) noexcept {
        EventManager* evtMgr = EventManager::GetInstance();
        if (!evtMgr) return DefWindowProcW(hWnd, msg, wParam, lParam);

        switch (msg) {
        case WM_CLOSE:
            m_Data.shouldClose.store(true, std::memory_order_relaxed);
            return 0;
        case WM_DESTROY:
            m_Data.shouldClose.store(true, std::memory_order_relaxed);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
            ValidateRect(hWnd, nullptr);
            return 0;
        case WM_SETCURSOR:
            if (LOWORD(lParam) == HTCLIENT) {
                // Use cached cursor handle to avoid a syscall every time
                SetCursor(m_HCursor ? m_HCursor : LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_ARROW)));
                return TRUE;
            }
            break;

        case WM_ENTERSIZEMOVE:
            m_Data.isResizing.store(true, std::memory_order_relaxed);
            return 0;
        case WM_EXITSIZEMOVE:
            m_Data.isResizing.store(false, std::memory_order_relaxed);
            if (!m_Data.isMinimized.load(std::memory_order_relaxed)) {
                evtMgr->Push(WindowResizeEvent(static_cast<int>(m_Data.width.load()), static_cast<int>(m_Data.height.load())));
            }
            UpdateCursorClip();
            return 0;
        case WM_SIZE: {
            if (wParam == SIZE_MINIMIZED) {
                m_Data.isMinimized.store(true, std::memory_order_relaxed);
                return 0;
            }
            m_Data.isMinimized.store(false, std::memory_order_relaxed);
            const uint32_t newWidth = LOWORD(lParam);
            const uint32_t newHeight = HIWORD(lParam);
            if (newWidth > 0 && newHeight > 0) {
                m_Data.width.store(newWidth);
                m_Data.height.store(newHeight);
                if (!m_Data.isResizing.load(std::memory_order_relaxed)) {
                    evtMgr->Push(WindowResizeEvent(static_cast<int>(newWidth), static_cast<int>(newHeight)));
                }
                UpdateCursorClip();
            }
            return 0;
        }
        case WM_MOVE: {
            m_PositionX.store(GET_X_LPARAM(lParam), std::memory_order_relaxed);
            m_PositionY.store(GET_Y_LPARAM(lParam), std::memory_order_relaxed);
            UpdateCursorClip();
            return 0;
        }
        case WM_GETMINMAXINFO: {
            auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
            const uint32_t minW = m_MinWidth.load(std::memory_order_relaxed);
            const uint32_t minH = m_MinHeight.load(std::memory_order_relaxed);
            const uint32_t maxW = m_MaxWidth.load(std::memory_order_relaxed);
            const uint32_t maxH = m_MaxHeight.load(std::memory_order_relaxed);
            if (minW > 0 && minH > 0) {
                mmi->ptMinTrackSize.x = static_cast<LONG>(minW);
                mmi->ptMinTrackSize.y = static_cast<LONG>(minH);
            }
            if (maxW > 0 && maxH > 0) {
                mmi->ptMaxTrackSize.x = static_cast<LONG>(maxW);
                mmi->ptMaxTrackSize.y = static_cast<LONG>(maxH);
            }
            return 0;
        }
        case WM_DPICHANGED: {
            m_Dpi.store(LOWORD(wParam), std::memory_order_relaxed);
            auto* rect = reinterpret_cast<RECT*>(lParam);
            // SetWindowPos below synchronously cascades WM_WINDOWPOSCHANGED ->
            // WM_SIZE (sent, not posted) when the size actually changes; that
            // nested WM_SIZE call (handled above) already updates width/height
            // and fires the resize event before SetWindowPos returns here, so
            // firing a second event afterwards (as the previous version did)
            // would double-dispatch it.
            SetWindowPos(hWnd, nullptr, rect->left, rect->top,
                rect->right - rect->left, rect->bottom - rect->top,
                SWP_NOZORDER | SWP_NOACTIVATE);
            UpdateCursorClip();
            return 0;
        }
        case WM_DISPLAYCHANGE:
            m_ScreenMetrics.valid = false;
            if (!m_Data.isMinimized.load(std::memory_order_relaxed)) {
                evtMgr->Push(WindowResizeEvent(static_cast<int>(m_Data.width.load()), static_cast<int>(m_Data.height.load())));
            }
            return 0;
        case WM_POWERBROADCAST:
            // Distinct from isResizing: this is an OS suspend/resume, not an
            // interactive drag-resize. Conflating the two previously meant a
            // resume could clear isResizing out from under an in-progress
            // drag in the (rare) case a suspend happened mid-drag.
            if (wParam == PBT_APMSUSPEND) {
                m_Data.isSuspended.store(true, std::memory_order_relaxed);
            }
            else if (wParam == PBT_APMRESUMEAUTOMATIC) {
                m_Data.isSuspended.store(false, std::memory_order_relaxed);
                if (!m_Data.isMinimized.load(std::memory_order_relaxed)) {
                    evtMgr->Push(WindowResizeEvent(static_cast<int>(m_Data.width.load()), static_cast<int>(m_Data.height.load())));
                }
            }
            return TRUE;

        case WM_SETFOCUS:
            m_Data.isFocused.store(true, std::memory_order_relaxed);
            UpdateCursorClip();
            // Consider pushing a WindowFocusEvent(true) here so the input
            // layer can react immediately rather than polling IsFocused() —
            // see the accompanying notes for a suggested event definition.
            return 0;
        case WM_KILLFOCUS:
            m_Data.isFocused.store(false, std::memory_order_relaxed);
            ClipCursor(nullptr); // OS releases this automatically too; be explicit
            // Pushing a WindowFocusEvent(false) here is also where an input
            // system should release all currently-held keys/buttons — a key
            // released while another window has focus never reaches this
            // WndProc, so without this signal it reads as permanently "stuck
            // down" after an alt-tab.
            return 0;

            // Keyboard is kept on the legacy pump for text translation simplicity
        case WM_KEYDOWN: {
            const bool isRepeat = (lParam & 0x40000000) != 0;
            if (!isRepeat) evtMgr->Push(KeyPressedEvent(static_cast<int>(wParam)));
            return 0;
        }
        case WM_KEYUP:
            evtMgr->Push(KeyReleasedEvent(static_cast<int>(wParam)));
            return 0;

            // Alt, F10, and Alt+<key> combos arrive here instead of
            // WM_KEYDOWN/UP. Forward them the same way, but `break` (not
            // `return`) so DefWindowProc still sees them afterwards — that's
            // what keeps Alt+F4 and friends working. Only the system-menu
            // activation itself is suppressed, in WM_SYSCOMMAND below.
        case WM_SYSKEYDOWN: {
            const bool isRepeat = (lParam & 0x40000000) != 0;
            if (!isRepeat) evtMgr->Push(KeyPressedEvent(static_cast<int>(wParam)));
            break;
        }
        case WM_SYSKEYUP:
            evtMgr->Push(KeyReleasedEvent(static_cast<int>(wParam)));
            break;

        case WM_SYSCOMMAND:
            // Swallow a lone Alt/F10 press activating the system menu (the
            // jarring white menu-bar flash) while letting every other
            // syscommand — including SC_CLOSE from Alt+F4 — reach
            // DefWindowProc normally. 0xFFF0 mask per Microsoft's own
            // documented guidance for inspecting WM_SYSCOMMAND's wParam.
            if ((wParam & 0xFFF0) == SC_KEYMENU) {
                return 0;
            }
            break;

        default:
            break;
        }
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
}