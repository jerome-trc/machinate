/**
 * @file vulkan.hpp
 * @brief Interfaces and helpers for Vulkan functionality.
 */

#include "vulkan.hpp"

#include "console.hpp"
#include "log.hpp"
#include "string.hpp"
#include "src/defines.hpp"

#include <SDL2/SDL_vulkan.h>
#include <imgui_impl_sdl.h>
#include <imgui_impl_vulkan.h>
#include <magic_enum.hpp>
#include <set>
#include <string>
#include <vk_mem_alloc.h>

static constexpr uint32_t MXN_VK_VERSION = VK_MAKE_VERSION(
	Machinate_VERSION_MAJOR, Machinate_VERSION_MINOR, Machinate_VERSION_PATCH);

static constexpr uint32_t MIN_IMG_COUNT = 2;

static constexpr std::array DEVICE_EXTENSIONS = { VK_KHR_SWAPCHAIN_EXTENSION_NAME,
												  VK_KHR_MULTIVIEW_EXTENSION_NAME };

#ifndef NDEBUG
static constexpr std::array<const char*, 1> VALIDATION_LAYERS = {
	"VK_LAYER_KHRONOS_validation"
};
#else
static constexpr std::array<const char*, 0> VALIDATION_LAYERS = {};
#endif

static VKAPI_ATTR VkBool32 VKAPI_CALL debug_messenger_callback(
	VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT type,
	const VkDebugUtilsMessengerCallbackDataEXT* cbdata, void*)
{
	static constexpr const char* TYPE_NAMES[8] = { "",
												   "GENERAL",
												   "VALIDATION",
												   "GENERAL/VALIDATION",
												   "PERFORMANCE",
												   "GENERAL/PERFORMANCE",
												   "VALIDATION/PERFORMANCE",
												   "GENERAL/VALIDATION/PERFORMANCE" };

	switch (severity)
	{
	case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
		if (streq(cbdata->pMessageIdName, "Loader Message"))
			break;
		
		MXN_DEBUGF("(VK) {}\n\t{}", TYPE_NAMES[type], cbdata->pMessage);
		break;
	case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
		break;
		MXN_WARNF("(VK) {}\n\t{}", TYPE_NAMES[type], cbdata->pMessage);
		break;
	case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
		break;
		MXN_ERRF("(VK) {}\n\t{}", TYPE_NAMES[type], cbdata->pMessage);
		break;
	case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT: break;
	default: MXN_LOGF("(VK) {}\n\t{}", TYPE_NAMES[type], cbdata->pMessage); break;
	}

	return VK_FALSE;
}

static void imgui_debug_callback(const VkResult err)
{
	if (err < VK_SUCCESS)
	{
		MXN_ERRF("(VK/IMGUI) {}", magic_enum::enum_name(err));
		assert(false);
	}
}

