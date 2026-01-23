#include "pch.h"

#include "PipelineManager.h"

#include "vulkan/Backend.h"
#include "renderer/Renderer.h"
#include "renderer/RenderScene.h"

void PipelineManager::initPipelines() {
	DeletionQueue shaderDeletionQ;

	// shader setup
	std::vector<ShaderStageInfo> meshShaderStages;
	ShaderStageInfo vertexStage = {
		.filePath = "res/shaders/mesh_vert.spv",
		.stage = VK_SHADER_STAGE_VERTEX_BIT
	};
	ShaderStageInfo fragmentStage = {
		.filePath = "res/shaders/mesh_frag.spv",
		.stage = VK_SHADER_STAGE_FRAGMENT_BIT
	};

	meshShaderStages.push_back(vertexStage);
	meshShaderStages.push_back(fragmentStage);

	setupShaders(PipelinePresents::opaqueSettings.shaderStages, meshShaderStages, shaderDeletionQ);
	setupShaders(PipelinePresents::transparentSettings.shaderStages, meshShaderStages, shaderDeletionQ);
	setupShaders(PipelinePresents::wireframeSettings.shaderStages, meshShaderStages, shaderDeletionQ);


	// separate shaders needed for bounding boxes
	ShaderStageInfo bbVertStage = {
		.filePath = "res/shaders/aabb_vert.spv",
		.stage = VK_SHADER_STAGE_VERTEX_BIT
	};
	ShaderStageInfo bbFragStage = {
		.filePath = "res/shaders/aabb_frag.spv",
		.stage = VK_SHADER_STAGE_FRAGMENT_BIT
	};
	PipelinePresents::boundingBoxSettings.shaderStagesInfo.push_back(bbVertStage);
	PipelinePresents::boundingBoxSettings.shaderStagesInfo.push_back(bbFragStage);

	setupShaders(PipelinePresents::boundingBoxSettings.shaderStages, PipelinePresents::boundingBoxSettings.shaderStagesInfo, shaderDeletionQ);


	ShaderStageInfo skyboxVertStage = {
		.filePath = "res/shaders/skybox_vert.spv",
		.stage = VK_SHADER_STAGE_VERTEX_BIT
	};

	ShaderStageInfo skyboxFragStage = {
		.filePath = "res/shaders/skybox_frag.spv",
		.stage = VK_SHADER_STAGE_FRAGMENT_BIT
	};
	PipelinePresents::skyboxPipelineSettings.shaderStagesInfo.push_back(skyboxVertStage);
	PipelinePresents::skyboxPipelineSettings.shaderStagesInfo.push_back(skyboxFragStage);

	setupShaders(PipelinePresents::skyboxPipelineSettings.shaderStages, PipelinePresents::skyboxPipelineSettings.shaderStagesInfo, shaderDeletionQ);


	// Default pipelines setup
	PushConstantDef defaultPushConstantsInfo = {
		.enabled = true,
		.offset = 0,
		.size = static_cast<uint32_t>(sizeof(GPUDrawPushConstants)),
		.stageFlags = VK_SHADER_STAGE_VERTEX_BIT
	};

	PipelinePresents::metalRoughMatSettings.pushConstantsInfo = defaultPushConstantsInfo;

	PipelinePresent defaultConfig;
	defaultConfig.pushConstantsInfo = defaultPushConstantsInfo;
	defaultConfig.descriptorSetInfo = PipelinePresents::metalRoughMatSettings.descriptorSetInfo;


	// pipeline layout handles default push constant data for meshes
	// both pipelines will share the same layout data
	VkPipelineLayout defaultLayout = PipelineManager::setupPipelineLayout(defaultConfig);

	// delete based off some hierarchy since they all have the same layout
	Pipelines::opaquePipeline.layout = defaultLayout;
	Pipelines::transparentPipeline.layout = defaultLayout;
	Pipelines::wireframePipeline.layout = defaultLayout;
	Pipelines::boundingBoxPipeline.layout = defaultLayout;

	// graphic pipelines can share the same builder
	PipelineBuilder pipeline_builder;
	pipeline_builder._pipelineLayout = defaultLayout;


	// ===OPAQUE PIPELINE===
	PipelinePresents::opaqueSettings.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	PipelinePresents::opaqueSettings.polygonMode = VK_POLYGON_MODE_FILL;
	PipelinePresents::opaqueSettings.cullMode = VK_CULL_MODE_NONE;
	PipelinePresents::opaqueSettings.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	PipelinePresents::opaqueSettings.enableBlending = false;
	PipelinePresents::opaqueSettings.enableDepthTest = true;
	PipelinePresents::opaqueSettings.enableDepthWrite = true;
	PipelinePresents::opaqueSettings.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
	PipelinePresents::opaqueSettings.colorFormat = Renderer::getDrawImage().imageFormat;
	PipelinePresents::opaqueSettings.depthFormat = Renderer::getDepthImage().imageFormat;

	PipelineManager::setupPipelineConfig(pipeline_builder, PipelinePresents::opaqueSettings);
	Pipelines::opaquePipeline.pipeline = pipeline_builder.createPipeline(PipelinePresents::opaqueSettings);

	// ===TRANSPARENT PIPELINE===
	pipeline_builder.initializePipelineSTypes();
	PipelinePresents::transparentSettings.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	PipelinePresents::transparentSettings.polygonMode = VK_POLYGON_MODE_FILL;
	PipelinePresents::transparentSettings.cullMode = VK_CULL_MODE_NONE;
	PipelinePresents::transparentSettings.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	PipelinePresents::transparentSettings.enableBlending = true;
	PipelinePresents::transparentSettings.enableDepthTest = true;
	PipelinePresents::transparentSettings.enableDepthWrite = false;
	PipelinePresents::transparentSettings.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
	PipelinePresents::transparentSettings.colorFormat = Renderer::getDrawImage().imageFormat;
	PipelinePresents::transparentSettings.depthFormat = Renderer::getDepthImage().imageFormat;

	PipelineManager::setupPipelineConfig(pipeline_builder, PipelinePresents::transparentSettings);
	Pipelines::transparentPipeline.pipeline = pipeline_builder.createPipeline(PipelinePresents::transparentSettings);


	// ===WIREFRAME PIPELINE===
	pipeline_builder.initializePipelineSTypes();
	PipelinePresents::wireframeSettings.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	PipelinePresents::wireframeSettings.polygonMode = VK_POLYGON_MODE_LINE;
	PipelinePresents::wireframeSettings.cullMode = VK_CULL_MODE_BACK_BIT;
	PipelinePresents::wireframeSettings.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	PipelinePresents::wireframeSettings.enableBlending = false;
	PipelinePresents::wireframeSettings.enableDepthTest = true;
	PipelinePresents::wireframeSettings.enableDepthWrite = true;
	PipelinePresents::wireframeSettings.depthCompareOp = VK_COMPARE_OP_LESS;
	PipelinePresents::wireframeSettings.colorFormat = Renderer::getDrawImage().imageFormat;
	PipelinePresents::wireframeSettings.depthFormat = Renderer::getDepthImage().imageFormat;

	PipelineManager::setupPipelineConfig(pipeline_builder, PipelinePresents::wireframeSettings);
	Pipelines::wireframePipeline.pipeline = pipeline_builder.createPipeline(PipelinePresents::wireframeSettings);


	// ===BOUNDINGBOX PIPELINE===
	pipeline_builder.initializePipelineSTypes();
	PipelinePresents::boundingBoxSettings.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
	PipelinePresents::boundingBoxSettings.polygonMode = VK_POLYGON_MODE_LINE;
	PipelinePresents::boundingBoxSettings.cullMode = VK_CULL_MODE_NONE;
	PipelinePresents::boundingBoxSettings.frontFace = VK_FRONT_FACE_CLOCKWISE;
	PipelinePresents::boundingBoxSettings.enableBlending = false;
	PipelinePresents::boundingBoxSettings.enableDepthTest = true;
	PipelinePresents::boundingBoxSettings.enableDepthWrite = false;
	PipelinePresents::boundingBoxSettings.depthCompareOp = VK_COMPARE_OP_LESS;
	PipelinePresents::boundingBoxSettings.colorFormat = Renderer::getDrawImage().imageFormat;
	PipelinePresents::boundingBoxSettings.depthFormat = Renderer::getDepthImage().imageFormat;

	PipelineManager::setupPipelineConfig(pipeline_builder, PipelinePresents::boundingBoxSettings);
	Pipelines::boundingBoxPipeline.pipeline = pipeline_builder.createPipeline(PipelinePresents::boundingBoxSettings);


	// ===SKYBOX PIPELINE===
	PushConstantDef skyboxPC = {
		.enabled = true,
		.offset = 0,
		.size = sizeof(glm::mat4),
		.stageFlags = VK_SHADER_STAGE_VERTEX_BIT
	};
	PipelinePresents::skyboxPipelineSettings.pushConstantsInfo = skyboxPC;

	VkPipelineLayout skyboxLayout = PipelineManager::setupPipelineLayout(PipelinePresents::skyboxPipelineSettings);
	Pipelines::skyboxPipeline.layout = skyboxLayout;
	pipeline_builder._pipelineLayout = skyboxLayout; // reset the pipelinebuilder layout

	pipeline_builder.initializePipelineSTypes();
	PipelinePresents::skyboxPipelineSettings.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	PipelinePresents::skyboxPipelineSettings.polygonMode = VK_POLYGON_MODE_FILL;
	PipelinePresents::skyboxPipelineSettings.cullMode = VK_CULL_MODE_NONE;
	PipelinePresents::skyboxPipelineSettings.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	PipelinePresents::skyboxPipelineSettings.enableBlending = false;
	PipelinePresents::skyboxPipelineSettings.enableDepthTest = true;
	PipelinePresents::skyboxPipelineSettings.enableDepthWrite = false;
	PipelinePresents::skyboxPipelineSettings.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
	PipelinePresents::skyboxPipelineSettings.colorFormat = Renderer::getDrawImage().imageFormat;
	PipelinePresents::skyboxPipelineSettings.depthFormat = Renderer::getDepthImage().imageFormat;

	PipelineManager::setupPipelineConfig(pipeline_builder, PipelinePresents::skyboxPipelineSettings);
	Pipelines::skyboxPipeline.pipeline = (pipeline_builder.createPipeline(PipelinePresents::skyboxPipelineSettings));


	// === COMPUTE PIPELINE SETUP STAGE ===

	ComputePipelineBuilder compute_builder;

	PushConstantDef computePC = {
		.enabled = true,
		.offset = 0,
		.size = static_cast<uint32_t>(sizeof(PushConstantBlock)),
		.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
	};


	ShaderStageInfo colorCorrectCShader = {
		.shaderName = "Color Correction",
		.filePath = "res/shaders/color_correction_comp.spv",
		.stage = VK_SHADER_STAGE_COMPUTE_BIT
	};
	colorCorrectCShader.pushConstantData.data1 = glm::vec4(0.067f, 1.f, 1.06f, 1.f);
	colorCorrectCShader.pushConstantData.data2 = glm::vec4(0.f);
	colorCorrectCShader.pushConstantData.data3 = glm::vec4(0.f);
	colorCorrectCShader.pushConstantData.data4 = glm::vec4(0.f);


	ShaderStageInfo cubemapSetup = {
		.shaderName = "cubemap",
		.filePath = "res/shaders/hdr2cubemap_comp.spv",
		.stage = VK_SHADER_STAGE_COMPUTE_BIT
	};
	cubemapSetup.pushConstantData.data1 = glm::vec4(0.f);
	cubemapSetup.pushConstantData.data2 = glm::vec4(0.f);
	cubemapSetup.pushConstantData.data3 = glm::vec4(0.f);
	cubemapSetup.pushConstantData.data4 = glm::vec4(0.f);


	PipelinePresents::colorCorrectionPipelineSettings.shaderStagesInfo.push_back(colorCorrectCShader);
	PipelinePresents::hdr2cubemapPipelineSettings.shaderStagesInfo.push_back(cubemapSetup);

	setupShaders(PipelinePresents::colorCorrectionPipelineSettings.shaderStages,
		PipelinePresents::colorCorrectionPipelineSettings.shaderStagesInfo, shaderDeletionQ);

	setupShaders(PipelinePresents::hdr2cubemapPipelineSettings.shaderStages,
		PipelinePresents::hdr2cubemapPipelineSettings.shaderStagesInfo, shaderDeletionQ);


	PipelinePresents::hdr2cubemapPipelineSettings.pushConstantsInfo = computePC;
	PipelinePresents::hdr2cubemapPipelineSettings.descriptorSetInfo.descriptorLayouts = DescriptorSetOverwatch::getCubeMappingDescriptors().descriptorLayouts;
	Pipelines::hdr2cubemapPipeline._computePipelineLayout = PipelineManager::setupPipelineLayout(PipelinePresents::hdr2cubemapPipelineSettings);


	PipelinePresents::colorCorrectionPipelineSettings.pushConstantsInfo = computePC;
	PipelinePresents::colorCorrectionPipelineSettings.descriptorSetInfo.descriptorLayouts = DescriptorSetOverwatch::getPostProcessDescriptors().descriptorLayouts;

	Pipelines::postProcessPipeline._computePipelineLayout = PipelineManager::setupPipelineLayout(PipelinePresents::colorCorrectionPipelineSettings);
	Pipelines::postProcessPipeline.compEffects = compute_builder.build(Pipelines::postProcessPipeline, PipelinePresents::colorCorrectionPipelineSettings);

	// Cube map pipeline/layout will be deleted during the mapping function in AssetManager
	Pipelines::hdr2cubemapPipeline.compEffects = compute_builder.build(Pipelines::hdr2cubemapPipeline, PipelinePresents::hdr2cubemapPipelineSettings);


	shaderDeletionQ.flush(); // deferred deletion of shader modules


	Engine::getDeletionQueue().push_function([=] {
		for (auto& effect : Pipelines::postProcessPipeline.compEffects) {
			vkDestroyPipeline(Backend::getDevice(), effect.pipeline, nullptr);
		}

		for (auto& effect : Pipelines::hdr2cubemapPipeline.compEffects) {
			vkDestroyPipeline(Backend::getDevice(), effect.pipeline, nullptr);
		}

		vkDestroyPipeline(Backend::getDevice(), Pipelines::skyboxPipeline.pipeline, nullptr);
		vkDestroyPipeline(Backend::getDevice(), Pipelines::transparentPipeline.pipeline, nullptr);
		vkDestroyPipeline(Backend::getDevice(), Pipelines::boundingBoxPipeline.pipeline, nullptr);
		vkDestroyPipeline(Backend::getDevice(), Pipelines::wireframePipeline.pipeline, nullptr);
		vkDestroyPipeline(Backend::getDevice(), Pipelines::opaquePipeline.pipeline, nullptr);

		// All graphics pipelines share this layout, so we only destroy it once
		vkDestroyPipelineLayout(Backend::getDevice(), Pipelines::opaquePipeline.layout, nullptr);
		vkDestroyPipelineLayout(Backend::getDevice(), Pipelines::skyboxPipeline.layout, nullptr);

		vkDestroyPipelineLayout(Backend::getDevice(), Pipelines::postProcessPipeline._computePipelineLayout, nullptr);
		vkDestroyPipelineLayout(Backend::getDevice(), Pipelines::hdr2cubemapPipeline._computePipelineLayout, nullptr);
	});
}

