#include "pch.h"

#include "RendererUtils.h"
#include "vulkan/Backend.h"
#include "renderer/Renderer.h"
#include "renderer/types/Texture.h"

VkSemaphore RendererUtils::createSemaphore() {
	VkDevice device = Backend::getDevice();

	VkSemaphoreCreateInfo semaphoreInfo{};
	semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

	VkSemaphore semaphore;
	VK_CHECK(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &semaphore));

	return semaphore;
}

VkFence RendererUtils::createFence() {
	VkDevice device = Backend::getDevice();

	VkFenceCreateInfo fenceInfo{};
	fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

	VkFence fence;
	VK_CHECK(vkCreateFence(device, &fenceInfo, nullptr, &fence));

	return fence;
}

void RendererUtils::createTextureImage(void* data, AllocatedImage& renderImage, VkImageUsageFlags usage,
	VkMemoryPropertyFlags properties, VkSampleCountFlagBits samples, DeletionQueue* deletionQueue, VmaAllocator allocator) {

	// bytes-per-channel (float) * channels-per-pixel * layers
	size_t layerCount = renderImage.isCubeMap ? renderImage.imageExtent.depth : 1;

	size_t pixelBytes = 0;

	switch (renderImage.imageFormat) {
	case VK_FORMAT_R8G8B8A8_UNORM:
		pixelBytes = sizeof(uint8_t) * 4; // 4 bytes
		break;
	case VK_FORMAT_R32G32B32A32_SFLOAT:
		pixelBytes = sizeof(float) * 4; // 16 bytes
		break;
	default:
		throw std::runtime_error("Unsupported texture format in createTextureImage");
	}

	size_t dataSize = static_cast<unsigned long long>(renderImage.imageExtent.width) * renderImage.imageExtent.height * pixelBytes * layerCount;

	AllocatedBuffer uploadbuffer = BufferUtils::createBuffer(dataSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, allocator);

	memcpy(uploadbuffer.info.pMappedData, data, dataSize);

	createRenderImage(renderImage, usage | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
		properties, samples, deletionQueue, allocator);

	CommandBuffer::immediateCmdSubmit([&](VkCommandBuffer cmd) {

		RendererUtils::transitionImage(cmd, renderImage.image, renderImage.imageFormat, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

		VkBufferImageCopy copyRegion = {};
		copyRegion.bufferOffset = 0;
		copyRegion.bufferRowLength = 0;
		copyRegion.bufferImageHeight = 0;

		copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		copyRegion.imageSubresource.mipLevel = 0;
		copyRegion.imageSubresource.baseArrayLayer = 0;
		copyRegion.imageSubresource.layerCount = static_cast<uint32_t>(layerCount);
		copyRegion.imageExtent = renderImage.imageExtent;

		// copy the buffer into the image
		vkCmdCopyBufferToImage(cmd, uploadbuffer.buffer, renderImage.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
			&copyRegion);

		if (renderImage.mipmapped) {
			Textures::generateMipmaps(cmd, renderImage);
		}
		else {
			RendererUtils::transitionImage(cmd, renderImage.image, renderImage.imageFormat,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				renderImage.isCubeMap ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		}
	},
	AssetManager::getGraphicsCmdSubmit(),
	Backend::getGraphicsQueue()
	);

	BufferUtils::destroyBuffer(uploadbuffer, allocator);
}

void RendererUtils::createRenderImage(AllocatedImage& renderImage, VkImageUsageFlags usage,
	VkMemoryPropertyFlags properties, VkSampleCountFlagBits samples, DeletionQueue* deletionQueue, VmaAllocator allocator) {

	VkImageCreateInfo imgInfo{};
	imgInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imgInfo.imageType = VK_IMAGE_TYPE_2D;
	imgInfo.extent = renderImage.imageExtent;
	imgInfo.format = renderImage.imageFormat;
	imgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imgInfo.usage = usage;
	imgInfo.samples = samples;
	imgInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	if (renderImage.mipmapped) {
		imgInfo.mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(renderImage.imageExtent.width, renderImage.imageExtent.height)))) + 1;
	}
	else {
		imgInfo.mipLevels = 1;
	}

	if (renderImage.isCubeMap) {
		imgInfo.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
		imgInfo.arrayLayers = 6;
	}
	else {
		imgInfo.arrayLayers = 1;
	}

	VmaAllocationCreateInfo imgAllocInfo = {};
	imgAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
	imgAllocInfo.requiredFlags = properties;

	VK_CHECK(vmaCreateImage(allocator, &imgInfo, &imgAllocInfo, &renderImage.image, &renderImage.allocation, nullptr));

	VkImageViewCreateInfo viewInfo{};
	viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	viewInfo.image = renderImage.image;
	viewInfo.format = renderImage.imageFormat;
	if (usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) {
		viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
	}
	else {
		viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	}
	viewInfo.subresourceRange.levelCount = imgInfo.mipLevels;
	viewInfo.subresourceRange.baseMipLevel = 0;
	viewInfo.subresourceRange.baseArrayLayer = 0;

	if (renderImage.isCubeMap) {
		// Cube view for sampling (used by fragment shader)
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
		viewInfo.subresourceRange.layerCount = 6;
		VK_CHECK(vkCreateImageView(Backend::getDevice(), &viewInfo, nullptr, &renderImage.imageView));

		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
		// Cube view for writing (used by compute shader)
		VK_CHECK(vkCreateImageView(Backend::getDevice(), &viewInfo, nullptr, &renderImage.storageView));
	}
	else {
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewInfo.subresourceRange.layerCount = 1;
		VK_CHECK(vkCreateImageView(Backend::getDevice(), &viewInfo, nullptr, &renderImage.imageView));
	}

	// Textures image creation is used with this function and it has seperate cleanup
	if (deletionQueue != nullptr) {
		auto* imageView = &renderImage.imageView;
		auto* image = &renderImage.image;
		auto* storageView = &renderImage.storageView;

		deletionQueue->push_function([=]() {
			if (*imageView != VK_NULL_HANDLE) {
				vkDestroyImageView(Backend::getDevice(), *imageView, nullptr);
				*imageView = VK_NULL_HANDLE;
			}
			if (storageView && *storageView != VK_NULL_HANDLE) {
				vkDestroyImageView(Backend::getDevice(), *storageView, nullptr);
				*storageView = VK_NULL_HANDLE;
			}
			if (*image != VK_NULL_HANDLE) {
				vmaDestroyImage(allocator, *image, renderImage.allocation);
				*image = VK_NULL_HANDLE;
			}
		});
	}
}

