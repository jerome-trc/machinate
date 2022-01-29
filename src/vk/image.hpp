/**
 * @file vk/image.hpp
 * @brief `vma_image`, a structure wrapping a VMA-allocated image and its view.
 */

#pragma once

#include <filesystem>
#include <vulkan/vulkan.hpp>

struct VmaAllocation_T;
typedef VmaAllocation_T* VmaAllocation;

struct VmaAllocationCreateInfo;

namespace mxn::vk
{
	class context;

	/// @brief Wraps an image allocated using VMA alongside a view its memory.
	struct vma_image final
	{
		::vk::Image image;
		::vk::ImageView view;
		::vk::DeviceMemory memory;
		VmaAllocation allocation = VK_NULL_HANDLE;

		constexpr vma_image() noexcept = default;

		/// @note Output is left uninitialised in the event of an error.
		vma_image(
			const context&, const ::vk::ImageCreateInfo&, ::vk::ImageViewCreateInfo&&,
			const VmaAllocationCreateInfo&, const std::string& debug_postfix = "");

		static vma_image from_file(const context&, const std::filesystem::path&);

		vma_image(const vma_image&);
		vma_image& operator=(const vma_image&);
		vma_image(vma_image&&);
		vma_image& operator=(vma_image&&);

		explicit operator bool() const noexcept
		{
			return image && view && memory && allocation != VK_NULL_HANDLE;
		}

		bool operator!() const noexcept
		{
			return !image && !view && !memory && allocation == VK_NULL_HANDLE;
		}

		void destroy(const context&);
	};
} // namespace mxn::vk
