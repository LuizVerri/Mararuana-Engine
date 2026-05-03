#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <string>

namespace Mar {

    // Window - RAII wrapper for a GLFW window.
    class Window {
    public:
        Window(uint32_t width, uint32_t height, std::string title);
        ~Window();

        // No copy, no move.
        Window(const Window&) = delete;
        Window& operator=(const Window&) = delete;
        Window(Window&&) = delete;
        Window& operator=(Window&&) = delete;

        // Query
        [[nodiscard]] bool       ShouldClose()     const noexcept;
        [[nodiscard]] uint32_t   GetWidth()        const noexcept { return m_Width; }
        [[nodiscard]] uint32_t   GetHeight()       const noexcept { return m_Height; }
        [[nodiscard]] const std::string& GetTitle() const noexcept { return m_Title; }

        // Native handle for graphics backend only.
        [[nodiscard]] GLFWwindow* GetNativeWindow() noexcept { return m_Window; }
        [[nodiscard]] const GLFWwindow* GetNativeWindow() const noexcept { return m_Window; }

        // Frame ops
        void PollEvents()  noexcept;
        void SwapBuffers() noexcept;

        // Explicit close.
        void Close() noexcept;

    private:
        void Init();

        GLFWwindow* m_Window = nullptr;
        uint32_t    m_Width;
        uint32_t    m_Height;
        std::string m_Title;
        bool        m_Closed = false;
    };

} // namespace Mar