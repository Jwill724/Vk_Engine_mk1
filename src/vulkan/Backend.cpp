#include "pch.h"

#include "Backend.h"
#include "renderer/Renderer.h"
#include "PipelineManager.h"
#include "imgui/EditorImgui.h"
#include "input/UserInput.h"
#include "Engine.h"

namespace Backend {
	VkInstance _instance = VK_NULL_HANDLE;
	VkInstance& getInstance() { return _instance; }

	VkDebugUtilsMessengerEXT _debugMessenger = VK_NULL_HANDLE;

	VkSurfaceKHR _surface = VK_NULL_HANDLE;
	VkSurfaceKHR& getSurface() { return _surface; }

	VkPhysicalDevice _physicalDevice = VK_NULL_HANDLE;
	VkPhysicalDevice& getPhysicalDevice() { return _physicalDevice; }

	VkDevice _device = VK_NULL_HANDLE;
	VkDevice& getDevice() { return _device; }

	QueueFamilyIndices _queueFamilyIndices;
	QueueFamilyIndices& getQueueFamilyIndices() { return _queueFamilyIndices; }

	VkQueue _graphicsQueue = VK_NULL_HANDLE;
	VkQueue& getGraphicsQueue() { return _graphicsQueue; }

	VkQueue _presentQueue = VK_NULL_HANDLE;
	VkQueue& getPresentQueue() { return _presentQueue; }

	// created but inactive for now
	VkQueue _transferQueue = VK_NULL_HANDLE;
	VkQueue& getTransferQueue() { return _transferQueue; }

	VkSwapchainKHR _swapchain = VK_NULL_HANDLE;
	std::vector<VkImage> _swapchainImages;
	// A view of image and describes how to access
	std::vector<VkImageView> _swapchainImageViews;
	VkFormat _swapchainImageFormat;
	VkExtent2D _swapchainExtent;

	// TODO: swapchain state struct
	VkSwapchainKHR& getSwapchain() { return _swapchain; }
	VkExtent2D& getSwapchainExtent() { return _swapchainExtent; }
	std::vector<VkImageView>& getSwapchainImageViews() { return _swapchainImageViews; }
	std::vector<VkImage>& getSwapchainImages() { return _swapchainImages; }
	VkFormat& getSwapchainImageFormat() { return _swapchainImageFormat; }

	void createInstance();
	void createSurface();
	void pickPhysicalDevice();
	void createLogicalDevice();
	void createSwapchain();
	void cleanupSwapchain();
	void createImageViews();
	VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities);
}

void Backend::initBackend() {
	createInstance();
	BackendTools::setupDebugMessenger(_instance, _debugMessenger);
	createSurface();
	pickPhysicalDevice();
	createLogicalDevice();

	// setup allocations for engine and renderer
	Engine::getAllocator() = VulkanUtils::createAllocator(_physicalDevice, _device, _instance);
	// the engine deletion queue is flushed once at cleanup
	Engine::getDeletionQueue().push_function([&]() {
		vmaDestroyAllocator(Engine::getAllocator());
	});

	Renderer::getRenderImageAllocator() = VulkanUtils::createAllocator(_physicalDevice, _device, _instance);

	Backend::createSwapchain();
	Backend::createImageViews();
	Renderer::setupRenderImages();

	DescriptorSetOverwatch::initImageDescriptors();

	PipelineManager::initPipelines();
	EditorImgui::initImgui();
}

void Backend::createInstance() {
	if (BackendTools::enableValidationLayers) {
		if (!BackendTools::checkValidationLayerSupport()) {
			std::cout << "Validation layers requested, but not available!" << std::endl;
		}
	}

	VkApplicationInfo appInfo{};
	appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	appInfo.pApplicationName = "Pen";
	appInfo.applicationVersion = VK_MAKE_VERSION(1, 3, 0);
	appInfo.pEngineName = "Engine";
	appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
	appInfo.apiVersion = VK_API_VERSION_1_3;

	VkInstanceCreateInfo createInfo{};
	createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	createInfo.pApplicationInfo = &appInfo;

	auto reqExtensions = BackendTools::getRequiredExtensions();
	createInfo.enabledExtensionCount = static_cast<uint32_t>(reqExtensions.size());
	createInfo.ppEnabledExtensionNames = reqExtensions.data();

	VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
	if (BackendTools::enableValidationLayers) {
		createInfo.enabledLayerCount = static_cast<uint32_t>(BackendTools::validationLayers.size());
		createInfo.ppEnabledLayerNames = BackendTools::validationLayers.data();

		BackendTools::populateDebugMessengerCreateInfo(debugCreateInfo);
		createInfo.pNext = &debugCreateInfo;
	}
	else {
		createInfo.enabledLayerCount = 0;
		createInfo.pNext = nullptr;
	}

	VK_CHECK(vkCreateInstance(&createInfo, nullptr, &_instance));
}

