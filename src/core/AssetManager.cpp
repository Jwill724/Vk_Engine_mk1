#include "pch.h"

#include "AssetManager.h"

#include "Engine.h"
#include "vulkan/Backend.h"

#include "utils/BufferUtils.h"
#include "utils/VulkanUtils.h"
#include "utils/RendererUtils.h"
#include "renderer/RenderScene.h"
#include "renderer/CommandBuffer.h"
#include "renderer/Descriptor.h"
#include "renderer/types/Texture.h"

constexpr float ANISOTROPHY = 16.f;

namespace AssetManager {
	std::vector<std::shared_ptr<MeshAsset>> testMeshes;
	std::vector<std::shared_ptr<MeshAsset>>& getTestMeshes() { return testMeshes; }

	std::filesystem::path _basePath;
	std::filesystem::path& getBasePath() { return _basePath; }

	// Textures
	AllocatedImage _whiteImage;
	AllocatedImage& getWhiteImage() { return _whiteImage; }

	AllocatedImage _blackImage;
	AllocatedImage _greyImage;
	AllocatedImage _errorCheckerboardImage;
	AllocatedImage& getCheckboardTex() { return _errorCheckerboardImage; }

	AllocatedImage _metalRoughMat;
	AllocatedImage& getMetalRoughMat() { return _metalRoughMat; }

	AllocatedImage _normalMat;
	AllocatedImage& getNormalMat() { return _normalMat; }

	VkPhysicalDeviceProperties _deviceProps;
	VkSampler _defaultSamplerLinear;
	VkSampler _defaultSamplerNearest;
	VkSampler getDefaultSamplerLinear() { return _defaultSamplerLinear; }
	VkSampler getDefaultSamplerNearest() { return _defaultSamplerNearest; }


	void initTextures();
	DeletionQueue _assetDeletionQueue;
	DeletionQueue& getAssetDeletionQueue() { return _assetDeletionQueue; }
	VmaAllocator _assetAllocator;
	VmaAllocator& getAssetAllocation() { return _assetAllocator; }

	// for asset setup
	// For now only area of engine to have this just for asset loading
	ImmCmdSubmitDef _immGraphicsCmdSubmit{};
	ImmCmdSubmitDef& getGraphicsCmdSubmit() { return _immGraphicsCmdSubmit; }

	ImmCmdSubmitDef _immTransferCmdSubmit{};

	void convertHDR2Cubemap();
}

// all that is needed to call in engine
void AssetManager::loadAssets() {
	_assetAllocator = VulkanUtils::createAllocator(Backend::getPhysicalDevice(), Backend::getDevice(), Backend::getInstance());
	_assetDeletionQueue.push_function([=] {
		vmaDestroyAllocator(_assetAllocator);
	});

	// single command submit for copying and moving asset data
	CommandBuffer::setupImmediateCmdBuffer(_immGraphicsCmdSubmit, Backend::getGraphicsQueue());

	// only meant for pre rendering copies to gpu
	CommandBuffer::setupImmediateCmdBuffer(_immTransferCmdSubmit, Backend::getTransferQueue());

	initTextures();

	// Primary file loading
	//std::string assetPath = { "res/models/structure.glb" };
	//std::string assetPath = { "res/models/structure_mat.glb" };
	//std::string assetPath = { "res/models/monkey.glb" };
	std::string assetPath = { "res/models/sponza.glb" };
	//std::string assetPath = { "res/models/town4new.glb" };

	auto assetFile = loadGltf(assetPath);
	assert(assetFile.has_value());

	RenderScene::loadedScenes["asset"] = *assetFile;
}

