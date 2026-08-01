# ==============================================================================
#  Mararuana-Engine — the core library
# ------------------------------------------------------------------------------
#  Defines the engine target (a static library), its external dependencies,
#  source files, public headers, and the shader compilation pipeline.
# ==============================================================================

project(Mararuana-Engine
  VERSION     0.1.0
  DESCRIPTION "An engine"
  LANGUAGES   CXX
)

# Prefixing variables with MARARUANA_ keeps them from colliding if this
# project ever ends up as a subdirectory inside something bigger.
set(MARARUANA_LIB_NAME Mararuana-Engine CACHE INTERNAL "Name of the engine library target")

set(MAR_ENGINE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}" CACHE INTERNAL "Engine root directory")

# ------------------------------------------------------------------------------
# Local CMake modules
# ------------------------------------------------------------------------------
# Custom find/include scripts (e.g. cmake/Spdlog.cmake). The engine's
# option machinery (mar_option, mar_configure_target, mar_compile_shaders,
# mar_target_resources) was already included by the root CMakeLists.txt
# before this add_subdirectory() ran, so there's nothing to re-include here.
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# ------------------------------------------------------------------------------
# External dependencies
# ------------------------------------------------------------------------------
find_package(Vulkan REQUIRED)

include(Spdlog)

# ------------------------------------------------------------------------------
# Target: the engine library
# ------------------------------------------------------------------------------
add_library(${MARARUANA_LIB_NAME} STATIC)

target_compile_options(${MARARUANA_LIB_NAME} PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:$<$<NOT:$<CONFIG:Debug>>:/O2>>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:$<$<NOT:$<CONFIG:Debug>>:-O2>>
)

target_sources(${MARARUANA_LIB_NAME}
  PRIVATE
    src/Log.cpp
    src/Window.cpp
    src/EventSystem/EventScopedToken.cpp
    src/RendererSystem/Vulkan/VulkanPipeline.cpp
    src/RendererSystem/Vulkan/VulkanInstance.cpp

  PUBLIC
    FILE_SET HEADERS
      BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/include
      FILES
        include/Mararuana/Application.h
        include/Mararuana/Core/Core.h
        include/Mararuana/EntryPoint.h
        include/Mararuana/Log.h
        include/Mararuana/Mararuana.h
        include/Mararuana/Window.h
        include/Mararuana/EventSystem/EventBase.h
        include/Mararuana/EventSystem/EventBuffer.h
        include/Mararuana/EventSystem/EventManager.h
        include/Mararuana/EventSystem/EventScopedToken.h
        include/Mararuana/EventSystem/EventSystem.h
        include/Mararuana/EventSystem/BaseDerivedEvents/BaseDerivedEvents.h
        include/Mararuana/EventSystem/BaseDerivedEvents/ExternalEvents.h
        include/Mararuana/EventSystem/BaseDerivedEvents/KeyEvents.h
        include/Mararuana/EventSystem/BaseDerivedEvents/MouseEvents.h
        include/Mararuana/EventSystem/BaseDerivedEvents/WindowEvents.h
        include/Mararuana/RendererSystem/Vulkan/VulkanPipeline.h
        include/Mararuana/RendererSystem/Vulkan/VulkanInstance.h
        include/Mararuana/Core/Version.h
        include/Mararuana/Core/Definitions.h
        include/Mararuana/Core/Compiler.h
        include/Mararuana/Core/Assert.h
        include/Mararuana/Core/Intrinsics.h
)

target_link_libraries(${MARARUANA_LIB_NAME}
  PUBLIC
    Vulkan::Vulkan
    spdlog::spdlog
)

