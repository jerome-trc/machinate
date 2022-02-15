/**
 * @file vk/detail.hpp
 * @brief Simple symbols related to Vulkan which don't belong anywhere else.
 */

#pragma once

#include "../ecs.hpp"
#include "ubo.hpp"

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.hpp>

namespace mxn
{
	struct camera;
}

namespace mxn::vk
{
	constexpr VmaAllocationCreateInfo VMA_ALLOC_CREATEINFO_GENERAL = {
		.flags = 0,
		.usage = VMA_MEMORY_USAGE_GPU_ONLY,
		.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		.preferredFlags = 0,
		.memoryTypeBits = 0,
		.pool = VK_NULL_HANDLE,
		.pUserData = nullptr,
		.priority = 0.0f
	};

	constexpr VmaAllocationCreateInfo VMA_ALLOC_CREATEINFO_STAGING = {
		.flags = 0,
		.usage = VMA_MEMORY_USAGE_GPU_ONLY,
		.requiredFlags =
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		.preferredFlags = 0,
		.memoryTypeBits = 0,
		.pool = VK_NULL_HANDLE,
		.pUserData = nullptr,
		.priority = 0.0f
	};

	/// @brief Vulkan representation of `mxn::camera`.
	struct camera final
	{
		struct
		{
			glm::mat4 view = {}, proj = {}, projview = {};
			glm::vec3 position = {};
		} camera;

		void update(const context&, const mxn::camera& viewp);
	};

	static constexpr uint32_t INVALID_QUEUE_FAMILY = std::numeric_limits<uint32_t>::max(),
							  MAX_POINTLIGHT_COUNT = 2000u;
	static constexpr size_t POINTLIGHT_BUFSIZE =
		sizeof(point_light) * MAX_POINTLIGHT_COUNT + sizeof(glm::vec4);
} // namespace mxn::vk