VkPipelineLayout PipelineManager::setupPipelineLayout(PipelinePresent& pipelineInfo) {
	PushConstantDef matrixRange = {
		.enabled = pipelineInfo.pushConstantsInfo.enabled,
		.offset = pipelineInfo.pushConstantsInfo.offset,
		.size = pipelineInfo.pushConstantsInfo.size,
		.stageFlags = pipelineInfo.pushConstantsInfo.stageFlags
	};

	VkPipelineLayout pipelineLayout;
	createPipelineLayout(pipelineLayout, pipelineInfo.descriptorSetInfo, &matrixRange);

	return pipelineLayout;
}

// THE PIPELINE MANAGER
void PipelineManager::createPipelineLayout(VkPipelineLayout& pipelineLayout, DescriptorsCentral& descriptors, const PushConstantDef* pushConstants) {
	VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
	pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutInfo.flags = 0;

	if (!descriptors.enableDescriptorsSetAndLayout) {
		pipelineLayoutInfo.setLayoutCount = 0;
		pipelineLayoutInfo.pSetLayouts = nullptr;
	}
	else {
		pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(descriptors.descriptorLayouts.size());
		pipelineLayoutInfo.pSetLayouts = descriptors.descriptorLayouts.data();
	}

	VkPushConstantRange pushConstantRange{};
	if (pushConstants && pushConstants->enabled) {
		pushConstantRange.stageFlags = pushConstants->stageFlags;
		pushConstantRange.offset     = pushConstants->offset;
		pushConstantRange.size       = pushConstants->size;
		pipelineLayoutInfo.pushConstantRangeCount = 1;
		pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
	}
	else {
		pipelineLayoutInfo.pushConstantRangeCount = 0;
		pipelineLayoutInfo.pPushConstantRanges = nullptr;
	}

	VK_CHECK(vkCreatePipelineLayout(Backend::getDevice(), &pipelineLayoutInfo, nullptr, &pipelineLayout));
}

