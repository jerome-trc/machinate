/**
 * @file vk/buffer.hpp
 * @brief `vma_buffer`, a structure wrapping a VMA-allocated buffer.
 */

#pragma once

#include <vulkan/vulkan.hpp>

struct VmaAllocation_T;
typedef VmaAllocation_T* VmaAllocation;

struct VmaAllocationCreateInfo;

struct VmaAllocator_T;
typedef VmaAllocator_T* VmaAllocator;

namespace mxn::vk
{
	class context;

	/// @brief Wraps a buffer allocated using VMA alongside its memory.
	struct vma_buffer final
	{
		::vk::Buffer buffer;
		::vk::DeviceMemory memory;
		VmaAllocation allocation = VK_NULL_HANDLE;

		[[nodiscard]] static vma_buffer staging_preset(const context&, ::vk::DeviceSize);

		[[nodiscard]] static vma_buffer ubo_preset(const context&, ::vk::DeviceSize);
		[[nodiscard]] static vma_buffer ubo_preset(
			const context&, ::vk::DeviceSize,
			std::array<uint32_t, 2> shared_queue_familes);

		constexpr vma_buffer() noexcept = default;

		vma_buffer(
			const context&, const ::vk::BufferCreateInfo&,
			const VmaAllocationCreateInfo&);

		void copy_to(
			const context&, vma_buffer&, std::initializer_list<::vk::BufferCopy>) const;

		void destroy(const context&);

		/// @note Returns the size allocated by VMA, which may be larger than the
		/// size of the resource within. The whole allocation can be mapped but
		/// operations on the contained resource should be kept to that resource's size.
		[[nodiscard]] ::vk::DeviceSize alloc_size(const VmaAllocator vma) const;
	};
} // namespace mxn::vk
