#include "pch.h"

#include "common/Vk_Types.h"
#include "RenderScene.h"
#include "Renderer.h"
#include "vulkan/Backend.h"
#include "vulkan/PipelineManager.h"
#include "RenderGraph.h"

namespace RenderScene {
	GPUSceneData sceneData;
	GPUSceneData& getCurrentSceneData() { return sceneData; }

	Camera mainCamera;

	Frustum currentFrustum;

	DrawContext mainDrawContext;

	std::unordered_map<std::string, std::shared_ptr<LoadedGLTF>> loadedScenes;

	MaterialInstance _defaultData;
	GLTFMetallic_Roughness metalRoughMaterial;

	VkDescriptorSetLayout _gpuSceneDataDescriptorLayout;
	VkDescriptorSetLayout& getGPUSceneDescriptorLayout() { return _gpuSceneDataDescriptorLayout; }
}

void RenderScene::setCamera() {
	mainCamera.velocity = glm::vec3(0.f);
	mainCamera.position = SPAWNPOINT;

	mainCamera.pitch = 0;
	mainCamera.yaw = -90.f;
}

// Mesh transforms
void RenderScene::updateScene() {
	//reset counters
	auto& stats = Engine::getStats();

	stats.drawcallCount = 0;
	stats.triangleCount = 0;

	//begin clock
	auto start = std::chrono::system_clock::now();

	float aspect = static_cast<float>(Renderer::getDrawExtent().width) / static_cast<float>(Renderer::getDrawExtent().height);

	mainCamera.update(Engine::getWindow(), Engine::getLastTimeCount());

	glm::mat4 camView = mainCamera.getViewMatrix();
	glm::mat4 camProj = glm::perspective(glm::radians(70.f), aspect, 0.1f, 1000.f);

	// invert the Y direction on projection matrix so that we are more similar
	// to opengl and gltf axis
	camProj[1][1] *= -1;

	sceneData.view = camView;
	sceneData.proj = camProj;
	sceneData.viewproj = camProj * camView;

	currentFrustum = RenderGraph::extractFrustum(sceneData.viewproj);
	mainDrawContext.frustum = currentFrustum;

	sceneData.ambientColor = glm::vec4(0.5f);
	sceneData.sunlightColor = glm::vec4(0.8f, 0.85f, 0.9f, 1.f);
	sceneData.sunlightDirection = glm::vec4(2.0f, 0.0f, -1.0f, 0.0f);
	sceneData.cameraPosition = glm::vec4(mainCamera.position, 0.f);

	//mainDrawContext.enableCull = false;
	loadedScenes["asset"]->Draw(glm::mat4{ 1.f }, mainDrawContext);

	auto end = std::chrono::system_clock::now();

	//convert to microseconds (integer), and then come back to miliseconds
	auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
	stats.sceneUpdateTime = elapsed.count() / 1000.f;
}

