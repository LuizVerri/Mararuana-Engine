# Subproject CMake file for Sandbox
# Add sources, targets and project-specific settings.

# Add executable target and its source files
add_executable(Sandbox)

target_sources(Sandbox
  PRIVATE
    src/main.cpp
)

target_link_libraries(Sandbox PRIVATE Mararuana-Engine)