VkPipelineShaderStageCreateInfo PipelineManager::createPipelineShaderStage(VkShaderStageFlagBits stage, VkShaderModule shaderModule) {
	VkPipelineShaderStageCreateInfo shaderStageInfo{};
	shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	shaderStageInfo.stage = stage;
	shaderStageInfo.module = shaderModule;
	shaderStageInfo.pName = "main";

	return shaderStageInfo;
}

void PipelineManager::setupShaders(std::vector<VkPipelineShaderStageCreateInfo>& shaderStages, std::vector<ShaderStageInfo>& shaderStageInfo, DeletionQueue& shaderDeletionQueue) {
	shaderStages.clear();

	for (auto& shaders : shaderStageInfo) {
		VkPipelineShaderStageCreateInfo shader = setShader(shaders.filePath, shaders.stage, shaderDeletionQueue);
		shaderStages.push_back(shader);
	}
}

VkPipelineShaderStageCreateInfo PipelineManager::setShader(const char* shaderFile, VkShaderStageFlagBits stage, DeletionQueue& shaderDeleteQueue) {
	VkShaderModule shaderModule;
	VulkanUtils::loadShaderModule(shaderFile, Backend::getDevice(), &shaderModule);
	VkPipelineShaderStageCreateInfo shaderStage = createPipelineShaderStage(stage, shaderModule);

	shaderDeleteQueue.push_function([=] {
		vkDestroyShaderModule(Backend::getDevice(), shaderModule, nullptr);
	});

	return shaderStage;
}

