/**
 * @file vk/image.cpp
 * @brief `vma_image`, a structure wrapping a VMA-allocated image and its view.
 */

#include "image.hpp"

#include "../log.hpp"
#include "context.hpp"

#include <magic_enum.hpp>
#include <vk_mem_alloc.h>

using namespace mxn::vk;

vma_image::vma_image(
	const context& ctxt, const ::vk::ImageCreateInfo& img_create_info,
	::vk::ImageViewCreateInfo&& view_create_info, const VmaAllocationCreateInfo& vma_info,
	const std::string& debug_postfix)
{
	assert(!view_create_info.image);

	const auto& c_ici = static_cast<VkImageCreateInfo>(img_create_info);
	VkImage img = VK_NULL_HANDLE;
	VmaAllocationInfo alloc_info = {};
	const VkResult res =
		vmaCreateImage(ctxt.vma, &c_ici, &vma_info, &img, &allocation, &alloc_info);

	if (res < VK_SUCCESS)
	{
		MXN_ERRF(
			"(VK) VMA failed to create/allocate/bind image: {}",
			magic_enum::enum_name(res));
		return;
	}

	image = ::vk::Image(img);
	memory = ::vk::DeviceMemory(alloc_info.deviceMemory);
	view_create_info.image = image;
	view = ctxt.device.createImageView(view_create_info);

	if (!debug_postfix.empty())
	{
		ctxt.set_debug_name(image, fmt::format("MXN: Image, {}", debug_postfix));
		ctxt.set_debug_name(view, fmt::format("MXN: Image View, {}", debug_postfix));
		ctxt.set_debug_name(memory, fmt::format("MXN: Image Memory, {}", debug_postfix));
	}
}

vma_image::vma_image(const vma_image& other)
{
	image = other.image;
	view = other.view;
	memory = other.memory;
	allocation = other.allocation;
}

vma_image& vma_image::operator=(const vma_image& other)
{
	image = other.image;
	view = other.view;
	memory = other.memory;
	allocation = other.allocation;
	return *this;
}

vma_image::vma_image(vma_image&& other)
{
	image = other.image;
	view = other.view;
	memory = other.memory;
	allocation = other.allocation;
	other.image = ::vk::Image(VK_NULL_HANDLE);
	other.view = ::vk::ImageView(VK_NULL_HANDLE);
	other.memory = ::vk::DeviceMemory(VK_NULL_HANDLE);
	other.allocation = VK_NULL_HANDLE;
}

vma_image& vma_image::operator=(vma_image&& other)
{
	image = other.image;
	view = other.view;
	memory = other.memory;
	allocation = other.allocation;
	other.image = ::vk::Image(VK_NULL_HANDLE);
	other.view = ::vk::ImageView(VK_NULL_HANDLE);
	other.memory = ::vk::DeviceMemory(VK_NULL_HANDLE);
	other.allocation = VK_NULL_HANDLE;
	return *this;
}

void vma_image::destroy(const context& ctxt)
{
	vmaDestroyImage(ctxt.vma, image, allocation);
	ctxt.device.destroyImageView(view);
}
