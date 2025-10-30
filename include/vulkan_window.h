#ifndef ZEROPOINT_VULKAN_WINDOW_H
#define ZEROPOINT_VULKAN_WINDOW_H

#include "display.h"
#include <vulkan/vulkan.h>
#include <vector>
#include <string>

namespace ZeroPoint {

class VulkanWindow {
public:
    VulkanWindow(int scale = 2);
    ~VulkanWindow();

    // Initialize Vulkan and create window
    bool init();

    // Render the display
    void render(const Display& display);

    // Poll events (keyboard, mouse, close)
    void pollEvents();

    // Check if window should close
    bool shouldClose() const { return quit; }

private:
    // Window scale factor
    int scale;
    bool quit;

    // Vulkan core objects
    VkInstance instance;
    VkDebugUtilsMessengerEXT debugMessenger;
    VkSurfaceKHR surface;
    VkPhysicalDevice physicalDevice;
    VkDevice device;
    VkQueue graphicsQueue;
    VkQueue presentQueue;

    // Swap chain
    VkSwapchainKHR swapChain;
    std::vector<VkImage> swapChainImages;
    std::vector<VkImageView> swapChainImageViews;
    std::vector<VkFramebuffer> swapChainFramebuffers;
    VkFormat swapChainImageFormat;
    VkExtent2D swapChainExtent;

    // Render pass and pipeline
    VkRenderPass renderPass;
    VkPipelineLayout pipelineLayout;
    VkPipeline graphicsPipeline;

    // Command buffers
    VkCommandPool commandPool;
    std::vector<VkCommandBuffer> commandBuffers;

    // Synchronization
    std::vector<VkSemaphore> imageAvailableSemaphores;
    std::vector<VkSemaphore> renderFinishedSemaphores;
    std::vector<VkFence> inFlightFences;
    size_t currentFrame;
    static constexpr int MAX_FRAMES_IN_FLIGHT = 2;

    // Texture for framebuffer
    VkImage textureImage;
    VkDeviceMemory textureImageMemory;
    VkImageView textureImageView;
    VkSampler textureSampler;

    // Staging buffer for texture uploads
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    void* stagingBufferMapped;

    // Descriptor sets
    VkDescriptorSetLayout descriptorSetLayout;
    VkDescriptorPool descriptorPool;
    std::vector<VkDescriptorSet> descriptorSets;

    // Vertex buffer for fullscreen quad
    VkBuffer vertexBuffer;
    VkDeviceMemory vertexBufferMemory;

    // Initialization helpers
    void createInstance();
    void setupDebugMessenger();
    void createSurface();
    void pickPhysicalDevice();
    void createLogicalDevice();
    void createSwapChain();
    void createImageViews();
    void createRenderPass();
    void createDescriptorSetLayout();
    void createGraphicsPipeline();
    void createFramebuffers();
    void createCommandPool();
    void createTextureImage();
    void createTextureImageView();
    void createTextureSampler();
    void createVertexBuffer();
    void createStagingBuffer();
    void createDescriptorPool();
    void createDescriptorSets();
    void createCommandBuffers();
    void createSyncObjects();

    // Cleanup helpers
    void cleanup();
    void cleanupSwapChain();

    // Utility functions
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
    VkShaderModule createShaderModule(const std::vector<uint8_t>& code);

    // Debug callback
    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
        VkDebugUtilsMessageTypeFlagsEXT messageType,
        const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
        void* pUserData);
};

} // namespace ZeroPoint

#endif // ZEROPOINT_VULKAN_WINDOW_H
