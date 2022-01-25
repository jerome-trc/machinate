/**
 * @file vk/buffer.cpp
 * @brief `vma_buffer`, a structure wrapping a VMA-allocated buffer.
 */

#include "buffer.hpp"

#include "../log.hpp"
#include "context.hpp"
#include "detail.hpp"

#include <magic_enum.hpp>
#include <vk_mem_alloc.h>

using namespace mxn::vk;

vma_buffer::vma_buffer(
	const context& ctxt, const ::vk::BufferCreateInfo& buf_ci,
	const VmaAllocationCreateInfo& alloc_ci)
{
	const auto& c_bci = static_cast<VkBufferCreateInfo>(buf_ci);
	VkBuffer buf = VK_NULL_HANDLE;
	VmaAllocationInfo alloc_info = {};

	const VkResult res =
		vmaCreateBuffer(ctxt.vma, &c_bci, &alloc_ci, &buf, &allocation, &alloc_info);

	if (res < VK_SUCCESS)
	{
		MXN_ERRF(
			"(VK) VMA failed to create/allocate/bind buffer: {} ({})",
			magic_enum::enum_name(res), res);
		return;
	}

	buffer = ::vk::Buffer(buf);
	memory = ::vk::DeviceMemory(alloc_info.deviceMemory);
}

vma_buffer vma_buffer::staging_preset(const context& ctxt, const ::vk::DeviceSize size)
{
	return vma_buffer(
		ctxt,
		::vk::BufferCreateInfo(
			::vk::BufferCreateFlags(), size, ::vk::BufferUsageFlagBits::eTransferSrc,
			::vk::SharingMode::eExclusive),
		VMA_ALLOC_CREATEINFO_STAGING);
}

vma_buffer vma_buffer::ubo_preset(const context& ctxt, const ::vk::DeviceSize size)
{
	return vma_buffer(
		ctxt,
		::vk::BufferCreateInfo(
			::vk::BufferCreateFlags(), size,
			::vk::BufferUsageFlagBits::eUniformBuffer |
				::vk::BufferUsageFlagBits::eTransferDst,
			::vk::SharingMode::eExclusive),
		VMA_ALLOC_CREATEINFO_GENERAL);
}

vma_buffer vma_buffer::ubo_preset(
	const context& ctxt, const ::vk::DeviceSize size,
	const std::array<uint32_t, 2> shared_queue_families)
{
	return vma_buffer(
		ctxt,
		::vk::BufferCreateInfo(
			::vk::BufferCreateFlags(), size,
			::vk::BufferUsageFlagBits::eUniformBuffer |
				::vk::BufferUsageFlagBits::eTransferDst,
			::vk::SharingMode::eConcurrent, shared_queue_families),
		VMA_ALLOC_CREATEINFO_GENERAL);
}

void vma_buffer::copy_to(
	const context& ctxt, vma_buffer& other,
	const std::initializer_list<::vk::BufferCopy> regions) const
{
	auto cmdbuf = ctxt.begin_onetime_buffer();
	cmdbuf.copyBuffer(buffer, other.buffer, regions);
	ctxt.consume_onetime_buffer(std::move(cmdbuf));
}

void vma_buffer::destroy(const context& ctxt)
{
	vmaDestroyBuffer(ctxt.vma, buffer, allocation);
}

[[nodiscard]] ::vk::DeviceSize vma_buffer::alloc_size(const VmaAllocator vma) const
{
	VmaAllocationInfo alloc_info = {};
	vmaGetAllocationInfo(vma, allocation, &alloc_info);
	return alloc_info.size;
}