// Global helper functions
// Texture sampling, probably move somewhere else
static VkFilter extract_filter(fastgltf::Filter filter) {
	switch (filter) {
		// nearest samplers
	case fastgltf::Filter::Nearest:
	case fastgltf::Filter::NearestMipMapNearest:
	case fastgltf::Filter::NearestMipMapLinear:
		return VK_FILTER_NEAREST;

		// linear samplers
	case fastgltf::Filter::Linear:
	case fastgltf::Filter::LinearMipMapNearest:
	case fastgltf::Filter::LinearMipMapLinear:
	default:
		return VK_FILTER_LINEAR;
	}
}
static VkSamplerMipmapMode extract_mipmap_mode(fastgltf::Filter filter) {
	switch (filter) {
	case fastgltf::Filter::NearestMipMapNearest:
	case fastgltf::Filter::LinearMipMapNearest:
		return VK_SAMPLER_MIPMAP_MODE_NEAREST;

	case fastgltf::Filter::NearestMipMapLinear:
	case fastgltf::Filter::LinearMipMapLinear:
	default:
		return VK_SAMPLER_MIPMAP_MODE_LINEAR;
	}
}

// TEXTURES
void AssetManager::initTextures() {
	// reuse for now
	VkExtent3D texExtent = { 1, 1, 1 };

	VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
	VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT;
	VkMemoryPropertyFlags memoryProp = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
	VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;

	_whiteImage.imageExtent = texExtent;
	_whiteImage.imageFormat = format;
	_whiteImage.mipmapped = true;

	_greyImage.imageExtent = texExtent;
	_greyImage.imageFormat = format;
	_greyImage.mipmapped = true;

	_blackImage.imageExtent = texExtent;
	_blackImage.imageFormat = format;
	_blackImage.mipmapped = true;

	_metalRoughMat.imageExtent = texExtent;
	_metalRoughMat.imageFormat = format;
	_metalRoughMat.mipmapped = true;

	// From what I've read about modern GLTF pbr, g is roughness and b is metallic.
	uint8_t mrPixelData[4]{
		static_cast<uint8_t>(0.0f * 255), // metallic?
		static_cast<uint8_t>(0.5f * 255), // roughness
		static_cast<uint8_t>(0.0f * 255), // metallic?
		static_cast<uint8_t>(1.0f * 255)
	};

	RendererUtils::createTextureImage((void*)&mrPixelData, _metalRoughMat, usage, memoryProp, samples, &_assetDeletionQueue, _assetAllocator);

	_normalMat.imageExtent = texExtent;
	_normalMat.imageFormat = format;
	_normalMat.mipmapped = true;

	uint32_t flatNormal = glm::packUnorm4x8(glm::vec4(0.5f, 0.5f, 1.0f, 1.0f)); // X = 128, Y = 128, Z = 255, A = 255
	RendererUtils::createTextureImage((void*)&flatNormal, _normalMat, usage, memoryProp, samples, &_assetDeletionQueue, _assetAllocator);

	//3 default textures, white, grey, black. 1 pixel each
	// all same settings
	uint32_t white = glm::packUnorm4x8(glm::vec4(1, 1, 1, 1));
	RendererUtils::createTextureImage((void*)&white, _whiteImage, usage, memoryProp, samples, &_assetDeletionQueue, _assetAllocator);

	uint32_t grey = glm::packUnorm4x8(glm::vec4(0.66f, 0.66f, 0.66f, 1));
	RendererUtils::createTextureImage((void*)&grey, _greyImage, usage, memoryProp, samples, &_assetDeletionQueue, _assetAllocator);

	uint32_t black = glm::packUnorm4x8(glm::vec4(0, 0, 0, 1));
	RendererUtils::createTextureImage((void*)&black, _blackImage, usage, memoryProp, samples, &_assetDeletionQueue, _assetAllocator);

	//checkerboard image
	uint32_t magenta = glm::packUnorm4x8(glm::vec4(1, 0, 1, 1));
	std::array<uint32_t, 16 * 16 > pixels; //for 16x16 checkerboard texture
	for (int x = 0; x < 16; x++) {
		for (int y = 0; y < 16; y++) {
			pixels[y * 16 + x] = ((x % 2) ^ (y % 2)) ? magenta : black;
		}
	}

	VkExtent3D checkerboardedImageExtent { 16, 16, 1 };

	_errorCheckerboardImage.imageExtent = checkerboardedImageExtent;
	_errorCheckerboardImage.imageFormat = format;
	_errorCheckerboardImage.mipmapped = true;

	RendererUtils::createTextureImage(pixels.data(), _errorCheckerboardImage, usage, memoryProp, samples, &_assetDeletionQueue, _assetAllocator);

	vkGetPhysicalDeviceProperties(Backend::getPhysicalDevice(), &_deviceProps);
	_deviceProps.limits.maxSamplerAnisotropy;

	_defaultSamplerLinear = Textures::createSampler(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT, _deviceProps.limits.maxSamplerAnisotropy);

	_defaultSamplerNearest = Textures::createSampler(VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_REPEAT, _deviceProps.limits.maxSamplerAnisotropy);

	convertHDR2Cubemap();

	_assetDeletionQueue.push_function([=]() {
		vkDestroySampler(Backend::getDevice(), _defaultSamplerNearest, nullptr);
		vkDestroySampler(Backend::getDevice(), _defaultSamplerLinear, nullptr);
	});
}

