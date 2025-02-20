// Unit test to try out vulkan graphic with variations
// Based on https://github.com/Erkaman/vulkan_minimal_compute

#include "vulkan_common.h"
#include "vulkan_graphics_common.h"

// contains our compute shader, generated with:
//   glslangValidator -V vulkan_graphics_1.vert -o vulkan_graphics_1_vert.spirv
//   xxd -i vulkan_graphics_1_vert.spirv > vulkan_graphics_1_vert.inc
//   glslangValidator -V vulkan_graphics_1.frag -o vulkan_graphics_1_frag.spirv
//   xxd -i vulkan_graphics_1_frag.spirv > vulkan_graphics_1_frag.inc

#include "vulkan_graphics_1_vert.inc"
#include "vulkan_graphics_1_frag.inc"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <chrono>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

static bool indirect = false;
static int indirectOffset = 0; // in units of indirect structs

struct pushconstants
{
	float width;
	float height;
};

static void show_usage()
{
//	graphics_usage();
	printf("TBD\n");
}

static bool test_cmdopt(int& i, int argc, char** argv, vulkan_req_t& reqs)
{
	if (match(argv[i], "-ioff", "--indirect-offset"))
	{
		indirectOffset = get_arg(argv, ++i, argc);
		return true;
	}
	else if (match(argv[i], "-I", "--indirect"))
	{
		indirect = true;
		return true;
	}
	return graphic_cmdopt(i, argc, argv, reqs);
}

using namespace tracetooltests;

// ------------------------------ benchmark definition ------------------------
typedef struct Vertex {
    glm::vec3 pos;
    glm::vec3 color;
    glm::vec2 texCoord;
}Vertex;

