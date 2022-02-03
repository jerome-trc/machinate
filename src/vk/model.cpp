/**
 * @file vk/model.cpp
 * @brief Structures representing mesh data.
 */

#include "model.hpp"

#include "../file.hpp"
#include "../world.hpp"
#include "context.hpp"
#include "detail.hpp"

#include <Tracy.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <xxhash.h>

using namespace mxn::vk;

namespace std
{
	template<>
	struct hash<vertex>
	{
		size_t operator()(const vertex& vert) const
		{
			return XXH64(reinterpret_cast<const void*>(&vert), sizeof(vert), 0);
		}
	};
} // namespace std

using tri = std::array<uint32_t, 3>;
using mesh_pair = std::pair<std::vector<vertex>, std::vector<vertex::index_t>>;

[[nodiscard]] static std::pair<std::vector<glm::vec3>, std::vector<tri>> polygonise(
	const std::array<float, 8>&, const glm::vec3);

void mxn::vk::fill_vertex_buffer(
	const context& ctxt, vma_buffer& buf, const std::vector<vertex>& verts)
{
	void* d = nullptr;
	const auto res = vmaMapMemory(ctxt.vma, buf.allocation, &d);
	assert(res == VK_SUCCESS);
	memcpy(d, reinterpret_cast<const void*>(verts.data()), verts.size() * sizeof(vertex));
	vmaUnmapMemory(ctxt.vma, buf.allocation);
}

void mxn::vk::fill_index_buffer(
	const context& ctxt, vma_buffer& buf, const std::vector<uint32_t>& indices)
{
	void* d = nullptr;
	const auto res = vmaMapMemory(ctxt.vma, buf.allocation, &d);
	assert(res == VK_SUCCESS);
	memcpy(
		d, reinterpret_cast<const void*>(indices.data()),
		indices.size() * sizeof(vertex::index_t));
	vmaUnmapMemory(ctxt.vma, buf.allocation);
}

void material::destroy(const context& ctxt)
{
	info.destroy(ctxt);
	// TODO: Free descriptor set

	if (albedo) albedo.destroy(ctxt);
	if (normal) normal.destroy(ctxt);
}

model model::from_heightmap(const context& ctxt, const heightmap& hmap)
{
	mesh_pair mpair = {};
	auto& verts = mpair.first;
	auto& indices = mpair.second;

	static constexpr float HSCALE = .00001f;
	const glm::vec2 pos_offs = { heightmap::WORLD_SIZE * hmap.position.x,
								 heightmap::WORLD_SIZE * hmap.position.y };

	verts.reserve(heightmap::WIDTH * heightmap::WIDTH);

	for (size_t y = 0; y < heightmap::WIDTH; y++)
	{
		for (size_t x = 0; x < heightmap::WIDTH; x++)
		{
			verts.push_back({ .pos = { static_cast<float>(x + pos_offs.x),
									   static_cast<float>(y + pos_offs.y),
									   static_cast<float>(hmap.heights[y][x]) * HSCALE },
							  .colour = { 1.0f, 1.0f, 1.0f },
							  .uv = { /* TODO */ },
							  // Calculated post-hoc
							  .normal = {},
							  .binormal = {} });
		}
	}

	static constexpr size_t WM1 = heightmap::WIDTH - 1;

	indices.resize(WM1 * WM1 * 6);

	for (uint32_t ti = 0, vi = 0, z = 0; z < WM1; z++, vi++)
	{
		for (uint32_t x = 0; x < WM1; x++, ti += 6, vi++)
		{
			indices[ti] = vi;
			indices[ti + 3] = indices[ti + 2] = vi + 1;
			indices[ti + 4] = indices[ti + 1] = vi + WM1 + 1;
			indices[ti + 5] = vi + WM1 + 2;
		}
	}

	// Calculate vertex normals
	for (size_t e = 0; e < indices.size(); e += 3)
	{
		const uint32_t e0 = indices[e], e1 = indices[e + 1], e2 = indices[e + 2];

		const glm::vec3 v1 = verts[e1].pos - verts[e0].pos,
						v2 = verts[e2].pos - verts[e0].pos;

		const glm::vec3 normal = glm::normalize(glm::cross(v1, v2));

		verts[e0].normal += normal;
		verts[e1].normal += normal;
		verts[e2].normal += normal;
		verts[e0].normal = glm::normalize(verts[e0].normal);
		verts[e1].normal = glm::normalize(verts[e1].normal);
		verts[e2].normal = glm::normalize(verts[e2].normal);
	}

	const size_t vbsz = (verts.size() * sizeof(vertex)),
				 ibsz = (indices.size() * sizeof(vertex::index_t));

	model ret = { .meshes = {
					  { .verts = vma_buffer(
							ctxt,
							::vk::BufferCreateInfo(
								::vk::BufferCreateFlags(), vbsz,
								::vk::BufferUsageFlagBits::eTransferDst |
									::vk::BufferUsageFlagBits::eVertexBuffer |
									::vk::BufferUsageFlagBits::eIndexBuffer),
							VMA_ALLOC_CREATEINFO_GENERAL),
						.indices = vma_buffer(
							ctxt,
							::vk::BufferCreateInfo(
								::vk::BufferCreateFlags(), ibsz,
								::vk::BufferUsageFlagBits::eTransferDst |
									::vk::BufferUsageFlagBits::eVertexBuffer |
									::vk::BufferUsageFlagBits::eIndexBuffer),
							VMA_ALLOC_CREATEINFO_GENERAL),
						.index_count = static_cast<uint32_t>(indices.size()) } } };

	{
		vma_buffer staging = vma_buffer::staging_preset(ctxt, vbsz);
		fill_vertex_buffer(ctxt, staging, verts);
		staging.copy_to(ctxt, ret.meshes.back().verts, { ::vk::BufferCopy(0, 0, vbsz) });
		staging.destroy(ctxt);
	}

	{
		vma_buffer staging = vma_buffer::staging_preset(ctxt, ibsz);
		fill_index_buffer(ctxt, staging, indices);
		staging.copy_to(
			ctxt, ret.meshes.back().indices, { ::vk::BufferCopy(0, 0, ibsz) });
		staging.destroy(ctxt);
	}

	ctxt.set_debug_name(
		ret.meshes[0].verts.buffer,
		fmt::format("MXN: Buffer (V), Chunk {}, {}", hmap.position.x, hmap.position.y));
	ctxt.set_debug_name(
		ret.meshes[0].indices.buffer,
		fmt::format("MXN: Buffer (I), Chunk {}, {}", hmap.position.x, hmap.position.y));

	return ret;
}

