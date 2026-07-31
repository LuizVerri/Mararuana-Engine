#pragma once


#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>

#include <cstdint>
#include <cstddef> // std::byte (was previously only pulled in transitively via the .cpp — not guaranteed)
#include <atomic>

#include "Mararuana/EventSystem/EventSystem.h"

namespace Mar {

    // How the OS cursor behaves for this window.
    enum class CursorMode {
        Normal,     // Visible, free to move anywhere (default)
        Hidden,     // Invisible, still free to move anywhere (e.g. UI-driven custom cursor)
        Disabled    // Invisible AND confined to the client area — for FPS-style mouselook
    };

    struct WindowData {
        // All of these are written from the window-thread (inside PollEvents /
        // WndProc) but are intended to be polled from other threads too (e.g.
        // a render thread checking ShouldClose()/GetWidth()/IsResizing() every
        // frame) — hence atomic, consistent with width/height below.
        std::atomic<uint32_t> width{ 0 };
        std::atomic<uint32_t> height{ 0 };
        std::atomic<bool> closed{ false };
        std::atomic<bool> shouldClose{ false };
        std::atomic<bool> isResizing{ false };   // true only during an interactive drag-resize
        std::atomic<bool> isMinimized{ false };
        std::atomic<bool> isSuspended{ false };  // true during OS suspend/sleep (distinct from isResizing)
        std::atomic<bool> isFocused{ false };

        WindowData(uint32_t w, uint32_t h) noexcept
            : width(w), height(h) {
        }
    };

    class Window final {
    public:

        Window(uint32_t width, uint32_t height, const wchar_t* title);
        ~Window();

        Window(const Window&) = delete;
        Window& operator=(const Window&) = delete;
        Window(Window&&) = delete;
        Window& operator=(Window&&) = delete;

        // Call once per frame from the thread that created the window.
        void PollEvents() noexcept;
        [[nodiscard]] bool ShouldClose() const noexcept;
        void Close() noexcept;
        [[nodiscard]] bool IsValid() const noexcept { return m_hWnd != nullptr; }

        [[nodiscard]] bool IsResizing() const noexcept { return m_Data.isResizing.load(std::memory_order_relaxed); }
        [[nodiscard]] bool IsMinimized() const noexcept { return m_Data.isMinimized.load(std::memory_order_relaxed); }
        [[nodiscard]] bool IsSuspended() const noexcept { return m_Data.isSuspended.load(std::memory_order_relaxed); }
        [[nodiscard]] bool IsFocused() const noexcept { return m_Data.isFocused.load(std::memory_order_relaxed); }
        [[nodiscard]] uint32_t GetWidth() const noexcept { return m_Data.width.load(std::memory_order_relaxed); }
        [[nodiscard]] uint32_t GetHeight() const noexcept { return m_Data.height.load(std::memory_order_relaxed); }

        [[nodiscard]] HWND GetNativeWindow() const noexcept { return m_hWnd; }
        [[nodiscard]] HINSTANCE GetNativeInstance() const noexcept { return m_hInstance; }

        // --- Title / position / size ----------------------------------------
        // These mutate the OS window and are expected to be called from the
        // window's owning thread, same as PollEvents (standard Win32 affinity
        // rule — HWNDs are not free-threaded).
        void SetTitle(const wchar_t* title) noexcept;
        void SetPosition(int x, int y) noexcept;
        [[nodiscard]] int GetPositionX() const noexcept { return m_PositionX.load(std::memory_order_relaxed); }
        [[nodiscard]] int GetPositionY() const noexcept { return m_PositionY.load(std::memory_order_relaxed); }
        void SetMinimumSize(uint32_t width, uint32_t height) noexcept;
        void SetMaximumSize(uint32_t width, uint32_t height) noexcept;

        // --- Fullscreen ------------------------------------------------------
        // Borderless/windowed fullscreen (not exclusive fullscreen — see notes
        // in the accompanying explanation for why that's the right default for
        // a Vulkan swapchain-based window).
        bool SetFullscreen(bool enable) noexcept;
        void ToggleFullscreen() noexcept { SetFullscreen(!IsFullscreen()); }
        [[nodiscard]] bool IsFullscreen() const noexcept { return m_Fullscreen.load(std::memory_order_relaxed); }

        // --- Cursor ------------------------------------------------------
        void SetCursorMode(CursorMode mode) noexcept;
        [[nodiscard]] CursorMode GetCursorMode() const noexcept { return m_CursorMode.load(std::memory_order_relaxed); }

        // --- DPI ------------------------------------------------------
        [[nodiscard]] uint32_t GetDpi() const noexcept { return m_Dpi.load(std::memory_order_relaxed); }
        [[nodiscard]] float GetDpiScale() const noexcept { return static_cast<float>(GetDpi()) / 96.0f; }

        // --- Mouse: position & motion ----------------------------------------------------

        // OS-synced absolute cursor position, in physical client-area pixels.
        // Always whole-number valued (GetCursorPos has integer precision) —
        // stored as float purely for convenient use in float-based math.
        // Good for UI hit-testing / cursor rendering.
        [[nodiscard]] float GetMouseX() const noexcept { return m_VirtualMouseX.load(std::memory_order_relaxed); }
        [[nodiscard]] float GetMouseY() const noexcept { return m_VirtualMouseY.load(std::memory_order_relaxed); }

        // Raw (unscaled) relative motion accumulated during the last
        // PollEvents() call — the same values forwarded in MouseMotionEvent.
        // Resets to 0 for any frame with no motion.
        [[nodiscard]] float GetMouseDeltaX() const noexcept { return m_LastDeltaX.load(std::memory_order_relaxed); }
        [[nodiscard]] float GetMouseDeltaY() const noexcept { return m_LastDeltaY.load(std::memory_order_relaxed); }