mxn::vk::context::context(SDL_Window* window)
	: inst(ctor_instance(window)), surface(ctor_surface(window)), gpu(ctor_select_gpu()),
	  qfam_gfx(ctor_get_qfam_gfx()), qfam_pres(ctor_get_qfam_pres()),
	  qfam_trans(ctor_get_qfam_trans()), device(ctor_device()),
	  dispatch_loader(ctor_dispatch_loader()), 
	  debug_messenger(ctor_init_debug_messenger()),
	  vma(ctor_vma()),
	  q_gfx(device.getQueue(qfam_gfx, 0)), q_pres(device.getQueue(qfam_pres, 0)),
	  q_comp(device.getQueue(qfam_gfx, 1)),
	  cmdpool_gfx(device.createCommandPool(
		  { ::vk::CommandPoolCreateFlagBits::eResetCommandBuffer, qfam_gfx }, nullptr)),
	  cmdpool_trans(device.createCommandPool(
		  { ::vk::CommandPoolCreateFlagBits::eResetCommandBuffer, qfam_trans }, nullptr)),
	  cmdpool_comp(device.createCommandPool(
		  {
			  ::vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
			  qfam_gfx,
		  },
		  nullptr))
{
	std::tie(swapchain, imgformat, extent) = create_swapchain(window);
	std::tie(images, imgviews) = create_images_and_views();
	render_pass = create_renderpass();

	for (const auto& imgview : imgviews) framebufs.push_back(create_framebuffer(imgview));

	cmdbuf = create_commandbuffer();

	// Fullscreen viewport state ///////////////////////////////////////////////

	fullscreen_viewport = ::vk::Viewport(
		0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height),
		0.0f, 1.0f);
	fullscreen_scissor = ::vk::Rect2D({ 0, 0 }, extent);

	// Sync primitives /////////////////////////////////////////////////////////

	fence_render = device.createFence({ ::vk::FenceCreateFlagBits::eSignaled });
	sema_render = device.createSemaphore({});
	sema_present = device.createSemaphore({});

	// ImGui ///////////////////////////////////////////////////////////////////

	static constexpr std::array<::vk::DescriptorPoolSize, 10> IMGUI_DESCPOOL_SIZES = {
		::vk::DescriptorPoolSize { ::vk::DescriptorType::eSampler, 1000 },
		{ ::vk::DescriptorType::eCombinedImageSampler, 1000 },
		{ ::vk::DescriptorType::eSampledImage, 1000 },
		{
			::vk::DescriptorType::eStorageImage,
			1000,
		},
		{ ::vk::DescriptorType::eUniformTexelBuffer, 1000 },
		{ ::vk::DescriptorType::eStorageTexelBuffer, 1000 },
		{ ::vk::DescriptorType::eUniformBuffer, 1000 },
		{ ::vk::DescriptorType::eUniformBufferDynamic, 1000 },
		{ ::vk::DescriptorType::eStorageBufferDynamic, 1000 },
		{ ::vk::DescriptorType::eInputAttachment, 1000 }
	};

	const ::vk::DescriptorPoolCreateInfo descpool_imgui_ci(
		::vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
		1000 * std::size(IMGUI_DESCPOOL_SIZES), IMGUI_DESCPOOL_SIZES);

	descpool_imgui = device.createDescriptorPool(descpool_imgui_ci, nullptr);

	ImGui_ImplSDL2_InitForVulkan(window);
	ImGui_ImplVulkan_InitInfo imgui_init_info;
	imgui_init_info.Instance = inst;
	imgui_init_info.PhysicalDevice = gpu;
	imgui_init_info.Device = device;
	imgui_init_info.Queue = q_gfx;
	imgui_init_info.PipelineCache = VK_NULL_HANDLE;
	imgui_init_info.DescriptorPool = descpool_imgui;
	imgui_init_info.Allocator = nullptr;
	imgui_init_info.MinImageCount = MIN_IMG_COUNT;
	vkGetSwapchainImagesKHR(device, swapchain, &imgui_init_info.ImageCount, nullptr);
	imgui_init_info.CheckVkResultFn = imgui_debug_callback;
	imgui_init_info.Subpass = 0;
	imgui_init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
	ImGui_ImplVulkan_Init(&imgui_init_info, render_pass);

	device.resetCommandPool(cmdpool_gfx);
	::vk::CommandBuffer fontup_cmdbuf =
		device.allocateCommandBuffers(::vk::CommandBufferAllocateInfo(
			cmdpool_gfx, ::vk::CommandBufferLevel::ePrimary, 1))[0];
	fontup_cmdbuf.begin(::vk::CommandBufferBeginInfo(
		::vk::CommandBufferUsageFlagBits::eOneTimeSubmit, nullptr));
	ImGui_ImplVulkan_CreateFontsTexture(fontup_cmdbuf);
	fontup_cmdbuf.end();
	q_gfx.submit({ { {}, 0, fontup_cmdbuf, {} } }, ::vk::Fence());
	device.waitIdle();
	ImGui_ImplVulkan_DestroyFontUploadObjects();

	// Assign debug names //////////////////////////////////////////////////////

	set_debug_name(surface, "MXN: Surface");
	set_debug_name(device, "MXN: Logical Device");
	set_debug_name(descpool_imgui, "MXN: ImGui Descriptor Pool");
	set_debug_name(q_gfx, "MXN: Queue, Graphics");
	set_debug_name(q_pres, "MXN: Queue, Present");
	set_debug_name(q_comp, "MXN: Queue, Compute");
	set_debug_name(cmdpool_gfx, "MXN: Command Pool, Graphics");
	set_debug_name(cmdpool_trans, "MXN: Command Pool, Transfer");
	set_debug_name(cmdpool_comp, "MXN: Command Pool, Compute");
}

