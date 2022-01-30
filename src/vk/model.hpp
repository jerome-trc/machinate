/**
 * @file vk/model.hpp
 * @brief Structures representing mesh data.
 */

#pragma once

#include "buffer.hpp"
#include "image.hpp"
#include "ubo.hpp"

#include <assimp/Importer.hpp>
#include <filesystem>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <physfs.h>
#include <thread>
#include <vector>
#include <vulkan/vulkan.hpp>

namespace mxn
{
	struct heightmap;
	struct world_chunk;
}

namespace mxn::vk
{
	class context;

	struct vertex final
	{
		using index_t = uint32_t;

		glm::vec3 pos, colour;
		glm::vec2 uv;
		glm::vec3 normal, binormal;

		constexpr bool operator==(const vertex& other) const
		{
			return pos == other.pos && colour == other.colour && uv == other.uv &&
				   normal == other.normal && binormal == other.binormal;
		}
	};

	void fill_vertex_buffer(
		const context&, vma_buffer&, const std::vector<vertex>&);
	void fill_index_buffer(
		const context&, vma_buffer&, const std::vector<uint32_t>&);

	struct material_info final
	{
		int has_albedo = 0, has_normal = 0;
	};

	struct material final
	{
		ubo<material_info> info;
		::vk::DescriptorSet descset;
		vma_image albedo, normal;

		void destroy(const context&);
	};

	struct mesh final
	{
		vma_buffer verts, indices;
		uint32_t index_count;
	};

	struct model final
	{
		std::vector<mesh> meshes;

		static model from_heightmap(const context&, const heightmap&);
		static model from_world_chunk(const context&, const world_chunk&);

		void destroy(const context&);
	};

	class model_importer final
	{
		const context& ctxt;
		std::vector<std::filesystem::path> paths;

		Assimp::Importer importer;
		std::thread thread;
		std::vector<model> output;

		void import_file(const std::filesystem::path&);
		static PHYSFS_EnumerateCallbackResult import_dir(
			void* data, const char* orig_dir, const char* fname);

	public:
		model_importer(const context&, std::vector<std::filesystem::path>&&);
		std::vector<model>&& join();
	};
} // namespace mxn::vk
