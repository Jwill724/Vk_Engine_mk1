#include "pch.h"

#include "Renderer.h"
#include "renderer/CommandBuffer.h"
#include "vulkan/PipelineManager.h"
#include "imgui/EditorImgui.h"
#include "vulkan/Backend.h"
#include "RenderScene.h"
#include "RenderGraph.h"

uint32_t MSAACOUNT = 8u;

namespace Renderer {
	unsigned int _frameNumber{ 0 };

	FrameData _frames[MAX_FRAMES_IN_FLIGHT];
	FrameData& getCurrentFrame() { return _frames[_frameNumber % MAX_FRAMES_IN_FLIGHT]; }

	VkExtent3D _drawExtent;
	VkExtent3D& getDrawExtent() { return _drawExtent; }

	// primary render image
	AllocatedImage _drawImage;
	AllocatedImage& getDrawImage() { return _drawImage; }
	AllocatedImage _depthImage;
	AllocatedImage& getDepthImage() { return _depthImage; }
	AllocatedImage _msaaImage;
	AllocatedImage& getMSAAImage() { return _msaaImage; }

	AllocatedImage _postProcessImage;
	AllocatedImage& getPostProcessImage() { return _postProcessImage; }

	AllocatedImage _skyboxImage;
	AllocatedImage& getSkyBoxImage() { return _skyboxImage; }

	VkDescriptorSetLayout _drawImageDescriptorLayout;

	VkImageView& getDrawImageView() { return _drawImage.imageView; }
	DeletionQueue _renderImageDeletionQueue;
	DeletionQueue& getRenderImageDeletionQueue() { return _renderImageDeletionQueue; }

	VmaAllocator _renderImageAllocator;
	VmaAllocator& getRenderImageAllocator() { return _renderImageAllocator; }

	void recordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex);
	void colorCorrectPass(VkCommandBuffer cmd);
	void drawGeometry(VkCommandBuffer cmd);

	// Grabbed during physical device selection
	std::vector<VkSampleCountFlags> _availableSampleCounts;
	std::vector<VkSampleCountFlags>& getAvailableSampleCounts() { return _availableSampleCounts; }
}

void Renderer::setupRenderImages() {
	// draw image should match window extent
	_drawExtent = {
		static_cast<uint32_t>(Engine::getWindowExtent().width),
		static_cast<uint32_t>(Engine::getWindowExtent().height),
		1
	};

	_drawImage.imageFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
	_drawImage.imageExtent = _drawExtent;

	VkImageUsageFlags drawImageUsages{};
	drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	drawImageUsages |= VK_IMAGE_USAGE_STORAGE_BIT;
	drawImageUsages |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	drawImageUsages |= VK_IMAGE_USAGE_SAMPLED_BIT;

	// non sampled image
	// primary draw image color target
	RendererUtils::createRenderImage(_drawImage, drawImageUsages,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_SAMPLE_COUNT_1_BIT, &_renderImageDeletionQueue, _renderImageAllocator);

	// post process image
	_postProcessImage.imageFormat = _drawImage.imageFormat;
	_postProcessImage.imageExtent = _drawExtent;

	VkImageUsageFlags postUsages{};
	postUsages |= VK_IMAGE_USAGE_STORAGE_BIT;            // for compute shader write
	postUsages |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;       // to copy to swapchain
	postUsages |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;       // if needed in chain

	// compute draw image for post processing
	RendererUtils::createRenderImage(_postProcessImage, postUsages,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_SAMPLE_COUNT_1_BIT, &_renderImageDeletionQueue, _renderImageAllocator);

	VkSampleCountFlagBits sampleCount = static_cast<VkSampleCountFlagBits>(MSAACOUNT);

	_msaaImage.imageFormat = _drawImage.imageFormat;
	_msaaImage.imageExtent = _drawExtent;

	VkImageUsageFlags msaaImageUsages{};
	msaaImageUsages |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	msaaImageUsages |= VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;

	// msaa color attachment to the draw image
	RendererUtils::createRenderImage(_msaaImage, msaaImageUsages,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, sampleCount, &_renderImageDeletionQueue, _renderImageAllocator);


	// DEPTH
	_depthImage.imageFormat = VK_FORMAT_D32_SFLOAT;
	_depthImage.imageExtent = _drawExtent;

	VkImageUsageFlags depthImageUsages{};
	depthImageUsages |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

	RendererUtils::createRenderImage(_depthImage, depthImageUsages,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, sampleCount, &_renderImageDeletionQueue, _renderImageAllocator);


	// SKYBOX
	VkExtent3D skyboxCubeExtent = { 1024, 1024, 1 };
	_skyboxImage.imageExtent = skyboxCubeExtent;
	_skyboxImage.imageFormat = VK_FORMAT_R32G32B32A32_SFLOAT;
	_skyboxImage.isCubeMap = true;

	RendererUtils::createRenderImage(_skyboxImage, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_SAMPLE_COUNT_1_BIT, &_renderImageDeletionQueue, _renderImageAllocator);
}