GPUMeshBuffers AssetManager::uploadMesh(std::span<uint32_t> indices, std::span<Vertex> vertices) {
	GPUMeshBuffers newSurface;

	const size_t vertexBufferSize = vertices.size() * sizeof(Vertex);
	const size_t indexBufferSize = indices.size() * sizeof(uint32_t);

	//create vertex buffer
	newSurface.vertexBuffer = BufferUtils::createBuffer(vertexBufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		VMA_MEMORY_USAGE_GPU_ONLY, _assetAllocator);

	//find the address of the vertex buffer
	VkBufferDeviceAddressInfo deviceAdressInfo{ .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, .buffer = newSurface.vertexBuffer.buffer };
	newSurface.vertexBufferAddress = vkGetBufferDeviceAddress(Backend::getDevice(), &deviceAdressInfo);

	//create index buffer
	newSurface.indexBuffer = BufferUtils::createBuffer(indexBufferSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		VMA_MEMORY_USAGE_GPU_ONLY, _assetAllocator);

	AllocatedBuffer staging = BufferUtils::createBuffer(vertexBufferSize + indexBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY, _assetAllocator);

	void* data;
	vmaMapMemory(_assetAllocator, staging.allocation, &data);

	// copy vertex buffer
	memcpy(data, vertices.data(), vertexBufferSize);
	// copy index buffer
	memcpy((char*)data + vertexBufferSize, indices.data(), indexBufferSize);

	vmaUnmapMemory(_assetAllocator, staging.allocation);

	// Note that this pattern is not very efficient,
	// as we are waiting for the GPU command to fully execute before continuing with our CPU side logic.
	//
	// This is something people generally put on a background thread,
	// whose sole job is to execute uploads like this one, and deleting / reusing the staging buffers.

	CommandBuffer::immediateCmdSubmit([&](VkCommandBuffer cmd) {
		VkBufferCopy vertexCopy{ 0 };
		vertexCopy.dstOffset = 0;
		vertexCopy.srcOffset = 0;
		vertexCopy.size = vertexBufferSize;

		vkCmdCopyBuffer(cmd, staging.buffer, newSurface.vertexBuffer.buffer, 1, &vertexCopy);

		VkBufferCopy indexCopy{ 0 };
		indexCopy.dstOffset = 0;
		indexCopy.srcOffset = vertexBufferSize;
		indexCopy.size = indexBufferSize;

		vkCmdCopyBuffer(cmd, staging.buffer, newSurface.indexBuffer.buffer, 1, &indexCopy);
	},
	_immTransferCmdSubmit,
	Backend::getTransferQueue()
	);

	BufferUtils::destroyBuffer(staging, _assetAllocator);

	return newSurface;
}

