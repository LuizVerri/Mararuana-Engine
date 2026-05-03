# cmake/spdlog.cmake
include(FetchContent)

FetchContent_Declare(
  spdlog
  GIT_REPOSITORY https://github.com/gabime/spdlog.git
  GIT_TAG v1.16.0
)

# Optional: If you want to use spdlog only as a header-only
set(SPDLOG_BUILD_ALL OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(spdlog)
