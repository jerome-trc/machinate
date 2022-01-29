/// @file world.hpp

#pragma once

#include <glm/vec3.hpp>

namespace mxn
{
	struct world_chunk final
	{
		/// Despite the name, all dimensions are of this magnitude.
		static constexpr size_t WIDTH = 64;
		
		using arr_t = std::array<float, WIDTH * WIDTH * WIDTH>;

		/// World space distance from edge to edge.
		static constexpr float CELL_SIZE = 0.5f;

		/// World space distance from edge to edge.
		static constexpr float CHUNK_SIZE = CELL_SIZE  * (WIDTH - 1);

		/// The centre of this chunk in world space.
		glm::vec3 position = {};

		arr_t values = {};

		[[nodiscard]] static constexpr size_t index(size_t x, size_t y, size_t z) noexcept
		{
			assert(x < WIDTH && y < WIDTH && z < WIDTH);
			return (z * WIDTH * WIDTH) + (y * WIDTH) + x;
		}

		[[nodiscard]] constexpr float value_at(size_t x, size_t y, size_t z) const noexcept
		{
			return values[index(x, y, z)];
		}
	};
} // namespace mxn
