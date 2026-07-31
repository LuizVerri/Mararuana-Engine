# ==============================================================================
#  Sandbox — example app / engine playground
# ------------------------------------------------------------------------------
#  A minimal executable used during development to exercise and demo the
#  Mararuana-Engine.
#
#  The engine's configuration options (MAR_ENABLE_WINDOW, MAR_SIMD_LEVEL,
#  MAR_CUSTOM_EVENTS_HEADER) and MARARUANA_APP_ROOT are declared once, in
#  cmake/MararuanaOptions.cmake — not here. That keeps the engine and
#  Sandbox reading the exact same values (avoiding an ODR mismatch in the
#  event enum — see the note in cmake/MarTargetConfig.cmake) and keeps the
#  engine correctly configured even with MARARUANA_BUILD_APP=OFF.
#  mar_option, mar_configure_target, and mar_target_resources are already
#  available here automatically, included by the root CMakeLists.txt.
# ==============================================================================

project(Sandbox
  VERSION     0.1.0
  DESCRIPTION "Example application for Mararuana-Engine"
  LANGUAGES   CXX
)

set(SANDBOX_NAME Sandbox CACHE INTERNAL "Name of the Sandbox target")

add_executable(${SANDBOX_NAME})

target_sources(${SANDBOX_NAME}
  PRIVATE
    src/main.cpp

  PUBLIC
    FILE_SET HEADERS
      BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/include
      FILES
        include/CustomEvents/ItemPickupEvent.h
        include/CustomEvents/CustomEvents.h
)

target_link_libraries(${SANDBOX_NAME}
  PRIVATE
    Mararuana-Engine
)

# Applies MAR_ENABLE_WINDOW / MAR_SIMD_LEVEL / MAR_CUSTOM_EVENTS_HEADER to
# this target and wires up Sandbox/res/ automatically if it exists —
# including a post-build copy of both Sandbox's own res/ and the engine's
# (propagated via the target_link_libraries above) next to the final
# executable. Has to come AFTER target_link_libraries so the propagation
# logic can see Mararuana-Engine as an already-linked dependency. See
# cmake/MarTargetConfig.cmake and cmake/MarResources.cmake.
mar_configure_target(${SANDBOX_NAME})

# ------------------------------------------------------------------------------
# Version
# ------------------------------------------------------------------------------
target_compile_definitions(${SANDBOX_NAME} PRIVATE
    MARARUANA_APP_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    MARARUANA_APP_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    MARARUANA_APP_VERSION_PATCH=${PROJECT_VERSION_PATCH}
    MARARUANA_APP_VERSION_STRING=\"${PROJECT_VERSION}\"
)