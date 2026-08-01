#pragma once

// Engine identity and version metadata.
//
// ENGINE_VERSION_* is the version of Mar itself.
// SET_APP_VERSION is called by the application embedding Mar to stamp its
// own version — e.g. for a window title or a Vulkan
// VkApplicationInfo::applicationVersion.

#define ENGINE_NAME "Mararuana-Engine"
#define ENGINE_VERSION_MAJOR 0
#define ENGINE_VERSION_MINOR 10
#define ENGINE_VERSION_PATCH 0

#define SET_APP_VERSION(major, minor, patch) \
    constexpr int APP_VERSION_MAJOR = (major); \
    constexpr int APP_VERSION_MINOR = (minor); \
    constexpr int APP_VERSION_PATCH = (patch);
