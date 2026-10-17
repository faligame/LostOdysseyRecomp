// SPDX-License-Identifier: GPL-3.0-or-later
// Uses the statically linked Mesa ICD directly: no Linux loader or SDL WSI.
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include "probe.h"
#include <algorithm>
#include <cstdio>
#include <vector>
extern "C" PFN_vkVoidFunction vk_icdGetInstanceProcAddr(VkInstance, const char*);
extern "C" int sceKernelUsleep(unsigned);

namespace {
#define INSTANCE_COMMANDS(X) \
 X(DestroyInstance) X(EnumeratePhysicalDevices) X(GetPhysicalDeviceProperties) \
 X(GetPhysicalDeviceFeatures2) X(GetPhysicalDeviceMemoryProperties) \
 X(GetPhysicalDeviceQueueFamilyProperties) X(CreateDevice) X(GetDeviceProcAddr) \
 X(GetPhysicalDeviceDisplayPropertiesKHR) X(GetDisplayModePropertiesKHR) \
 X(GetPhysicalDeviceDisplayPlanePropertiesKHR) X(GetDisplayPlaneSupportedDisplaysKHR) \
 X(GetDisplayPlaneCapabilitiesKHR) X(CreateDisplayPlaneSurfaceKHR) X(DestroySurfaceKHR) \
 X(GetPhysicalDeviceSurfaceSupportKHR) X(GetPhysicalDeviceSurfaceCapabilitiesKHR) \
 X(GetPhysicalDeviceSurfaceFormatsKHR)
#define DEVICE_COMMANDS(X) \
 X(DestroyDevice) X(DeviceWaitIdle) X(GetDeviceQueue) X(CreateSwapchainKHR) \
 X(DestroySwapchainKHR) X(GetSwapchainImagesKHR) X(AcquireNextImageKHR) X(QueuePresentKHR) \
 X(CreateSemaphore) X(DestroySemaphore) X(CreateCommandPool) X(DestroyCommandPool) \
 X(AllocateCommandBuffers) X(ResetCommandBuffer) X(BeginCommandBuffer) X(EndCommandBuffer) \
 X(CmdPipelineBarrier) X(CmdClearColorImage) X(QueueSubmit) X(QueueWaitIdle)
#define DECLARE(name) PFN_vk##name name = nullptr;
struct Vulkan {
    INSTANCE_COMMANDS(DECLARE)
    DEVICE_COMMANDS(DECLARE)
    VkInstance instance{};
    VkDevice device{};
    VkSurfaceKHR surface{};
    VkSwapchainKHR swapchain{};
    VkCommandPool pool{};
    VkSemaphore acquired{};
    std::vector<VkSemaphore> rendered;
    ~Vulkan() {
        if (device && DeviceWaitIdle) DeviceWaitIdle(device);
        if (acquired && DestroySemaphore) DestroySemaphore(device, acquired, nullptr);
        if (DestroySemaphore) for (auto sem : rendered) if (sem) DestroySemaphore(device, sem, nullptr);
        if (pool && DestroyCommandPool) DestroyCommandPool(device, pool, nullptr);
        if (swapchain && DestroySwapchainKHR) DestroySwapchainKHR(device, swapchain, nullptr);
        if (device && DestroyDevice) DestroyDevice(device, nullptr);
        if (surface && DestroySurfaceKHR) DestroySurfaceKHR(instance, surface, nullptr);
        if (instance && DestroyInstance) DestroyInstance(instance, nullptr);
    }
};
#undef DECLARE
bool Check(VkResult result, const char* call) {
    if (result == VK_SUCCESS) return true;
    std::fprintf(stderr, "vulkan.%s=FAIL VkResult=%d\n", call, result);
    return false;
}
#define CHECK(call) if (!Check((call), #call)) return false
}