void PipelineManager::setupPipelineConfig(PipelineBuilder& pipeline, PipelinePresent& settings) {

	PipelineConfigs::inputAssemblyConfig(pipeline._inputAssembly, settings.topology, VK_FALSE);

	PipelineConfigs::rasterizerConfig(pipeline._rasterizer, settings.polygonMode, 1.f, settings.cullMode, settings.frontFace);

	PipelineConfigs::multisamplingConfig(pipeline._multisampling, Renderer::getAvailableSampleCounts(), MSAACOUNT, VK_FALSE);

	PipelineConfigs::colorBlendingConfig(pipeline._colorBlendAttachment,
		VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT, settings.enableBlending, VK_BLEND_FACTOR_ONE);

	if (settings.enableDepthTest && settings.enableDepthWrite) {
		PipelineConfigs::depthStencilConfig(pipeline._depthStencil, VK_TRUE, VK_TRUE, VK_FALSE, VK_FALSE, settings.depthCompareOp);
	}
	else if (settings.enableDepthTest && !settings.enableDepthWrite) {
		PipelineConfigs::depthStencilConfig(pipeline._depthStencil, VK_TRUE, VK_FALSE, VK_FALSE, VK_FALSE, settings.depthCompareOp);
	}
	else {
		PipelineConfigs::depthStencilConfig(pipeline._depthStencil, VK_FALSE, VK_FALSE, VK_FALSE, VK_FALSE, settings.depthCompareOp);
	}

	PipelineConfigs::setColorAttachmentAndDepthFormat(pipeline._colorAttachmentformat,
		settings.colorFormat, pipeline._renderInfo, settings.depthFormat);
}