void Renderer::init() {
	QueueFamilyIndices qFamIndices = Backend::getQueueFamilyIndices();

	for (auto& frame : _frames) {
		frame._graphicsCmdPool = CommandBuffer::createCommandPool(qFamIndices.graphicsFamily.value(), VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
		frame._graphicsCmdBuffer = CommandBuffer::createCommandBuffer(frame._graphicsCmdPool);

		frame._swapchainSemaphore = RendererUtils::createSemaphore();
		frame._renderSemaphore = RendererUtils::createSemaphore();
		frame._renderFence = RendererUtils::createFence();


		// create a descriptor pool
		std::vector<PoolSizeRatio> frameSizes = {
			{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 3 },
			{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3 },
			{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3 },
			{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4 },
		};

		frame._frameDescriptors = DescriptorManager{};
		frame._frameDescriptors.init(1000, frameSizes);

		Engine::getDeletionQueue().push_function([&]() {
			frame._frameDescriptors.destroyPools();
		});
	}

	DescriptorWriter drawImageWriter;
	drawImageWriter.writeImage(0, _postProcessImage.imageView, VK_NULL_HANDLE,
		VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
	drawImageWriter.writeImage(1, _drawImage.imageView, AssetManager::getDefaultSamplerLinear(),
		VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

	drawImageWriter.updateSet(Backend::getDevice(), DescriptorSetOverwatch::getPostProcessDescriptors().descriptorSet);

	RenderScene::setCamera();
}

void Renderer::recordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex) {
	VkCommandBufferBeginInfo cmdBeginInfo{};
	cmdBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	cmdBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	// Note: To achieve window extent = to draw extent during each resize the renderer will need to destroy the current
	// images and create new ones in the proper swapchain extent.
	_drawExtent.height = static_cast<uint32_t>(std::min(Backend::getSwapchainExtent().height, _drawImage.imageExtent.height));
	_drawExtent.width = static_cast<uint32_t>(std::min(Backend::getSwapchainExtent().width, _drawImage.imageExtent.width));

	VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

	// Transition to color/depth attachment layouts
	RendererUtils::transitionImage(cmd, _drawImage.image, _drawImage.imageFormat, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

	if (MSAACOUNT != 1u) {
		RendererUtils::transitionImage(cmd, _msaaImage.image, _msaaImage.imageFormat, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	}
	RendererUtils::transitionImage(cmd, _depthImage.image, _depthImage.imageFormat, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

	RendererUtils::transitionImage(cmd, _skyboxImage.image, _skyboxImage.imageFormat, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	// Render scene
	drawGeometry(cmd);

	// Transition draw image to SRC so we can copy to post process
	RendererUtils::transitionImage(cmd, _drawImage.image, _drawImage.imageFormat, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
	RendererUtils::transitionImage(cmd, _postProcessImage.image, _postProcessImage.imageFormat, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

	RendererUtils::copyImageToImage(
		cmd,
		_drawImage.image,
		_postProcessImage.image,
		{ _drawExtent.width, _drawExtent.height },
		{ _drawExtent.width, _drawExtent.height }
	);

	// Transition both back to GENERAL for compute
	RendererUtils::transitionImage(cmd, _drawImage.image, _drawImage.imageFormat, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
	RendererUtils::transitionImage(cmd, _postProcessImage.image, _postProcessImage.imageFormat, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);

	// Run compute shader on post-process image
	colorCorrectPass(cmd);

	// Transition post-process image to SRC for copy to swapchain
	RendererUtils::transitionImage(cmd, _postProcessImage.image, _postProcessImage.imageFormat, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

	// Transition swapchain image to DST for copy
	RendererUtils::transitionImage(cmd, Backend::getSwapchainImages()[imageIndex], _drawImage.imageFormat, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

	RendererUtils::copyImageToImage(
		cmd,
		_postProcessImage.image,
		Backend::getSwapchainImages()[imageIndex],
		{ _drawExtent.width, _drawExtent.height },
		Backend::getSwapchainExtent()
	);

	// Transition swapchain to COLOR_ATTACHMENT_OPTIMAL for ImGui
	RendererUtils::transitionImage(cmd,
		Backend::getSwapchainImages()[imageIndex],
		Backend::getSwapchainImageFormat(),
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

	// Draw ImGui
	EditorImgui::drawImgui(cmd, Backend::getSwapchainImageViews()[imageIndex], false);

	// Transition swapchain to PRESENT
	RendererUtils::transitionImage(cmd,
		Backend::getSwapchainImages()[imageIndex],
		Backend::getSwapchainImageFormat(),
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

	// END COMMAND BUFFER
	VK_CHECK(vkEndCommandBuffer(cmd));
}

void Renderer::drawGeometry(VkCommandBuffer cmd) {
	//begin a render pass  connected to our draw image
	VkRenderingAttachmentInfo colorAttachment = {
		.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
		.pNext = nullptr,
		.imageView = (MSAACOUNT == 1u) ? _drawImage.imageView : _msaaImage.imageView,
		.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		.resolveMode = (MSAACOUNT == 1u) ? VK_RESOLVE_MODE_NONE : VK_RESOLVE_MODE_AVERAGE_BIT,
		.resolveImageView = (MSAACOUNT == 1u) ? VK_NULL_HANDLE : _drawImage.imageView,
		.resolveImageLayout = (MSAACOUNT == 1u) ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.storeOp = VK_ATTACHMENT_STORE_OP_STORE
	};

	VkRenderingAttachmentInfo depthAttachment = {
		.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
		.pNext = nullptr,
		.imageView = _depthImage.imageView,
		.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
		.resolveMode = VK_RESOLVE_MODE_NONE,
		.resolveImageView = VK_NULL_HANDLE,
		.resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.storeOp = VK_ATTACHMENT_STORE_OP_STORE
	};
	depthAttachment.clearValue.depthStencil.depth = 1.f;

	VkRenderingInfo renderInfo = {
		.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
		.pNext = nullptr,
		.flags = 0,
		.renderArea = { {0, 0}, { _drawExtent.width, _drawExtent.height } },
		.layerCount = 1,
		.viewMask = 0,
		.colorAttachmentCount = 1,
		.pColorAttachments = &colorAttachment,
		.pDepthAttachment = &depthAttachment,
		.pStencilAttachment = nullptr
	};

	vkCmdBeginRendering(cmd, &renderInfo);

	RenderScene::renderDrawScene(cmd, getCurrentFrame());

	vkCmdEndRendering(cmd);
}

// requires push constants to render
void Renderer::colorCorrectPass(VkCommandBuffer cmd) {
	// The current shader
	auto& effect = Pipelines::postProcessPipeline.getComputeEffect();

	auto& layout = Pipelines::postProcessPipeline._computePipelineLayout;

	auto& pcData = PipelinePresents::colorCorrectionPipelineSettings.pushConstantsInfo;

	// bind the background compute pipeline
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, effect.pipeline);

	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout,
		0, 1, &DescriptorSetOverwatch::getPostProcessDescriptors().descriptorSet, 0, nullptr);

	vkCmdPushConstants(cmd, layout, pcData.stageFlags, pcData.offset, pcData.size, &effect.data);

	// execute the compute pipeline dispatch. We are using 16x16 workgroup size so we need to divide by it
	vkCmdDispatch(cmd, static_cast<uint32_t>(std::ceil(_drawExtent.width / 16.f)), static_cast<uint32_t>(std::ceil(_drawExtent.height / 16.f)), 1);
}

void Renderer::RenderFrame() {
	VkDevice device = Backend::getDevice();
	VkSwapchainKHR swapchain = Backend::getSwapchain();

	auto& frame = getCurrentFrame();

	RenderScene::updateScene();

	VK_CHECK(vkWaitForFences(device, 1, &frame._renderFence, VK_TRUE, UINT64_MAX));

	frame._deletionQueue.flush();
	frame._frameDescriptors.clearPools();

	uint32_t imageIndex;
	VkResult result = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, frame._swapchainSemaphore, VK_NULL_HANDLE, &imageIndex);
	if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
		vkQueueWaitIdle(Backend::getGraphicsQueue());
		Backend::resizeSwapchain();
		return;
	}
	else if (result != VK_SUCCESS) {
		throw std::runtime_error("Failed to present swap chain image!");
	}

	// Only reset the fence if work is submitted
	VK_CHECK(vkResetFences(device, 1, &frame._renderFence));

	VK_CHECK(vkResetCommandBuffer(frame._graphicsCmdBuffer, 0));
	recordCommandBuffer(frame._graphicsCmdBuffer, imageIndex);

	VkCommandBufferSubmitInfo cmdInfo{};
	cmdInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
	cmdInfo.pNext = nullptr;
	cmdInfo.commandBuffer = frame._graphicsCmdBuffer;
	cmdInfo.deviceMask = 0;

	VkSemaphoreSubmitInfo waitInfo{};
	waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
	waitInfo.pNext = nullptr;
	waitInfo.semaphore = frame._swapchainSemaphore;
	waitInfo.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR;
	waitInfo.deviceIndex = 0;
	waitInfo.value = 1;

	VkSemaphoreSubmitInfo signalInfo{};
	signalInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
	signalInfo.pNext = nullptr;
	signalInfo.semaphore = frame._renderSemaphore;
	signalInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT;
	signalInfo.deviceIndex = 0;
	signalInfo.value = 1;

	VkSubmitInfo2 graphicsSubmitInfo{};
	graphicsSubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
	graphicsSubmitInfo.pNext = nullptr;
	graphicsSubmitInfo.waitSemaphoreInfoCount = (waitInfo.semaphore == VK_NULL_HANDLE) ? 0 : 1;
	graphicsSubmitInfo.pWaitSemaphoreInfos = (waitInfo.semaphore == VK_NULL_HANDLE) ? nullptr : &waitInfo;

	graphicsSubmitInfo.signalSemaphoreInfoCount = (signalInfo.semaphore == VK_NULL_HANDLE) ? 0 : 1;
	graphicsSubmitInfo.pSignalSemaphoreInfos = (signalInfo.semaphore == VK_NULL_HANDLE) ? nullptr : &signalInfo;

	graphicsSubmitInfo.commandBufferInfoCount = 1;
	graphicsSubmitInfo.pCommandBufferInfos = &cmdInfo;

	VK_CHECK(vkQueueSubmit2(Backend::getGraphicsQueue(), 1, &graphicsSubmitInfo, frame._renderFence));

	VkPresentInfoKHR presentInfo{};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pWaitSemaphores = &frame._renderSemaphore;

	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains = &swapchain;
	presentInfo.pImageIndices = &imageIndex;

	result = vkQueuePresentKHR(Backend::getPresentQueue(), &presentInfo);
	if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || Engine::windowModMode().windowResized) {
		if (Backend::getGraphicsQueue() != Backend::getPresentQueue()) {
			vkQueueWaitIdle(Backend::getPresentQueue());
		}
		else {
			vkQueueWaitIdle(Backend::getGraphicsQueue());
		}

		Backend::resizeSwapchain();
	}
	else if (result != VK_SUCCESS) {
		throw std::runtime_error("Failed to present swap chain image!");
	}

	// double buffering, frames indexed at 0 and 1
	_frameNumber++;
}

void Renderer::cleanup() {
	VkDevice device = Backend::getDevice();

	RenderScene::metalRoughMaterial.clearResources(device);

	for (auto& frame : _frames) {
		vkDestroyCommandPool(device, frame._graphicsCmdPool, nullptr);

		vkDestroyFence(device, frame._renderFence, nullptr);
		vkDestroySemaphore(device, frame._renderSemaphore, nullptr);
		vkDestroySemaphore(device, frame._swapchainSemaphore, nullptr);

		frame._deletionQueue.flush();
	}

	_renderImageDeletionQueue.flush();
	vmaDestroyAllocator(_renderImageAllocator);
}