mxn::vk::context::~context()
{
	device.waitIdle();
	ImGui_ImplVulkan_Shutdown();
	destroy_swapchain();
	device.destroyDescriptorPool(descpool_imgui);
	device.destroySemaphore(sema_present);
	device.destroySemaphore(sema_render);
	device.destroyFence(fence_render);
	device.destroyCommandPool(cmdpool_comp, nullptr);
	device.destroyCommandPool(cmdpool_trans, nullptr);
	device.destroyCommandPool(cmdpool_gfx, nullptr);
	inst.destroySurfaceKHR(surface, nullptr);
	vmaDestroyAllocator(vma);
	inst.destroyDebugUtilsMessengerEXT(debug_messenger, nullptr, dispatch_loader);
	device.destroy(nullptr);
	inst.destroy(nullptr);
}

// Public interface: core //////////////////////////////////////////////////////

void mxn::vk::context::start_render() noexcept
{
	ImGui::Render();

	const auto res_fencewait = device.waitForFences(
		1, &fence_render, true, std::numeric_limits<uint64_t>::max());
	assert(
		res_fencewait == ::vk::Result::eSuccess ||
		res_fencewait == ::vk::Result::eTimeout);

	const auto res_fencereset = device.resetFences(1, &fence_render);
	assert(res_fencereset != ::vk::Result::eErrorOutOfDeviceMemory);

	cmdbuf.reset(::vk::CommandBufferResetFlags());

	const auto res_acq = device.acquireNextImageKHR(
		swapchain, std::numeric_limits<uint64_t>::max(), sema_present, ::vk::Fence(),
		&img_idx);
	assert(
		res_acq == ::vk::Result::eSuccess || res_acq == ::vk::Result::eTimeout ||
		res_acq == ::vk::Result::eNotReady || res_acq == ::vk::Result::eSuboptimalKHR);

	cmdbuf.begin(::vk::CommandBufferBeginInfo(
		::vk::CommandBufferUsageFlagBits::eOneTimeSubmit, nullptr));

	static constexpr std::array CLEAR_COLOUR = { 0.0f, 0.0f, 0.0f, 1.0f };
	static ::vk::ClearValue CLEAR_VAL = (::vk::ClearColorValue(CLEAR_COLOUR));

	cmdbuf.beginRenderPass(
		::vk::RenderPassBeginInfo(
			render_pass, framebufs[img_idx], ::vk::Rect2D({ 0, 0 }, extent), 1,
			&CLEAR_VAL),
		::vk::SubpassContents::eInline);
}

bool mxn::vk::context::finish_render() noexcept
{
	bool ret = true;

	ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmdbuf);

	cmdbuf.endRenderPass();
	cmdbuf.end();

	static constexpr ::vk::PipelineStageFlags WAIT_STAGE =
		::vk::PipelineStageFlagBits::eColorAttachmentOutput;

	const ::vk::SubmitInfo submit_info(sema_present, WAIT_STAGE, cmdbuf, sema_render);
	q_gfx.submit(submit_info, fence_render);
	try
	{
		const ::vk::Result res =
			q_gfx.presentKHR(::vk::PresentInfoKHR(sema_render, swapchain, img_idx));
		if (res == ::vk::Result::eSuboptimalKHR) ret = false;
	}
	catch (::vk::OutOfDateKHRError& err)
	{
		ret = false;
	}

	frame++;

	return ret;
}