const static std::vector<Vertex> vertices = {
    {{-0.5f, -0.5f, 0.1f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
    {{0.5f, -0.5f, 0.1f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}},
    {{0.5f, 0.5f, 0.1f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
    {{-0.5f, 0.5f, 0.1f}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f}},
    
    {{-0.5f, -0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
    {{0.5f, -0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}},
    {{0.5f, 0.5f, -0.5f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
    {{-0.5f, 0.5f, -0.5f}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f}}
};

const static std::vector<uint16_t> indices = {
        0, 1, 2, 2, 3, 0,
        4, 5, 6, 6, 7, 4
};

typedef struct Transform {
    alignas(16) glm::mat4 model;
    alignas(16) glm::mat4 view;
    alignas(16) glm::mat4 proj;    
} Transform;

class benchmarkContext : public GraphicContext
{
public:
    benchmarkContext() : GraphicContext(){}
    ~benchmarkContext() { destroy(); }
    void destroy()
    {
        m_vertexBuffer = nullptr;
        m_indexBuffer = nullptr;
        m_transformUniformBuffer = nullptr;
        m_bgSampler = nullptr;
        m_bgImageView = nullptr;
        m_pipeline = nullptr;
        m_descriptor = nullptr;

        if (m_frameFence != VK_NULL_HANDLE)
        {
            vkDestroyFence(m_vulkanSetup.device, m_frameFence, nullptr);
            m_frameFence = VK_NULL_HANDLE;
        }
    }

    // contexts and resources related with benchmark
    uint32_t width = 0 ;
    uint32_t height = 0;
    std::shared_ptr<Buffer> m_vertexBuffer;
    std::shared_ptr<Buffer> m_indexBuffer;
    std::shared_ptr<Buffer> m_transformUniformBuffer;

    std::shared_ptr<Sampler> m_bgSampler;
    std::shared_ptr<ImageView> m_bgImageView;

    std::shared_ptr<GraphicPipeline> m_pipeline;
    std::shared_ptr<DescriptorSet> m_descriptor;

    VkFence m_frameFence = VK_NULL_HANDLE;

};

static benchmarkContext g_benchmark;
static void render(const vulkan_setup_t& vulkan);

int main(int argc, char** argv)
{
	vulkan_req_t req;
	req.usage = show_usage;
	req.cmdopt = test_cmdopt;
	vulkan_setup_t vulkan = test_init(argc, argv, "vulkan_graphics_1", req);

    p__loops = 1;

    g_benchmark.initBasic(vulkan, req);

	const uint32_t width = static_cast<uint32_t>(std::get<int>(req.options.at("width")));
	const uint32_t height = static_cast<uint32_t>(std::get<int>(req.options.at("height")));
    g_benchmark.width = width;
    g_benchmark.height = height;

    // ------------------------ vulkan resources created -----------------------------

    /*************************** shader module **************************************/
    auto vertShader = std::make_shared<Shader>(vulkan.device);
    vertShader->create(vulkan_graphics_1_vert_spirv, vulkan_graphics_1_vert_spirv_len);

    auto fragShader = std::make_shared<Shader>(vulkan.device);
    fragShader->create(vulkan_graphics_1_frag_spirv, vulkan_graphics_1_frag_spirv_len);

    /******************* vertex/index buffers & uniform buffers *********************/
    VkDeviceSize size;
    // vbo
    size = sizeof(Vertex)*vertices.size();
    auto vertexBuffer = std::make_shared<Buffer>(vulkan.device);
    vertexBuffer->create(VK_BUFFER_USAGE_TRANSFER_DST_BIT|VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, size, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    g_benchmark.updateBuffer(vertices, vertexBuffer);

    // index buffer
    size = sizeof(uint16_t)*indices.size();
    auto indexBuffer = std::make_shared<Buffer>(vulkan.device);
    indexBuffer->create(VK_BUFFER_USAGE_TRANSFER_DST_BIT|VK_BUFFER_USAGE_INDEX_BUFFER_BIT, size, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    g_benchmark.updateBuffer(indices, indexBuffer);

    // ubo
    auto transformUniformBuffer = std::make_shared<Buffer>(vulkan.device);
    transformUniformBuffer->create(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, (VkDeviceSize)sizeof(Transform), VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    transformUniformBuffer->map();

    /******************** sampled texture image for background **********************/
    // loading image
    int texWidth, texHeight, texChannels;
    stbi_uc* pixels = stbi_load("texture/girl.jpg", &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
    VkDeviceSize imageSize = texWidth * texHeight * 4;

    if (!pixels) {
        throw std::runtime_error("failed to load texture image!");
    }

    // create image/imageview to be sampled as the background
    // image/imageView shared_ptr is stored in RenderPass' attachmentInfo
    auto bgImage = std::make_shared<Image>(vulkan.device);
    bgImage->create( {(uint32_t)texWidth, (uint32_t)texHeight, 1}, VK_FORMAT_R8G8B8A8_SRGB, 
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    auto bgImageView = std::make_shared<ImageView>(bgImage);
    bgImageView->create(VK_IMAGE_VIEW_TYPE_2D /*, VK_IMAGE_ASPECT_COLOR_BIT*/);

    // create sampler for bg image. sampler ptr is stored in benchmark
    auto bgSampler = std::make_shared<Sampler>(vulkan.device);
    bgSampler->create(VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT,
            req.samplerAnisotropy, vulkan.device_properties.limits.maxSamplerAnisotropy);

    g_benchmark.updateImage((char*)pixels, imageSize, bgImage, {(uint32_t)texWidth, (uint32_t)texHeight, 1});

    /*********************** initialize data and submit staging commandBuffer ***************************/
    g_benchmark.submitStaging(true, {}, {}, false);

    // ---------------------------- descriptor setup ---------------------------------

    /******************************* descriptor *************************************/
    // descriptorSet Layout: 
    //   insert each binding according to the shader resources
    auto mainDescSetLayout = std::make_shared<DescriptorSetLayout>(vulkan.device);
    mainDescSetLayout->insertBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT);
    mainDescSetLayout->insertBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT);
    mainDescSetLayout->create();
    // if there's one more set in shader, create another SetLayout.

    // descriptorPool
    #define MAX_DESCRIPTOR_SET_SIZE 4
    auto mainDescSetPool = std::make_shared<DescriptorSetPool>(mainDescSetLayout);
    mainDescSetPool->create(MAX_DESCRIPTOR_SET_SIZE);

    // descritorSet
    auto descriptor = std::make_shared<DescriptorSet>(mainDescSetPool);
    descriptor->create();
    //configure descriptor set, and then update
    descriptor->setBuffer(0, transformUniformBuffer);  //layout(set=0,binding=0) uniform transformBuffer { }
    descriptor->setCombinedImageSampler(1, bgImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, bgSampler);  //layout(set=0, binding=1) uniform sampler2D
    descriptor->update(); // automatically upate according to your setting above


    // ------------------------- graphic pipeline setup ------------------------------

    /***************************** pipeline layout **********************************/
    std::unordered_map<uint32_t, std::shared_ptr<DescriptorSetLayout>>  //set num --> descriptorSetLayout
            layoutMap = { {0, mainDescSetLayout} };

    auto pipelineLayout = std::make_shared<PipelineLayout>(vulkan.device);
    pipelineLayout->create(layoutMap, {});


    /*************************** pipeline shader stage ******************************/
    ShaderPipelineState vertShaderState(VK_SHADER_STAGE_VERTEX_BIT, vertShader);
    ShaderPipelineState fragShaderState(VK_SHADER_STAGE_FRAGMENT_BIT, fragShader);

    /*************************** graphicPipeline state*******************************/
    // input vertext
    GraphicPipelineState pipelineState;
    pipelineState.setVertexBinding(0, vertexBuffer, sizeof(Vertex)); // vertexBuffer 
    pipelineState.setVertexAttribute(0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, Vertex::pos));  //pos, color and texCoord Attrib in vertexBuffer
    pipelineState.setVertexAttribute(1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, Vertex::color));
    pipelineState.setVertexAttribute(2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, Vertex::texCoord));

    pipelineState.setDynamic(0, VK_DYNAMIC_STATE_VIEWPORT);
    pipelineState.setDynamic(0, VK_DYNAMIC_STATE_SCISSOR);


    /******************************** render pass **********************************/
    // images used as attachments
    auto colorImage = std::make_shared<Image>(vulkan.device);
    colorImage->create({width, height, 1}, VK_FORMAT_R8G8B8A8_UNORM, 
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    auto colorImageView = std::make_shared<ImageView>(colorImage);
    colorImageView->create(VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT);

    auto depthImage = std::make_shared<Image>(vulkan.device);
    depthImage->create({width, height, 1}, VK_FORMAT_D32_SFLOAT, 
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    auto depthImageView = std::make_shared<ImageView>(depthImage);
    depthImageView->create(VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_DEPTH_BIT);

    // attachments: set VkAttachmentDescription
    AttachmentInfo color{ 0, colorImageView, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    AttachmentInfo depth{ 1, depthImageView, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

    // subpass: set VkAttachmentReference
    SubpassInfo subpass{};
    subpass.addColorAttachment(color, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    subpass.setDepthStencilAttachment(depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

    // create renderpass
    auto renderpass = std::make_shared<RenderPass>(vulkan.device);
    renderpass->create({color,depth}, {subpass});

    VkPipelineColorBlendAttachmentState colorBlendAttachment;
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_TRUE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    //continue: graphicPipeline state 
    pipelineState.setColorBlendAttachment(color.m_location, colorBlendAttachment);


    /********************************** framebuffer *********************************/
    auto framebuffer = std::make_shared<FrameBuffer>(vulkan.device);
    framebuffer->create(renderpass, {width, height});

    /*************************** graphic pipeline creation **************************/
    std::vector<ShaderPipelineState> shaderStages = {vertShaderState, fragShaderState};
    auto pipeline = std::make_shared<GraphicPipeline>(pipelineLayout);
    pipeline->create(shaderStages, pipelineState, renderpass);

    /****************************** save all resources ******************************/
    g_benchmark.m_vertexBuffer = std::move(vertexBuffer);
    g_benchmark.m_indexBuffer = std::move(indexBuffer);
    g_benchmark.m_transformUniformBuffer = std::move(transformUniformBuffer);

    g_benchmark.m_bgSampler = std::move(bgSampler);
    g_benchmark.m_bgImageView = std::move(bgImageView);
    g_benchmark.m_descriptor = std::move(descriptor);

    g_benchmark.m_pipeline = std::move(pipeline);

    g_benchmark.m_renderPass = std::move(renderpass);
    g_benchmark.m_framebuffer = std::move(framebuffer);

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    vkCreateFence(vulkan.device, &fenceInfo, nullptr, &g_benchmark.m_frameFence);

    /********************************** rendering ***********************************/
    render(vulkan);
	test_done(vulkan);

    return 1;
}

void updateTransformData(std::shared_ptr<Buffer> dstBuffer)
{
    static auto startTime = std::chrono::high_resolution_clock::now();

    auto currentTime = std::chrono::high_resolution_clock::now();
    float time = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();

    Transform ubo{};
    ubo.model = glm::rotate(glm::mat4(1.0f), time*glm::radians(60.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    ubo.view = glm::lookAt(glm::vec3(2.0f, 1.0f, 1.0f), glm::vec3(0.2f, -0.1f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    ubo.proj = glm::perspective(glm::radians(45.0f), g_benchmark.width / (float) g_benchmark.height, 0.1f, 10.0f);
    ubo.proj[1][1] *= -1;

    assert(dstBuffer->m_mappedAddress!=nullptr);
    memcpy(dstBuffer->m_mappedAddress, &ubo, sizeof(ubo));
}

static void render(const vulkan_setup_t& vulkan)
{
    while (p__loops--)
    {
        VkCommandBuffer defaultCmd = g_benchmark.m_defaultCommandBuffer->getHandle();

        vkWaitForFences(vulkan.device, 1, &g_benchmark.m_frameFence, VK_TRUE, UINT64_MAX);
        updateTransformData(g_benchmark.m_transformUniformBuffer);

        vkResetFences(vulkan.device, 1, &g_benchmark.m_frameFence);
        vkResetCommandBuffer(defaultCmd, 0);

        g_benchmark.m_defaultCommandBuffer->begin();
        g_benchmark.m_defaultCommandBuffer->beginRenderPass(g_benchmark.m_renderPass, g_benchmark.m_framebuffer);

        vkCmdBindPipeline(defaultCmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_benchmark.m_pipeline->getHandle());
        //one alternative: g_benchmark.m_defaultCommandBuffer->bindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, g_benchmark.m_pipeline);

        // bind vertex buffer to bindings
        VkBuffer vertexBuffers[] = {g_benchmark.m_vertexBuffer->getHandle()};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(defaultCmd, 0, 1, vertexBuffers, offsets);
        // bind index buffer
        vkCmdBindIndexBuffer(defaultCmd, g_benchmark.m_indexBuffer->getHandle(), 0, VK_INDEX_TYPE_UINT16);

        VkDescriptorSet descriptor = g_benchmark.m_descriptor->getHandle();
        vkCmdBindDescriptorSets(defaultCmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_benchmark.m_pipeline->m_pipelineLayout->getHandle(), 0, 1, &descriptor, 0, nullptr);

        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(g_benchmark.width);
        viewport.height = static_cast<float>(g_benchmark.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(defaultCmd, 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = {g_benchmark.width, g_benchmark.height};
        vkCmdSetScissor(defaultCmd, 0, 1, &scissor);
        vkCmdDrawIndexed(defaultCmd, static_cast<uint32_t>(indices.size()), 1, 0, 0, 0);

        g_benchmark.m_defaultCommandBuffer->endRenderPass();
        g_benchmark.m_defaultCommandBuffer->end();

        // submit
        g_benchmark.submit(g_benchmark.m_defaultQueue, std::vector<std::shared_ptr<CommandBuffer>>{g_benchmark.m_defaultCommandBuffer}, g_benchmark.m_frameFence);

    }
}