void Backend::createSurface() {
	VK_CHECK(glfwCreateWindowSurface(_instance, Engine::getWindow(), nullptr, &_surface));
}

void Backend::pickPhysicalDevice() {
	uint32_t deviceCount = 0;
	vkEnumeratePhysicalDevices(_instance, &deviceCount, nullptr);

	if (deviceCount == 0) {
		throw std::runtime_error("Failed to find GPUs with Vulkan support!");
	}

	std::vector<VkPhysicalDevice> devices(deviceCount);

	vkEnumeratePhysicalDevices(_instance, &deviceCount, devices.data());
	for (const auto& device : devices) {
		if (BackendTools::isDeviceSuitable(device, _surface)) {
			_physicalDevice = device;
			// can be used throughout code now
			_queueFamilyIndices = VulkanUtils::FindQueueFamilies(_physicalDevice, _surface);

			Renderer::getAvailableSampleCounts() = VulkanUtils::findSupportedSampleCounts(_physicalDevice);
			break;
		}
	}

	if (_physicalDevice == VK_NULL_HANDLE) {
		throw std::runtime_error("Failed to find a suitable GPU!");
	}
}

void Backend::createLogicalDevice() {
	std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
	std::set<uint32_t> uniqueQueueFamilies;

	// Only add queue families that exist
	if (_queueFamilyIndices.graphicsFamily.has_value())
		uniqueQueueFamilies.insert(_queueFamilyIndices.graphicsFamily.value());
	if (_queueFamilyIndices.presentFamily.has_value())
		uniqueQueueFamilies.insert(_queueFamilyIndices.presentFamily.value());
	if (_queueFamilyIndices.transferFamily.has_value())
		uniqueQueueFamilies.insert(_queueFamilyIndices.transferFamily.value());

	float queuePriority = 1.0f;
	for (uint32_t queueFamily : uniqueQueueFamilies) {
		VkDeviceQueueCreateInfo queueCreateInfo{};
		queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		queueCreateInfo.queueFamilyIndex = queueFamily;
		queueCreateInfo.queueCount = 1;
		queueCreateInfo.pQueuePriorities = &queuePriority;
		queueCreateInfos.push_back(queueCreateInfo);
	}

	VkPhysicalDeviceFeatures2 baseFeatures{};
	baseFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
	baseFeatures.features.fillModeNonSolid = VK_TRUE;
	baseFeatures.features.samplerAnisotropy = VK_TRUE;

	// vulkan 1.2 features
	VkPhysicalDeviceVulkan12Features features12{};
	features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
	features12.bufferDeviceAddress = VK_TRUE;
	features12.descriptorIndexing = VK_TRUE;
	features12.scalarBlockLayout = VK_TRUE;

	// vulkan 1.3 features
	VkPhysicalDeviceVulkan13Features features13{};
	features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
	features13.dynamicRendering = VK_TRUE;
	features13.synchronization2 = VK_TRUE;

	baseFeatures.pNext = &features12;
	features12.pNext = &features13;

	VkDeviceCreateInfo createInfo{};
	createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	createInfo.pNext = &baseFeatures;
	createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
	createInfo.pQueueCreateInfos = queueCreateInfos.data();
	createInfo.enabledExtensionCount = static_cast<uint32_t>(BackendTools::deviceExtensions.size());
	createInfo.ppEnabledExtensionNames = BackendTools::deviceExtensions.data();
	createInfo.enabledLayerCount = BackendTools::enableValidationLayers ? static_cast<uint32_t>(BackendTools::validationLayers.size()) : 0;
	createInfo.ppEnabledLayerNames = BackendTools::enableValidationLayers ? BackendTools::validationLayers.data() : nullptr;

	VK_CHECK(vkCreateDevice(_physicalDevice, &createInfo, nullptr, &_device));

	// Retrieve queues only if they were created
	if (_queueFamilyIndices.graphicsFamily.has_value())
		vkGetDeviceQueue(_device, _queueFamilyIndices.graphicsFamily.value(), 0, &_graphicsQueue);
	if (_queueFamilyIndices.presentFamily.has_value())
		vkGetDeviceQueue(_device, _queueFamilyIndices.presentFamily.value(), 0, &_presentQueue);
	if (_queueFamilyIndices.transferFamily.has_value())
		vkGetDeviceQueue(_device, _queueFamilyIndices.transferFamily.value(), 0, &_transferQueue);
}