void mxn::vk::context::rebuild_swapchain(SDL_Window* const window)
{
	MXN_DEBUG("(VK) Rebuilding swapchain...");

	device.waitIdle();
	destroy_swapchain();
	device.waitIdle();

	std::tie(swapchain, imgformat, extent) = create_swapchain(window);
	std::tie(images, imgviews) = create_images_and_views();
	render_pass = create_renderpass();

	for (const auto& imgview : imgviews) framebufs.push_back(create_framebuffer(imgview));

	cmdbuf = create_commandbuffer();

	auto formats = gpu.getSurfaceFormatsKHR(surface);
	auto presmodes = gpu.getSurfacePresentModesKHR(surface);

	fullscreen_viewport = ::vk::Viewport(
		0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height),
		0.0f, 1.0f);
	fullscreen_scissor = ::vk::Rect2D({ 0, 0 }, extent);
}

// Public interface: console commands //////////////////////////////////////////

void mxn::vk::context::vkdiag(const std::vector<std::string> args) const
{
	if (args.size() <= 1)
	{
		MXN_LOG("Use `help vkdiag` for options.");
		return;
	}

	if (args[1] == "ext")
	{
		MXN_LOG(
			"All supported instance extensions:");
		auto exts_inst = ::vk::enumerateInstanceExtensionProperties();

		for (const auto& ext : exts_inst)
			MXN_LOGF("\t{} (vers. {})", ext.extensionName, ext.specVersion);

		MXN_LOG("All supported device extensions:");
		const auto exts_gpu = gpu.enumerateDeviceExtensionProperties();

		for (const auto& ext : exts_gpu)
			MXN_LOGF("\t{} (vers. {})", ext.extensionName, ext.specVersion);

		return;
	}

	if (args[1] == "gpu")
	{
		MXN_LOG("Physical device information:");

		const auto props = gpu.getProperties();
		const auto feats = gpu.getFeatures();

		MXN_LOGF("Name: {}", props.deviceName);
		MXN_LOGF(
			"\tDriver version: {}", VK_VERSION_MAJOR(props.driverVersion),
			VK_VERSION_MAJOR(props.driverVersion),
			VK_VERSION_PATCH(props.driverVersion));
		MXN_LOGF(
			"\tAPI version: {}.{}.{}", VK_VERSION_MAJOR(props.apiVersion),
			VK_VERSION_MAJOR(props.apiVersion), VK_VERSION_PATCH(props.apiVersion));

		MXN_LOGF(
			"\tSupports tessellation shaders: {}",
			feats.tessellationShader == VK_TRUE ? "yes" : "no");
		MXN_LOGF(
			"\tSupports dual-source blending: {}",
			feats.dualSrcBlend == VK_TRUE ? "yes" : "no");
		MXN_LOGF(
			"\tSupports logic operations: {}", feats.logicOp == VK_TRUE ? "yes" : "no");
		MXN_LOGF(
			"\tSupports anisotropic filtering: {}",
			feats.samplerAnisotropy == VK_TRUE ? "yes" : "no");

		return;
	}

	if (args[1] == "queue")
	{
		const auto qfams = gpu.getQueueFamilyProperties();

		MXN_LOG("All device queue families:");

		for (size_t i = 0; i < qfams.size(); i++)
		{
			MXN_LOGF("- Queue Family {}", i);
			MXN_LOGF(
				"Flags: {}", magic_enum::flags::enum_name(static_cast<VkQueueFlagBits>(
								 static_cast<VkQueueFlags>(qfams[i].queueFlags))));
			MXN_LOGF("Queue count: {}", qfams[i].queueCount);
		}

		return;
	}
}

// Private /////////////////////////////////////////////////////////////////////

