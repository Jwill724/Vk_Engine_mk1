#include "pch.h"

#include "Camera.h"
#include "imgui/EditorImgui.h"
#include "renderer/RenderScene.h"

void Camera::update(GLFWwindow* window, float& lastTime) {
	float currentTime = static_cast<float>(glfwGetTime());
	float deltaTime = currentTime - lastTime;
	lastTime = currentTime;

	processInput(window, deltaTime);

	position += velocity;
}

void Camera::processInput(GLFWwindow* window, float dt) {
	UserInput::updateLocalInput(window, true, true);

	float baseSpeed = UserInput::keyboard.isPressed(GLFW_KEY_LEFT_SHIFT) ? 25.f : 10.f;
	float speed = baseSpeed * dt;

	// Mouse rotation, imgui can be properly used with free cam
	if (!ImGui::GetIO().WantCaptureMouse && UserInput::mouse.leftPressed) {
		float sensitivity = 30.f;
		yaw -= UserInput::mouse.delta.x* sensitivity;
		pitch += UserInput::mouse.delta.y * sensitivity;
		pitch = std::clamp(pitch, -89.f, 89.f);
	}

	float radPitch = glm::radians(pitch);
	float radYaw = glm::radians(yaw);

	glm::vec3 front = glm::normalize(glm::vec3(
		cos(radPitch) * cos(radYaw),
		sin(radPitch),
		cos(radPitch) * sin(radYaw)
	));

	currentView = front;

	glm::vec3 right = glm::normalize(glm::cross(front, glm::vec3(0.f, 1.f, 0.f)));
	glm::vec3 up = glm::normalize(glm::cross(right, front));

	// for world orientation relative to the camera movements
	glm::vec3 upWorld(0.f, 1.f, 0.f);
	glm::vec3 flatFoward = glm::normalize(glm::vec3(currentView.x, 0.f, currentView.z));
	glm::vec3 rightFoward = glm::normalize(glm::vec3(right.x, 0.f, right.z));

	float extraVertSpeed = 1000.f;

	glm::vec3 horiz = glm::vec3(0.f);
	if (UserInput::keyboard.isPressed(GLFW_KEY_W)) horiz += flatFoward;
	if (UserInput::keyboard.isPressed(GLFW_KEY_S)) horiz -= flatFoward;
	if (UserInput::keyboard.isPressed(GLFW_KEY_A)) horiz -= rightFoward;
	if (UserInput::keyboard.isPressed(GLFW_KEY_D)) horiz += rightFoward;

	glm::vec3 vert = glm::vec3(0.f);
	if (UserInput::keyboard.isPressed(GLFW_KEY_SPACE)) vert += up * upWorld * extraVertSpeed;
	if (UserInput::keyboard.isPressed(GLFW_KEY_LEFT_CONTROL)) vert -= up * upWorld * extraVertSpeed;

	if (glm::length(horiz) > 0.f) horiz = glm::normalize(horiz);
	velocity = (horiz * extraVertSpeed) + vert;

	if (glm::length(velocity) > 0.f) {
		velocity = glm::normalize(velocity) * speed;
	}
	else {
		velocity = glm::vec3(0.f);
	}

	if (UserInput::keyboard.isPressed(GLFW_KEY_R)) {
		reset();
	}
}

glm::mat4 Camera::getViewMatrix() {
	return glm::lookAt(position, position + currentView, glm::vec3(0.f, 1.f, 0.f));
}

glm::mat4 Camera::getRotationMatrix() const {
	glm::quat pitchRotation = glm::angleAxis(glm::radians(pitch), glm::vec3{ 1.f, 0.f, 0.f });
	glm::quat yawRotation = glm::angleAxis(glm::radians(yaw), glm::vec3{ 0.f, 1.f, 0.f });

	glm::quat orientation = yawRotation * pitchRotation;
	return glm::toMat4(orientation);
}

void Camera::reset() {
	position = SPAWNPOINT;
	pitch = 0.f;
	yaw = -90.f;
}