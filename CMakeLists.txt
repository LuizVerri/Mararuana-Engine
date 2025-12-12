# Top-level CMake project for Mararuana-Engine
# Perform global configuration and include sub-projects.

# Minimum required CMake version
cmake_minimum_required(VERSION 3.10)

# Set a default build type for single-config generators (e.g., Makefile, Ninja).
# This does not affect multi-config generators (Visual Studio, Xcode).
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Release")
endif()

# Enable MSVC hot-reload-friendly debug info format if the policy is available.
# Keeps IDE edit-and-continue experience for Debug / RelWithDebInfo when using MSVC.
if(POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT
      "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

# Project declaration
project(Mararuana-Engine LANGUAGES C CXX)

# Include sub-projects
add_subdirectory("Mararuana-Engine")
