#include "Mararuana/Window.h"

namespace Mar {

    Window::Window(uint32_t width, uint32_t height, std::string title)
        : m_Width(width), m_Height(height), m_Title(std::move(title))
    {
        Init();
    }

    Window::~Window()
    {
        Close();
    }

    void Window::Init()
    {
        glfwInit();
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

        m_Window = glfwCreateWindow(
            static_cast<int>(m_Width),
            static_cast<int>(m_Height),
            m_Title.c_str(),
            nullptr,
            nullptr
        );
    }

    void Window::Close() noexcept
    {
        if (m_Closed) return;
        m_Closed = true;

        if (m_Window)
        {
            glfwDestroyWindow(m_Window);
            m_Window = nullptr;
        }
        glfwTerminate();
    }

    bool Window::ShouldClose() const noexcept
    {
        return m_Window && glfwWindowShouldClose(m_Window);
    }

    void Window::PollEvents() noexcept
    {
        glfwPollEvents();
    }

    void Window::SwapBuffers() noexcept
    {
        glfwSwapBuffers(m_Window);
    }

} // namespace Mar