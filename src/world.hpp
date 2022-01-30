/// @file world.hpp

#pragma once

#include <glm/vec3.hpp>

namespace mxn
{
	struct world_chunk final
	{
		/// Despite the name, all dimensions are of this magnitude.
		static constexpr size_t WIDTH = 64;
		
		/// World space distance from edge to opposing edge.
		static constexpr float CELL_SIZE = 0.5f;

		/// World space distance from edge to opposing edge.
		static constexpr float WORLD_SIZE = CELL_SIZE  * (WIDTH - 1);

		using arr_t = std::array<float, WIDTH * WIDTH * WIDTH>;

		/// The position of this chunk on the "grid" of chunks.
		/// Considered to be at the "centre" of the chunk.
		glm::ivec3 position = {};

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

	struct heightmap final
	{
		/// Despite the name, all dimensions are of this magnitude.
		static constexpr size_t WIDTH = 32;

		/// World space distance from edge to edge.
		static constexpr float WORLD_SIZE = static_cast<float>(WIDTH);

		using arr_t = std::array<std::array<uint16_t, WIDTH>, WIDTH>;

		/// The position of this chunk on the "grid" of chunks.
		glm::ivec2 position = {};

		arr_t heights = {};
	};
} // namespace mxn
