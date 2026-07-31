#pragma once

// Application.h
//
// CRTP-based application framework. Eliminates all virtual dispatch,
// heap allocation, and vtable overhead. The derived class type is resolved
// entirely at compile time via static_cast in the self() helper.
//
// Usage:
//   class MyApp final : public Mar::Application<MyApp> { ... };
//   MAR_MAIN(MyApp)

#include "Mararuana/EventSystem/EventSystem.h"
#include <cstdint>
#include <atomic>
#include <chrono>
#include <thread>

#ifdef MAR_ENABLE_WINDOW
#include "Mararuana/Window.h"
#endif

#ifndef MAR_ENABLE_WINDOW
#include <random>
#endif

namespace Mar {

    // ---------------------------------------------------------------------------
    // ApplicationSpecification
    // Unified startup parameters. Width/Height are ignored in headless builds.
    // Uses const wchar_t* for zero-allocation, native Win32 compatibility.
    // ---------------------------------------------------------------------------
    struct ApplicationSpecification {
        const wchar_t* Name = L"Mararuana-App";
        uint32_t       Width = 1280;
        uint32_t       Height = 720;
    };

    // ---------------------------------------------------------------------------
    // Application<Derived>
    //
    // CRTP base class. Derived must inherit as:
    //     class Foo final : public Application<Foo>
    //
    // Lifecycle:
    //   1. Derived constructs, forwarding spec to Application(spec).
    //   2. Call Run(). Blocks until ShouldClose() or Stop().
    //   3. OnShutdown() is invoked (compile-time resolved) after loop exits.
    //   4. Derived destructor runs (stack unwinding, no virtual dtor needed).
    //
    // Hooks (override by declaring in Derived with the same signature):
    //   void OnUpdate() noexcept   — called once per frame, before event dispatch.
    //   void OnShutdown() noexcept — called once after the main loop terminates.
    // ---------------------------------------------------------------------------
    template<typename Derived>
    class Application {
    public:
        // -----------------------------------------------------------------------
        // Construction / Destruction
        // -----------------------------------------------------------------------
        explicit Application([[maybe_unused]] const ApplicationSpecification& spec)
#ifdef MAR_ENABLE_WINDOW
            : m_evtMgr(new EventManager()), m_AppWindow(spec.Width, spec.Height, spec.Name)
#endif
        {
            // Compile-time validation: Derived must actually inherit from this
            // instantiation. Evaluated when Run() is called (Derived is complete).
            static_assert(
                std::is_base_of_v<Application<Derived>, Derived>,
                "CRTP violation: Derived must inherit from Application<Derived>"
                );
        }

        // Non-virtual destructor. No vtable is emitted.
        // Stack-allocated instances are destroyed via normal scope unwinding.
        ~Application() = default;

        // Non-transferable: owns OS window handle and event subscriptions.
        Application(const Application&) = delete;
        Application& operator=(const Application&) = delete;
        Application(Application&&) = delete;
        Application& operator=(Application&&) = delete;

        // -----------------------------------------------------------------------
        // Main Loop
        // -----------------------------------------------------------------------
        // -----------------------------------------------------------------------
        // Main Loop com Limitador de FPS
        // -----------------------------------------------------------------------
        void Run() {
#ifdef MAR_ENABLE_WINDOW
            // Alvo de performance (pode virar um parâmetro do ApplicationSpecification depois)
            constexpr double targetFPS = 144.0;
            constexpr std::chrono::duration<double, std::nano> targetFrameTime{ 1'000'000'000.0 / targetFPS };

            // Loop principal
            while (m_Running.load(std::memory_order_relaxed) &&
                !m_AppWindow.ShouldClose())
            {
                // Marca o início do quadro
                const auto frameStart = std::chrono::high_resolution_clock::now();

                // Executa o frame do motor
                m_AppWindow.PollEvents();
                self().OnUpdate();       // Compile-time resolved
                m_evtMgr->Dispatch();

                // Calcula quanto tempo sobrou
                const auto frameEnd = std::chrono::high_resolution_clock::now();
                const auto elapsed = frameEnd - frameStart;

                if (elapsed < targetFrameTime) {
                    // Faz a Thread "dormir" o tempo exato que sobrou do quadro
                    std::this_thread::sleep_for(targetFrameTime - elapsed);
                }
            }
#else
            RunSyntheticEventLoop();
#endif
            // Loop has fully exited. Safe to tear down client resources.
            self().OnShutdown();         // Compile-time resolved
        }

        // Thread-safe stop signal. Works in both windowed and headless modes.
        void Stop() noexcept {
            m_Running.store(false, std::memory_order_relaxed);
        }

        // -----------------------------------------------------------------------
        // Default Hook Implementations
        //
        // If Derived declares its own OnUpdate/OnShutdown with the same
        // signature, it hides these. The CRTP call self().OnUpdate() resolves
        // to Derived::OnUpdate() at compile time — no vtable, no indirection.
        // -----------------------------------------------------------------------
        void OnUpdate()   noexcept {}
        void OnShutdown() noexcept {}

    protected:
        std::atomic<bool> m_Running{ true };
        EventManager* m_evtMgr;

#ifdef MAR_ENABLE_WINDOW
        Window m_AppWindow;
#endif

    private:
        // CRTP downcast. Zero runtime cost: the compiler optimizes this to
        // a no-op (identity cast) since Derived's address == Application's
        // address in single-inheritance layouts.
        
        [[nodiscard]] Derived& self()       noexcept { return static_cast<Derived&>(*this); }
        [[nodiscard]] const Derived& self() const noexcept { return static_cast<const Derived&>(*this); }

    };

} // namespace Mar