#pragma once

#include "common/Vk_Types.h"
#include <unordered_map>
#include <filesystem>
#include "renderer/RenderGraph.h"

struct LoadedGLTF : public IRenderable {
	// storage for all the data on a given glTF file
	std::unordered_map<std::string, std::shared_ptr<MeshAsset>> meshes;
	std::unordered_map<std::string, std::shared_ptr<Node>> nodes;
	std::vector<AllocatedImage> images;
	std::unordered_map<std::string, std::shared_ptr<GLTFMaterial>> materials;

	// nodes that dont have a parent, for iterating through the file in tree order
	std::vector<std::shared_ptr<Node>> topNodes;

	std::filesystem::path basePath;

	std::vector<VkSampler> samplers;

	AllocatedBuffer materialDataBuffer;

	~LoadedGLTF() { clearAll(); };

	virtual void Draw(const glm::mat4& topMatrix, DrawContext& ctx);

private:
	void clearAll();
};

namespace AssetManager {

	AllocatedImage& getWhiteImage();
	AllocatedImage& getCheckboardTex();
	AllocatedImage& getMetalRoughMat();
	AllocatedImage& getNormalMat();

	VkSampler getDefaultSamplerLinear();
	VkSampler getDefaultSamplerNearest();

	std::filesystem::path& getBasePath();

	void loadAssets();
	std::vector<std::shared_ptr<MeshAsset>>& getTestMeshes();

	std::optional<std::shared_ptr<LoadedGLTF>> loadGltf(std::string_view filePath);

	ImmCmdSubmitDef& getGraphicsCmdSubmit();

	DeletionQueue& getAssetDeletionQueue();
	VmaAllocator& getAssetAllocation();

	// MESHES
	GPUMeshBuffers uploadMesh(std::span<uint32_t> indices, std::span<Vertex> vertices);
}