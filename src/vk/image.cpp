/**
 * @file vk/image.cpp
 * @brief `vma_image`, a structure wrapping a VMA-allocated image and its view.
 */

#include "image.hpp"

#include "../file.hpp"
#include "../log.hpp"
#include "context.hpp"

#include <SOIL2/SOIL2.h>
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
	}
}

vma_image vma_image::from_file(const context& ctxt, const std::filesystem::path& path)
{
	if (path.empty()) return {};

	const auto mem = vfs_read(path);
	assert(!mem.empty());

	int w = -1, h = -1, chans = -1;
	unsigned char* img = SOIL_load_image_from_memory(
		mem.data(), mem.size(), &w, &h, &chans, SOIL_LOAD_RGBA);

	assert(img != nullptr);

	const ::vk::ImageCreateInfo img_ci(
		::vk::ImageCreateFlags(), ::vk::ImageType::e2D, ::vk::Format::eR8G8B8A8Unorm,
		::vk::Extent3D(w, h, 1), 1, 1, ::vk::SampleCountFlagBits::e1,
		::vk::ImageTiling::eLinear,
		::vk::ImageUsageFlagBits::eTransferDst | ::vk::ImageUsageFlagBits::eSampled,
		::vk::SharingMode::eExclusive, // One queue family only
		{},
		::vk::ImageLayout::ePreinitialized
	);

	::vk::ImageViewCreateInfo view_ci(
		::vk::ImageViewCreateFlags(), {}, ::vk::ImageViewType::e2D,
		::vk::Format::eR8G8B8A8Unorm,
		{}, // Default component mapping; all swizzles identity
		::vk::ImageSubresourceRange(::vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1));

	::vk::Image staging_image = {};
	VmaAllocation staging_alloc = VK_NULL_HANDLE;

	{
		::vk::ImageCreateInfo staging_ci = img_ci;
		staging_ci.usage = ::vk::ImageUsageFlagBits::eTransferSrc;
		const auto& c_ici = static_cast<VkImageCreateInfo>(staging_ci);
		VkImage img = VK_NULL_HANDLE;
		VmaAllocationInfo alloc_info = {};
		const VkResult res = vmaCreateImage(
			ctxt.vma, &c_ici, &VMA_ALLOC_CREATEINFO_STAGING, &img, &staging_alloc,
			&alloc_info);

		if (res < VK_SUCCESS)
		{
			throw std::runtime_error(fmt::format(
				"(VK) VMA failed to create/allocate/bind staging image: {}",
				magic_enum::enum_name(res)));
		}

		staging_image = ::vk::Image(img);
	}

	auto ret = vma_image(
		ctxt, img_ci, std::move(view_ci), VMA_ALLOC_CREATEINFO_GENERAL,
		fmt::format("MXN: Image, {}", path.string()));

	auto cmdbuf = ctxt.begin_onetime_buffer();
	ctxt.record_image_layout_change(
		cmdbuf, staging_image, ::vk::ImageLayout::ePreinitialized,
		::vk::ImageLayout::eTransferSrcOptimal);
	ctxt.record_image_layout_change(
		cmdbuf, ret.image, ::vk::ImageLayout::ePreinitialized,
		::vk::ImageLayout::eTransferDstOptimal);

	const ::vk::ImageSubresourceLayers srl(::vk::ImageAspectFlagBits::eColor, 0, 0, 1);

	cmdbuf.copyImage(
		staging_image, ::vk::ImageLayout::eTransferSrcOptimal, ret.image,
		::vk::ImageLayout::eTransferDstOptimal,
		::vk::ImageCopy(srl, {}, srl, {}, ::vk::Extent3D(w, h, 1)));

	ctxt.record_image_layout_change(
		cmdbuf, ret.image, ::vk::ImageLayout::eTransferDstOptimal,
		::vk::ImageLayout::eShaderReadOnlyOptimal);
	ctxt.consume_onetime_buffer(std::move(cmdbuf));

	vmaDestroyImage(ctxt.vma, staging_image, staging_alloc);
	SOIL_free_image_data(img);
	return ret;
}

// Copiers, movers, teardown ///////////////////////////////////////////////////

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
