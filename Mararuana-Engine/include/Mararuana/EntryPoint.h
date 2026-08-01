#pragma once

// EntryPoint.h
//
// Provides the MAR_MAIN macro, which generates a standard main() function.
// The application object is stack-allocated — eliminating heap allocation,
// pointer indirection, and the need for a virtual destructor.
//
// Usage (in exactly ONE translation unit):
//     MAR_MAIN(Sandbox)
//
// Do NOT include this header in more than one translation unit (ODR violation).

#include "Mararuana/Log.h"

#define MAR_MAIN(AppClass)                                                    \
    int main([[maybe_unused]] int argc, [[maybe_unused]] char** argv) {       \
        Mar::Log::Init();                                                     \
                                                                              \
        MAR_CORE_INFO("Mararuana Engine starting");                           \
        MAR_CORE_INFO("Application class: " #AppClass);                       \
                                                                              \
        /* Stack-allocated. Destructor runs automatically on scope exit. */   \
        /* No heap allocation. No virtual destructor. No delete. */           \
        AppClass app;                                                         \
        app.Run();                                                            \
                                                                              \
        MAR_CORE_INFO("Mararuana Engine shutdown complete");                  \
        return 0;                                                             \
    }