void RendererUtils::destroyTexImage(VkDevice device, const AllocatedImage& img, VmaAllocator allocator) {
	vkDestroyImageView(device, img.imageView, nullptr);
	vmaDestroyImage(allocator, img.image, img.allocation);
}

void RendererUtils::transitionImage(VkCommandBuffer cmd, VkImage image, VkFormat format, VkImageLayout currentLayout, VkImageLayout newLayout) {
	VkImageMemoryBarrier2 imageBarrier{};
	imageBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
	imageBarrier.pNext = nullptr;

	imageBarrier.oldLayout = currentLayout;
	imageBarrier.newLayout = newLayout;
	imageBarrier.image = image;

	// Handle depth/stencil formats properly
	VkImageAspectFlags aspectMask = 0;
	if (newLayout == VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL ||
		newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {

		aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;

		if (VulkanUtils::hasStencilComponent(format)) {
			aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
		}
	}
	else {
		aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	}

	imageBarrier.subresourceRange.aspectMask = aspectMask;
	imageBarrier.subresourceRange.baseMipLevel = 0;
	imageBarrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
	imageBarrier.subresourceRange.baseArrayLayer = 0;
	imageBarrier.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;

	// Select the correct pipeline stages and access masks based on layouts
	switch (currentLayout) {
	case VK_IMAGE_LAYOUT_UNDEFINED:
		imageBarrier.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
		imageBarrier.srcAccessMask = 0; // No dependencies
		break;
	case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
		imageBarrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
		imageBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
		break;
	case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
		imageBarrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
		imageBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
		break;
	case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
		imageBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
		imageBarrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
		break;
	case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
		imageBarrier.srcStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
		imageBarrier.srcAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
		break;
	default:
		imageBarrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
		imageBarrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
		break;
	}

	switch (newLayout) {
	case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
		imageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
		imageBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
		break;
	case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
		imageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
		imageBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
		break;
	case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
		imageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
		imageBarrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
		break;
	case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
		imageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
		imageBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
		break;
	case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
		imageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
		imageBarrier.dstAccessMask = VK_ACCESS_2_NONE; // No need for shader access
		break;
	default:
		imageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
		imageBarrier.dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT;
		break;
	}

	VkDependencyInfo depInfo{};
	depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
	depInfo.pNext = nullptr;
	depInfo.imageMemoryBarrierCount = 1;
	depInfo.pImageMemoryBarriers = &imageBarrier;

	vkCmdPipelineBarrier2(cmd, &depInfo);
}

void RendererUtils::copyImageToImage(VkCommandBuffer cmd, VkImage source, VkImage destination, VkExtent2D srcSize, VkExtent2D dstSize) {
	VkImageBlit2 blitRegion{};
	blitRegion.sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2;
	blitRegion.pNext = nullptr;

	blitRegion.srcOffsets[1].x = srcSize.width;
	blitRegion.srcOffsets[1].y = srcSize.height;
	blitRegion.srcOffsets[1].z = 1;

	blitRegion.dstOffsets[1].x = dstSize.width;
	blitRegion.dstOffsets[1].y = dstSize.height;
	blitRegion.dstOffsets[1].z = 1;

	blitRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	blitRegion.srcSubresource.baseArrayLayer = 0;
	blitRegion.srcSubresource.layerCount = 1;
	blitRegion.srcSubresource.mipLevel = 0;

	blitRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	blitRegion.dstSubresource.baseArrayLayer = 0;
	blitRegion.dstSubresource.layerCount = 1;
	blitRegion.dstSubresource.mipLevel = 0;

	VkBlitImageInfo2 blitInfo{};
	blitInfo.sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2;
	blitInfo.pNext = nullptr;

	blitInfo.dstImage = destination;
	blitInfo.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	blitInfo.srcImage = source;
	blitInfo.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	blitInfo.filter = VK_FILTER_LINEAR;
	blitInfo.regionCount = 1;
	blitInfo.pRegions = &blitRegion;

	vkCmdBlitImage2(cmd, &blitInfo);
}