        // Sensitivity multiplier applied to raw deltas before they're folded
        // into GetRawMouseX/Y(). Default 1.0 (no scaling — output equals raw
        // hardware counts until you set something else).
        void SetMouseSensitivity(float sensitivity) noexcept { SetMouseSensitivity(sensitivity, sensitivity); }
        void SetMouseSensitivity(float sensitivityX, float sensitivityY) noexcept {
            m_SensitivityX.store(sensitivityX, std::memory_order_relaxed);
            m_SensitivityY.store(sensitivityY, std::memory_order_relaxed);
        }
        [[nodiscard]] float GetMouseSensitivityX() const noexcept { return m_SensitivityX.load(std::memory_order_relaxed); }
        [[nodiscard]] float GetMouseSensitivityY() const noexcept { return m_SensitivityY.load(std::memory_order_relaxed); }

        // Continuously-accumulated, sensitivity-scaled, double-precision
        // mouse position. Unlike GetMouseX/Y() (OS cursor, always
        // integer-valued, clamped to the screen), this value:
        //   - is driven by raw input deltas (hardware mouse counts), which
        //     are NOT affected by OS pointer acceleration/speed settings
        //   - is scaled by SetMouseSensitivity() — that multiplication is
        //     what actually produces fractional (sub-integer) values, e.g. a
        //     raw delta of 3 counts at 0.5 sensitivity contributes exactly
        //     1.5, not 1 or 2
        //   - accumulates in double precision with no intermediate rounding,
        //     so slow/precise movement is never truncated away
        //   - is unbounded: it does not clamp to the screen or window, and
        //     keeps accumulating even while the cursor is hidden/confined
        // This is the value to drive an FPS-style camera from.
        [[nodiscard]] double GetRawMouseX() const noexcept { return m_RawMouseX.load(std::memory_order_relaxed); }
        [[nodiscard]] double GetRawMouseY() const noexcept { return m_RawMouseY.load(std::memory_order_relaxed); }
        void ResetRawMouse() noexcept {
            m_RawMouseX.store(0.0, std::memory_order_relaxed);
            m_RawMouseY.store(0.0, std::memory_order_relaxed);
        }

        // --- Screen metrics ----------------------------------------------------
        struct ScreenMetricsCache {
            int width = 0, height = 0;
            int vdWidth = 0, vdHeight = 0;   // virtual desktop (all monitors combined)
            int vdOriginX = 0, vdOriginY = 0;
            bool valid = false;
        };
        // Lazily refreshed: invalidated on WM_DISPLAYCHANGE, recomputed on
        // next access after that.
        [[nodiscard]] const ScreenMetricsCache& GetScreenMetrics() noexcept;

    private:

        void RefreshScreenMetrics() noexcept;

        void Init() noexcept;
        void RegisterRawInput() const noexcept;
        void UnregisterRawInput() const noexcept;
        void DisableIME() const noexcept;

        void UpdateCursorClip() noexcept;
        void ClipCursorToClient() noexcept;

        static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) noexcept;
        LRESULT HandleMessage(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) noexcept;

        static constexpr const wchar_t* kClassName = L"Mararuana Vulkan Window";

        static std::atomic<int> s_ClassRefCount;
        static bool RegisterWindowClass(HINSTANCE hInstance) noexcept;
        static void UnregisterWindowClass(HINSTANCE hInstance) noexcept;

    private:
        HINSTANCE  m_hInstance = nullptr;
        HWND       m_hWnd = nullptr;
        WindowData m_Data;
        DWORD      m_CreationThreadId = 0; // for a debug-only affinity check in PollEvents

        ScreenMetricsCache m_ScreenMetrics;

        std::atomic<int> m_PositionX{ 0 };
        std::atomic<int> m_PositionY{ 0 };
        std::atomic<uint32_t> m_MinWidth{ 0 }, m_MinHeight{ 0 }; // 0 = unconstrained
        std::atomic<uint32_t> m_MaxWidth{ 0 }, m_MaxHeight{ 0 };
        std::atomic<uint32_t> m_Dpi{ 96 };

        std::atomic<bool> m_Fullscreen{ false };
        WINDOWPLACEMENT   m_SavedPlacement{}; // windowed placement, saved on entering fullscreen; .length set before use
        LONG_PTR          m_SavedStyle = 0;
        LONG_PTR          m_SavedExStyle = 0;

        std::atomic<CursorMode> m_CursorMode{ CursorMode::Normal };
        bool m_CursorHiddenApplied = false; // tracks our own ShowCursor() calls so we toggle it exactly once per transition

        // --- Unified Raw Input State ---
        // m_AccumDeltaX/Y: in-progress accumulator for the frame currently
        // being built. Window-thread only, never exposed publicly.
        float m_AccumDeltaX = 0.0f;
        float m_AccumDeltaY = 0.0f;
        bool  m_MouseMoved = false;

        std::atomic<float> m_LastDeltaX{ 0.0f };
        std::atomic<float> m_LastDeltaY{ 0.0f };
        std::atomic<float> m_VirtualMouseX{ 0.0f }; // Replaces GetCursorPos
        std::atomic<float> m_VirtualMouseY{ 0.0f };

        std::atomic<float>  m_SensitivityX{ 1.0f };
        std::atomic<float>  m_SensitivityY{ 1.0f };
        std::atomic<double> m_RawMouseX{ 0.0 };
        std::atomic<double> m_RawMouseY{ 0.0 };

        alignas(8) std::byte m_RawInputBuffer[sizeof(RAWINPUT) * 32];
        // Cached OS cursor handle to avoid repeated LoadCursorW calls in WM_SETCURSOR
        HCURSOR m_HCursor = nullptr;
    };

} // namespace Mar