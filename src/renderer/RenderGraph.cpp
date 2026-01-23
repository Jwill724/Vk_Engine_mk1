#include "pch.h"

#include "RenderGraph.h"
#include "RenderScene.h"

void MeshNode::Draw(const glm::mat4& topMatrix, DrawContext& ctx) {
	glm::mat4 nodeMatrix;
	for (auto& s : mesh->surfaces) {
		nodeMatrix = topMatrix * worldTransform;

		RenderObject def;
		def.indexCount = s.count;
		def.firstIndex = s.startIndex;
		def.indexBuffer = mesh->meshBuffers.indexBuffer.buffer;
		def.material = &s.material->data;
		def.aabb = RenderGraph::transformAABB(s.aabb, nodeMatrix);
		def.transform = nodeMatrix;
		def.vertexBufferAddress = mesh->meshBuffers.vertexBufferAddress;

		if (s.material->data.passType == MaterialPass::Transparent) {
			if (!ctx.enableCull || RenderGraph::isVisible(def.aabb, ctx.frustum)) {
				ctx.TransparentSurfaces.push_back(def);
			}
		}
		else {
			if (!ctx.enableCull || RenderGraph::isVisible(def.aabb, ctx.frustum)) {
				ctx.OpaqueSurfaces.push_back(def);
			}
		}
	}

	// recurse down
	Node::Draw(topMatrix, ctx);
}

Frustum RenderGraph::extractFrustum(const glm::mat4& viewproj) {
	const glm::mat4 vpt = glm::transpose(viewproj);

	Frustum frustum;

	frustum.planes[0] = vpt[3] + vpt[0]; // left
	frustum.planes[1] = vpt[3] - vpt[0]; // right
	frustum.planes[2] = vpt[3] + vpt[1]; // bot
	frustum.planes[3] = vpt[3] - vpt[1]; // top
	frustum.planes[4] = vpt[3] + vpt[2]; // near
	frustum.planes[5] = vpt[3] - vpt[2]; // far

	for (int i = 0; i < 6; ++i) {
		frustum.planes[i] /= glm::length(glm::vec3(frustum.planes[i]));
	}

	glm::mat4 invVp = glm::inverse(viewproj);
	int i = 0;
	for (int x = -1; x <= 1; x += 2) {
		for (int y = -1; y <= 1; y += 2) {
			for (int z = -1; z <= 1; z += 2) {
				glm::vec4 corner = invVp * glm::vec4(x, y, z, 1.f);
				frustum.points[i++] = glm::vec3(corner) / corner.w;
			}
		}
	}

	return frustum;
}

AABB RenderGraph::transformAABB(const AABB& localBox, const glm::mat4& transform) {
	// Convert to min/max corners first
	const glm::vec3 vmin = localBox.vmin;
	const glm::vec3 vmax = localBox.vmax;

	// If the extents are invalid, early out
	if (!isFiniteVec3(vmin) || !isFiniteVec3(vmax))
		return{};

	const glm::vec3 corners[8] = {
		glm::vec3(transform * glm::vec4(vmin.x, vmin.y, vmin.z, 1.f)),
		glm::vec3(transform * glm::vec4(vmin.x, vmax.y, vmin.z, 1.f)),
		glm::vec3(transform * glm::vec4(vmin.x, vmin.y, vmax.z, 1.f)),
		glm::vec3(transform * glm::vec4(vmin.x, vmax.y, vmax.z, 1.f)),
		glm::vec3(transform * glm::vec4(vmax.x, vmin.y, vmin.z, 1.f)),
		glm::vec3(transform * glm::vec4(vmax.x, vmax.y, vmin.z, 1.f)),
		glm::vec3(transform * glm::vec4(vmax.x, vmin.y, vmax.z, 1.f)),
		glm::vec3(transform * glm::vec4(vmax.x, vmax.y, vmax.z, 1.f))
	};

	// Now apply the min/max algorithm from before using the 8 transformed
	// corners
	glm::vec3 newVmin = corners[0];
	glm::vec3 newVmax = newVmin;

	// Start looping from corner 1 onwards
	for (size_t i = 1; i < 8; ++i) {
		const auto& current = corners[i];
		newVmin = glm::min(newVmin, current);
		newVmax = glm::max(newVmax, current);
	}

	AABB worldBox;
	worldBox.vmin = newVmin;
	worldBox.vmax = newVmax;
	worldBox.origin = (newVmax + newVmin) * 0.5f;
	worldBox.extent = (newVmax - newVmin) * 0.5f;
	worldBox.sphereRadius = glm::length(worldBox.extent);

	return worldBox;
}

