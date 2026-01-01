# Top-level CMake project for Mararuana-Engine.
# Perform global configuration and include sub-projects.

cmake_minimum_required(VERSION 3.28)

# Policies that should be set early (before project() when they affect project behavior).
# This policy only works for CMake versions above 3.25.
if(POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
endif()

# Project declaration.
project(Mararuana-Engine LANGUAGES C CXX)

# Require C++20 for the whole tree.
# This may be changed later for greater portability; for now, the project will require C++20 only for a better workflow.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Set a default build type for single-config generators (e.g., Makefile, Ninja).
# This does not affect multi-config generators (Visual Studio, Xcode).
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
  #      <variable>     <value>  CACHE <type>        <docstring>         [FORCE]
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build." FORCE) 
endif()

# Compiler-specific options
if(MSVC)
  # Example MSVC debug-info control (policy CMP0141 handled above)
  if(POLICY CMP0141)
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT
        "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
  endif()
endif()

# Include sub-projects
add_subdirectory(Mararuana-Engine)
add_subdirectory(Sandbox)
