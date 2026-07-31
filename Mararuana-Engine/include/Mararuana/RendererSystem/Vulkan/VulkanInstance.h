#pragma once

#include "Mararuana/Core/Core.h"

// Deliberately NOT defining VK_USE_PLATFORM_WIN32_KHR or including
// <windows.h> here. Doing that in a header drags windows.h (and its
// min/max/near/far macro soup) into every translation unit that includes
// this file — the same class of bug already hit in FastIO. Platform
// surface extensions are resolved inside VulkanInstance.cpp instead, where
// WIN32_LEAN_AND_MEAN / NOMINMAX can be scoped to a single .cpp.
#include <windows.h>

#include <vulkan/vulkan.h>

namespace Mar {

    // Thin RAII owner of a VkInstance (+ optional debug messenger).
    // Non-copyable: a VkInstance is a unique handle to driver-side state.
    // Movable: so it can be built once at startup and handed off without
    // an extra layer of indirection (e.g. std::unique_ptr<VulkanInstance>).
    class VulkanInstance
    {
    public:
        VulkanInstance();
        ~VulkanInstance();

        VulkanInstance(const VulkanInstance&) = delete;
        VulkanInstance& operator=(const VulkanInstance&) = delete;

        VulkanInstance(VulkanInstance&& other) noexcept;
        VulkanInstance& operator=(VulkanInstance&& other) noexcept;

        [[nodiscard]] VkInstance getHandle() const noexcept { return m_VkInstance; }
        [[nodiscard]] bool isValid() const noexcept { return m_VkInstance != VK_NULL_HANDLE; }

    private:
        void createDebugMessenger();
        void destroyDebugMessenger();

        VkInstance               m_VkInstance = VK_NULL_HANDLE; // owning
        VkDebugUtilsMessengerEXT m_DebugMessenger = VK_NULL_HANDLE; // owning
    };

} // namespace Mar