// PIPELINE CONFIGURATION

void PipelineConfigs::inputAssemblyConfig(VkPipelineInputAssemblyStateCreateInfo& inputAssembly, VkPrimitiveTopology topology, bool primitiveRestartEnabled) {
	inputAssembly.topology = topology;
	inputAssembly.primitiveRestartEnable = primitiveRestartEnabled;
}
void PipelineConfigs::rasterizerConfig(VkPipelineRasterizationStateCreateInfo& rasterizer, VkPolygonMode mode, float lineWidth, VkCullModeFlags cullMode, VkFrontFace frontFace) {
	rasterizer.polygonMode = mode;
	rasterizer.lineWidth = lineWidth;
	rasterizer.cullMode = cullMode;
	rasterizer.frontFace = frontFace;
}

// 1u, 2u, 4u, 8u, 16u, 32u, 64u for msaa counts
// Even though max is 8 as of now
void PipelineConfigs::multisamplingConfig(VkPipelineMultisampleStateCreateInfo& multisampling,
	const std::vector<VkSampleCountFlags>& samples, uint32_t chosenMSAACount, bool sampleShadingEnabled) {

	bool isPowerOfTwo = (chosenMSAACount != 0) && ((chosenMSAACount & (chosenMSAACount - 1)) == 0);
	bool isWithinRange = (chosenMSAACount <= 64);
	if (!isPowerOfTwo || !isWithinRange) {
		throw std::runtime_error("Invalid MSAA count! Must be a power of two up to 64.");
	}

	// Default to sample count 1
	VkSampleCountFlagBits msaaSample = VK_SAMPLE_COUNT_1_BIT;
	if (static_cast<VkSampleCountFlagBits>(chosenMSAACount) == msaaSample) {
		multisampling.rasterizationSamples = msaaSample;
	}
	else {
		bool found = false;
		for (auto sample : samples) {

			if (sample == static_cast<VkSampleCountFlags>(chosenMSAACount)) {
				msaaSample = static_cast<VkSampleCountFlagBits>(sample);
				multisampling.rasterizationSamples = msaaSample;
				found = true;
				break;
			}
		}
		if (!found) {
			throw std::runtime_error("Failed to find valid sample count!");
		}
	}

	multisampling.sampleShadingEnable = sampleShadingEnabled;
	multisampling.minSampleShading = 1.0f;
	multisampling.pSampleMask = nullptr;
	// no alpha to coverage either
	multisampling.alphaToCoverageEnable = VK_FALSE;
	multisampling.alphaToOneEnable = VK_FALSE;
}