void Backend::createSwapchain() {
	BackendTools::SwapChainSupportDetails swapChainSupport = BackendTools::querySwapChainSupport(_physicalDevice, _surface);
	VkSurfaceFormatKHR surfaceFormat = BackendTools::chooseSwapSurfaceFormat(swapChainSupport.formats);
	// TODO: Can only run proper fps with v-sync
	VkPresentModeKHR presentMode = BackendTools::chooseSwapSurfacePresentMode(swapChainSupport.presentModes);
	VkExtent2D extent = chooseSwapExtent(swapChainSupport.capabilities);

	// sticking to min delays, request one more than min
	uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;

	if (swapChainSupport.capabilities.maxImageCount > 0 &&
		imageCount > swapChainSupport.capabilities.maxImageCount) {
		imageCount = swapChainSupport.capabilities.maxImageCount;
	}

	VkSwapchainCreateInfoKHR createInfo{};
	createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	createInfo.surface = _surface;
	createInfo.minImageCount = imageCount;
	createInfo.imageFormat = surfaceFormat.format;
	createInfo.imageColorSpace = surfaceFormat.colorSpace;
	createInfo.imageExtent = extent;
	createInfo.imageArrayLayers = 1;
	createInfo.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	createInfo.preTransform = swapChainSupport.capabilities.currentTransform;
	createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	createInfo.presentMode = presentMode;
	createInfo.clipped = VK_TRUE;

	uint32_t qFamIndices[] = {
		_queueFamilyIndices.graphicsFamily.value(),
		_queueFamilyIndices.presentFamily.value()
	};

	if (_queueFamilyIndices.graphicsFamily != _queueFamilyIndices.presentFamily) {
		// Images can be used across multiple queue families without explicit ownership transfer
		createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
		createInfo.queueFamilyIndexCount = 2;
		createInfo.pQueueFamilyIndices = qFamIndices;
	}
	else {
		// An image is owned by one queue family, ownership transfer ship is explicit
		// best performance
		createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
		createInfo.queueFamilyIndexCount = 0;
		createInfo.pQueueFamilyIndices = nullptr;
	}

	createInfo.oldSwapchain = _swapchain;

	VK_CHECK(vkCreateSwapchainKHR(_device, &createInfo, nullptr, &_swapchain));

	if (createInfo.oldSwapchain != VK_NULL_HANDLE) {
		vkDestroySwapchainKHR(_device, createInfo.oldSwapchain, nullptr);
	}

	vkGetSwapchainImagesKHR(_device, _swapchain, &imageCount, nullptr);
	_swapchainImages.resize(imageCount);
	vkGetSwapchainImagesKHR(_device, _swapchain, &imageCount, _swapchainImages.data());

	_swapchainImageFormat = surfaceFormat.format;
	_swapchainExtent = extent;
}

void Backend::resizeSwapchain() {
	cleanupSwapchain();

	Engine::windowModMode().updateWindowSize();

	createSwapchain();
	createImageViews();

	Engine::windowModMode().windowResized = false;
}

void Backend::cleanupSwapchain() {
	// do not ever touch this in any way or it breaks
	if (_swapchain != VK_NULL_HANDLE) {
		vkDestroySwapchainKHR(_device, _swapchain, nullptr);
		_swapchain = VK_NULL_HANDLE;
	}

	for (size_t i = 0; i < _swapchainImageViews.size(); i++) {
		vkDestroyImageView(_device, _swapchainImageViews[i], nullptr);
	}
}

VkExtent2D Backend::chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities) {
	if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
		return capabilities.currentExtent;
	}
	else {
		int width, height;
		glfwGetFramebufferSize(Engine::getWindow(), &width, &height);

		VkExtent2D actualExtent = {
			static_cast<uint32_t>(width),
			static_cast<uint32_t>(height)
		};
		actualExtent.width = std::clamp(actualExtent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
		actualExtent.height = std::clamp(actualExtent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);

		return actualExtent;
	}
}

void Backend::createImageViews() {
	_swapchainImageViews.resize(_swapchainImages.size());

	for (uint32_t i = 0; i < _swapchainImages.size(); i++) {
		_swapchainImageViews[i] = BufferUtils::createImageView(_device, _swapchainImages[i], _swapchainImageFormat, VK_IMAGE_ASPECT_COLOR_BIT, 1);
	}
}

void Backend::cleanupBackend() {

	Renderer::cleanup();

	cleanupSwapchain();

	vkDestroySurfaceKHR(_instance, _surface, nullptr);

	vkDestroyDevice(_device, nullptr);

	if (BackendTools::enableValidationLayers) {
		BackendTools::DestroyDebugUtilsMessengerEXT(_instance, _debugMessenger, nullptr);
	}
	vkDestroyInstance(_instance, nullptr);
}