void RenderScene::renderDrawScene(VkCommandBuffer cmd, FrameData& frame) {
	//reset counters
	auto& stats = Engine::getStats();

	auto& extent = Renderer::getDrawExtent();

	VkDevice device = Backend::getDevice();

	stats.drawcallCount = 0;
	stats.triangleCount = 0;

	// Opaque, transparent, wireframe, bounding box all share the same push constant settings
	PushConstantDef defaultPC = PipelinePresents::metalRoughMatSettings.pushConstantsInfo;

	//begin clock
	auto start = std::chrono::system_clock::now();

	VkViewport viewport = {};
	viewport.x = 0;
	viewport.y = 0;
	viewport.width = static_cast<float>(extent.width);
	viewport.height = static_cast<float>(extent.height);
	viewport.minDepth = 0.f;
	viewport.maxDepth = 1.f;

	vkCmdSetViewport(cmd, 0, 1, &viewport);

	VkRect2D scissor = {};
	scissor.offset.x = 0;
	scissor.offset.y = 0;
	scissor.extent.width = extent.width;
	scissor.extent.height = extent.height;

	vkCmdSetScissor(cmd, 0, 1, &scissor);

	AllocatedBuffer gpuSceneDataBuffer = BufferUtils::createBuffer(sizeof(GPUSceneData),
		VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, Renderer::getRenderImageAllocator());

	//add it to the deletion queue of this frame so it gets deleted once its been used
	frame._deletionQueue.push_function([=]() {
		BufferUtils::destroyBuffer(gpuSceneDataBuffer, Renderer::getRenderImageAllocator());
	});

	//write the buffer
	GPUSceneData* sceneDataPtr = reinterpret_cast<GPUSceneData*>(gpuSceneDataBuffer.mapped);
	*sceneDataPtr = sceneData;

	// create a descriptor set that binds that buffer and update it
	VkDescriptorSet sceneDescriptor = frame._frameDescriptors.allocateDescriptor(Backend::getDevice(), _gpuSceneDataDescriptorLayout);

	DescriptorWriter writer;
	writer.writeBuffer(0, gpuSceneDataBuffer.buffer, sizeof(GPUSceneData), 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
	writer.updateSet(Backend::getDevice(), sceneDescriptor);

	// SORT INDICES OF OPAQUE DRAW ARRAY
	std::vector<uint32_t> opaque_draws;
	opaque_draws.reserve(mainDrawContext.OpaqueSurfaces.size());

	for (uint32_t i = 0; i < mainDrawContext.OpaqueSurfaces.size(); ++i) {
		opaque_draws.push_back(i);
	}

	//sort the opaque surfaces by material and mesh
	std::sort(opaque_draws.begin(), opaque_draws.end(), [&](const auto& iA, const auto& iB) {
		const RenderObject& A = mainDrawContext.OpaqueSurfaces[iA];
		const RenderObject& B = mainDrawContext.OpaqueSurfaces[iB];
		if (A.material == B.material) {
			return A.indexBuffer < B.indexBuffer;
		}
		else {
			return A.material < B.material;
		}
	});

	// SORT INDICES OF TRANSPARENT DRAW ARRAY
	std::vector<uint32_t> transparent_draws;
	transparent_draws.reserve(mainDrawContext.TransparentSurfaces.size());

	for (uint32_t i = 0; i < mainDrawContext.TransparentSurfaces.size(); i++) {
		transparent_draws.push_back(i);
	}

	// sort by distance from the camera, descending (back to front)
	std::sort(transparent_draws.begin(), transparent_draws.end(), [&](uint32_t iA, uint32_t iB) {
		const RenderObject& A = mainDrawContext.TransparentSurfaces[iA];
		const RenderObject& B = mainDrawContext.TransparentSurfaces[iB];

		glm::vec3 camPos = glm::vec3(sceneData.cameraPosition);

		float distA = glm::length(A.aabb.origin - camPos);
		float distB = glm::length(B.aabb.origin - camPos);

		// For transparency, we want farther objects rendered first so sort in descending order
		return distA > distB;
	});


	// === SKYBOX DRAW ===
	{
		VkDescriptorSet skyBoxDescriptor = frame._frameDescriptors.allocateDescriptor(device,
			PipelinePresents::skyboxPipelineSettings.descriptorSetInfo.descriptorLayouts.back()); // what a mouthful

		DescriptorWriter skyBoxWriter;
		skyBoxWriter.writeImage(0, Renderer::getSkyBoxImage().imageView, AssetManager::getDefaultSamplerLinear(),
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
		skyBoxWriter.updateSet(Backend::getDevice(), skyBoxDescriptor);

		glm::mat4 view = glm::mat4(glm::mat3(sceneData.view)); // strip translation
		glm::mat4 viewproj = sceneData.proj * view;
		glm::mat4 invVp = glm::inverse(viewproj);

		// Bind pipeline
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipelines::skyboxPipeline.pipeline);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
			Pipelines::skyboxPipeline.layout, 0, 1, &skyBoxDescriptor, 0, 0);

		// Push constants
		auto& skyboxPC = PipelinePresents::skyboxPipelineSettings.pushConstantsInfo;

		vkCmdPushConstants(cmd, Pipelines::skyboxPipeline.layout, skyboxPC.stageFlags, skyboxPC.offset, skyboxPC.size, &invVp);

		vkCmdDraw(cmd, 3, 1, 0, 0);
	}

	// draw state tracking
	GraphicsPipeline* lastPipeline = nullptr;
	MaterialInstance* lastMaterial = nullptr;
	VkBuffer lastIndexBuffer = VK_NULL_HANDLE;

	auto draw = [&](const RenderObject& r) {
		GraphicsPipeline* pipelineToUse = r.material->pipeline;

		// override pipeline with swapper if enabled
		if (pipelineOverride.enabled) {
			pipelineToUse = getPipelineByType(pipelineOverride.selected);
		}

		if (r.material != lastMaterial) {
			lastMaterial = r.material;
			//rebind pipeline and descriptors if the material changed
			if (pipelineToUse != lastPipeline) {
				lastPipeline = pipelineToUse;

				vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineToUse->pipeline);
				vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineToUse->layout, 0, 1, &sceneDescriptor, 0, nullptr);
			}

			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineToUse->layout, 1, 1,
				&r.material->materialSet, 0, nullptr);
		}
		//rebind index buffer if needed
		if (r.indexBuffer != lastIndexBuffer) {
			lastIndexBuffer = r.indexBuffer;
			vkCmdBindIndexBuffer(cmd, r.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
		}

		// calculate final mesh matrix
		GPUDrawPushConstants push_constants;
		push_constants.worldMatrix = r.transform;
		push_constants.vertexBuffer = r.vertexBufferAddress;

		vkCmdPushConstants(cmd, pipelineToUse->layout, defaultPC.stageFlags, defaultPC.offset, defaultPC.size, &push_constants);

		vkCmdDrawIndexed(cmd, r.indexCount, 1, r.firstIndex, 0, 0);

		//stats
		stats.drawcallCount++;
		stats.triangleCount += r.indexCount / 3;
	};

	for (auto& r : opaque_draws) {
		draw(mainDrawContext.OpaqueSurfaces[r]);
	}

	for (auto& r : transparent_draws) {
		draw(mainDrawContext.TransparentSurfaces[r]);
	}

	// VISIBLE AABB FOR OBJECTS
	if (RenderSceneSettings::drawBoundingBoxes && (!opaque_draws.empty() || !transparent_draws.empty())) {
		std::vector<glm::vec3> allVerts;
		std::vector<uint32_t> drawOffsets;

		uint32_t offset = 0;
		for (auto& r : opaque_draws) {
			const RenderObject& obj = mainDrawContext.OpaqueSurfaces[r];

			auto verts = RenderGraph::GetAABBVertices(obj.aabb);

			offset = static_cast<uint32_t>(allVerts.size());
			drawOffsets.push_back(offset);
			allVerts.insert(allVerts.end(), verts.begin(), verts.end());
		}

		for (auto& r : transparent_draws) {
			const RenderObject& obj = mainDrawContext.TransparentSurfaces[r];

			auto verts = RenderGraph::GetAABBVertices(obj.aabb);

			offset = static_cast<uint32_t>(allVerts.size());
			drawOffsets.push_back(offset);
			allVerts.insert(allVerts.end(), verts.begin(), verts.end());
		}

		const size_t totalSize = sizeof(glm::vec3) * allVerts.size();

		AllocatedBuffer aabbVBO = BufferUtils::createBuffer(
			totalSize,
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
			VMA_MEMORY_USAGE_CPU_TO_GPU,
			Renderer::getRenderImageAllocator()
		);
		memcpy(aabbVBO.mapped, allVerts.data(), totalSize);

		frame._deletionQueue.push_function([=]() {
			BufferUtils::destroyBuffer(aabbVBO, Renderer::getRenderImageAllocator());
		});

		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipelines::boundingBoxPipeline.pipeline);

		VkDeviceSize vtxOffset = 0;
		vkCmdBindVertexBuffers(cmd, 0, 1, &aabbVBO.buffer, &vtxOffset);

		GPUDrawPushConstants pc;
		pc.worldMatrix = sceneData.viewproj;

		// must get the buffer address
		VkBufferDeviceAddressInfo info = { VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
		info.buffer = aabbVBO.buffer;
		pc.vertexBuffer = vkGetBufferDeviceAddress(device, &info);

		vkCmdPushConstants(
			cmd,
			Pipelines::boundingBoxPipeline.layout,
			defaultPC.stageFlags,
			defaultPC.offset,
			defaultPC.size,
			&pc
		);

		const uint32_t vertsPerAABB = 24;
		for (uint32_t i = 0; i < drawOffsets.size(); ++i) {
			uint32_t vertexOffset = drawOffsets[i];
			vkCmdDraw(cmd, vertsPerAABB, 1, vertexOffset, 0);
		}
	}

	// prepare next frame
	mainDrawContext.OpaqueSurfaces.clear();
	mainDrawContext.TransparentSurfaces.clear();

	auto end = std::chrono::system_clock::now();

	//convert to microseconds (integer), and then come back to ms
	auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
	stats.meshDrawTime = elapsed.count() / 1000.f;
}

MaterialInstance GLTFMetallic_Roughness::writeMaterial(VkDevice device, MaterialPass pass, const MaterialResources& resources, DescriptorManager& descriptor) {
	MaterialInstance matData;
	matData.passType = pass;
	if (pass == MaterialPass::Transparent) {
		matData.pipeline = &Pipelines::transparentPipeline;
	}
	else {
		matData.pipeline = &Pipelines::opaquePipeline;
	}

	matData.materialSet = descriptor.allocateDescriptor(device, materialLayout);

	writer.clear();
	writer.writeBuffer(0, resources.dataBuffer, sizeof(MaterialConstants), resources.dataBufferOffset, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
	writer.writeImage(1, resources.colorImage.imageView, resources.colorSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
	writer.writeImage(2, resources.metalRoughImage.imageView, resources.metalRoughSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
	writer.writeImage(3, resources.normalImage.imageView, resources.normalSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

	writer.updateSet(device, matData.materialSet);

	return matData;
}

void GLTFMetallic_Roughness::clearResources(VkDevice device) {
	vkDestroyDescriptorSetLayout(device, materialLayout, nullptr);
}