void PipelineConfigs::colorBlendingConfig(VkPipelineColorBlendAttachmentState& colorBlend, VkColorComponentFlags colorComponents, bool blendEnabled, VkBlendFactor blendFactor) {
	colorBlend.colorWriteMask = colorComponents;
	colorBlend.blendEnable = blendEnabled;
	colorBlend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
	colorBlend.dstColorBlendFactor = blendFactor;
	colorBlend.colorBlendOp = VK_BLEND_OP_ADD;
	colorBlend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	colorBlend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	colorBlend.alphaBlendOp = VK_BLEND_OP_ADD;
}
void PipelineConfigs::setColorAttachmentAndDepthFormat(VkFormat& colorAttachmentformat, VkFormat colorFormat, VkPipelineRenderingCreateInfo& renderInfo, VkFormat depthFormat) {
	colorAttachmentformat = colorFormat;
	renderInfo.colorAttachmentCount = 1;
	renderInfo.pColorAttachmentFormats = &colorAttachmentformat;

	renderInfo.depthAttachmentFormat = depthFormat;
}
void PipelineConfigs::depthStencilConfig(VkPipelineDepthStencilStateCreateInfo& depthStencil, bool depthTestEnabled, bool depthWriteEnabled, bool depthBoundsTestEnabled, bool stencilTestEnabled, VkCompareOp depthCompare) {
	depthStencil.depthTestEnable = depthTestEnabled;
	depthStencil.depthWriteEnable = depthWriteEnabled;
	depthStencil.depthCompareOp = depthCompare;
	depthStencil.depthBoundsTestEnable = depthBoundsTestEnabled;
	depthStencil.stencilTestEnable = stencilTestEnabled;
	depthStencil.front = {};
	depthStencil.back = {};
	depthStencil.minDepthBounds = 0.f;
	depthStencil.maxDepthBounds = 1.f;
}