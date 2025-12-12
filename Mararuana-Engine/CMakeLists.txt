# Subproject CMake file for Mararuana-Engine
# Add sources, targets and project-specific settings.

# Add executable target and its source files
add_executable(Mararuana-Engine
  "src/Mararuana-Engine.cpp"
)

# Require C++20 when CMake supports target property setting
if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET Mararuana-Engine PROPERTY CXX_STANDARD 20)
  set_property(TARGET Mararuana-Engine PROPERTY CXX_STANDARD_REQUIRED ON)
  set_property(TARGET Mararuana-Engine PROPERTY CXX_EXTENSIONS OFF)
endif()

# Example: allow users to enable extra warnings or sanitizers via options
# (uncomment / adapt as needed)
# option(ENABLE_WARNINGS "Enable extra compiler warnings" ON)
# if(ENABLE_WARNINGS)
#   target_compile_options(Mararuana-Engine PRIVATE
#     $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
#     $<$<CXX_COMPILER_ID:MSVC>:/W4>
#   )
# endif()