// Frustum culling
bool RenderGraph::isVisible(const AABB& aabb, const Frustum& frus) {
	if (!boxInFrustum(frus, aabb)) {
		return false;
	}

	return true; // object is in the view frustum
}

bool RenderGraph::boxInFrustum(const Frustum& fru, const AABB& box) {
	//For each plane in the frustum
	for (int i = 0; i < 6; i++) {
		const glm::vec3 center = (box.vmax + box.vmin) * 0.5f;
		const glm::vec3 extents = (box.vmax - box.vmin) * 0.5f;

		glm::vec3 normal = glm::vec3(fru.planes[i]);
		float d = fru.planes[i].w;

		float dist = glm::dot(normal, center) + d;

		if (dist < -box.sphereRadius) return false;

		float r =
			extents.x * abs(normal.x) +
			extents.y * abs(normal.y) +
			extents.z * abs(normal.z);

		if (dist + r < 0.f) return false;
	}

	int out;

	// Check +X
	out = 0;
	for (int i = 0; i < 8; i++)
		out += (fru.points[i].x > box.vmax.x) ? 1 : 0;
	if (out == 8) return false;

	// Check -X
	out = 0;
	for (int i = 0; i < 8; i++)
		out += (fru.points[i].x < box.vmin.x) ? 1 : 0;
	if (out == 8) return false;

	// Check +Y
	out = 0;
	for (int i = 0; i < 8; i++)
		out += (fru.points[i].y > box.vmax.y) ? 1 : 0;
	if (out == 8) return false;

	// Check -Y
	out = 0;
	for (int i = 0; i < 8; i++)
		out += (fru.points[i].y < box.vmin.y) ? 1 : 0;
	if (out == 8) return false;

	// Check +Z
	out = 0;
	for (int i = 0; i < 8; i++)
		out += (fru.points[i].z > box.vmax.z) ? 1 : 0;
	if (out == 8) return false;

	// Check -Z
	out = 0;
	for (int i = 0; i < 8; i++)
		out += (fru.points[i].z < box.vmin.z) ? 1 : 0;
	if (out == 8) return false;

	return true;
}

std::vector<glm::vec3> RenderGraph::GetAABBVertices(const AABB& box) {
	const glm::vec3 vmin = box.vmin;
	const glm::vec3 vmax = box.vmax;

	// If the extents are invalid, early out
	if (!isFiniteVec3(vmin) || !isFiniteVec3(vmax))
		return{};

	const glm::vec3 corners[8] = {
		glm::vec3(vmin.x, vmin.y, vmin.z),
		glm::vec3(vmin.x, vmax.y, vmin.z),
		glm::vec3(vmin.x, vmin.y, vmax.z),
		glm::vec3(vmin.x, vmax.y, vmax.z),
		glm::vec3(vmax.x, vmin.y, vmin.z),
		glm::vec3(vmax.x, vmax.y, vmin.z),
		glm::vec3(vmax.x, vmin.y, vmax.z),
		glm::vec3(vmax.x, vmax.y, vmax.z)
	};

	// Now connect the corners to form 12 lines
	std::vector<glm::vec3> vertices = {
		corners[0], corners[1], // Line 1
		corners[2], corners[3], // Line 2
		corners[4], corners[5],
		corners[6], corners[7],

		corners[0], corners[2],
		corners[1], corners[3],
		corners[4], corners[6],
		corners[5], corners[7],

		corners[0], corners[4],
		corners[1], corners[5],
		corners[2], corners[6],
		corners[3], corners[7]  // Line 12
	};

	return vertices;
}