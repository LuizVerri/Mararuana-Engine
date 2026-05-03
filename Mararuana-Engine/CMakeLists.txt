# Subproject CMake file for Mararuana-Engine
# Add sources, targets and project-specific settings.

set(LIB_NAME Mararuana-Engine)

add_library(${LIB_NAME} STATIC)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

find_package(Vulkan REQUIRED)

include(spdlog)
include(GLFW)

target_sources(${LIB_NAME}
  PRIVATE
    "src/Mararuana/Application.cpp"
    "src/Mararuana/Log.cpp"
    "src/Mararuana/Window.cpp"
  PUBLIC
    FILE_SET HEADERS
      BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/src
      FILES
        "src/Mararuana/Application.h"
        "src/Mararuana/Core.h"
        "src/Mararuana.h"
        "src/Mararuana/Log.h"
        "src/Mararuana/EntryPoint.h"
        "src/Mararuana/Events/EventBuffer.h"
        "src/Mararuana/Events/Events.h"
        "src/Mararuana/Events/EventManager.h"
        "src/Mararuana/Events/EventScopedToken.h"
        "src/Mararuana/Events/EventSystem.h"
        "src/Mararuana/Events/EventBase.h"
        "src/Mararuana/Window.h")

target_link_libraries(${LIB_NAME}
  PUBLIC
    Vulkan::Vulkan
    spdlog::spdlog
    glfw
)

target_include_directories(${LIB_NAME}
  PUBLIC 
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
)