/**
 * @file vk/model.hpp
 * @brief Structures representing mesh data.
 */

#pragma once

#include "buffer.hpp"

#include <assimp/Importer.hpp>
#include <filesystem>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <physfs.h>
#include <thread>
#include <vector>
#include <vulkan/vulkan.hpp>

namespace mxn::vk
{
	class context;

	struct vertex final
	{
		using index_t = uint32_t;

		glm::vec3 pos, colour;
		glm::vec2 uv;
		glm::vec3 normal, binormal;
	};

	struct bufslice final
	{
		const ::vk::Buffer buffer; /// Non-owning.
		const ::vk::DeviceSize offset = 0, size = 0;
	};

	struct mesh final
	{
		bufslice verts, indices;
		size_t index_count;
	};

	struct model final
	{
		vma_buffer buffer;
		std::vector<mesh> meshes;
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