model model::from_world_chunk(const context& ctxt, const world_chunk& chunk)
{
	static constexpr float HALFCHUNK = mxn::world_chunk::WORLD_SIZE * 0.5f,
						   HALFCELL = mxn::world_chunk::CELL_SIZE * 0.5f;

	mesh_pair mpair = {};
	auto& verts = mpair.first;
	auto& indices = mpair.second;

	const glm::vec3 world_pos = {
		static_cast<float>(chunk.position.x) * mxn::world_chunk::WORLD_SIZE,
		static_cast<float>(chunk.position.y) * mxn::world_chunk::WORLD_SIZE,
		static_cast<float>(chunk.position.z) * mxn::world_chunk::WORLD_SIZE
	};

	for (size_t z = 0; z < world_chunk::WIDTH - 1; z++)
	{
		for (size_t y = 0; y < world_chunk::WIDTH - 1; y++)
		{
			for (size_t x = 0; x < world_chunk::WIDTH - 1; x++)
			{
				const glm::vec3 cell_pos = {
					(world_pos.x - HALFCHUNK) +
						(mxn::world_chunk::CELL_SIZE * static_cast<float>(x)) + HALFCELL,
					(world_pos.y - HALFCHUNK) +
						(mxn::world_chunk::CELL_SIZE * static_cast<float>(y)) + HALFCELL,
					(world_pos.z - HALFCHUNK) +
						(mxn::world_chunk::CELL_SIZE * static_cast<float>(z)) + HALFCELL
				};

				const auto p = polygonise(
					{ chunk.value_at(x, y, z), chunk.value_at(x + 1, y, z),
					  chunk.value_at(x + 1, y + 1, z), chunk.value_at(x, y + 1, z),
					  chunk.value_at(x, y, z + 1), chunk.value_at(x + 1, y, z + 1),
					  chunk.value_at(x + 1, y + 1, z + 1),
					  chunk.value_at(x, y + 1, z + 1) },
					cell_pos);

				const auto offset = static_cast<uint32_t>(verts.size());

				for (const auto& t : p.second)
				{
					indices.push_back(t[0] + offset);
					indices.push_back(t[1] + offset);
					indices.push_back(t[2] + offset);
				}

				for (const auto& v : p.first)
				{
					vertex vx = { .pos = { v.x, v.y, v.z },
								  .colour = { 1.0f, 1.0f, 1.0f },
								  .uv = { /* TODO */ },
								  // Calculated post-hoc
								  .normal = {},
								  .binormal = {} };

					verts.push_back(vx);
				}
			}
		}
	}

	// Calculate vertex normals
	for (size_t e = 0; e < indices.size(); e += 3)
	{
		const uint32_t e0 = indices[e], e1 = indices[e + 1], e2 = indices[e + 2];

		const glm::vec3 v1 = verts[e1].pos - verts[e0].pos,
						v2 = verts[e2].pos - verts[e0].pos;

		const glm::vec3 normal = glm::normalize(glm::cross(v1, v2));

		verts[e0].normal += normal;
		verts[e1].normal += normal;
		verts[e2].normal += normal;
		verts[e0].normal = glm::normalize(verts[e0].normal);
		verts[e1].normal = glm::normalize(verts[e1].normal);
		verts[e2].normal = glm::normalize(verts[e2].normal);
	}

	const size_t vbsz = (verts.size() * sizeof(vertex)),
				 ibsz = (indices.size() * sizeof(vertex::index_t));

	model ret = { .meshes = {
					  { .verts = vma_buffer(
							ctxt,
							::vk::BufferCreateInfo(
								::vk::BufferCreateFlags(), vbsz,
								::vk::BufferUsageFlagBits::eTransferDst |
									::vk::BufferUsageFlagBits::eVertexBuffer |
									::vk::BufferUsageFlagBits::eIndexBuffer),
							VMA_ALLOC_CREATEINFO_GENERAL),
						.indices = vma_buffer(
							ctxt,
							::vk::BufferCreateInfo(
								::vk::BufferCreateFlags(), ibsz,
								::vk::BufferUsageFlagBits::eTransferDst |
									::vk::BufferUsageFlagBits::eVertexBuffer |
									::vk::BufferUsageFlagBits::eIndexBuffer),
							VMA_ALLOC_CREATEINFO_GENERAL),
						.index_count = static_cast<uint32_t>(indices.size()) } } };

	{
		vma_buffer staging = vma_buffer::staging_preset(ctxt, vbsz);
		fill_vertex_buffer(ctxt, staging, verts);
		staging.copy_to(ctxt, ret.meshes.back().verts, { ::vk::BufferCopy(0, 0, vbsz) });
		staging.destroy(ctxt);
	}

	{
		vma_buffer staging = vma_buffer::staging_preset(ctxt, ibsz);
		fill_index_buffer(ctxt, staging, indices);
		staging.copy_to(
			ctxt, ret.meshes.back().indices, { ::vk::BufferCopy(0, 0, ibsz) });
		staging.destroy(ctxt);
	}

	ctxt.set_debug_name(
		ret.meshes[0].verts.buffer,
		fmt::format(
			"MXN: Buffer (V), Chunk {}, {}, {}", chunk.position.x, chunk.position.y,
			chunk.position.z));
	ctxt.set_debug_name(
		ret.meshes[0].indices.buffer,
		fmt::format(
			"MXN: Buffer (I), Chunk {}, {}, {}", chunk.position.x, chunk.position.y,
			chunk.position.z));

	return ret;
}

void model::destroy(const context& ctxt)
{
	for (auto& mesh : meshes)
	{
		mesh.verts.destroy(ctxt);
		mesh.indices.destroy(ctxt);
	}
}

