#include "Engine.h"
#include "vulkan/Backend.h"
#include "renderer/Renderer.h"
#include "imgui/EditorImgui.h"
#include "core/AssetManager.h"
#include "renderer/RenderScene.h"

namespace Engine {
	Window _window;
	GLFWwindow* getWindow() { return _window.window; }
	// just returns the whole window struct for its use
	Window& windowModMode() { return _window; }

	DeletionQueue _mainDeletionQueue;
	VmaAllocator _allocator;

	EngineStats stats;
	EngineStats& getStats() { return stats; }

	DeletionQueue& getDeletionQueue() { return _mainDeletionQueue; }
	VmaAllocator& getAllocator() { return _allocator; }

	VkExtent2D _windowExtent = { 1280, 960 };
	VkExtent2D& getWindowExtent() { return _windowExtent; }

	bool _isInitialized{ false };
	bool isInitialized() { return _isInitialized; }
	bool stopRendering{ false };
	bool hasRenderStopped() { return stopRendering; }

	float lastTime = 0;
	float& getLastTimeCount() { return lastTime; }

	void init();
	void cleanup();
}

void Engine::init() {
	_window.initWindow(_windowExtent.width, _windowExtent.height);

	Backend::initBackend();

	AssetManager::loadAssets();

	_isInitialized = true;
}

void Engine::run() {
	init();
	Renderer::init();

	glfwSetWindowFocusCallback(_window.window, EditorImgui::MyWindowFocusCallback);
	lastTime = static_cast<float>(glfwGetTime());

	// main loop
	while (WindowIsOpen(_window.window)) {
		auto start = std::chrono::system_clock::now();

		glfwPollEvents();

		EditorImgui::renderImgui();

		Renderer::RenderFrame();

		auto end = std::chrono::system_clock::now();
		auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
		stats.frametime = elapsed.count() / 1000.f;
	}

	stopRendering = true;

	cleanup();
}

void Engine::cleanup() {
	if (_isInitialized) {
		_isInitialized = false;
		vkDeviceWaitIdle(Backend::getDevice());

		RenderScene::loadedScenes.clear();

		AssetManager::getAssetDeletionQueue().flush();

		_mainDeletionQueue.flush();

		Backend::cleanupBackend();
		_window.cleanupWindow();
	}
}