# ------------------------------------------------------------------------------
# The app's include/ (always visible to the engine — no manual path to set)
# ------------------------------------------------------------------------------
# EventBase.h does "#include MAR_CUSTOM_EVENTS_H", which resolves to a
# relative path such as "CustomEvents/CustomEvents.h" inside the APP's
# include/ — only the app knows what that header actually contains, the
# engine doesn't. Without this, compiling the engine itself fails to find
# the header, since EventBase.h gets compiled as part of the engine's own
# translation units too.
#
# PRIVATE: this only needs to satisfy the engine's own compilation. The
# app already sees its own include/ directly (mar_configure_target() adds
# it when called from the app's own CMakeLists.txt), so it doesn't need
# this passed back to it.
#
# MARARUANA_APP_ROOT is declared once, in cmake/MararuanaOptions.cmake —
# change it there if some other app ever takes over from Sandbox.
if(NOT MAR_CUSTOM_EVENTS_HEADER)
    message(FATAL_ERROR "MAR_CUSTOM_EVENTS_HEADER is empty (see cmake/MararuanaOptions.cmake).")
endif()

set(MARARUANA_CUSTOM_EVENTS_PATH "${MARARUANA_APP_ROOT}/include/${MAR_CUSTOM_EVENTS_HEADER}")
if(NOT EXISTS "${MARARUANA_CUSTOM_EVENTS_PATH}" OR IS_DIRECTORY "${MARARUANA_CUSTOM_EVENTS_PATH}")
    message(FATAL_ERROR
        "Mararuana-Engine: could not find the header the app is supposed to provide at "
        "'${MARARUANA_CUSTOM_EVENTS_PATH}'.\n"
        "An empty file is enough to get the engine compiling — fill in real events later.\n"
        "MARARUANA_APP_ROOT is currently '${MARARUANA_APP_ROOT}' and "
        "MAR_CUSTOM_EVENTS_HEADER is currently '${MAR_CUSTOM_EVENTS_HEADER}' "
        "(see cmake/MararuanaOptions.cmake).")
endif()

target_include_directories(${MARARUANA_LIB_NAME} PRIVATE
    "${MARARUANA_APP_ROOT}/include"
)

# Applies MAR_ENABLE_WINDOW / MAR_SIMD_LEVEL / MAR_CUSTOM_EVENTS_HEADER
# (declared in cmake/MararuanaOptions.cmake) and wires up
# Mararuana-Engine/res/ automatically if it exists — see
# cmake/MarResources.cmake. Called here, not from Sandbox, so the engine
# stays correctly configured even with MARARUANA_BUILD_APP=OFF, and so
# CMAKE_CURRENT_SOURCE_DIR points at the right folder (Mararuana-Engine/,
# not Sandbox/) when resolving include/ and res/.
mar_configure_target(${MARARUANA_LIB_NAME})

# ------------------------------------------------------------------------------
# Version
# ------------------------------------------------------------------------------
# Exposed as plain compile definitions rather than a generated header, so
# this doesn't assume anything about how include/Mararuana/Core/Version.h
# is currently written — wire these into it however that file expects.
target_compile_definitions(${MARARUANA_LIB_NAME} PUBLIC
    MARARUANA_ENGINE_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    MARARUANA_ENGINE_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    MARARUANA_ENGINE_VERSION_PATCH=${PROJECT_VERSION_PATCH}
    MARARUANA_ENGINE_VERSION_STRING=\"${PROJECT_VERSION}\"
)

target_compile_definitions(${MARARUANA_LIB_NAME} PRIVATE 
_WIN32_WINNT=0x0A00
)

# ------------------------------------------------------------------------------
# Shader pipeline (GLSL -> SPIR-V)
# ------------------------------------------------------------------------------
# Shaders are the app's data, the same way CustomEvents is the app's code:
# every shader file under the app's res/shaders/ — at any depth, in any
# number of subfolders — is compiled to SPIR-V automatically and merged
# into any executable's final res/, mirroring the same subfolder layout.
# Nothing to list by hand; see cmake/MarShaders.cmake. End result:
# shader.vert.spv (and e.g. Player/outline.vert.spv) just show up next to
# Sandbox.exe after the build, no manual step needed.
mar_compile_shaders(${MARARUANA_LIB_NAME}
    "${MARARUANA_APP_ROOT}/res/shaders"
    "${CMAKE_CURRENT_BINARY_DIR}/res"
)