void model_importer::import_file(const std::filesystem::path& path)
{
	const aiScene* scene = importer.ReadFile(
		path.string(), aiProcess_CalcTangentSpace | aiProcess_Triangulate |
						   aiProcess_JoinIdenticalVertices | aiProcess_SortByPType);

	if (scene == nullptr)
	{
		MXN_ERRF(
			"Model import failed: {}\n\t{}", path.string(), importer.GetErrorString());
		return;
	}

	std::vector<mesh_pair> mesh_pairs;

	auto& model = output.emplace_back();
	::vk::DeviceSize total_size = 0, offset = 0;

	for (size_t i = 0; i < scene->mNumMeshes; i++)
	{
		auto& pair = mesh_pairs.emplace_back();
		auto& verts = pair.first;
		auto& indices = pair.second;
		const auto m = scene->mMeshes[i];

		for (unsigned int j = 0; j < m->mNumVertices; j++)
		{
			const auto &v = m->mVertices[j], &bt = m->mBitangents[j];
			const auto colour = m->mColors[j];
			const auto uv = m->mTextureCoords[j];
			const auto& norm = m->mNormals[j];

			verts.push_back({ .pos = { v.x, v.y, v.z },
							  .colour = { colour->r, colour->g, colour->b },
							  .uv = { uv->x, uv->y },
							  .normal = { norm.x, norm.y, norm.z },
							  .binormal = { bt.x, bt.y, bt.z } });
		}

		for (unsigned int j = 0; j < m->mNumFaces; j++)
			for (unsigned int k = 0; k < m->mFaces[j].mNumIndices; k++)
				indices.push_back(m->mFaces[j].mIndices[k]);

		const ::vk::DeviceSize vbsz = verts.size() * sizeof(vertex),
							   ibsz = indices.size() * sizeof(vertex::index_t);

		offset += vbsz + ibsz;
		total_size += vbsz + ibsz;
	}

	for (auto& pair : mesh_pairs)
	{
		if (pair.second.size() <= 0) continue;

		const ::vk::DeviceSize vbsz = pair.first.size() * sizeof(vertex),
							   ibsz = pair.second.size() * sizeof(vertex::index_t);

		model.meshes.push_back(
			mesh { .verts = vma_buffer(
					   ctxt,
					   ::vk::BufferCreateInfo(
						   ::vk::BufferCreateFlags(), vbsz,
						   ::vk::BufferUsageFlagBits::eTransferDst |
							   ::vk::BufferUsageFlagBits::eVertexBuffer |
							   ::vk::BufferUsageFlagBits::eIndexBuffer),
					   VMA_ALLOC_CREATEINFO_GENERAL),
				   .indices = vma_buffer(
					   ctxt,
					   ::vk::BufferCreateInfo(
						   ::vk::BufferCreateFlags(), ibsz,
						   ::vk::BufferUsageFlagBits::eTransferDst |
							   ::vk::BufferUsageFlagBits::eVertexBuffer |
							   ::vk::BufferUsageFlagBits::eIndexBuffer),
					   VMA_ALLOC_CREATEINFO_GENERAL),
				   .index_count = static_cast<uint32_t>(pair.second.size()) });

		{
			vma_buffer staging = vma_buffer::staging_preset(ctxt, vbsz);
			fill_vertex_buffer(ctxt, staging, pair.first);
			staging.copy_to(
				ctxt, model.meshes.back().verts, { ::vk::BufferCopy(0, 0, vbsz) });
			staging.destroy(ctxt);
		}

		{
			vma_buffer staging = vma_buffer::staging_preset(ctxt, vbsz);
			fill_index_buffer(ctxt, staging, pair.second);
			staging.copy_to(
				ctxt, model.meshes.back().indices, { ::vk::BufferCopy(0, 0, ibsz) });
			staging.destroy(ctxt);
		}
	}
}

PHYSFS_EnumerateCallbackResult model_importer::import_dir(
	void* data, const char* orig_dir, const char* fname)
{
	auto importer = reinterpret_cast<model_importer*>(data);

	char p[256];
	strcpy(p, orig_dir);
	strcat(p, "/");
	strcat(p, fname);

	const std::filesystem::path path(p);

	if (vfs_isdir(p)) vfs_recur(p, data, import_dir);

	if (!importer->importer.IsExtensionSupported(path.extension().string()))
		return PHYSFS_ENUM_OK;

	importer->import_file(path);
	return PHYSFS_ENUM_STOP;
}

model_importer::model_importer(
	const context& ctxt, std::vector<std::filesystem::path>&& p)
	: ctxt(ctxt), paths(std::move(p))
{
	thread = std::thread([&]() -> void {
		tracy::SetThreadName(
			fmt::format("MXN: Model Import, {}", reinterpret_cast<void*>(this)).c_str());

		for (const auto& path : paths)
		{
			if (vfs_isdir(path))
				vfs_recur(path, reinterpret_cast<void*>(this), import_dir);
			else
				import_file(path);
		}
	});
}

std::vector<model>&& model_importer::join()
{
	thread.join();
	return std::move(output);
}

// The following marching cubes implementation is courtesy of Matthew Fisher
// https://graphics.stanford.edu/~mdfisher/MarchingCubes.html
// (no license)

static constexpr int MARCHING_CUBES_EDGES[256] = {
	0x0,   0x109, 0x203, 0x30a, 0x406, 0x50f, 0x605, 0x70c, 0x80c, 0x905, 0xa0f, 0xb06,
	0xc0a, 0xd03, 0xe09, 0xf00, 0x190, 0x99,  0x393, 0x29a, 0x596, 0x49f, 0x795, 0x69c,
	0x99c, 0x895, 0xb9f, 0xa96, 0xd9a, 0xc93, 0xf99, 0xe90, 0x230, 0x339, 0x33,	 0x13a,
	0x636, 0x73f, 0x435, 0x53c, 0xa3c, 0xb35, 0x83f, 0x936, 0xe3a, 0xf33, 0xc39, 0xd30,
	0x3a0, 0x2a9, 0x1a3, 0xaa,	0x7a6, 0x6af, 0x5a5, 0x4ac, 0xbac, 0xaa5, 0x9af, 0x8a6,
	0xfaa, 0xea3, 0xda9, 0xca0, 0x460, 0x569, 0x663, 0x76a, 0x66,  0x16f, 0x265, 0x36c,
	0xc6c, 0xd65, 0xe6f, 0xf66, 0x86a, 0x963, 0xa69, 0xb60, 0x5f0, 0x4f9, 0x7f3, 0x6fa,
	0x1f6, 0xff,  0x3f5, 0x2fc, 0xdfc, 0xcf5, 0xfff, 0xef6, 0x9fa, 0x8f3, 0xbf9, 0xaf0,
	0x650, 0x759, 0x453, 0x55a, 0x256, 0x35f, 0x55,	 0x15c, 0xe5c, 0xf55, 0xc5f, 0xd56,
	0xa5a, 0xb53, 0x859, 0x950, 0x7c0, 0x6c9, 0x5c3, 0x4ca, 0x3c6, 0x2cf, 0x1c5, 0xcc,
	0xfcc, 0xec5, 0xdcf, 0xcc6, 0xbca, 0xac3, 0x9c9, 0x8c0, 0x8c0, 0x9c9, 0xac3, 0xbca,
	0xcc6, 0xdcf, 0xec5, 0xfcc, 0xcc,  0x1c5, 0x2cf, 0x3c6, 0x4ca, 0x5c3, 0x6c9, 0x7c0,
	0x950, 0x859, 0xb53, 0xa5a, 0xd56, 0xc5f, 0xf55, 0xe5c, 0x15c, 0x55,  0x35f, 0x256,
	0x55a, 0x453, 0x759, 0x650, 0xaf0, 0xbf9, 0x8f3, 0x9fa, 0xef6, 0xfff, 0xcf5, 0xdfc,
	0x2fc, 0x3f5, 0xff,	 0x1f6, 0x6fa, 0x7f3, 0x4f9, 0x5f0, 0xb60, 0xa69, 0x963, 0x86a,
	0xf66, 0xe6f, 0xd65, 0xc6c, 0x36c, 0x265, 0x16f, 0x66,	0x76a, 0x663, 0x569, 0x460,
	0xca0, 0xda9, 0xea3, 0xfaa, 0x8a6, 0x9af, 0xaa5, 0xbac, 0x4ac, 0x5a5, 0x6af, 0x7a6,
	0xaa,  0x1a3, 0x2a9, 0x3a0, 0xd30, 0xc39, 0xf33, 0xe3a, 0x936, 0x83f, 0xb35, 0xa3c,
	0x53c, 0x435, 0x73f, 0x636, 0x13a, 0x33,  0x339, 0x230, 0xe90, 0xf99, 0xc93, 0xd9a,
	0xa96, 0xb9f, 0x895, 0x99c, 0x69c, 0x795, 0x49f, 0x596, 0x29a, 0x393, 0x99,	 0x190,
	0xf00, 0xe09, 0xd03, 0xc0a, 0xb06, 0xa0f, 0x905, 0x80c, 0x70c, 0x605, 0x50f, 0x406,
	0x30a, 0x203, 0x109, 0x0
};

