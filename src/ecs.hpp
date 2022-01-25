/**
 * @file ecs.hpp
 * @brief Entity-component-system-related symbols.
 */

#pragma once

#include <glm/vec3.hpp>

namespace mxn
{
	struct alignas(32) point_light final
	{
		glm::vec3 position;
		float radius = 5.0f;
		glm::vec3 intensity = { 1.0f, 1.0f, 1.0f };
	};
}
