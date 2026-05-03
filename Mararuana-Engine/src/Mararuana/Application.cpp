#include "Application.h"
#include "Mararuana/Events/EventSystem.h"
#include "Mararuana/Log.h"

#include <random>

namespace Mar {

#ifdef MAR_WINDOW
    Application::Application(const ApplicationSpecification& spec)
        : m_AppWindow(spec.Width, spec.Height, spec.Name)
    {
        MAR_SUBSCRIBE(m_EventManager, WindowCloseEvent, {
            Shutdown();
            });

        glfwSetWindowUserPointer(m_AppWindow.GetNativeWindow(), this);

        glfwSetWindowCloseCallback(m_AppWindow.GetNativeWindow(),
            [](GLFWwindow* window) {
                auto* app = static_cast<Application*>(glfwGetWindowUserPointer(window));
                WindowCloseEvent event{};
                (void)app->GetEventManager().Push(event);
            }
        );
    }
#else
    Application::Application(const ApplicationSpecification& /*spec*/)
    {
        MAR_SUBSCRIBE(m_EventManager, WindowCloseEvent, {
            Shutdown();
            });
    }
#endif

    Application::~Application() = default;

#ifdef MAR_WINDOW
    void Application::Shutdown() noexcept
    {
        m_AppWindow.Close();
        m_Running = false;
    }
#else
    void Application::Shutdown() noexcept
    {
        m_Running = false;
    }
#endif

    void Application::Run()
    {
        std::mt19937 rng{ std::random_device{}() };

        constexpr int kMinEventsPerFrame = 1;
        constexpr int kMaxEventsPerFrame = 16;
        constexpr int kMinKey = 0;
        constexpr int kMaxKey = 23023;

        std::uniform_int_distribution<int> eventCountDist(kMinEventsPerFrame, kMaxEventsPerFrame);
        std::uniform_int_distribution<int> keyDist(kMinKey, kMaxKey);

#ifdef MAR_WINDOW
        while (m_Running)
        {
            m_AppWindow.PollEvents();

            const int eventCount = eventCountDist(rng);
            for (int i = 0; i < eventCount; ++i)
            {
                KeyPressedEvent e(keyDist(rng));
                if (!m_EventManager.Push(e))
                    break;
            }

            m_EventManager.Dispatch();
            m_AppWindow.SwapBuffers();
        }
#else
        while (m_Running)
        {
            const int eventCount = eventCountDist(rng);
            for (int i = 0; i < eventCount; ++i)
            {
                KeyPressedEvent e(keyDist(rng));
                if (!m_EventManager.Push(e))
                    break;
            }

            m_EventManager.Dispatch();
        }
#endif
    }

    EventManager& Application::GetEventManager() noexcept
    {
        return m_EventManager;
    }

} // namespace Mar