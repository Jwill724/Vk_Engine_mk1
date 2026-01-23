#include "Window.h"
#include <stdexcept>
#include <iostream>
#include "Engine.h"
#include "renderer/Renderer.h"

static void framebufferResizeCallback(GLFWwindow* window, int width, int height) {
	auto win = reinterpret_cast<Window*>(glfwGetWindowUserPointer(window));
	win->windowResized = true;
}

bool WindowIsOpen(GLFWwindow* window) {
	return !(glfwWindowShouldClose(window));
}

void Window::updateWindowSize() {
	int width = 0, height = 0;
	glfwGetFramebufferSize(window, &width, &height);
	while (width == 0 || height == 0) {
		glfwGetFramebufferSize(window, &width, &height);
		glfwWaitEvents();
	}

	// Ensures global window extent is up to date
	Engine::getWindowExtent().width = static_cast<uint32_t>(width);
	Engine::getWindowExtent().height = static_cast<uint32_t>(height);

	Renderer::getDrawExtent() = {
		static_cast<uint32_t>(Engine::getWindowExtent().width),
		static_cast<uint32_t>(Engine::getWindowExtent().height),
		1
	};
}

void Window::initWindow(const uint32_t width, const uint32_t height) {
	if (!glfwInit()) {
		throw std::runtime_error("Failed to initialize GLFW!");
	}

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

	window = glfwCreateWindow(width, height, "This is bullshit", nullptr, nullptr);
	if (!window) {
		throw std::runtime_error("Failed to create window!");
	}

	glfwSetWindowUserPointer(window, this);
	glfwSetFramebufferSizeCallback(window, framebufferResizeCallback);
}

void Window::cleanupWindow() {
	glfwDestroyWindow(window);
	glfwTerminate();
}