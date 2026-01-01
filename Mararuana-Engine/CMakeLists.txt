# Subproject CMake file for Mararuana-Engine
# Add sources, targets and project-specific settings.

# Add executable target and its source files
add_library(Mararuana-Engine)

target_sources(Mararuana-Engine
  PRIVATE
    src/Mararuana/Application.cpp
  PUBLIC
    FILE_SET HEADERS
    BASE_DIRS src
    FILES
      src/Mararuana/Application.h
      src/Mararuana/Core.h
      src/Mararuana.h
)