std::tuple<::vk::SwapchainKHR, ::vk::Format, ::vk::Extent2D> mxn::vk::context::
	create_swapchain(SDL_Window* const window) const
{
	std::tuple<::vk::SwapchainKHR, ::vk::Format, ::vk::Extent2D> ret = {};

	const auto formats = gpu.getSurfaceFormatsKHR(surface);
	const auto presmodes = gpu.getSurfacePresentModesKHR(surface);
	const auto caps = gpu.getSurfaceCapabilitiesKHR(surface);

	::vk::SurfaceFormatKHR srf_format;

	for (const auto& format : formats)
	{
		if (format.format == ::vk::Format::eB8G8R8A8Srgb &&
			format.colorSpace == ::vk::ColorSpaceKHR::eSrgbNonlinear)
		{
			srf_format = format;
			std::get<1>(ret) = srf_format.format;
			break;
		}
	}

	auto presmode = ::vk::PresentModeKHR::eFifo;
	const auto psm = std::find_if(
		presmodes.begin(), presmodes.end(), [](const ::vk::PresentModeKHR& pm) -> bool {
			return pm == ::vk::PresentModeKHR::eMailbox;
		});

	if (*psm != ::vk::PresentModeKHR::eMailbox)
		MXN_LOG("(VK) Mailbox present mode unavailable; falling back to FIFO.");
	else
		presmode = *psm;

	int window_size_x = -1, window_size_y = -1;
	SDL_Vulkan_GetDrawableSize(window, &window_size_x, &window_size_y);

	if (caps.currentExtent.width != UINT32_MAX)
		std::get<2>(ret) = caps.currentExtent;
	else
	{
		std::get<2>(ret) =
			::vk::Extent2D { std::clamp(
								 static_cast<uint32_t>(window_size_x),
								 caps.minImageExtent.width, caps.maxImageExtent.width),
							 std::clamp(
								 static_cast<uint32_t>(window_size_y),
								 caps.minImageExtent.height,
								 caps.maxImageExtent.height) };
	}

	uint32_t min_img_c = caps.minImageCount + 1;
	if (caps.maxImageCount > 0 && min_img_c > caps.maxImageCount)
	{ min_img_c = caps.maxImageCount; }

	const uint32_t qfams[2] = { qfam_gfx, qfam_pres };
	std::vector<uint32_t> qfam_vec;

	if (qfam_gfx != qfam_pres) qfam_vec = { qfam_gfx, qfam_pres };

	const auto ci = ::vk::SwapchainCreateInfoKHR(
		{}, surface, min_img_c, srf_format.format, srf_format.colorSpace,
		std::get<2>(ret), 1,
		::vk::ImageUsageFlags(::vk::ImageUsageFlagBits::eColorAttachment),
		qfams[0] != qfams[1] ? ::vk::SharingMode::eConcurrent
							 : ::vk::SharingMode::eExclusive,
		qfam_vec, caps.currentTransform, ::vk::CompositeAlphaFlagBitsKHR::eOpaque,
		presmode, true);
	std::get<0>(ret) = device.createSwapchainKHR(ci, nullptr);
	return ret;
}

std::tuple<std::vector<::vk::Image>, std::vector<::vk::ImageView>> mxn::vk::context::
	create_images_and_views() const
{
	std::tuple<std::vector<::vk::Image>, std::vector<::vk::ImageView>> ret = {};

	auto& images = std::get<0>(ret);
	images = device.getSwapchainImagesKHR(swapchain);

	auto& imgviews = std::get<1>(ret);

	for (const auto& image : images)
	{
		const ::vk::ImageViewCreateInfo ci = {
			{},
			image,
			::vk::ImageViewType::e2D,
			imgformat,
			::vk::ComponentMapping {},
			::vk::ImageSubresourceRange { ::vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 }
		};
		imgviews.push_back(device.createImageView(ci, nullptr));
	}

	return ret;
}