bool ProbeVulkan()
{
    Vulkan v;
    const auto create = reinterpret_cast<PFN_vkCreateInstance>(
        vk_icdGetInstanceProcAddr(nullptr, "vkCreateInstance"));
    if (!create) return false;
    const char* extensions[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_DISPLAY_EXTENSION_NAME};
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "Lost Odyssey PS5 probe";
    app.apiVersion = VK_API_VERSION_1_2; // Current portable pack's SPIR-V target.
    VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ici.pApplicationInfo = &app;
    ici.enabledExtensionCount = 2;
    ici.ppEnabledExtensionNames = extensions;
    CHECK(create(&ici, nullptr, &v.instance));
#define LOAD_INSTANCE(name) v.name = reinterpret_cast<PFN_vk##name>(vk_icdGetInstanceProcAddr(v.instance, "vk" #name)); \
    if (!v.name) { std::fprintf(stderr, "vulkan.missing=vk%s\n", #name); return false; }
    INSTANCE_COMMANDS(LOAD_INSTANCE)
#undef LOAD_INSTANCE
    uint32_t count = 0;
    CHECK(v.EnumeratePhysicalDevices(v.instance, &count, nullptr));
    if (!count) return false;
    std::vector<VkPhysicalDevice> devices(count);
    CHECK(v.EnumeratePhysicalDevices(v.instance, &count, devices.data()));
    VkPhysicalDevice gpu = devices[0];
    VkPhysicalDeviceProperties properties{};
    v.GetPhysicalDeviceProperties(gpu, &properties);
    std::fprintf(stderr, "vulkan.device=%s api=%u.%u.%u\n", properties.deviceName,
        VK_VERSION_MAJOR(properties.apiVersion), VK_VERSION_MINOR(properties.apiVersion), VK_VERSION_PATCH(properties.apiVersion));
    VkPhysicalDeviceVulkan12Features f12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    features.pNext = &f12;
    v.GetPhysicalDeviceFeatures2(gpu, &features);
    std::fprintf(stderr, "vulkan.features geometry=%u independentBlend=%u scalarLayout=%u BDA=%u runtimeArray=%u partiallyBound=%u variableDescriptors=%u\n",
        features.features.geometryShader, features.features.independentBlend, f12.scalarBlockLayout,
        f12.bufferDeviceAddress, f12.runtimeDescriptorArray, f12.descriptorBindingPartiallyBound,
        f12.descriptorBindingVariableDescriptorCount);
    VkPhysicalDeviceMemoryProperties memory{};
    v.GetPhysicalDeviceMemoryProperties(gpu, &memory);
    for (uint32_t i = 0; i < memory.memoryHeapCount; ++i)
        std::fprintf(stderr, "vulkan.heap[%u]=%llu flags=0x%x\n", i,
            static_cast<unsigned long long>(memory.memoryHeaps[i].size), memory.memoryHeaps[i].flags);

    CHECK(v.GetPhysicalDeviceDisplayPropertiesKHR(gpu, &count, nullptr));
    if (!count) return false;
    std::vector<VkDisplayPropertiesKHR> displays(count);
    CHECK(v.GetPhysicalDeviceDisplayPropertiesKHR(gpu, &count, displays.data()));
    const auto display = displays[0].display;
    CHECK(v.GetDisplayModePropertiesKHR(gpu, display, &count, nullptr));
    if (!count) return false;
    std::vector<VkDisplayModePropertiesKHR> modes(count);
    CHECK(v.GetDisplayModePropertiesKHR(gpu, display, &count, modes.data()));
    auto mode = modes[0];
    for (const auto& m : modes) {
        std::fprintf(stderr, "vulkan.mode=%ux%u@%u_mHz\n", m.parameters.visibleRegion.width,
            m.parameters.visibleRegion.height, m.parameters.refreshRate);
        if (m.parameters.visibleRegion.width == 1280 && m.parameters.visibleRegion.height == 720) mode = m;
    }
    CHECK(v.GetPhysicalDeviceDisplayPlanePropertiesKHR(gpu, &count, nullptr));
    std::vector<VkDisplayPlanePropertiesKHR> planes(count);
    CHECK(v.GetPhysicalDeviceDisplayPlanePropertiesKHR(gpu, &count, planes.data()));
    uint32_t plane = UINT32_MAX;
    for (uint32_t i = 0; i < planes.size(); ++i) {
        uint32_t n = 0;
        CHECK(v.GetDisplayPlaneSupportedDisplaysKHR(gpu, i, &n, nullptr));
        std::vector<VkDisplayKHR> supported(n);
        CHECK(v.GetDisplayPlaneSupportedDisplaysKHR(gpu, i, &n, supported.data()));
        if (std::find(supported.begin(), supported.end(), display) != supported.end()) { plane = i; break; }
    }
    if (plane == UINT32_MAX) return false;
    VkDisplayPlaneCapabilitiesKHR pc{};
    CHECK(v.GetDisplayPlaneCapabilitiesKHR(gpu, mode.displayMode, plane, &pc));
    if (!(pc.supportedAlpha & VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR) ||
        !(displays[0].supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)) return false;
    VkDisplaySurfaceCreateInfoKHR sci{VK_STRUCTURE_TYPE_DISPLAY_SURFACE_CREATE_INFO_KHR};
    sci.displayMode = mode.displayMode;
    sci.planeIndex = plane;
    sci.planeStackIndex = planes[plane].currentStackIndex;
    sci.transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    sci.globalAlpha = 1;
    sci.alphaMode = VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR;
    sci.imageExtent = mode.parameters.visibleRegion;
    CHECK(v.CreateDisplayPlaneSurfaceKHR(v.instance, &sci, nullptr, &v.surface));
    v.GetPhysicalDeviceQueueFamilyProperties(gpu, &count, nullptr);
    std::vector<VkQueueFamilyProperties> queues(count);
    v.GetPhysicalDeviceQueueFamilyProperties(gpu, &count, queues.data());
    uint32_t family = UINT32_MAX;
    for (uint32_t i = 0; i < count; ++i) {
        VkBool32 present = false;
        CHECK(v.GetPhysicalDeviceSurfaceSupportKHR(gpu, i, v.surface, &present));
        if (present && (queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) { family = i; break; }
    }
    if (family == UINT32_MAX) return false;
    float priority = 1;
    VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex = family; qci.queueCount = 1; qci.pQueuePriorities = &priority;
    const char* swapExtension = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.queueCreateInfoCount = 1; dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = 1; dci.ppEnabledExtensionNames = &swapExtension;
    CHECK(v.CreateDevice(gpu, &dci, nullptr, &v.device));
#define LOAD_DEVICE(name) v.name = reinterpret_cast<PFN_vk##name>(v.GetDeviceProcAddr(v.device, "vk" #name)); \
    if (!v.name) { std::fprintf(stderr, "vulkan.missing=vk%s\n", #name); return false; }
    DEVICE_COMMANDS(LOAD_DEVICE)
#undef LOAD_DEVICE
    VkQueue queue{};
    v.GetDeviceQueue(v.device, family, 0, &queue);
    VkSurfaceCapabilitiesKHR caps{};
    CHECK(v.GetPhysicalDeviceSurfaceCapabilitiesKHR(gpu, v.surface, &caps));
    CHECK(v.GetPhysicalDeviceSurfaceFormatsKHR(gpu, v.surface, &count, nullptr));
    std::vector<VkSurfaceFormatKHR> formats(count);
    CHECK(v.GetPhysicalDeviceSurfaceFormatsKHR(gpu, v.surface, &count, formats.data()));
    auto format = std::find_if(formats.begin(), formats.end(), [](auto f) {
        return f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR &&
            (f.format == VK_FORMAT_B8G8R8A8_UNORM || f.format == VK_FORMAT_R8G8B8A8_UNORM);
    });
    if (format == formats.end() || !(caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT)) return false;
    VkSwapchainCreateInfoKHR swap{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    swap.surface = v.surface;
    swap.minImageCount = std::max(3u, caps.minImageCount);
    if (caps.maxImageCount) swap.minImageCount = std::min(swap.minImageCount, caps.maxImageCount);
    swap.imageFormat = format->format; swap.imageColorSpace = format->colorSpace;
    swap.imageExtent = caps.currentExtent.width == UINT32_MAX ? sci.imageExtent : caps.currentExtent;
    swap.imageArrayLayers = 1; swap.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    swap.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swap.preTransform = caps.currentTransform;
    swap.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if (!(caps.supportedCompositeAlpha & swap.compositeAlpha)) return false;
    swap.presentMode = VK_PRESENT_MODE_FIFO_KHR; swap.clipped = true;
    CHECK(v.CreateSwapchainKHR(v.device, &swap, nullptr, &v.swapchain));
    CHECK(v.GetSwapchainImagesKHR(v.device, v.swapchain, &count, nullptr));
    std::vector<VkImage> images(count);
    CHECK(v.GetSwapchainImagesKHR(v.device, v.swapchain, &count, images.data()));
    VkSemaphoreCreateInfo sem{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    CHECK(v.CreateSemaphore(v.device, &sem, nullptr, &v.acquired));
    // Presentation can outlive QueueWaitIdle. Reuse a render-finished semaphore
    // only after acquiring its own swapchain image again.
    v.rendered.resize(images.size());
    for (auto& ready : v.rendered) CHECK(v.CreateSemaphore(v.device, &sem, nullptr, &ready));
    VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool.queueFamilyIndex = family; pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    CHECK(v.CreateCommandPool(v.device, &pool, nullptr, &v.pool));
    VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    alloc.commandPool = v.pool; alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; alloc.commandBufferCount = 1;
    VkCommandBuffer cmd{};
    CHECK(v.AllocateCommandBuffers(v.device, &alloc, &cmd));
    for (unsigned frame = 0; frame < 90; ++frame) {
        uint32_t index = 0;
        CHECK(v.AcquireNextImageKHR(v.device, v.swapchain, 5000000000ull, v.acquired, VK_NULL_HANDLE, &index));
        CHECK(v.ResetCommandBuffer(cmd, 0));
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        CHECK(v.BeginCommandBuffer(cmd, &begin));
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = images[index]; barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        v.CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
            0, nullptr, 0, nullptr, 1, &barrier);
        VkClearColorValue color{{0.05f, 0.12f + frame / 180.0f, 0.3f, 1.0f}};
        v.CmdClearColorImage(cmd, images[index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &color, 1, &barrier.subresourceRange);
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; barrier.dstAccessMask = 0;
        v.CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0,
            0, nullptr, 0, nullptr, 1, &barrier);
        CHECK(v.EndCommandBuffer(cmd));
        VkPipelineStageFlags stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.waitSemaphoreCount = 1; submit.pWaitSemaphores = &v.acquired; submit.pWaitDstStageMask = &stage;
        submit.commandBufferCount = 1; submit.pCommandBuffers = &cmd;
        submit.signalSemaphoreCount = 1; submit.pSignalSemaphores = &v.rendered[index];
        CHECK(v.QueueSubmit(queue, 1, &submit, VK_NULL_HANDLE));
        VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        present.waitSemaphoreCount = 1; present.pWaitSemaphores = &v.rendered[index];
        present.swapchainCount = 1; present.pSwapchains = &v.swapchain; present.pImageIndices = &index;
        CHECK(v.QueuePresentKHR(queue, &present));
        CHECK(v.QueueWaitIdle(queue)); // Deliberately serialized diagnostic, not a performance test.
        sceKernelUsleep(33333);
    }
    std::fprintf(stderr, "vulkan.present=PASS frames=90 SDR=%ux%u; no shader/game rendering tested\n",
        swap.imageExtent.width, swap.imageExtent.height);
    return true;
}
