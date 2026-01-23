#include "pch.h"

#include "EditorImgui.h"

#include "vulkan/Backend.h"
#include "vulkan/PipelineManager.h"
#include "renderer/Renderer.h"
#include "input/UserInput.h"
#include "renderer/RenderScene.h"

void EditorImgui::initImgui() {
	//  the size of the pool is very oversize, but it's copied from imgui demo
	//  itself.

	VkDevice device = Backend::getDevice();

	VkDescriptorPoolSize pool_sizes[] = { { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
		{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
		{ VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 } };

	VkDescriptorPoolCreateInfo pool_info = {};
	pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
	pool_info.maxSets = 1000;
	pool_info.poolSizeCount = (uint32_t)std::size(pool_sizes);
	pool_info.pPoolSizes = pool_sizes;

	VkDescriptorPool imguiPool;
	VK_CHECK(vkCreateDescriptorPool(device, &pool_info, nullptr, &imguiPool));

	// this initializes the core structures of imgui
	ImGui::CreateContext();

	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;  // Enable keyboard controls
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;   // Enable gamepad controls
	io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange; // Prevent ImGui from overriding the cursor
	io.IniFilename = nullptr; // Won't create imgui file

	ImGui_ImplGlfw_InitForVulkan(Engine::getWindow(), true);

	// this initializes imgui for Vulkan
	ImGui_ImplVulkan_InitInfo init_info = {};
	init_info.Instance = Backend::getInstance();
	init_info.PhysicalDevice = Backend::getPhysicalDevice();
	init_info.Device = device;
	init_info.Queue = Backend::getGraphicsQueue();
	init_info.DescriptorPool = imguiPool;
	init_info.MinImageCount = 3;
	init_info.ImageCount = 3;
	init_info.UseDynamicRendering = true;

	//dynamic rendering parameters for imgui to use
	init_info.PipelineRenderingCreateInfo = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
	init_info.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
	init_info.PipelineRenderingCreateInfo.pColorAttachmentFormats = &Backend::getSwapchainImageFormat();

	init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

	ImGui_ImplVulkan_Init(&init_info);

	ImGui_ImplVulkan_CreateFontsTexture();

	// add the destroy the imgui created structures
	Engine::getDeletionQueue().push_function([=]() {
		ImGui_ImplVulkan_Shutdown();
		vkDestroyDescriptorPool(Backend::getDevice(), imguiPool, nullptr);
	});
}

// call before RenderFrame
void EditorImgui::renderImgui() {

	auto& stats = Engine::getStats();

	ImGui_ImplVulkan_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();

	ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - 10.f, 10.f), ImGuiCond_Always, ImVec2(1.f, 0.f));
//	ImGui::SetNextWindowBgAlpha(1.f);
	ImGui::Begin("Stats", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize);
	ImGui::Text("Camera Pos: %.2f %.2f %.2f", RenderScene::mainCamera.position.x, RenderScene::mainCamera.position.y, RenderScene::mainCamera.position.z);
	ImGui::Text("FPS: %.1f", 1000.f / stats.frametime);
	ImGui::Text("frame time %f ms", stats.frametime);
	ImGui::Text("draw time %f ms", stats.meshDrawTime);
	ImGui::Text("update time %f ms", stats.sceneUpdateTime);
	ImGui::Text("triangles %i", stats.triangleCount);
	ImGui::Text("draws %i", stats.drawcallCount);

	ImGui::End();

	// -- DEBUG TOOLS WINDOW --
	ImGui::Begin("Debug");

	// Pipeline override section
	if (ImGui::CollapsingHeader("Pipeline Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Checkbox("Pipeline Override", &pipelineOverride.enabled);

		static int selected = static_cast<int>(pipelineOverride.selected);
		const char* names[] = { "Opaque", "Transparent", "Wireframe" };

		if (ImGui::Combo("Force Pipeline", &selected, names, IM_ARRAYSIZE(names))) {
			pipelineOverride.selected = static_cast<PipelineType>(selected);
		}

		if (ImGui::TreeNode("Debug Draw")) {
			ImGui::Checkbox("Draw AABB", &RenderSceneSettings::drawBoundingBoxes);
			ImGui::TreePop();
		}
	}

	// Background controls section (Compute shader/post process effects)
	if (ImGui::CollapsingHeader("Options", ImGuiTreeNodeFlags_DefaultOpen)) {
		ComputeEffect& selected = Pipelines::postProcessPipeline.getComputeEffect();

		ImGui::Text("Color Correction");

		ImGui::SliderFloat("Brightness", &selected.data.data1.x, 0.0f, 2.0f);
		ImGui::SliderFloat("Saturation", &selected.data.data1.y, 0.0f, 2.0f);
		ImGui::SliderFloat("Contrast", &selected.data.data1.z, 0.0f, 2.0f);
	}

	ImGui::End();
	ImGui::Render();
}

// draws into a swapchain image
void EditorImgui::drawImgui(VkCommandBuffer cmd, VkImageView targetImageView, bool shouldClear) {
	VkClearValue clearValue{};
	clearValue.color = { 0.0f, 0.0f, 0.0f, 1.0f }; // Black clear color

	VkRenderingAttachmentInfo colorAttachment{};
	colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	colorAttachment.pNext = nullptr;

	colorAttachment.imageView = targetImageView;
	colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	colorAttachment.loadOp = shouldClear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
	colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

	if (shouldClear) {
		colorAttachment.clearValue = clearValue;
	}

	VkRenderingInfo renderingInfo{};
	renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
	renderingInfo.colorAttachmentCount = 1;
	renderingInfo.pColorAttachments = &colorAttachment;
	renderingInfo.renderArea = { {0, 0}, { Backend::getSwapchainExtent().width,  Backend::getSwapchainExtent().height} };
	renderingInfo.layerCount = 1;
	renderingInfo.viewMask = 0;

	vkCmdBeginRendering(cmd, &renderingInfo);

	ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);

	vkCmdEndRendering(cmd);
}

void EditorImgui::MyWindowFocusCallback(GLFWwindow* window, int focused) {
	ImGui_ImplGlfw_WindowFocusCallback(window, focused);  // Forward to ImGui
}