std::optional<std::shared_ptr<LoadedGLTF>> AssetManager::loadGltf(std::string_view filePath) {
	fmt::print("Loading GLTF: {}\n", filePath);

	auto scene = std::make_shared<LoadedGLTF>();
	LoadedGLTF& file = *scene.get();

	std::filesystem::path path = filePath;

	_basePath = path.parent_path();
	file.basePath = _basePath;

	fastgltf::Parser parser;

	// Load the glTF file into memory using the modern API
	auto data = fastgltf::GltfDataBuffer::FromPath(path);
	if (!data || data.error() != fastgltf::Error::None) {
		fmt::print("Failed to load file: error code {}\n", static_cast<int>(data.error()));
		return std::nullopt;
	}

	constexpr auto gltfOptions =
		fastgltf::Options::DontRequireValidAssetMember |
		fastgltf::Options::AllowDouble |
		fastgltf::Options::LoadGLBBuffers |
		fastgltf::Options::LoadExternalBuffers |
		fastgltf::Options::LoadExternalImages;

	auto type = fastgltf::determineGltfFileType(data.get());
	fastgltf::Asset gltf;

	if (type == fastgltf::GltfType::glTF) {
		auto result = parser.loadGltf(data.get(), path.parent_path(), gltfOptions);
		if (!result || result.error() != fastgltf::Error::None) {
			fmt::print("Failed to parse .gltf: error code {}\n", static_cast<int>(result.error()));
			return std::nullopt;
		}
		gltf = std::move(result.get());
	}
	else if (type == fastgltf::GltfType::GLB) {
		auto result = parser.loadGltfBinary(data.get(), path.parent_path(), gltfOptions);
		if (!result || result.error() != fastgltf::Error::None) {
			fmt::print("Failed to parse .glb: error code {}\n", static_cast<int>(result.error()));
			return std::nullopt;
		}
		gltf = std::move(result.get());
	}
	else {
		fmt::print("Unknown or unsupported glTF file type\n");
		return std::nullopt;
	}

	size_t materialSize = gltf.materials.size();

	if (materialSize == 0) materialSize = 1;

	DescriptorSetOverwatch::initAssetDescriptors(materialSize);

	for (fastgltf::Sampler& sampler : gltf.samplers) {

		VkSamplerCreateInfo sampl = { .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO, .pNext = nullptr };
		sampl.maxLod = VK_LOD_CLAMP_NONE;
		sampl.minLod = 0;

		sampl.magFilter = extract_filter(sampler.magFilter.value_or(fastgltf::Filter::Nearest));
		sampl.minFilter = extract_filter(sampler.minFilter.value_or(fastgltf::Filter::Nearest));

		sampl.mipmapMode = extract_mipmap_mode(sampler.minFilter.value_or(fastgltf::Filter::Nearest));

		sampl.anisotropyEnable = VK_TRUE;
		sampl.maxAnisotropy = _deviceProps.limits.maxSamplerAnisotropy;
		sampl.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		sampl.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		sampl.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;

		VkSampler newSampler;
		vkCreateSampler(Backend::getDevice(), &sampl, nullptr, &newSampler);

		file.samplers.push_back(newSampler);
	}

	std::vector<std::shared_ptr<MeshAsset>> meshes;
	std::vector<std::shared_ptr<Node>> nodes;
	std::vector<std::shared_ptr<GLTFMaterial>> materials;

	// MeshNodes depend on meshes, meshes depend on materials, and materials on textures

	// load all textures
	for (fastgltf::Image& image : gltf.images) {
		std::optional<AllocatedImage> img = Textures::loadImage(gltf, image);

		if (img.has_value()) {
			scene->images.push_back(*img);
		}
		else {
			scene->images.push_back(_errorCheckerboardImage);
			fmt::print("gltf failed to load texture {}\n", image.name);
		}
	}

	// create buffer to hold the material data
	file.materialDataBuffer = BufferUtils::createBuffer(sizeof(GLTFMetallic_Roughness::MaterialConstants) * materialSize,
		VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, _assetAllocator);

	int data_index = 0;
	GLTFMetallic_Roughness::MaterialConstants* sceneMaterialConstants =
		(GLTFMetallic_Roughness::MaterialConstants*)file.materialDataBuffer.info.pMappedData;

	// Setup one default material if asset doesn't come with materials
	if (gltf.materials.empty()) {
		auto newMat = std::make_shared<GLTFMaterial>();
		materials.push_back(newMat);
		file.materials["Default"] = newMat;

		GLTFMetallic_Roughness::MaterialConstants constants;
		sceneMaterialConstants[0] = constants;

		MaterialPass passType = MaterialPass::MainColor;

		GLTFMetallic_Roughness::MaterialResources materialResources;
		materialResources.colorImage = _whiteImage;
		materialResources.colorSampler = _defaultSamplerLinear;
		materialResources.metalRoughImage = _metalRoughMat;
		materialResources.metalRoughSampler = _defaultSamplerNearest;
		materialResources.normalImage = _normalMat;
		materialResources.normalSampler = _defaultSamplerLinear;

		materialResources.dataBuffer = file.materialDataBuffer.buffer;
		materialResources.dataBufferOffset = 0;

		newMat->data = RenderScene::metalRoughMaterial.writeMaterial(
			Backend::getDevice(),
			passType,
			materialResources,
			DescriptorSetOverwatch::assetDescriptorManager
		);
	}

	for (fastgltf::Material& mat : gltf.materials) {
		auto newMat = std::make_shared<GLTFMaterial>();
		materials.push_back(newMat);
		file.materials[mat.name.c_str()] = newMat;

		GLTFMetallic_Roughness::MaterialConstants constants;

		// write material parameters to buffer
		sceneMaterialConstants[data_index] = constants;

		MaterialPass passType = MaterialPass::MainColor;

		if (mat.alphaMode == fastgltf::AlphaMode::Blend) {
			fmt::print("Material name: {}\n", mat.name);
			passType = MaterialPass::Transparent;
		}

		GLTFMetallic_Roughness::MaterialResources materialResources;
		materialResources.colorImage = _whiteImage;
		materialResources.colorSampler = _defaultSamplerLinear;
		materialResources.metalRoughImage = _metalRoughMat;
		materialResources.metalRoughSampler = _defaultSamplerNearest;
		materialResources.normalImage = _normalMat;
		materialResources.normalSampler = _defaultSamplerLinear;

		// set the uniform buffer for the material data
		materialResources.dataBuffer = file.materialDataBuffer.buffer;
		materialResources.dataBufferOffset = data_index * sizeof(GLTFMetallic_Roughness::MaterialConstants);
		// grab textures from gltf file
		if (mat.pbrData.baseColorTexture.has_value()) {
			size_t img = gltf.textures[mat.pbrData.baseColorTexture.value().textureIndex].imageIndex.value();
			size_t sampler = gltf.textures[mat.pbrData.baseColorTexture.value().textureIndex].samplerIndex.value();

			materialResources.colorImage = scene->images[img];
			materialResources.colorSampler = file.samplers[sampler];

			constants.colorFactors.x = mat.pbrData.baseColorFactor[0];
			constants.colorFactors.y = mat.pbrData.baseColorFactor[1];
			constants.colorFactors.z = mat.pbrData.baseColorFactor[2];
			constants.colorFactors.w = mat.pbrData.baseColorFactor[3];
		}

		if (mat.pbrData.metallicRoughnessTexture.has_value()) {
			size_t img = gltf.textures[mat.pbrData.metallicRoughnessTexture.value().textureIndex].imageIndex.value();
			size_t sampler = gltf.textures[mat.pbrData.metallicRoughnessTexture.value().textureIndex].samplerIndex.value();

			materialResources.metalRoughImage = scene->images[img];
			materialResources.metalRoughSampler = file.samplers[sampler];

			constants.metal_rough_factors.x = mat.pbrData.metallicFactor;
			constants.metal_rough_factors.y = mat.pbrData.roughnessFactor;
		}

		if (mat.normalTexture.has_value()) {
			size_t img = gltf.textures[mat.normalTexture.value().textureIndex].imageIndex.value();
			size_t sampler = gltf.textures[mat.normalTexture.value().textureIndex].samplerIndex.value();

			materialResources.normalImage = scene->images[img];
			materialResources.normalSampler = file.samplers[sampler];

			constants.normalScale = mat.normalTexture->scale;
		}

		// build material
		newMat->data = RenderScene::metalRoughMaterial.writeMaterial(
			Backend::getDevice(),
			passType,
			materialResources,
			DescriptorSetOverwatch::assetDescriptorManager
		);

		data_index++;
	}

	// use the same vectors for all meshes so that the memory doesnt reallocate as often
	std::vector<uint32_t> indices;
	std::vector<Vertex> vertices;

	for (fastgltf::Mesh& mesh : gltf.meshes) {
		std::shared_ptr<MeshAsset> newmesh = std::make_shared<MeshAsset>();
		meshes.push_back(newmesh);
		file.meshes[mesh.name.c_str()] = newmesh;
		newmesh->name = mesh.name;

		// clear the mesh arrays each mesh, we dont want to merge them by error
		indices.clear();
		vertices.clear();

		for (auto&& p : mesh.primitives) {
			GeoSurface newSurface;
			newSurface.startIndex = (uint32_t)indices.size();
			newSurface.count = (uint32_t)gltf.accessors[p.indicesAccessor.value()].count;

			size_t initial_vtx = vertices.size();

			// load indexes
			fastgltf::Accessor& indexaccessor = gltf.accessors[p.indicesAccessor.value()];
			indices.reserve(indices.size() + indexaccessor.count);

			fastgltf::iterateAccessorWithIndex<std::uint32_t>(gltf, indexaccessor,
				[&](uint32_t idx, size_t /*index*/) {
					indices.push_back(static_cast<uint32_t>(idx + initial_vtx));
				});

			// load vertex positions
			fastgltf::Accessor& posAccessor = gltf.accessors[p.findAttribute("POSITION")->accessorIndex];
			vertices.resize(vertices.size() + posAccessor.count);

			fastgltf::iterateAccessorWithIndex<glm::vec3>(gltf, posAccessor,
				[&](glm::vec3 v, size_t index) {
					Vertex newvtx;
					newvtx.position = v;
					newvtx.normal = { 1, 0, 0 };
					newvtx.color = glm::vec4{ 1.f };
					newvtx.uv_x = 0;
					newvtx.uv_y = 0;
					vertices[initial_vtx + index] = newvtx;
				});


			// load vertex normals
			auto normals = p.findAttribute("NORMAL");
			if (normals != p.attributes.end()) {

				fastgltf::iterateAccessorWithIndex<glm::vec3>(gltf, gltf.accessors[(*normals).accessorIndex],
					[&](glm::vec3 v, size_t index) {
						vertices[initial_vtx + index].normal = v;
					});
			}

			// load UVs
			auto uv = p.findAttribute("TEXCOORD_0");
			if (uv != p.attributes.end()) {

				fastgltf::iterateAccessorWithIndex<glm::vec2>(gltf, gltf.accessors[(*uv).accessorIndex],
					[&](glm::vec2 v, size_t index) {
						vertices[initial_vtx + index].uv_x = v.x;
						vertices[initial_vtx + index].uv_y = v.y;
					});
			}

			// load vertex colors
			auto colors = p.findAttribute("COLOR_0");
			if (colors != p.attributes.end()) {

				fastgltf::iterateAccessorWithIndex<glm::vec4>(gltf, gltf.accessors[(*colors).accessorIndex],
					[&](glm::vec4 v, size_t index) {
						vertices[initial_vtx + index].color = v;
					});
			}

			if (p.materialIndex.has_value()) {
				newSurface.material = materials[p.materialIndex.value()];
			}
			else {
				newSurface.material = materials[0];
			}

			glm::vec3 vmin = vertices[initial_vtx].position;
			glm::vec3 vmax = vertices[initial_vtx].position;

			for (int i = static_cast<int>(initial_vtx); i < vertices.size(); i++) {
				vmin = glm::min(vmin, vertices[i].position);
				vmax = glm::max(vmax, vertices[i].position);
			}

			assert(vmax.x >= vmin.x && vmax.y >= vmin.y && vmax.z >= vmin.z);

			// calculate origin and extents from the min/max, use extent length for radius
			newSurface.aabb.vmin = vmin;
			newSurface.aabb.vmax = vmax;
			newSurface.aabb.origin = (vmax + vmin) * 0.5f;
			newSurface.aabb.extent = (vmax - vmin) * 0.5f;
			newSurface.aabb.sphereRadius = glm::length(newSurface.aabb.extent);

			newmesh->surfaces.push_back(newSurface);
		}

		newmesh->meshBuffers = uploadMesh(indices, vertices);
	}


	//TODO: Possible SceneGraph cutoff here

	// load all nodes and their meshes
	for (fastgltf::Node& node : gltf.nodes) {
		std::shared_ptr<Node> newNode;

		// find if the node has a mesh, and if it does hook it to the mesh pointer and allocate it with the MeshNode struct
		if (node.meshIndex.has_value()) {
			newNode = std::make_shared<MeshNode>();
			static_cast<MeshNode*>(newNode.get())->mesh = meshes[*node.meshIndex];
		}
		else {
			newNode = std::make_shared<Node>();
		}

		nodes.push_back(newNode);
		file.nodes[node.name.c_str()];

		// load the GLTF transform data, and convert it into a gltf final transform matrix
		std::visit(fastgltf::visitor{
			[&](const fastgltf::math::fmat4x4& matrix) {
				memcpy(&newNode->localTransform, matrix.data(), sizeof(matrix));
			},
			[&](const fastgltf::TRS& transform) {
				glm::vec3 tl(transform.translation[0], transform.translation[1], transform.translation[2]);
				glm::quat rot(transform.rotation[3], transform.rotation[0], transform.rotation[1], transform.rotation[2]);
				glm::vec3 sc(transform.scale[0], transform.scale[1], transform.scale[2]);

				glm::mat4 tm = glm::translate(glm::mat4(1.f), tl);
				glm::mat4 rm = glm::toMat4(rot);
				glm::mat4 sm = glm::scale(glm::mat4(1.f), sc);

				newNode->localTransform = tm * rm * sm;
				}
			}, node.transform);
	}


	// run loop again to setup transform hierarchy
	for (int i = 0; i < gltf.nodes.size(); i++) {
		fastgltf::Node& node = gltf.nodes[i];
		std::shared_ptr<Node>& sceneNode = nodes[i];

		for (auto& c : node.children) {
			sceneNode->children.push_back(nodes[c]);
			nodes[c]->parent = sceneNode;
		}
	}

	// find the top nodes, with no parents
	for (auto& node : nodes) {
		if (node->parent.lock() == nullptr) {
			file.topNodes.push_back(node);
			node->refreshTransform(glm::mat4{ 1.f });
		}
	}

	return scene;
}