static constexpr signed char MARCHING_CUBES_TRIS[256][16] = {
	{ -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
	{ 0, 8, 3, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
	{ 0, 1, 9, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
	{ 1, 8, 3, 9, 8, 1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
	{ 1, 2, 10, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
	{ 0, 8, 3, 1, 2, 10, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
	{ 9, 2, 10, 0, 2, 9, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
	{ 2, 8, 3, 2, 10, 8, 10, 9, 8, -1, -1, -1, -1, -1, -1, -1 },
	{ 3, 11, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
	{ 0, 11, 2, 8, 11, 0, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
	{ 1, 9, 0, 2, 3, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
	{ 1, 11, 2, 1, 9, 11, 9, 8, 11, -1, -1, -1, -1, -1, -1, -1 },
	{ 3, 10, 1, 11, 10, 3, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
	{ 0, 10, 1, 0, 8, 10, 8, 11, 10, -1, -1, -1, -1, -1, -1, -1 },
	{ 3, 9, 0, 3, 11, 9, 11, 10, 9, -1, -1, -1, -1, -1, -1, -1 },
	{ 9, 8, 10, 10, 8, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
	{ 4, 7, 8, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
	{ 4, 3, 0, 7, 3, 4, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
	{ 0, 1, 9, 8, 4, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
	{ 4, 1, 9, 4, 7, 1, 7, 3, 1, -1, -1, -1, -1, -1, -1, -1 },
	{ 1, 2, 10, 8, 4, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
	{ 3, 4, 7, 3, 0, 4, 1, 2, 10, -1, -1, -1, -1, -1, -1, -1 },
	{ 9, 2, 10, 9, 0, 2, 8, 4, 7, -1, -1, -1, -1, -1, -1, -1 },
	{ 2, 10, 9, 2, 9, 7, 2, 7, 3, 7, 9, 4, -1, -1, -1, -1 },
	{ 8, 4, 7, 3, 11, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
	{ 11, 4, 7, 11, 2, 4, 2, 0, 4, -1, -1, -1, -1, -1, -1, -1 },
	{ 9, 0, 1, 8, 4, 7, 2, 3, 11, -1, -1, -1, -1, -1, -1, -1 },
	{ 4, 7, 11, 9, 4, 11, 9, 11, 2, 9, 2, 1, -1, -1, -1, -1 },
	{ 3, 10, 1, 3, 11, 10, 7, 8, 4, -1, -1, -1, -1, -1, -1, -1 },
	{ 1, 11, 10, 1, 4, 11, 1, 0, 4, 7, 11, 4, -1, -1, -1, -1 },
	{ 4, 7, 8, 9, 0, 11, 9, 11, 10, 11, 0, 3, -1, -1, -1, -1 },
	{ 4, 7, 11, 4, 11, 9, 9, 11, 10, -1, -1, -1, -1, -1, -1, -1 },
	{ 9, 5, 4, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
	{ 9, 5, 4, 0, 8, 3, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
	{ 0, 5, 4, 1, 5, 0, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
	{ 8, 5, 4, 8, 3, 5, 3, 1, 5, -1, -1, -1, -1, -1, -1, -1 },
	{ 1, 2, 10, 9, 5, 4, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
	{ 3, 0, 8, 1, 2, 10, 4, 9, 5, -1, -1, -1, -1, -1, -1, -1 },
	{ 5, 2, 10, 5, 4, 2, 4, 0, 2, -1, -1, -1, -1, -1, -1, -1 },
	{ 2, 10, 5, 3, 2, 5, 3, 5, 4, 3, 4, 8, -1, -1, -1, -1 },
	{ 9, 5, 4, 2, 3, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
	{ 0, 11, 2, 0, 8, 11, 4, 9, 5, -1, -1, -1, -1, -1, -1, -1 },
	{ 0, 5, 4, 0, 1, 5, 2, 3, 11, -1, -1, -1, -1, -1, -1, -1 },
	{ 2, 1, 5, 2, 5, 8, 2, 8, 11, 4, 8, 5, -1, -1, -1, -1 },
	{ 10, 3, 11, 10, 1, 3, 9, 5, 4, -1, -1, -1, -1, -1, -1, -1 },
	{ 4, 9, 5, 0, 8, 1, 8, 10, 1, 8, 11, 10, -1, -1, -1, -1 },
	{ 5, 4, 0, 5, 0, 11, 5, 11, 10, 11, 0, 3, -1, -1, -1, -1 },
	{ 5, 4, 8, 5, 8, 10, 10, 8, 11, -1, -1, -1, -1, -1, -1, -1 },
	{ 9, 7, 8, 5, 7, 9, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
	{ 9, 3, 0, 9, 5, 3, 5, 7, 3, -1, -1, -1, -1, -1, -1, -1 },
	{ 0, 7, 8, 0, 1, 7, 1, 5, 7, -1, -1, -1, -1, -1, -1, -1 },
	{ 1, 5, 3, 3, 5, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
	{ 9, 7, 8, 9, 5, 7, 10, 1, 2, -1, -1, -1, -1, -1, -1, -1 },
	{ 10, 1, 2, 9, 5, 0, 5, 3, 0, 5, 7, 3, -1, -1, -1, -1 },
	{ 8, 0, 2, 8, 2, 5, 8, 5, 7, 10, 5, 2, -1, -1, -1, -1 },
	{ 2, 10, 5, 2, 5, 3, 3, 5, 7, -1, -1, -1, -1, -1, -1, -1 },
	{ 7, 9, 5, 7, 8, 9, 3, 11, 2, -1, -1, -1, -1, -1, -1, -1 },
	{ 9, 5, 7, 9, 7, 2, 9, 2, 0, 2, 7, 11, -1, -1, -1, -1 },
	{ 2, 3, 11, 0, 1, 8, 1, 7, 8, 1, 5, 7, -1, -1, -1, -1 },
	{ 11, 2, 1, 11, 1, 7, 7, 1, 5, -1, -1, -1, -1, -1, -1, -1 },
	{ 9, 5, 8, 8, 5, 7, 10, 1, 3, 10, 3, 11, -1, -1, -1, -1 },
	{ 5, 7, 0, 5, 0, 9, 7, 11, 0, 1, 0, 10, 11, 10, 0, -1 },
	{ 11, 10, 0, 11, 0, 3, 10, 5, 0, 8, 0, 7, 5, 7, 0, -1 },
	{ 11, 10, 5, 7, 11, 5, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
	{ 10, 6, 5, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
	{ 0, 8, 3, 5, 10, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
	{ 9, 0, 1, 5, 10, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
	{ 1, 8, 3, 1, 9, 8, 5, 10, 6, -1, -1, -1, -1, -1, -1, -1 },
	{ 1, 6, 5, 2, 6, 1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
	{ 1, 6, 5, 1, 2, 6, 3, 0, 8, -1, -1, -1, -1, -1, -1, -1 },
	{ 9, 6, 5, 9, 0, 6, 0, 2, 6, -1, -1, -1, -1, -1, -1, -1 },
	{ 5, 9, 8, 5, 8, 2, 5, 2, 6, 3, 2, 8, -1, -1, -1, -1 },
	{ 2, 3, 11, 10, 6, 5, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
	{ 11, 0, 8, 11, 2, 0, 10, 6, 5, -1, -1, -1, -1, -1, -1, -1 },
	{ 0, 1, 9, 2, 3, 11, 5, 10, 6, -1, -1, -1, -1, -1, -1, -1 },
	{ 5, 10, 6, 1, 9, 2, 9, 11, 2, 9, 8, 11, -1, -1, -1, -1 },
	{ 6, 3, 11, 6, 5, 3, 5, 1, 3, -1, -1, -1, -1, -1, -1, -1 },
	{ 0, 8, 11, 0, 11, 5, 0, 5, 1, 5, 11, 6, -1, -1, -1, -1 },
	{ 3, 11, 6, 0, 3, 6, 0, 6, 5, 0, 5, 9, -1, -1, -1, -1 },
	{ 6, 5, 9, 6, 9, 11, 11, 9, 8, -1, -1, -1, -1, -1, -1, -1 },
	{ 5, 10, 6, 4, 7, 8, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
	{ 4, 3, 0, 4, 7, 3, 6, 5, 10, -1, -1, -1, -1, -1, -1, -1 },
	{ 1, 9, 0, 5, 10, 6, 8, 4, 7, -1, -1, -1, -1, -1, -1, -1 },
	{ 10, 6, 5, 1, 9, 7, 1, 7, 3, 7, 9, 4, -1, -1, -1, -1 },
	{ 6, 1, 2, 6, 5, 1, 4, 7, 8, -1, -1, -1, -1, -1, -1, -1 },
	{ 1, 2, 5, 5, 2, 6, 3, 0, 4, 3, 4, 7, -1, -1, -1, -1 },
	{ 8, 4, 7, 9, 0, 5, 0, 6, 5, 0, 2, 6, -1, -1, -1, -1 },
	{ 7, 3, 9, 7, 9, 4, 3, 2, 9, 5, 9, 6, 2, 6, 9, -1 },
	{ 3, 11, 2, 7, 8, 4, 10, 6, 5, -1, -1, -1, -1, -1, -1, -1 },
	{ 5, 10, 6, 4, 7, 2, 4, 2, 0, 2, 7, 11, -1, -1, -1, -1 },
	{ 0, 1, 9, 4, 7, 8, 2, 3, 11, 5, 10, 6, -1, -1, -1, -1 },
	{ 9, 2, 1, 9, 11, 2, 9, 4, 11, 7, 11, 4, 5, 10, 6, -1 },
	{ 8, 4, 7, 3, 11, 5, 3, 5, 1, 5, 11, 6, -1, -1, -1, -1 },
	{ 5, 1, 11, 5, 11, 6, 1, 0, 11, 7, 11, 4, 0, 4, 11, -1 },
	{ 0, 5, 9, 0, 6, 5, 0, 3, 6, 11, 6, 3, 8, 4, 7, -1 },
	{ 6, 5, 9, 6, 9, 11, 4, 7, 9, 7, 11, 9, -1, -1, -1, -1 },
	{ 10, 4, 9, 6, 4, 10, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
	{ 4, 10, 6, 4, 9, 10, 0, 8, 3, -1, -1, -1, -1, -1, -1, -1 },
	{ 10, 0, 1, 10, 6, 0, 6, 4, 0, -1, -1, -1, -1, -1, -1, -1 },
	{ 8, 3, 1, 8, 1, 6, 8, 6, 4, 6, 1, 10, -1, -1, -1, -1 },
	{ 1, 4, 9, 1, 2, 4, 2, 6, 4, -1, -1, -1, -1, -1, -1, -1 },
	{ 3, 0, 8, 1, 2, 9, 2, 4, 9, 2, 6, 4, -1, -1, -1, -1 },
	{ 0, 2, 4, 4, 2, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
	{ 8, 3, 2, 8, 2, 4, 4, 2, 6, -1, -1, -1, -1, -1, -1, -1 },
	{ 10, 4, 9, 10, 6, 4, 11, 2, 3, -1, -1, -1, -1, -1, -1, -1 },
	{ 0, 8, 2, 2, 8, 11, 4, 9, 10, 4, 10, 6, -1, -1, -1, -1 },
	{ 3, 11, 2, 0, 1, 6, 0, 6, 4, 6, 1, 10, -1, -1, -1, -1 },
	{ 6, 4, 1, 6, 1, 10, 4, 8, 1, 2, 1, 11, 8, 11, 1, -1 },
	{ 9, 6, 4, 9, 3, 6, 9, 1, 3, 11, 6, 3, -1, -1, -1, -1 },
	{ 8, 11, 1, 8, 1, 0, 11, 6, 1, 9, 1, 4, 6, 4, 1, -1 },
	{ 3, 11, 6, 3, 6, 0, 0, 6, 4, -1, -1, -1, -1, -1, -1, -1 },
	{ 6, 4, 8, 11, 6, 8, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
	{ 7, 10, 6, 7, 8, 10, 8, 9, 10, -1, -1, -1, -1, -1, -1, -1 },
	{ 0, 7, 3, 0, 10, 7, 0, 9, 10, 6, 7, 10, -1, -1, -1, -1 },
	{ 10, 6, 7, 1, 10, 7, 1, 7, 8, 1, 8, 0, -1, -1, -1, -1 },
	{ 10, 6, 7, 10, 7, 1, 1, 7, 3, -1, -1, -1, -1, -1, -1, -1 },
	{ 1, 2, 6, 1, 6, 8, 1, 8, 9, 8, 6, 7, -1, -1, -1, -1 },
	{ 2, 6, 9, 2, 9, 1, 6, 7, 9, 0, 9, 3, 7, 3, 9, -1 },
	{ 7, 8, 0, 7, 0, 6, 6, 0, 2, -1, -1, -1, -1, -1, -1, -1 },
	{ 7, 3, 2, 6, 7, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
	{ 2, 3, 11, 10, 6, 8, 10, 8, 9, 8, 6, 7, -1, -1, -1, -1 },
	{ 2, 0, 7, 2, 7, 11, 0, 9, 7, 6, 7, 10, 9, 10, 7, -1 },
	{ 1, 8, 0, 1, 7, 8, 1, 10, 7, 6, 7, 10, 2, 3, 11, -1 },
	{ 11, 2, 1, 11, 1, 7, 10, 6, 1, 6, 7, 1, -1, -1, -1, -1 },
	{ 8, 9, 6, 8, 6, 7, 9, 1, 6, 11, 6, 3, 1, 3, 6, -1 },
	{ 0, 9, 1, 11, 6, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
	{ 7, 8, 0, 7, 0, 6, 3, 11, 0, 11, 6, 0, -1, -1, -1, -1 },
	{ 7, 11, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
	{ 7, 6, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
	{ 3, 0, 8, 11, 7, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
	{ 0, 1, 9, 11, 7, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
	{ 8, 1, 9, 8, 3, 1, 11, 7, 6, -1, -1, -1, -1, -1, -1, -1 },
	{ 10, 1, 2, 6, 11, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
	{ 1, 2, 10, 3, 0, 8, 6, 11, 7, -1, -1, -1, -1, -1, -1, -1 },
	{ 2, 9, 0, 2, 10, 9, 6, 11, 7, -1, -1, -1, -1, -1, -1, -1 },
	{ 6, 11, 7, 2, 10, 3, 10, 8, 3, 10, 9, 8, -1, -1, -1, -1 },
	{ 7, 2, 3, 6, 2, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
	{ 7, 0, 8, 7, 6, 0, 6, 2, 0, -1, -1, -1, -1, -1, -1, -1 },
	{ 2, 7, 6, 2, 3, 7, 0, 1, 9, -1, -1, -1, -1, -1, -1, -1 },
	{ 1, 6, 2, 1, 8, 6, 1, 9, 8, 8, 7, 6, -1, -1, -1, -1 },
	{ 10, 7, 6, 10, 1, 7, 1, 3, 7, -1, -1, -1, -1, -1, -1, -1 },
	{ 10, 7, 6, 1, 7, 10, 1, 8, 7, 1, 0, 8, -1, -1, -1, -1 },
	{ 0, 3, 7, 0, 7, 10, 0, 10, 9, 6, 10, 7, -1, -1, -1, -1 },
	{ 7, 6, 10, 7, 10, 8, 8, 10, 9, -1, -1, -1, -1, -1, -1, -1 },
	{ 6, 8, 4, 11, 8, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
	{ 3, 6, 11, 3, 0, 6, 0, 4, 6, -1, -1, -1, -1, -1, -1, -1 },
	{ 8, 6, 11, 8, 4, 6, 9, 0, 1, -1, -1, -1, -1, -1, -1, -1 },
	{ 9, 4, 6, 9, 6, 3, 9, 3, 1, 11, 3, 6, -1, -1, -1, -1 },
	{ 6, 8, 4, 6, 11, 8, 2, 10, 1, -1, -1, -1, -1, -1, -1, -1 },
	{ 1, 2, 10, 3, 0, 11, 0, 6, 11, 0, 4, 6, -1, -1, -1, -1 },
	{ 4, 11, 8, 4, 6, 11, 0, 2, 9, 2, 10, 9, -1, -1, -1, -1 },
	{ 10, 9, 3, 10, 3, 2, 9, 4, 3, 11, 3, 6, 4, 6, 3, -1 },
	{ 8, 2, 3, 8, 4, 2, 4, 6, 2, -1, -1, -1, -1, -1, -1, -1 },
	{ 0, 4, 2, 4, 6, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
	{ 1, 9, 0, 2, 3, 4, 2, 4, 6, 4, 3, 8, -1, -1, -1, -1 },
	{ 1, 9, 4, 1, 4, 2, 2, 4, 6, -1, -1, -1, -1, -1, -1, -1 },
	{ 8, 1, 3, 8, 6, 1, 8, 4, 6, 6, 10, 1, -1, -1, -1, -1 },
	{ 10, 1, 0, 10, 0, 6, 6, 0, 4, -1, -1, -1, -1, -1, -1, -1 },
	{ 4, 6, 3, 4, 3, 8, 6, 10, 3, 0, 3, 9, 10, 9, 3, -1 },
	{ 10, 9, 4, 6, 10, 4, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
	{ 4, 9, 5, 7, 6, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
	{ 0, 8, 3, 4, 9, 5, 11, 7, 6, -1, -1, -1, -1, -1, -1, -1 },
	{ 5, 0, 1, 5, 4, 0, 7, 6, 11, -1, -1, -1, -1, -1, -1, -1 },
	{ 11, 7, 6, 8, 3, 4, 3, 5, 4, 3, 1, 5, -1, -1, -1, -1 },
	{ 9, 5, 4, 10, 1, 2, 7, 6, 11, -1, -1, -1, -1, -1, -1, -1 },
	{ 6, 11, 7, 1, 2, 10, 0, 8, 3, 4, 9, 5, -1, -1, -1, -1 },
	{ 7, 6, 11, 5, 4, 10, 4, 2, 10, 4, 0, 2, -1, -1, -1, -1 },
	{ 3, 4, 8, 3, 5, 4, 3, 2, 5, 10, 5, 2, 11, 7, 6, -1 },
	{ 7, 2, 3, 7, 6, 2, 5, 4, 9, -1, -1, -1, -1, -1, -1, -1 },
	{ 9, 5, 4, 0, 8, 6, 0, 6, 2, 6, 8, 7, -1, -1, -1, -1 },
	{ 3, 6, 2, 3, 7, 6, 1, 5, 0, 5, 4, 0, -1, -1, -1, -1 },
	{ 6, 2, 8, 6, 8, 7, 2, 1, 8, 4, 8, 5, 1, 5, 8, -1 },
	{ 9, 5, 4, 10, 1, 6, 1, 7, 6, 1, 3, 7, -1, -1, -1, -1 },
	{ 1, 6, 10, 1, 7, 6, 1, 0, 7, 8, 7, 0, 9, 5, 4, -1 },
	{ 4, 0, 10, 4, 10, 5, 0, 3, 10, 6, 10, 7, 3, 7, 10, -1 },
	{ 7, 6, 10, 7, 10, 8, 5, 4, 10, 4, 8, 10, -1, -1, -1, -1 },
	{ 6, 9, 5, 6, 11, 9, 11, 8, 9, -1, -1, -1, -1, -1, -1, -1 },
	{ 3, 6, 11, 0, 6, 3, 0, 5, 6, 0, 9, 5, -1, -1, -1, -1 },
	{ 0, 11, 8, 0, 5, 11, 0, 1, 5, 5, 6, 11, -1, -1, -1, -1 },
	{ 6, 11, 3, 6, 3, 5, 5, 3, 1, -1, -1, -1, -1, -1, -1, -1 },
	{ 1, 2, 10, 9, 5, 11, 9, 11, 8, 11, 5, 6, -1, -1, -1, -1 },
	{ 0, 11, 3, 0, 6, 11, 0, 9, 6, 5, 6, 9, 1, 2, 10, -1 },
	{ 11, 8, 5, 11, 5, 6, 8, 0, 5, 10, 5, 2, 0, 2, 5, -1 },
	{ 6, 11, 3, 6, 3, 5, 2, 10, 3, 10, 5, 3, -1, -1, -1, -1 },
	{ 5, 8, 9, 5, 2, 8, 5, 6, 2, 3, 8, 2, -1, -1, -1, -1 },
	{ 9, 5, 6, 9, 6, 0, 0, 6, 2, -1, -1, -1, -1, -1, -1, -1 },
	{ 1, 5, 8, 1, 8, 0, 5, 6, 8, 3, 8, 2, 6, 2, 8, -1 },
	{ 1, 5, 6, 2, 1, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
	{ 1, 3, 6, 1, 6, 10, 3, 8, 6, 5, 6, 9, 8, 9, 6, -1 },
	{ 10, 1, 0, 10, 0, 6, 9, 5, 0, 5, 6, 0, -1, -1, -1, -1 },
	{ 0, 3, 8, 5, 6, 10, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
	{ 10, 5, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
	{ 11, 5, 10, 7, 5, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
	{ 11, 5, 10, 11, 7, 5, 8, 3, 0, -1, -1, -1, -1, -1, -1, -1 },
	{ 5, 11, 7, 5, 10, 11, 1, 9, 0, -1, -1, -1, -1, -1, -1, -1 },
	{ 10, 7, 5, 10, 11, 7, 9, 8, 1, 8, 3, 1, -1, -1, -1, -1 },
	{ 11, 1, 2, 11, 7, 1, 7, 5, 1, -1, -1, -1, -1, -1, -1, -1 },
	{ 0, 8, 3, 1, 2, 7, 1, 7, 5, 7, 2, 11, -1, -1, -1, -1 },
	{ 9, 7, 5, 9, 2, 7, 9, 0, 2, 2, 11, 7, -1, -1, -1, -1 },
	{ 7, 5, 2, 7, 2, 11, 5, 9, 2, 3, 2, 8, 9, 8, 2, -1 },
	{ 2, 5, 10, 2, 3, 5, 3, 7, 5, -1, -1, -1, -1, -1, -1, -1 },
	{ 8, 2, 0, 8, 5, 2, 8, 7, 5, 10, 2, 5, -1, -1, -1, -1 },
	{ 9, 0, 1, 5, 10, 3, 5, 3, 7, 3, 10, 2, -1, -1, -1, -1 },
	{ 9, 8, 2, 9, 2, 1, 8, 7, 2, 10, 2, 5, 7, 5, 2, -1 },
	{ 1, 3, 5, 3, 7, 5, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
	{ 0, 8, 7, 0, 7, 1, 1, 7, 5, -1, -1, -1, -1, -1, -1, -1 },
	{ 9, 0, 3, 9, 3, 5, 5, 3, 7, -1, -1, -1, -1, -1, -1, -1 },
	{ 9, 8, 7, 5, 9, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
	{ 5, 8, 4, 5, 10, 8, 10, 11, 8, -1, -1, -1, -1, -1, -1, -1 },
	{ 5, 0, 4, 5, 11, 0, 5, 10, 11, 11, 3, 0, -1, -1, -1, -1 },
	{ 0, 1, 9, 8, 4, 10, 8, 10, 11, 10, 4, 5, -1, -1, -1, -1 },
	{ 10, 11, 4, 10, 4, 5, 11, 3, 4, 9, 4, 1, 3, 1, 4, -1 },
	{ 2, 5, 1, 2, 8, 5, 2, 11, 8, 4, 5, 8, -1, -1, -1, -1 },
	{ 0, 4, 11, 0, 11, 3, 4, 5, 11, 2, 11, 1, 5, 1, 11, -1 },
	{ 0, 2, 5, 0, 5, 9, 2, 11, 5, 4, 5, 8, 11, 8, 5, -1 },
	{ 9, 4, 5, 2, 11, 3, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
	{ 2, 5, 10, 3, 5, 2, 3, 4, 5, 3, 8, 4, -1, -1, -1, -1 },
	{ 5, 10, 2, 5, 2, 4, 4, 2, 0, -1, -1, -1, -1, -1, -1, -1 },
	{ 3, 10, 2, 3, 5, 10, 3, 8, 5, 4, 5, 8, 0, 1, 9, -1 },
	{ 5, 10, 2, 5, 2, 4, 1, 9, 2, 9, 4, 2, -1, -1, -1, -1 },
	{ 8, 4, 5, 8, 5, 3, 3, 5, 1, -1, -1, -1, -1, -1, -1, -1 },
	{ 0, 4, 5, 1, 0, 5, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
	{ 8, 4, 5, 8, 5, 3, 9, 0, 5, 0, 3, 5, -1, -1, -1, -1 },
	{ 9, 4, 5, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
	{ 4, 11, 7, 4, 9, 11, 9, 10, 11, -1, -1, -1, -1, -1, -1, -1 },
	{ 0, 8, 3, 4, 9, 7, 9, 11, 7, 9, 10, 11, -1, -1, -1, -1 },
	{ 1, 10, 11, 1, 11, 4, 1, 4, 0, 7, 4, 11, -1, -1, -1, -1 },
	{ 3, 1, 4, 3, 4, 8, 1, 10, 4, 7, 4, 11, 10, 11, 4, -1 },
	{ 4, 11, 7, 9, 11, 4, 9, 2, 11, 9, 1, 2, -1, -1, -1, -1 },
	{ 9, 7, 4, 9, 11, 7, 9, 1, 11, 2, 11, 1, 0, 8, 3, -1 },
	{ 11, 7, 4, 11, 4, 2, 2, 4, 0, -1, -1, -1, -1, -1, -1, -1 },
	{ 11, 7, 4, 11, 4, 2, 8, 3, 4, 3, 2, 4, -1, -1, -1, -1 },
	{ 2, 9, 10, 2, 7, 9, 2, 3, 7, 7, 4, 9, -1, -1, -1, -1 },
	{ 9, 10, 7, 9, 7, 4, 10, 2, 7, 8, 7, 0, 2, 0, 7, -1 },
	{ 3, 7, 10, 3, 10, 2, 7, 4, 10, 1, 10, 0, 4, 0, 10, -1 },
	{ 1, 10, 2, 8, 7, 4, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
	{ 4, 9, 1, 4, 1, 7, 7, 1, 3, -1, -1, -1, -1, -1, -1, -1 },
	{ 4, 9, 1, 4, 1, 7, 0, 8, 1, 8, 7, 1, -1, -1, -1, -1 },
	{ 4, 0, 3, 7, 4, 3, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
	{ 4, 8, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
	{ 9, 10, 8, 10, 11, 8, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
	{ 3, 0, 9, 3, 9, 11, 11, 9, 10, -1, -1, -1, -1, -1, -1, -1 },
	{ 0, 1, 10, 0, 10, 8, 8, 10, 11, -1, -1, -1, -1, -1, -1, -1 },
	{ 3, 1, 10, 11, 3, 10, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
	{ 1, 2, 11, 1, 11, 9, 9, 11, 8, -1, -1, -1, -1, -1, -1, -1 },
	{ 3, 0, 9, 3, 9, 11, 1, 2, 9, 2, 11, 9, -1, -1, -1, -1 },
	{ 0, 2, 11, 8, 0, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
	{ 3, 2, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
	{ 2, 3, 8, 2, 8, 10, 10, 8, 9, -1, -1, -1, -1, -1, -1, -1 },
	{ 9, 10, 2, 0, 9, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
	{ 2, 3, 8, 2, 8, 10, 0, 1, 8, 1, 10, 8, -1, -1, -1, -1 },
	{ 1, 10, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
	{ 1, 3, 8, 9, 1, 8, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
	{ 0, 9, 1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
	{ 0, 3, 8, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 },
	{ -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 }
};

[[nodiscard]] static constexpr glm::vec3 vert_interp(
	const glm::vec3& p1, const glm::vec3& p2, float val1, float val2) noexcept
{
	return (p1 + (-val1 / (val2 - val1)) * (p2 - p1));
}

static std::pair<std::vector<glm::vec3>, std::vector<tri>> polygonise(
	const std::array<float, 8>& cell, const glm::vec3 cellpos)
{
	static constexpr float SHIFT = mxn::world_chunk::CELL_SIZE;

	size_t ndx = 0;

	for (size_t i = 0; i < cell.size(); i++)
	{
		if (cell[i] < 0.0f) ndx |= (1 << i);
	}

	// Cube is entirely in/out of surface
	if (MARCHING_CUBES_EDGES[ndx] == 0) return {};

	glm::vec3 verts[12] = {};
	signed char local_remap[12] = {};

	// Find the vertices where the surface intersects the cube

	if (MARCHING_CUBES_EDGES[ndx] & 1)
	{
		verts[0] = vert_interp(
			glm::vec3(cellpos.x, cellpos.y, cellpos.z),
			glm::vec3(cellpos.x + SHIFT, cellpos.y, cellpos.z), cell[0], cell[1]);
	}

	if (MARCHING_CUBES_EDGES[ndx] & 2)
	{
		verts[1] = vert_interp(
			glm::vec3(cellpos.x + SHIFT, cellpos.y, cellpos.z),
			glm::vec3(cellpos.x + SHIFT, cellpos.y + SHIFT, cellpos.z), cell[1], cell[2]);
	}

	if (MARCHING_CUBES_EDGES[ndx] & 4)
	{
		verts[2] = vert_interp(
			glm::vec3(cellpos.x + SHIFT, cellpos.y + SHIFT, cellpos.z),
			glm::vec3(cellpos.x, cellpos.y + SHIFT, cellpos.z), cell[2], cell[3]);
	}

	if (MARCHING_CUBES_EDGES[ndx] & 8)
	{
		verts[3] = vert_interp(
			glm::vec3(cellpos.x, cellpos.y + SHIFT, cellpos.z),
			glm::vec3(cellpos.x, cellpos.y, cellpos.z), cell[3], cell[0]);
	}

	if (MARCHING_CUBES_EDGES[ndx] & 16)
	{
		verts[4] = vert_interp(
			glm::vec3(cellpos.x, cellpos.y, cellpos.z + SHIFT),
			glm::vec3(cellpos.x + SHIFT, cellpos.y, cellpos.z + SHIFT), cell[4], cell[5]);
	}

	if (MARCHING_CUBES_EDGES[ndx] & 32)
	{
		verts[5] = vert_interp(
			glm::vec3(cellpos.x + SHIFT, cellpos.y, cellpos.z + SHIFT),
			glm::vec3(cellpos.x + SHIFT, cellpos.y + SHIFT, cellpos.z + SHIFT), cell[5],
			cell[6]);
	}

	if (MARCHING_CUBES_EDGES[ndx] & 64)
	{
		verts[6] = vert_interp(
			glm::vec3(cellpos.x + SHIFT, cellpos.y + SHIFT, cellpos.z + SHIFT),
			glm::vec3(cellpos.x, cellpos.y + SHIFT, cellpos.z + SHIFT), cell[6], cell[7]);
	}

	if (MARCHING_CUBES_EDGES[ndx] & 128)
	{
		verts[7] = vert_interp(
			glm::vec3(cellpos.x, cellpos.y + SHIFT, cellpos.z + SHIFT),
			glm::vec3(cellpos.x, cellpos.y, cellpos.z + SHIFT), cell[7], cell[4]);
	}

	if (MARCHING_CUBES_EDGES[ndx] & 256)
	{
		verts[8] = vert_interp(
			glm::vec3(cellpos.x, cellpos.y, cellpos.z),
			glm::vec3(cellpos.x, cellpos.y, cellpos.z + SHIFT), cell[0], cell[4]);
	}

	if (MARCHING_CUBES_EDGES[ndx] & 512)
	{
		verts[9] = vert_interp(
			glm::vec3(cellpos.x + SHIFT, cellpos.y, cellpos.z),
			glm::vec3(cellpos.x + SHIFT, cellpos.y, cellpos.z + SHIFT), cell[1], cell[5]);
	}

	if (MARCHING_CUBES_EDGES[ndx] & 1024)
	{
		verts[10] = vert_interp(
			glm::vec3(cellpos.x + SHIFT, cellpos.y + SHIFT, cellpos.z),
			glm::vec3(cellpos.x + SHIFT, cellpos.y + SHIFT, cellpos.z + SHIFT), cell[2],
			cell[6]);
	}

	if (MARCHING_CUBES_EDGES[ndx] & 2048)
	{
		verts[11] = vert_interp(
			glm::vec3(cellpos.x, cellpos.y + SHIFT, cellpos.z),
			glm::vec3(cellpos.x, cellpos.y + SHIFT, cellpos.z + SHIFT), cell[3], cell[7]);
	}

	memset(local_remap, -1, sizeof(local_remap));

	const auto& cube_tri = MARCHING_CUBES_TRIS[ndx];

	std::pair<std::vector<glm::vec3>, std::vector<tri>> ret = {};

	for (size_t i = 0; cube_tri[i] != -1; i++)
	{
		if (local_remap[cube_tri[i]] != -1) continue;

		local_remap[cube_tri[i]] = ret.first.size();
		ret.first.emplace_back(verts[cube_tri[i]]);
	}

	for (size_t i = 0; i < ret.first.size(); i++) verts[i] = ret.first[i];

	for (size_t i = 0; cube_tri[i] != -1; i += 3)
	{
		ret.second.emplace_back(
			tri { static_cast<uint32_t>(local_remap[cube_tri[i]]),
				  static_cast<uint32_t>(local_remap[cube_tri[i + 1]]),
				  static_cast<uint32_t>(local_remap[cube_tri[i + 2]]) });
	}

	return ret;
}