::vk::RenderPass mxn::vk::context::create_renderpass() const
{
	const std::array attachments { ::vk::AttachmentDescription(
		::vk::AttachmentDescriptionFlags(), imgformat, ::vk::SampleCountFlagBits::e1,
		::vk::AttachmentLoadOp::eClear, ::vk::AttachmentStoreOp::eStore,
		::vk::AttachmentLoadOp::eDontCare, ::vk::AttachmentStoreOp::eDontCare,
		::vk::ImageLayout::eUndefined, ::vk::ImageLayout::ePresentSrcKHR) };

	const std::array colour_attachments = { ::vk::AttachmentReference(
		0, ::vk::ImageLayout::eColorAttachmentOptimal) };

	const std::array subpasses { ::vk::SubpassDescription(
		::vk::SubpassDescriptionFlags(), ::vk::PipelineBindPoint::eGraphics, {},
		colour_attachments, {}, {}, {}) };

	const std::array dependencies { ::vk::SubpassDependency(
		VK_SUBPASS_EXTERNAL, 0, ::vk::PipelineStageFlagBits::eColorAttachmentOutput,
		::vk::PipelineStageFlagBits::eColorAttachmentOutput, ::vk::AccessFlags(),
		::vk::AccessFlagBits::eColorAttachmentWrite, ::vk::DependencyFlags()) };

	const ::vk::RenderPassCreateInfo ci(
		::vk::RenderPassCreateFlags(), attachments, subpasses, dependencies);

	return device.createRenderPass(ci, nullptr);
}

::vk::Framebuffer mxn::vk::context::create_framebuffer(
	const ::vk::ImageView& imgview) const
{
	const std::array attachments = { imgview };

	const ::vk::FramebufferCreateInfo ci(
		::vk::FramebufferCreateFlags(), render_pass, attachments, extent.width,
		extent.height, 1);

	return device.createFramebuffer(ci, nullptr);
}

::vk::CommandBuffer mxn::vk::context::create_commandbuffer() const
{
	const ::vk::CommandBufferAllocateInfo alloci(
		cmdpool_gfx, ::vk::CommandBufferLevel::ePrimary, 1);
	return device.allocateCommandBuffers(alloci)[0];
}

void mxn::vk::context::destroy_swapchain()
{
	for (auto& framebuf : framebufs) device.destroyFramebuffer(framebuf, nullptr);

	framebufs.clear();

	device.freeCommandBuffers(cmdpool_gfx, cmdbuf);
	device.destroyRenderPass(render_pass, nullptr);

	for (auto& imgview : imgviews) device.destroyImageView(imgview, nullptr);

	imgviews.clear();
}

// Constructor helpers /////////////////////////////////////////////////////////

::vk::Instance mxn::vk::context::ctor_instance(SDL_Window* const window) const
{
	assert(window != nullptr);

	const ::vk::ApplicationInfo app_info(
		"Machinate", MXN_VK_VERSION, "Machinate", MXN_VK_VERSION, VK_API_VERSION_1_2);

	std::vector<const char*> reqexts;
	uint32_t ext_c = 0;

	if (SDL_Vulkan_GetInstanceExtensions(window, &ext_c, nullptr) == SDL_FALSE)
	{
		throw std::runtime_error("(VK) Failed to count extensions required to "
								 "create an SDL2 surface.");
	}

	reqexts.resize(ext_c);

	if (SDL_Vulkan_GetInstanceExtensions(window, &ext_c, reqexts.data()) == SDL_FALSE)
	{
		throw std::runtime_error("(VK) Failed to acquire names of all extensions "
								 "required to create an SDL2 surface.");
	}

	reqexts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
	reqexts.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);

	const ::vk::InstanceCreateInfo inst_ci(
		::vk::InstanceCreateFlags(), &app_info, VALIDATION_LAYERS, reqexts);

	return ::vk::createInstance(inst_ci, nullptr);
}

::vk::SurfaceKHR mxn::vk::context::ctor_surface(SDL_Window* const window) const
{
	assert(window != nullptr);

	VkSurfaceKHR srf = VK_NULL_HANDLE;

	if (SDL_Vulkan_CreateSurface(window, inst, &srf) == SDL_FALSE)
		throw std::runtime_error("(VK) Failed to create SDL2 window surface.");

	return ::vk::SurfaceKHR(srf);
}

