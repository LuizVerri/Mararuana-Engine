#include "Mararuana/RendererSystem/Vulkan/VulkanInstance.h"
#include "Mararuana/Log.h"

#include <cstring>
#include <utility>
#include <vector>

// Platform surface extension setup lives here, not in the header, so any
// windows.h macro pollution is confined to this single translation unit.
//
// MAR_PLATFORM_WINDOWS / MAR_PLATFORM_LINUX are meant to be defined by
// CMake — one authoritative source for platform selection instead of every
// file re-deriving it from compiler-native macros. The #ifndef fallback
// below only kicks in if the build system hasn't defined it yet.
#ifndef MAR_PLATFORM_WINDOWS
#   ifdef _WIN32
#       define MAR_PLATFORM_WINDOWS 1
#   endif
#endif

#if defined(MAR_PLATFORM_WINDOWS)
#   ifndef WIN32_LEAN_AND_MEAN
#       define WIN32_LEAN_AND_MEAN
#   endif
#   ifndef NOMINMAX
#       define NOMINMAX
#   endif
#   ifndef VK_USE_PLATFORM_WIN32_KHR
#       define VK_USE_PLATFORM_WIN32_KHR
#   endif
#   include <vulkan/vulkan_win32.h>
#endif

namespace Mar {

    namespace {

        // Validation layers add real per-call overhead — never on in a
        // shipped build.
#if !defined(NDEBUG)
        constexpr bool kEnableValidationLayers = true;
#else
        constexpr bool kEnableValidationLayers = false;
#endif

        constexpr const char* kValidationLayerName = "VK_LAYER_KHRONOS_validation";

        // Stateless capability queries — neither of these touches instance
        // state, so both live here as free functions rather than as
        // private static members of VulkanInstance.
        bool isValidationLayerSupported(const char* layerName)
        {
            uint32_t layerCount = 0;
            vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

            std::vector<VkLayerProperties> availableLayers(layerCount);
            vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

            for (const VkLayerProperties& layer : availableLayers) {
                if (std::strcmp(layer.layerName, layerName) == 0) {
                    return true;
                }
            }
            return false;
        }

        bool isExtensionSupported(const char* extensionName)
        {
            uint32_t count = 0;
            vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);

            std::vector<VkExtensionProperties> extensions(count);
            vkEnumerateInstanceExtensionProperties(nullptr, &count, extensions.data());

            for (const VkExtensionProperties& ext : extensions) {
                if (std::strcmp(ext.extensionName, extensionName) == 0) {
                    return true;
                }
            }
            return false;
        }

        VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
            VkDebugUtilsMessageSeverityFlagBitsEXT severity,
            VkDebugUtilsMessageTypeFlagsEXT /*type*/,
            const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
            void* /*userData*/)
        {
            switch (severity) {
            case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
                MAR_CORE_ERROR("[Vulkan] {}", callbackData->pMessage);
                break;
            case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
                MAR_CORE_WARN("[Vulkan] {}", callbackData->pMessage);
                break;
            case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
                MAR_CORE_INFO("[Vulkan] {}", callbackData->pMessage);
                break;
            default:
                MAR_CORE_TRACE("[Vulkan] {}", callbackData->pMessage);
                break;
            }
            // VK_FALSE = don't abort the call that triggered this message —
            // always correct for a logging sink.
            return VK_FALSE;
        }

        void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo)
        {
            createInfo = {};
            createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
            createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT
                | VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT
                | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
                | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
                | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
                | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            createInfo.pfnUserCallback = debugCallback;
        }

        VkResult createDebugUtilsMessengerEXT(
            VkInstance instance,
            const VkDebugUtilsMessengerCreateInfoEXT* createInfo,
            const VkAllocationCallbacks* allocator,
            VkDebugUtilsMessengerEXT* messenger)
        {
            auto func = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
            if (func == nullptr) {
                return VK_ERROR_EXTENSION_NOT_PRESENT;
            }
            return func(instance, createInfo, allocator, messenger);
        }

        void destroyDebugUtilsMessengerEXT(
            VkInstance instance,
            VkDebugUtilsMessengerEXT messenger,
            const VkAllocationCallbacks* allocator)
        {
            auto func = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
            if (func != nullptr) {
                func(instance, messenger, allocator);
            }
        }

    } // unnamed namespace

    VulkanInstance::VulkanInstance()
    {
        // 1. Confirm the requested validation layer actually exists on this
        //    system before asking the driver to enable it — requesting a
        //    layer that isn't installed fails instance creation entirely.
        const bool layersAvailable = kEnableValidationLayers && isValidationLayerSupported(kValidationLayerName);
        if (kEnableValidationLayers && !layersAvailable) {
            MAR_CORE_WARN("Validation layer '{}' requested but not available on this system; continuing without it.", kValidationLayerName);
        }

        // The messenger needs VK_EXT_debug_utils specifically, separate
        // from the layer itself — some minimal loaders ship the layer
        // without it.
        const bool debugUtilsAvailable = layersAvailable && isExtensionSupported(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

        // 2. This engine targets Vulkan 1.4, but the loader on the running
        //    machine may only support an older version — clamp instead of
        //    handing the driver a version number it doesn't understand.
        uint32_t supportedApiVersion = VK_API_VERSION_1_0;
        vkEnumerateInstanceVersion(&supportedApiVersion);
        if (supportedApiVersion < VK_API_VERSION_1_4) {
            MAR_CORE_WARN("Vulkan loader only supports up to {}.{}.{}; engine targets 1.4.",
                VK_API_VERSION_MAJOR(supportedApiVersion),
                VK_API_VERSION_MINOR(supportedApiVersion),
                VK_API_VERSION_PATCH(supportedApiVersion));
        }
        const uint32_t targetApiVersion = (supportedApiVersion < VK_API_VERSION_1_4) ? supportedApiVersion : VK_API_VERSION_1_4;

        VkApplicationInfo appInfo{};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = ENGINE_NAME;
        appInfo.applicationVersion = VK_MAKE_API_VERSION(0, ENGINE_VERSION_MAJOR, ENGINE_VERSION_MINOR, ENGINE_VERSION_PATCH);
        appInfo.pEngineName = ENGINE_NAME;
        appInfo.engineVersion = VK_MAKE_API_VERSION(0, ENGINE_VERSION_MAJOR, ENGINE_VERSION_MINOR, ENGINE_VERSION_PATCH);
        appInfo.apiVersion = targetApiVersion;

        // 3. Required instance extensions: surface + platform surface
        //    backend, plus debug-utils when validation is on. A vector
        //    (built once at startup) instead of a fixed array so the
        //    conditional extension can just be appended.
        std::vector<const char*> requiredExtensions = {
            VK_KHR_SURFACE_EXTENSION_NAME,
#if defined(MAR_PLATFORM_WINDOWS)
            VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
#endif
        };
        if (debugUtilsAvailable) {
            requiredExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }

        VkInstanceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo = &appInfo;
        createInfo.enabledExtensionCount = static_cast<uint32_t>(requiredExtensions.size());
        createInfo.ppEnabledExtensionNames = requiredExtensions.data();
        createInfo.enabledLayerCount = layersAvailable ? 1u : 0u;
        createInfo.ppEnabledLayerNames = layersAvailable ? &kValidationLayerName : nullptr;

        VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
        if (debugUtilsAvailable) {
            // Chaining onto pNext also covers vkCreateInstance /
            // vkDestroyInstance themselves; the separate messenger created
            // below only covers everything *between* those two calls.
            populateDebugMessengerCreateInfo(debugCreateInfo);
            createInfo.pNext = &debugCreateInfo;
        }

        const VkResult result = vkCreateInstance(&createInfo, nullptr, &m_VkInstance);
        MAR_CORE_ASSERT(result == VK_SUCCESS, "Failed to create VkInstance (VkResult = {}).", static_cast<int>(result));

        if (debugUtilsAvailable) {
            createDebugMessenger();
        }
    }

    VulkanInstance::~VulkanInstance()
    {
        // Order matters: the messenger must be destroyed before the
        // instance it was registered against.
        destroyDebugMessenger();
        if (m_VkInstance != VK_NULL_HANDLE) {
            vkDestroyInstance(m_VkInstance, nullptr);
        }
    }

    VulkanInstance::VulkanInstance(VulkanInstance&& other) noexcept
        : m_VkInstance(std::exchange(other.m_VkInstance, VK_NULL_HANDLE))
        , m_DebugMessenger(std::exchange(other.m_DebugMessenger, VK_NULL_HANDLE))
    {
    }

    VulkanInstance& VulkanInstance::operator=(VulkanInstance&& other) noexcept
    {
        if (this != &other) {
            destroyDebugMessenger();
            if (m_VkInstance != VK_NULL_HANDLE) {
                vkDestroyInstance(m_VkInstance, nullptr);
            }
            m_VkInstance = std::exchange(other.m_VkInstance, VK_NULL_HANDLE);
            m_DebugMessenger = std::exchange(other.m_DebugMessenger, VK_NULL_HANDLE);
        }
        return *this;
    }

    void VulkanInstance::createDebugMessenger()
    {
        VkDebugUtilsMessengerCreateInfoEXT createInfo{};
        populateDebugMessengerCreateInfo(createInfo);

        const VkResult result = createDebugUtilsMessengerEXT(m_VkInstance, &createInfo, nullptr, &m_DebugMessenger);
        MAR_CORE_ASSERT(result == VK_SUCCESS, "Failed to create VkDebugUtilsMessengerEXT (VkResult = {}).", static_cast<int>(result));
    }

    void VulkanInstance::destroyDebugMessenger()
    {
        if (m_DebugMessenger != VK_NULL_HANDLE) {
            destroyDebugUtilsMessengerEXT(m_VkInstance, m_DebugMessenger, nullptr);
            m_DebugMessenger = VK_NULL_HANDLE;
        }
    }

} // namespace Mar