#pragma once

#include <cstdint>
#include <vector>

#include <windows.h>
#include <vulkan/vulkan.h>

namespace Mar {

    // Graphics pipeline wrapper. Non-owning of the VkDevice (the device
    // outlives every pipeline built from it), sole owner of the resulting
    // VkPipeline handle.
    //
    // Render pass and pipeline layout are taken as dependencies rather than
    // created here: they're shared across many pipelines and depend on
    // descriptor-set / attachment layouts decided elsewhere in the
    // renderer, so building them again per-pipeline would mean duplicated
    // ownership for no reason.
    class VulkanPipeline
    {
    public:
        VulkanPipeline(VkDevice device,
            VkRenderPass renderPass,
            VkPipelineLayout pipelineLayout,
            const char* vertFilepath,
            const char* fragFilepath);
        ~VulkanPipeline();

        VulkanPipeline(const VulkanPipeline&) = delete;
        VulkanPipeline& operator=(const VulkanPipeline&) = delete;

        VulkanPipeline(VulkanPipeline&& other) noexcept;
        VulkanPipeline& operator=(VulkanPipeline&& other) noexcept;

        [[nodiscard]] VkPipeline getHandle() const noexcept { return m_GraphicsPipeline; }
        [[nodiscard]] bool isValid() const noexcept { return m_GraphicsPipeline != VK_NULL_HANDLE; }

    private:
        // Reads raw SPIR-V bytecode from disk. SPIR-V is a stream of
        // 32-bit words — the type VkShaderModuleCreateInfo::pCode expects —
        // so codeSize (bytes) is later derived as code.size() * 4 rather
        // than tracked separately.
        [[nodiscard]] static std::vector<uint32_t> readFile(const char* filepath);
        [[nodiscard]] VkShaderModule createShaderModule(const std::vector<uint32_t>& code) const;
        void createGraphicsPipeline(VkRenderPass renderPass,
            VkPipelineLayout pipelineLayout,
            const char* vertFilepath,
            const char* fragFilepath);

        VkDevice   m_Device = VK_NULL_HANDLE; // non-owning
        VkPipeline m_GraphicsPipeline = VK_NULL_HANDLE; // owning
    };

} // namespace Mar