void LoadedGLTF::Draw(const glm::mat4& topMatrix, DrawContext& ctx) {
	// create renderables from the scenenodes
	for (auto& n : topNodes) {
		n->Draw(topMatrix, ctx);
	}
}

void LoadedGLTF::clearAll() {
	auto device = Backend::getDevice();
	auto allocator = AssetManager::getAssetAllocation();

	vkQueueWaitIdle(Backend::getGraphicsQueue());

	DescriptorSetOverwatch::assetDescriptorManager.destroyPools();

	BufferUtils::destroyBuffer(materialDataBuffer, allocator);

	for (auto& [k, v] : meshes) {
		BufferUtils::destroyBuffer(v->meshBuffers.indexBuffer, allocator);
		BufferUtils::destroyBuffer(v->meshBuffers.vertexBuffer, allocator);
	}

	for (auto& img : images) {
		//dont destroy the default images
		if (img.image == VK_NULL_HANDLE ||
			img.image == AssetManager::getCheckboardTex().image ||
			img.image == AssetManager::getWhiteImage().image ||
			img.image == AssetManager::getMetalRoughMat().image ||
			img.image == AssetManager::getNormalMat().image)
		{
			continue;
		}
		RendererUtils::destroyTexImage(device, img, allocator);
	}

	for (auto& sampler : samplers) {
		if (sampler == VK_NULL_HANDLE ||
			sampler == AssetManager::getDefaultSamplerLinear() ||
			sampler == AssetManager::getDefaultSamplerNearest())
		{
			continue;
		}

		vkDestroySampler(device, sampler, nullptr);
	}
}

