#pragma once

#include "Mararuana/Events/EventSystem.h"
#include "Mararuana/Window.h"

#include <string>

namespace Mar {

#ifdef MAR_WINDOW
    // App config with window support.
    struct ApplicationSpecification {
        std::string Name = "Mararuana-App";
        uint32_t    Width = 1280;
        uint32_t    Height = 720;
    };
#else
    // App config without window.
    struct ApplicationSpecification {
        std::string Name = "Mararuana-App";
    };
#endif

    class Application {
    public:
        explicit Application(const ApplicationSpecification& spec);
        virtual ~Application();

        // Disable copy and move.
        Application(const Application&) = delete;
        Application& operator=(const Application&) = delete;
        Application(Application&&) = delete;
        Application& operator=(Application&&) = delete;

        void Run();

        // Access event manager.
        [[nodiscard]] EventManager& GetEventManager() noexcept;

        // Optional override for cleanup.
        virtual void Shutdown() noexcept;

    protected:
        bool m_Running = true;
        EventManager m_EventManager;

#ifdef MAR_WINDOW
        // Main window instance.
        Window m_AppWindow;
#endif
    };

    // Implemented by client app.
    Application* CreateApplication();

} // namespace Mar