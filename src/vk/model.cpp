/**
 * @file vk/model.cpp
 * @brief Structures representing mesh data.
 */

#include "model.hpp"

#include "../file.hpp"
#include "context.hpp"
#include "detail.hpp"

#include <Tracy.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

using namespace mxn::vk;

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

	using mesh_pair = std::pair<std::vector<vertex>, std::vector<vertex::index_t>>;
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

	model.buffer = vma_buffer(
		ctxt,
		::vk::BufferCreateInfo(
			::vk::BufferCreateFlags(), total_size,
			::vk::BufferUsageFlagBits::eTransferDst |
				::vk::BufferUsageFlagBits::eVertexBuffer |
				::vk::BufferUsageFlagBits::eIndexBuffer),
		VMA_ALLOC_CREATEINFO_GENERAL);

	vma_buffer staging = vma_buffer::staging_preset(ctxt, total_size);
	offset = total_size = 0;

	for (auto& pair : mesh_pairs)
	{
		if (pair.second.size() <= 0) continue;

		const ::vk::DeviceSize vbsz = pair.first.size() * sizeof(vertex),
							   ibsz = pair.second.size() * sizeof(vertex::index_t);

		model.meshes.push_back(mesh { .verts = bufslice { .buffer = model.buffer.buffer,
														  .offset = offset,
														  .size = vbsz },
									  .indices = bufslice { .buffer = model.buffer.buffer,
															.offset = offset + vbsz,
															.size = ibsz },
									  .index_count = pair.second.size() });

		void* dv = ctxt.device.mapMemory(staging.memory, offset, vbsz);
		memcpy(dv, reinterpret_cast<void*>(pair.first.data()), vbsz);
		ctxt.device.unmapMemory(staging.memory);

		void* di = ctxt.device.mapMemory(staging.memory, offset + vbsz, ibsz);
		memcpy(di, reinterpret_cast<void*>(pair.second.data()), ibsz);
		ctxt.device.unmapMemory(staging.memory);

		offset += (vbsz + ibsz);
	}

	staging.copy_to(ctxt, model.buffer, { ::vk::BufferCopy(0, 0, total_size) });
	staging.destroy(ctxt);
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
			fmt::format("Model Import, {}", reinterpret_cast<void*>(this)).c_str());

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