void AssetManager::convertHDR2Cubemap() {
	const char* hdrPath = "res/textures/cubemap/wasteland_clouds_puresky_4k.hdr";

	int w, h, channels;
	float* data = stbi_loadf(hdrPath, &w, &h, &channels, 4);
	if (!data) {
		throw std::runtime_error(std::string("Failed to load HDR: ") + stbi_failure_reason());
	}

	AllocatedImage equirect;
	equirect.imageExtent = { uint32_t(w), uint32_t(h), 1 };
	equirect.imageFormat = VK_FORMAT_R32G32B32A32_SFLOAT;

	RendererUtils::createTextureImage(
		data,
		equirect,
		VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		VK_SAMPLE_COUNT_1_BIT,
		nullptr,
		_assetAllocator
	);
	stbi_image_free(data);

	DescriptorWriter cubeMapWriter;
	cubeMapWriter.writeImage(0, equirect.imageView, _defaultSamplerLinear,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
	cubeMapWriter.writeImage(1, Renderer::getSkyBoxImage().storageView, nullptr,
		VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
	cubeMapWriter.updateSet(Backend::getDevice(), DescriptorSetOverwatch::getCubeMappingDescriptors().descriptorSet);


	auto& pipeline = Pipelines::hdr2cubemapPipeline;
	auto& skyboxImg = Renderer::getSkyBoxImage();

	CommandBuffer::immediateCmdSubmit([&](VkCommandBuffer cmd) {
		RendererUtils::transitionImage(
			cmd,
			equirect.image, equirect.imageFormat,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
		);

		RendererUtils::transitionImage(
			cmd,
			skyboxImg.image, skyboxImg.imageFormat,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_GENERAL
		);

		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.getComputeEffect().pipeline);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
			pipeline._computePipelineLayout,
			0, 1, &DescriptorSetOverwatch::getCubeMappingDescriptors().descriptorSet,
			0, nullptr);

		// push only resolution
		int resolution = int(skyboxImg.imageExtent.width);

		auto& pc = PipelinePresents::hdr2cubemapPipelineSettings.pushConstantsInfo;

		vkCmdPushConstants(cmd, pipeline._computePipelineLayout, pc.stageFlags, pc.offset, pc.size, &resolution);

		// dispatch: local_size = 8x8x1, so groups = ceil(res/8)
		uint32_t groups = (resolution + 7) / 8;
		vkCmdDispatch(cmd, groups, groups, /*depth=*/6);

		// barrier: make those writes visible to fragment skybox
		VkImageMemoryBarrier mb{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
		mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		mb.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
		mb.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		mb.image = skyboxImg.image;
		mb.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, /*baseMip*/0,1, /*baseLayer*/0,6 };

		vkCmdPipelineBarrier(
			cmd,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			0,
			0, nullptr,
			0, nullptr,
			1, &mb
		);
	},
	_immGraphicsCmdSubmit,
	Backend::getGraphicsQueue());

	vkDestroyImageView(Backend::getDevice(), equirect.imageView, nullptr);
	vmaDestroyImage(_assetAllocator, equirect.image, equirect.allocation);
}