[[nodiscard]] static bool suitable_gfx_queue_family(
	const ::vk::QueueFamilyProperties& props)
{
	return props.queueCount >= 2 &&
		   static_cast<bool>(props.queueFlags & ::vk::QueueFlagBits::eGraphics) &&
		   static_cast<bool>(props.queueFlags & ::vk::QueueFlagBits::eCompute);
}

::vk::PhysicalDevice mxn::vk::context::ctor_select_gpu() const
{
	auto gpus = inst.enumeratePhysicalDevices();

	if (gpus.size() < 1)
	{
		throw std::runtime_error(
			"None of this system's graphics devices support Vulkan.");
	}

	for (size_t i = 0; i < gpus.size(); i++)
	{
		const auto props = gpus[i].getProperties();
		const auto feats = gpus[i].getFeatures();

		if (!feats.tessellationShader && !feats.logicOp && !feats.multiViewport &&
			!feats.samplerAnisotropy)
			continue;

		auto qfprops = gpus[i].getQueueFamilyProperties();

		// Check for extension support

		const auto exts = gpus[i].enumerateDeviceExtensionProperties();
		std::set<std::string> reqexts;

		for (size_t i = 0; i < std::size(DEVICE_EXTENSIONS); i++)
			reqexts.emplace(DEVICE_EXTENSIONS[i]);

		for (const auto& ext : exts) reqexts.erase(ext.extensionName);

		if (!reqexts.empty()) continue;

		// Check surface support

		const auto formats = gpus[i].getSurfaceFormatsKHR(surface);
		const auto presmodes = gpus[i].getSurfacePresentModesKHR(surface);

		if (formats.size() < 1 || presmodes.size() < 1) continue;

		const auto qfam_props = gpus[i].getQueueFamilyProperties();

		uint32_t qf_gfx, qf_pres, qf_trans;
		qf_gfx = qf_pres = qf_trans = INVALID_QUEUE_FAMILY;

		for (size_t j = 0; j < qfam_props.size(); j++)
		{
			if (qfam_props[j].queueCount < 2) continue;

			const uint32_t j_u32 = static_cast<uint32_t>(j);

			if (suitable_gfx_queue_family(qfam_props[i]) &&
				qf_gfx == INVALID_QUEUE_FAMILY)
			{ qf_gfx = j_u32; }

			if (qfam_props[j].queueFlags & ::vk::QueueFlagBits::eTransfer &&
				j_u32 != qf_gfx && j_u32 != qf_pres)
			{ qf_trans = j_u32; }

			if (gpus[i].getSurfaceSupportKHR(j_u32, surface) &&
				qf_pres == INVALID_QUEUE_FAMILY)
			{ qf_pres = j_u32; }
		}

		if (qf_gfx == INVALID_QUEUE_FAMILY || qf_pres == INVALID_QUEUE_FAMILY ||
			qf_trans == INVALID_QUEUE_FAMILY)
			continue;

		MXN_LOGF(
			"(VK) Physical device:\n"
			"\t{} ({})\n"
			"\tDriver version: {}.{}.{}\n"
			"\tAPI version: {}.{}.{}",
			props.deviceName,
			props.deviceType == ::vk::PhysicalDeviceType::eDiscreteGpu
				? "dedicated"
				: "integrated/other",
			VK_VERSION_MAJOR(props.driverVersion), VK_VERSION_MINOR(props.driverVersion),
			VK_VERSION_PATCH(props.driverVersion), VK_VERSION_MAJOR(props.apiVersion),
			VK_VERSION_MINOR(props.apiVersion), VK_VERSION_PATCH(props.apiVersion));

		return gpus[i];
	}

	throw std::runtime_error("(VK) Failed to find a suitable GPU.");
}

uint32_t mxn::vk::context::ctor_get_qfam_gfx() const
{
	assert(gpu); // i.e. != VK_NULL_HANDLE

	const auto qfam_props = gpu.getQueueFamilyProperties();

	for (size_t i = 0; i < qfam_props.size(); i++)
		if (suitable_gfx_queue_family(qfam_props[i])) return static_cast<uint32_t>(i);

	return INVALID_QUEUE_FAMILY;
}

