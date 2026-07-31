#include "Mararuana/RendererSystem/Vulkan/VulkanPipeline.h"
#include "Mararuana/Core/Core.h"

#include <array>
#include <cstdio>
#include <utility>

namespace Mar {

    namespace {

        inline FILE* safeFOpen(const char* filepath, const char* mode)
        {
#if defined(_MSC_VER)
            FILE* file = nullptr;
            fopen_s(&file, filepath, mode);
            return file;
#else
            return std::fopen(filepath, mode);
#endif
        }

    } // unnamed namespace

    VulkanPipeline::VulkanPipeline(VkDevice device,
        VkRenderPass renderPass,
        VkPipelineLayout pipelineLayout,
        const char* vertFilepath,
        const char* fragFilepath)
        : m_Device(device)
    {
        MAR_CORE_ASSERT(device != VK_NULL_HANDLE, "VulkanPipeline constructed with an invalid VkDevice.");
        createGraphicsPipeline(renderPass, pipelineLayout, vertFilepath, fragFilepath);
    }

    VulkanPipeline::~VulkanPipeline()
    {
        if (m_GraphicsPipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(m_Device, m_GraphicsPipeline, nullptr);
        }
    }

    VulkanPipeline::VulkanPipeline(VulkanPipeline&& other) noexcept
        : m_Device(std::exchange(other.m_Device, VK_NULL_HANDLE))
        , m_GraphicsPipeline(std::exchange(other.m_GraphicsPipeline, VK_NULL_HANDLE))
    {
    }

    VulkanPipeline& VulkanPipeline::operator=(VulkanPipeline&& other) noexcept
    {
        if (this != &other) {
            if (m_GraphicsPipeline != VK_NULL_HANDLE) {
                vkDestroyPipeline(m_Device, m_GraphicsPipeline, nullptr);
            }
            m_Device = std::exchange(other.m_Device, VK_NULL_HANDLE);
            m_GraphicsPipeline = std::exchange(other.m_GraphicsPipeline, VK_NULL_HANDLE);
        }
        return *this;
    }

    std::vector<uint32_t> VulkanPipeline::readFile(const char* filepath)
    {
        FILE* file = safeFOpen(filepath, "rb");
        MAR_CORE_ASSERT(file != nullptr, "Failed to open shader file: {}", filepath);

        MAR_CORE_ASSERT(std::fseek(file, 0, SEEK_END) == 0, "Failed to seek in shader file: {}", filepath);
        const long fileSizeLong = std::ftell(file);
        MAR_CORE_ASSERT(fileSizeLong > 0, "Invalid or empty shader file: {}", filepath);

        const std::size_t fileSize = static_cast<std::size_t>(fileSizeLong);
        std::rewind(file);

        MAR_CORE_ASSERT(fileSize % sizeof(uint32_t) == 0, "Corrupted SPIR-V (size not a multiple of 4): {}", filepath);
        const std::size_t wordCount = fileSize / sizeof(uint32_t);

        // std::vector owns the buffer from here on — no explicit
        // free()/delete[] needed, and no risk of leaking it on an
        // early-return path added later.
        std::vector<uint32_t> buffer(wordCount);
        const std::size_t wordsRead = std::fread(buffer.data(), sizeof(uint32_t), wordCount, file);
        std::fclose(file);

        MAR_CORE_ASSERT(wordsRead == wordCount, "Failed to fully read shader file from disk: {}", filepath);

        return buffer;
    }

    VkShaderModule VulkanPipeline::createShaderModule(const std::vector<uint32_t>& code) const
    {
        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = code.size() * sizeof(uint32_t);
        createInfo.pCode = code.data();

        VkShaderModule shaderModule = VK_NULL_HANDLE;
        const VkResult result = vkCreateShaderModule(m_Device, &createInfo, nullptr, &shaderModule);
        MAR_CORE_ASSERT(result == VK_SUCCESS, "Failed to create VkShaderModule (VkResult = {}).", static_cast<int>(result));

        return shaderModule;
    }

    void VulkanPipeline::createGraphicsPipeline(VkRenderPass renderPass,
        VkPipelineLayout pipelineLayout,
        const char* vertFilepath,
        const char* fragFilepath)
    {
        const std::vector<uint32_t> vertCode = readFile(vertFilepath);
        const std::vector<uint32_t> fragCode = readFile(fragFilepath);

        const VkShaderModule vertModule = createShaderModule(vertCode);
        const VkShaderModule fragModule = createShaderModule(fragCode);

        VkPipelineShaderStageCreateInfo vertStageInfo{};
        vertStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertStageInfo.module = vertModule;
        vertStageInfo.pName = "main";

        VkPipelineShaderStageCreateInfo fragStageInfo{};
        fragStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragStageInfo.module = fragModule;
        fragStageInfo.pName = "main";

        const std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages{ vertStageInfo, fragStageInfo };

        // --- Fixed-function state ---
        // Vertex input is application-specific — depends on the actual
        // vertex struct these shaders expect — so it's left empty on
        // purpose. Plug in real VkVertexInputBindingDescription /
        // VkVertexInputAttributeDescription arrays before using this for
        // anything that reads per-vertex data.
        VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
        vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        inputAssembly.primitiveRestartEnable = VK_FALSE;

        // Viewport/scissor are dynamic so a window resize doesn't force a
        // full pipeline rebuild.
        const std::array<VkDynamicState, 2> dynamicStates{ VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.depthClampEnable = VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
        rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
        rasterizer.depthBiasEnable = VK_FALSE;

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.sampleShadingEnable = VK_FALSE;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
            | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachment.blendEnable = VK_FALSE;

        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.logicOpEnable = VK_FALSE;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
        pipelineInfo.pStages = shaderStages.data();
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = pipelineLayout;
        pipelineInfo.renderPass = renderPass;
        pipelineInfo.subpass = 0;
        pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

        const VkResult result = vkCreateGraphicsPipelines(
            m_Device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_GraphicsPipeline);

        // Shader modules are only needed while building the pipeline — the
        // driver copies/compiles the SPIR-V internally — so they're
        // destroyed here regardless of outcome, not kept around.
        vkDestroyShaderModule(m_Device, fragModule, nullptr);
        vkDestroyShaderModule(m_Device, vertModule, nullptr);

        MAR_CORE_ASSERT(result == VK_SUCCESS, "Failed to create graphics pipeline (VkResult = {}).", static_cast<int>(result));
    }

} // namespace Mar