uint32_t mxn::vk::context::ctor_get_qfam_pres() const
{
	assert(gpu); // i.e. != VK_NULL_HANDLE

	const auto qfam_props = gpu.getQueueFamilyProperties();

	for (size_t i = 0; i < qfam_props.size(); i++)
	{
		const auto i_u32 = static_cast<uint32_t>(i);

		if (gpu.getSurfaceSupportKHR(i_u32, surface)) return i_u32;
	}

	return INVALID_QUEUE_FAMILY;
}

uint32_t mxn::vk::context::ctor_get_qfam_trans() const
{
	assert(gpu); // i.e. != VK_NULL_HANDLE

	const auto qfam_props = gpu.getQueueFamilyProperties();

	for (size_t i = 0; i < qfam_props.size(); i++)
	{
		const auto i_u32 = static_cast<uint32_t>(i);

		if (qfam_props[i].queueFlags & ::vk::QueueFlagBits::eTransfer &&
			i_u32 != qfam_gfx && i_u32 != qfam_pres)
			return i_u32;
	}

	return INVALID_QUEUE_FAMILY;
}

::vk::Device mxn::vk::context::ctor_device() const
{
	static constexpr float QUEUE_PRIORITY[1] = { 1.0f };

	std::vector<::vk::DeviceQueueCreateInfo> devq_ci;

	if (qfam_gfx == qfam_pres)
	{
		devq_ci = { ::vk::DeviceQueueCreateInfo({}, qfam_gfx, 2, QUEUE_PRIORITY),
					::vk::DeviceQueueCreateInfo({}, qfam_trans, 1, QUEUE_PRIORITY) };
	}
	else
	{
		devq_ci = { ::vk::DeviceQueueCreateInfo({}, qfam_gfx, 2, QUEUE_PRIORITY),
					::vk::DeviceQueueCreateInfo({}, qfam_pres, 1, QUEUE_PRIORITY),
					::vk::DeviceQueueCreateInfo({}, qfam_trans, 1, QUEUE_PRIORITY) };
	}

	const auto feats = gpu.getFeatures();

	const ::vk::DeviceCreateInfo dev_ci(
		::vk::DeviceCreateFlags(), devq_ci, VALIDATION_LAYERS, DEVICE_EXTENSIONS, &feats);

	return gpu.createDevice(dev_ci, nullptr);
}

::vk::DispatchLoaderDynamic mxn::vk::context::ctor_dispatch_loader() const
{
	::vk::DispatchLoaderDynamic dld(vkGetInstanceProcAddr);
	dld.init(inst, device);
	return dld;
}

VmaAllocator mxn::vk::context::ctor_vma() const
{
	VmaAllocator ret = nullptr;

	const VmaAllocatorCreateInfo ci = { .flags = 0,
										.physicalDevice = gpu,
										.device = device,
										.preferredLargeHeapBlockSize = 0,
										.pAllocationCallbacks = nullptr,
										.pDeviceMemoryCallbacks = nullptr,
										.frameInUseCount = 0,
										.pHeapSizeLimit = nullptr,
										.pVulkanFunctions = nullptr,
										.pRecordSettings = nullptr,
										.instance = inst,
										.vulkanApiVersion = VK_API_VERSION_1_2,
										.pTypeExternalMemoryHandleTypes = nullptr };

	vmaCreateAllocator(&ci, &ret);
	return ret;
}

::vk::DebugUtilsMessengerEXT mxn::vk::context::ctor_init_debug_messenger() const
{
	const ::vk::DebugUtilsMessageSeverityFlagsEXT sev =
		::vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo |
		::vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
		::vk::DebugUtilsMessageSeverityFlagBitsEXT::eError |
		::vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose;

	const ::vk::DebugUtilsMessageTypeFlagsEXT types =
		::vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
		::vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance |
		::vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation;

	return inst.createDebugUtilsMessengerEXT(
		{ ::vk::DebugUtilsMessengerCreateFlagsEXT(), sev, types,
		  debug_messenger_callback },
		nullptr, dispatch_loader);
}
