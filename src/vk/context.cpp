/**
 * @file vk/context.cpp
 * @brief The class serving as the foundation for all other Vulkan operations.
 */

#include "context.hpp"

#include "../file.hpp"
#include "../log.hpp"
#include "../string.hpp"
#include "model.hpp"
#include "src/defines.hpp"

#include <SDL2/SDL_vulkan.h>
#include <Tracy.hpp>
#include <imgui_impl_sdl.h>
#include <imgui_impl_vulkan.h>
#include <magic_enum.hpp>
#include <set>

namespace mxn::vk
{
	struct pushconst final
	{
		glm::uvec2 viewport_size = {}, tile_nums = {};
		int debugview_index = 0;
	};
} // namespace mxn::vk

using namespace mxn::vk;

static constexpr uint32_t MXN_VK_VERSION = VK_MAKE_VERSION(
	Machinate_VERSION_MAJOR, Machinate_VERSION_MINOR, Machinate_VERSION_PATCH);

static constexpr uint32_t MIN_IMG_COUNT = 2;

static constexpr std::array DEVICE_EXTENSIONS = { VK_KHR_SWAPCHAIN_EXTENSION_NAME,
												  VK_KHR_MULTIVIEW_EXTENSION_NAME };

#ifndef NDEBUG
static constexpr std::array VALIDATION_LAYERS = { "VK_LAYER_KHRONOS_validation",
												  "VK_LAYER_LUNARG_standard_validation" };
#else
static constexpr std::array<const char*, 0> VALIDATION_LAYERS = {};
#endif

static constexpr unsigned int MAX_POINTLIGHTS_PER_TILE = 1023u, TILE_SIZE = 16u;
static constexpr size_t TILE_BUFFERSIZE =
	sizeof(uint32_t) + sizeof(std::array<uint32_t, MAX_POINTLIGHTS_PER_TILE>);

static_assert(TILE_BUFFERSIZE == (sizeof(int) * (MAX_POINTLIGHTS_PER_TILE + 1)));

static constexpr std::array CLEAR_COLOUR = { 0.0f, 0.0f, 0.0f, 1.0f };
static ::vk::ClearValue CLEAR_VAL = (::vk::ClearColorValue(CLEAR_COLOUR));

static constexpr std::array<::vk::DescriptorImageInfo, 0> NO_DESCIMG_INFO = {};
static constexpr std::array<::vk::DescriptorBufferInfo, 0> NO_DESCBUF_INFO = {};
static constexpr std::array<::vk::BufferView, 0> NO_BUFVIEWS = {};

static VKAPI_ATTR VkBool32 VKAPI_CALL debug_messenger_callback(
	const VkDebugUtilsMessageSeverityFlagBitsEXT severity,
	const VkDebugUtilsMessageTypeFlagsEXT type,
	const VkDebugUtilsMessengerCallbackDataEXT* const cbdata, void* const)
{
	static constexpr const char* TYPE_NAMES[8] = { "",
												   "GENERAL",
												   "VALIDATION",
												   "GENERAL/VALIDATION",
												   "PERFORMANCE",
												   "GENERAL/PERFORMANCE",
												   "VALIDATION/PERFORMANCE",
												   "GENERAL/VALIDATION/PERFORMANCE" };

	std::string msg = fmt::format("(VK) {}", TYPE_NAMES[type]);

	for (uint32_t i = 0; i < cbdata->objectCount; i++)
	{
		if (cbdata->pObjects[i].pObjectName == nullptr ||
			cbdata->pObjects[i].objectHandle == 0x0)
			continue;

		msg += fmt::format(
			"\n\t- {} 0x{:x}[{}]", magic_enum::enum_name(cbdata->pObjects[i].objectType),
			cbdata->pObjects[i].objectHandle, cbdata->pObjects[i].pObjectName);
	}

	msg += fmt::format("\n\t{}", cbdata->pMessage);

	switch (severity)
	{
	case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
		if (streq(cbdata->pMessageIdName, "Loader Message")) break;
		MXN_DEBUG(msg);
		break;
	case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT: MXN_WARN(msg); break;
	case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT: MXN_ERR(msg); break;
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

// Context, public interface ///////////////////////////////////////////////////

context::context(SDL_Window* const window)
	: inst(ctor_instance(window)), surface(ctor_surface(window)), gpu(ctor_select_gpu()),
	  qfam_gfx(ctor_get_qfam_gfx()), qfam_pres(ctor_get_qfam_pres()),
	  qfam_trans(ctor_get_qfam_trans()), device(ctor_device()),
	  dispatch_loader(ctor_dispatch_loader()),
	  debug_messenger(ctor_init_debug_messenger()), vma(ctor_vma()),
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
	const auto dsls = create_descset_layouts();
	dsl_obj = dsls[0];
	dsl_cam = dsls[1];
	dsl_lightcull = dsls[2];
	dsl_inter = dsls[3];
	dsl_mat = dsls[4];

	ubo_obj = ubo<glm::mat4>(*this, "Objects");
	ubo_cam = ubo<camera>(*this, "Camera");
	ubo_lights = ubo<std::vector<point_light>, POINTLIGHT_BUFSIZE>(
		*this, qfam_gfx, 0u, "Point Lights");

	texture_sampler = device.createSampler(
		::vk::SamplerCreateInfo(
			::vk::SamplerCreateFlags(), ::vk::Filter::eLinear, ::vk::Filter::eLinear,
			::vk::SamplerMipmapMode::eLinear, ::vk::SamplerAddressMode::eRepeat,
			::vk::SamplerAddressMode::eRepeat, ::vk::SamplerAddressMode::eRepeat, 0.0f,
			true, 16.0f, false, ::vk::CompareOp::eAlways, 0.0f, 0.0f,
			::vk::BorderColor::eIntOpaqueBlack, false),
		nullptr);

	descpool = create_descpool();

	const auto descsets = create_descsets();
	descset_obj = descsets[0];
	descset_cam = descsets[1];
	descset_lightcull = descsets[2];
	descset_inter = descsets[3];
	update_descset_obj();
	update_descset_cam();

	const ::vk::DescriptorBufferInfo uboinfo_cam(
		ubo_cam.get_buffer(), 0, ubo_cam.data_size);
	const std::array descwrites = { ::vk::WriteDescriptorSet(
		descset_cam, 0, 0, 1, ::vk::DescriptorType::eUniformBuffer, nullptr, &uboinfo_cam,
		nullptr) };
	const std::array<::vk::CopyDescriptorSet, 0> desccopies = {};
	device.updateDescriptorSets(descwrites, desccopies);

	create_swapchain(window);

	// Sync primitives /////////////////////////////////////////////////////////

	fence_render = device.createFence({ ::vk::FenceCreateFlagBits::eSignaled }, nullptr);
	sema_renderdone = device.createSemaphore({}, nullptr);
	sema_imgavail = device.createSemaphore({}, nullptr);
	sema_lightculldone = device.createSemaphore({}, nullptr);
	sema_prepassdone = device.createSemaphore({}, nullptr);
	sema_imgui = device.createSemaphore({}, nullptr);

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

	::vk::CommandBuffer fontup_cmdbuf =
		device.allocateCommandBuffers(::vk::CommandBufferAllocateInfo(
			cmdpool_gfx, ::vk::CommandBufferLevel::ePrimary, 1))[0];
	fontup_cmdbuf.begin(::vk::CommandBufferBeginInfo(
		::vk::CommandBufferUsageFlagBits::eOneTimeSubmit, nullptr));
	ImGui_ImplVulkan_CreateFontsTexture(fontup_cmdbuf);
	fontup_cmdbuf.end();
	q_gfx.submit({ { {}, 0, fontup_cmdbuf, {} } }, ::vk::Fence());
	q_gfx.waitIdle();
	device.freeCommandBuffers(cmdpool_gfx, fontup_cmdbuf);
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
	set_debug_name(fence_render, "MXN: Fence, Render");
	set_debug_name(sema_renderdone, "MXN: Semaphore, Render");
	set_debug_name(sema_imgavail, "MXN: Semaphore, Image Acquiry");
	set_debug_name(sema_lightculldone, "MXN: Semaphore, Light Cull");
	set_debug_name(sema_prepassdone, "MXN: Semaphore, Depth Pre-pass");
	set_debug_name(sema_imgui, "MXN: Semaphore, ImGui");
}

context::~context()
{
	device.waitIdle();
	ImGui_ImplVulkan_Shutdown();

	device.destroySampler(texture_sampler);
	destroy_swapchain();

	ubo_obj.destroy(*this);
	ubo_cam.destroy(*this);
	ubo_lights.destroy(*this);

	device.destroyDescriptorSetLayout(dsl_mat, nullptr);
	device.destroyDescriptorSetLayout(dsl_inter, nullptr);
	device.destroyDescriptorSetLayout(dsl_lightcull, nullptr);
	device.destroyDescriptorSetLayout(dsl_cam, nullptr);
	device.destroyDescriptorSetLayout(dsl_obj, nullptr);

	device.freeDescriptorSets(
		descpool,
		std::array { descset_obj, descset_cam, descset_lightcull, descset_inter });
	device.destroyDescriptorPool(descpool);

	device.destroyDescriptorPool(descpool_imgui);

	device.destroySemaphore(sema_renderdone);
	device.destroySemaphore(sema_imgavail);
	device.destroySemaphore(sema_lightculldone);
	device.destroySemaphore(sema_prepassdone);
	device.destroySemaphore(sema_imgui);
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

bool context::start_render() noexcept
{
	ImGui::Render();

	[[maybe_unused]] const auto res_fencewait = device.waitForFences(
		1, &fence_render, true, std::numeric_limits<uint64_t>::max());
	assert(
		res_fencewait == ::vk::Result::eSuccess ||
		res_fencewait == ::vk::Result::eTimeout);

	[[maybe_unused]] const auto res_fencereset = device.resetFences(1, &fence_render);
	assert(res_fencereset != ::vk::Result::eErrorOutOfDeviceMemory);

	const auto res_acq = device.acquireNextImageKHR(
		swapchain, std::numeric_limits<uint64_t>::max(), sema_imgavail, {});

	img_idx = res_acq.value;

	return res_acq.result != ::vk::Result::eErrorOutOfDateKHR;
}

void context::start_render_record() noexcept
{
	// Begin recording rendering command buffer ////////////////////////////////

	{
		cmdbufs_gfx[img_idx].reset(::vk::CommandBufferResetFlags());

		cmdbufs_gfx[img_idx].begin(::vk::CommandBufferBeginInfo(
			::vk::CommandBufferUsageFlagBits::eOneTimeSubmit, nullptr));

		const ::vk::RenderPassBeginInfo pass_info(
			render_pass, framebufs[img_idx], ::vk::Rect2D({}, extent), CLEAR_VAL);

		cmdbufs_gfx[img_idx].beginRenderPass(pass_info, ::vk::SubpassContents::eInline);

		cmdbufs_gfx[img_idx].pushConstants<pushconst>(
			ppl_render.layout, ::vk::ShaderStageFlagBits::eFragment, 0,
			std::array { pushconst { .viewport_size = { extent.width, extent.height },
									 .tile_nums = tile_count,
									 .debugview_index = 0 } });

		cmdbufs_gfx[img_idx].bindPipeline(
			::vk::PipelineBindPoint::eGraphics, ppl_render.handle);

		cmdbufs_gfx[img_idx].bindDescriptorSets(
			::vk::PipelineBindPoint::eGraphics, ppl_render.layout, 0,
			std::array { descset_obj, descset_cam, descset_lightcull, descset_inter },
			std::array<uint32_t, 0>());
	}

	// Begin recording depth pre-pass command buffer ///////////////////////////

	{
		cmdbuf_prepass.reset(::vk::CommandBufferResetFlags());

		cmdbuf_prepass.begin(::vk::CommandBufferBeginInfo(
			::vk::CommandBufferUsageFlagBits::eOneTimeSubmit, nullptr));

		static const ::vk::ClearValue
		DEPTH_CLEAR_VAL(::vk::ClearDepthStencilValue(1.0f, 0.0f));

		const ::vk::RenderPassBeginInfo pass_info(
			depth_prepass, prepass_framebuffer, ::vk::Rect2D({}, extent),
			DEPTH_CLEAR_VAL);

		cmdbuf_prepass.beginRenderPass(pass_info, ::vk::SubpassContents::eInline);

		cmdbuf_prepass.bindPipeline(::vk::PipelineBindPoint::eGraphics,
		ppl_depth.handle);

		cmdbuf_prepass.bindDescriptorSets(
			::vk::PipelineBindPoint::eGraphics, ppl_depth.layout, 0,
			{ descset_obj, descset_cam }, {});
	}
}

void context::record_draw(const model& model) noexcept
{
	for (const auto& mesh : model.meshes)
	{
		// Record rendering commands ///////////////////////////////////////////

		cmdbufs_gfx[img_idx].bindVertexBuffers(0, mesh.verts.buffer, { 0 });
		cmdbufs_gfx[img_idx].bindIndexBuffer(mesh.indices.buffer, 0, ::vk::IndexType::eUint32);
		cmdbufs_gfx[img_idx].drawIndexed(mesh.index_count, 1, 0, 0, 0);

		// Record depth-prepass commands ///////////////////////////////////////

		cmdbuf_prepass.bindVertexBuffers(0, mesh.verts.buffer, { 0 });
		cmdbuf_prepass.bindIndexBuffer(mesh.indices.buffer, 0, ::vk::IndexType::eUint32);
		cmdbuf_prepass.drawIndexed(mesh.index_count, 1, 0, 0, 0);
	}
}

void context::bind_material(const material& mat) noexcept
{
	cmdbufs_gfx[img_idx].bindDescriptorSets(
		::vk::PipelineBindPoint::eGraphics, ppl_render.layout, 4, mat.descset, {});
}

void context::end_render_record() noexcept
{
	cmdbufs_gfx[img_idx].endRenderPass();
	cmdbufs_gfx[img_idx].end();
	cmdbuf_prepass.endRenderPass();
	cmdbuf_prepass.end();
}

const ::vk::Semaphore& context::submit_prepass(
	const ::vk::ArrayProxyNoTemporaries<const ::vk::Semaphore>& wait_semas) noexcept
{
	assert(wait_semas.empty());

	const ::vk::SubmitInfo prepass_info(wait_semas, {}, cmdbuf_prepass, sema_prepassdone);
	[[maybe_unused]] const auto res_prepass = q_gfx.submit(1, &prepass_info, nullptr);

	assert(res_prepass == ::vk::Result::eSuccess);

	return sema_prepassdone;
}

const ::vk::Semaphore& context::compute_lightcull(
	const ::vk::ArrayProxyNoTemporaries<const ::vk::Semaphore>& wait_semas) noexcept
{
	static constexpr std::array<::vk::PipelineStageFlags, 1> WAITSTAGES_LIGHTCULL = {
		::vk::PipelineStageFlagBits::eComputeShader
	};

	const ::vk::SubmitInfo lightcull_info(
		wait_semas, WAITSTAGES_LIGHTCULL, cmdbuf_lightcull, sema_lightculldone);
	[[maybe_unused]] const auto res_lightcull =
		q_comp.submit(1, &lightcull_info, nullptr);

	assert(res_lightcull == ::vk::Result::eSuccess);

	return sema_lightculldone;
}

const ::vk::Semaphore& context::submit_geometry(
	const ::vk::ArrayProxyNoTemporaries<const ::vk::Semaphore>& wait_semas) noexcept
{
	static constexpr std::array<::vk::PipelineStageFlags, 2> WAITSTAGES_RENDER = {
		::vk::PipelineStageFlagBits::eColorAttachmentOutput,
		::vk::PipelineStageFlagBits::eFragmentShader
	};

	std::vector ws = { sema_imgavail };

	for (const auto& sema : wait_semas) ws.emplace_back(sema);

	assert(ws.size() == WAITSTAGES_RENDER.size());

	const ::vk::SubmitInfo render_info(
		ws, WAITSTAGES_RENDER, cmdbufs_gfx[img_idx], sema_renderdone);
	[[maybe_unused]] const auto res_render = q_gfx.submit(1, &render_info, {});

	assert(res_render == ::vk::Result::eSuccess);

	return sema_renderdone;
}

const ::vk::Semaphore& context::render_imgui(
	const ::vk::ArrayProxyNoTemporaries<const ::vk::Semaphore>& wait_semas) noexcept
{
	static constexpr std::array<::vk::PipelineStageFlags, 1> WAITSTAGES_IMGUI = {
		::vk::PipelineStageFlagBits::eTopOfPipe
	};

	assert(wait_semas.size() == WAITSTAGES_IMGUI.size());

	cmdbuf_imgui.reset(::vk::CommandBufferResetFlags());
	cmdbuf_imgui.begin(::vk::CommandBufferBeginInfo(
		::vk::CommandBufferUsageFlagBits::eOneTimeSubmit, nullptr));
	cmdbuf_imgui.beginRenderPass(
		::vk::RenderPassBeginInfo(
			imgui_pass, framebufs[img_idx], ::vk::Rect2D({}, extent), CLEAR_VAL),
		::vk::SubpassContents::eInline);
	ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmdbuf_imgui);
	cmdbuf_imgui.endRenderPass();
	cmdbuf_imgui.end();

	const ::vk::SubmitInfo imgui_info(
		wait_semas, WAITSTAGES_IMGUI, cmdbuf_imgui, sema_imgui);
	[[maybe_unused]] const auto res_imgui = q_gfx.submit(1, &imgui_info, fence_render);

	assert(res_imgui == ::vk::Result::eSuccess);

	return sema_imgui;
}

bool context::present_frame(const ::vk::Semaphore& wait_sema)
{
	bool ret = true;

	try
	{
		const ::vk::Result res =
			q_pres.presentKHR(::vk::PresentInfoKHR(wait_sema, swapchain, img_idx));

		if (res == ::vk::Result::eSuboptimalKHR) ret = false;
	}
	catch (::vk::OutOfDateKHRError& err)
	{
		ret = false;
	}

	frame++;
	return ret;
}

void context::rebuild_swapchain(SDL_Window* const window)
{
	MXN_DEBUG("(VK) Rebuilding swapchain...");
	device.waitIdle();
	destroy_swapchain();
	device.waitIdle();
	create_swapchain(window);
}

::vk::ShaderModule context::create_shader(
	const std::filesystem::path& path, const std::string& debug_name) const
{
	std::vector<unsigned char> code = vfs_read(path);

	::vk::ShaderModule ret = device.createShaderModule(::vk::ShaderModuleCreateInfo(
		::vk::ShaderModuleCreateFlags(), code.size(),
		reinterpret_cast<const uint32_t*>(code.data())));

	if (!debug_name.empty()) set_debug_name(ret, debug_name);

	return ret;
}

material context::create_material(
	const std::filesystem::path& albedo, const std::filesystem::path& normal,
	const std::string& debug_name
) const
{
	const ::vk::DescriptorSetAllocateInfo alloc_info(descpool, dsl_mat);

	auto ret = mxn::vk::material {
		.info { *this, fmt::format("MXN: UBO, Material Info, {}", debug_name) },
		.descset = device.allocateDescriptorSets(alloc_info)[0],
		.albedo = vma_image::from_file(*this, albedo),
		.normal = vma_image::from_file(*this, normal)
	};

	ret.info.data = {
		.has_albedo = ret.albedo ? 1 : 0,
		.has_normal = ret.normal ? 1 : 0
	};

	std::vector<::vk::WriteDescriptorSet> descwrites = {};

	const ::vk::DescriptorBufferInfo dbi(ret.info.get_buffer(), 0, ret.info.data_size);

	descwrites.emplace_back(
		ret.descset, 0, 0, ::vk::DescriptorType::eUniformBuffer,
		NO_DESCIMG_INFO, dbi, NO_BUFVIEWS);

	const ::vk::DescriptorImageInfo dii_albedo(texture_sampler, ret.albedo.view,
		::vk::ImageLayout::eShaderReadOnlyOptimal);

	if (ret.info.data.has_albedo)
	{
		descwrites.emplace_back(
			ret.descset, 1, 0, ::vk::DescriptorType::eCombinedImageSampler,
			dii_albedo, NO_DESCBUF_INFO, NO_BUFVIEWS);
	}

	const ::vk::DescriptorImageInfo dii_norm(texture_sampler, ret.normal.view,
		::vk::ImageLayout::eShaderReadOnlyOptimal);

	if (ret.info.data.has_normal)
	{
		descwrites.emplace_back(
			ret.descset, 2, 0, ::vk::DescriptorType::eCombinedImageSampler,
			dii_norm, NO_DESCBUF_INFO, NO_BUFVIEWS);
	}

	device.updateDescriptorSets(descwrites, std::array<::vk::CopyDescriptorSet, 0>());

	if (!debug_name.empty())
		set_debug_name(ret.descset, fmt::format("MXN: Desc. Set, {}", debug_name));

	return ret;
}

::vk::CommandBuffer context::begin_onetime_buffer() const
{
	const ::vk::CommandBufferAllocateInfo alloc_info(
		cmdpool_gfx, ::vk::CommandBufferLevel::ePrimary, 1);
	const auto ret = device.allocateCommandBuffers(alloc_info)[0];
	const ::vk::CommandBufferBeginInfo begin_info(
		::vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
	ret.begin(begin_info);
	return ret;
}

void context::consume_onetime_buffer(::vk::CommandBuffer&& cmdbuf) const
{
	cmdbuf.end();
	const ::vk::SubmitInfo submit_info({}, {}, cmdbuf, {});
	q_gfx.submit(submit_info);
	q_gfx.waitIdle();
	device.freeCommandBuffers(cmdpool_gfx, cmdbuf);
}

void context::record_image_layout_change(
	const ::vk::CommandBuffer& cmdbuf, const ::vk::Image& image,
	const ::vk::ImageLayout from, const ::vk::ImageLayout to) const
{
	::vk::AccessFlags src, dst;

	if (from == ::vk::ImageLayout::ePreinitialized &&
		to == ::vk::ImageLayout::eTransferSrcOptimal)
	{
		src = ::vk::AccessFlagBits::eHostWrite;
		dst = ::vk::AccessFlagBits::eTransferRead;
	}
	else if (
		from == ::vk::ImageLayout::ePreinitialized &&
		to == ::vk::ImageLayout::eTransferDstOptimal)
	{
		src = ::vk::AccessFlagBits::eHostWrite;
		dst = ::vk::AccessFlagBits::eTransferWrite;
	}
	else if (
		from == ::vk::ImageLayout::eUndefined &&
		to == ::vk::ImageLayout::eDepthStencilAttachmentOptimal)
	{
		src = ::vk::AccessFlags();
		dst = ::vk::AccessFlagBits::eDepthStencilAttachmentRead |
			  ::vk::AccessFlagBits::eDepthStencilAttachmentWrite;
	}
	else if (
		from == ::vk::ImageLayout::eUndefined &&
		to == ::vk::ImageLayout::eShaderReadOnlyOptimal)
	{
		src = ::vk::AccessFlags();
		dst = ::vk::AccessFlagBits::eShaderRead;
	}
	else if (
		from == ::vk::ImageLayout::eDepthStencilAttachmentOptimal &&
		to == ::vk::ImageLayout::eShaderReadOnlyOptimal)
	{
		src = ::vk::AccessFlagBits::eDepthStencilAttachmentRead |
			  ::vk::AccessFlagBits::eDepthStencilAttachmentWrite;
		dst = ::vk::AccessFlagBits::eShaderRead;
	}
	else if (
		from == ::vk::ImageLayout::eShaderReadOnlyOptimal &&
		to == ::vk::ImageLayout::eDepthStencilAttachmentOptimal)
	{
		src = ::vk::AccessFlagBits::eShaderRead;
		dst = ::vk::AccessFlagBits::eDepthStencilAttachmentRead |
			  ::vk::AccessFlagBits::eDepthStencilAttachmentWrite;
	}
	else if
	(
		from == ::vk::ImageLayout::eTransferDstOptimal &&
		to == ::vk::ImageLayout::eShaderReadOnlyOptimal)
	{
		src = ::vk::AccessFlagBits::eTransferWrite;
		dst = ::vk::AccessFlagBits::eShaderRead;
	}
	else
	{
		assert(false && "Unsupported image layout from/to combination.");
	}

	::vk::ImageAspectFlags aspect_mask;

	if (to == ::vk::ImageLayout::eDepthStencilAttachmentOptimal ||
		from == ::vk::ImageLayout::eDepthStencilAttachmentOptimal)
		aspect_mask = ::vk::ImageAspectFlagBits::eDepth;
	else
		aspect_mask = ::vk::ImageAspectFlagBits::eColor;

	const ::vk::ImageMemoryBarrier barrier(
		src, dst, from, to, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, image,
		::vk::ImageSubresourceRange(aspect_mask, 0, 1, 0, 1));

	cmdbuf.pipelineBarrier(
		// Happens before barrier
		::vk::PipelineStageFlagBits::eTopOfPipe |
			::vk::PipelineStageFlagBits::eEarlyFragmentTests |
			::vk::PipelineStageFlagBits::eHost |
			::vk::PipelineStageFlagBits::eVertexShader |
			::vk::PipelineStageFlagBits::eFragmentShader |
			::vk::PipelineStageFlagBits::eTransfer,
		// Waits for barrier
		::vk::PipelineStageFlagBits::eTopOfPipe |
			::vk::PipelineStageFlagBits::eEarlyFragmentTests |
			::vk::PipelineStageFlagBits::eHost |
			::vk::PipelineStageFlagBits::eVertexShader |
			::vk::PipelineStageFlagBits::eFragmentShader |
			::vk::PipelineStageFlagBits::eTransfer,
		::vk::DependencyFlags(), {}, {}, barrier);
}

void context::vkdiag(const std::vector<std::string>& args) const
{
	if (args.size() <= 1)
	{
		MXN_LOG("Use `help vkdiag` for options.");
		return;
	}

	if (args[1] == "ext")
	{
		MXN_LOG("All supported instance extensions:");
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
			VK_VERSION_MAJOR(props.driverVersion), VK_VERSION_PATCH(props.driverVersion));
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

// Context, private functionality //////////////////////////////////////////////

std::tuple<::vk::SwapchainKHR, ::vk::Format, ::vk::Extent2D> context::
	create_swapchain_core(SDL_Window* const window) const
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

std::tuple<std::vector<::vk::Image>, std::vector<::vk::ImageView>> context::
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

std::pair<::vk::RenderPass, ::vk::RenderPass> context::create_passes() const
{
	// Depth pre-pass //////////////////////////////////////////////////////////

	const ::vk::AttachmentDescription attach_depth {
		::vk::AttachmentDescriptionFlags(),
		depth_format(),
		::vk::SampleCountFlagBits::e1,
		::vk::AttachmentLoadOp::eClear,
		::vk::AttachmentStoreOp::eStore,
		::vk::AttachmentLoadOp::eDontCare,
		::vk::AttachmentStoreOp::eDontCare,
		::vk::ImageLayout::eDepthStencilAttachmentOptimal,
		::vk::ImageLayout::eDepthStencilReadOnlyOptimal
	};

	const ::vk::AttachmentReference attachref_depth {
		0, ::vk::ImageLayout::eDepthStencilAttachmentOptimal
	};

	const ::vk::SubpassDescription subpass_depth(
		::vk::SubpassDescriptionFlags(), ::vk::PipelineBindPoint::eGraphics, {}, {}, {},
		&attachref_depth);

	const ::vk::SubpassDependency depends_depth {
		VK_SUBPASS_EXTERNAL,
		0,
		::vk::PipelineStageFlagBits::eBottomOfPipe,
		::vk::PipelineStageFlagBits::eColorAttachmentOutput,
		::vk::AccessFlagBits::eMemoryRead,
		::vk::AccessFlagBits::eColorAttachmentRead |
			::vk::AccessFlagBits::eColorAttachmentWrite
	};

	const ::vk::RenderPassCreateInfo ci_depth { ::vk::RenderPassCreateFlags(),
												attach_depth, subpass_depth,
												depends_depth };

	// Render pass /////////////////////////////////////////////////////////////

	const ::vk::AttachmentDescription attach_render_main {
		::vk::AttachmentDescriptionFlags(), imgformat,
		::vk::SampleCountFlagBits::e1,		::vk::AttachmentLoadOp::eClear,
		::vk::AttachmentStoreOp::eStore,	::vk::AttachmentLoadOp::eDontCare,
		::vk::AttachmentStoreOp::eDontCare, ::vk::ImageLayout::eUndefined,
		::vk::ImageLayout::ePresentSrcKHR
	};

	const ::vk::AttachmentDescription attach_render_depth {
		::vk::AttachmentDescriptionFlags(),
		depth_format(),
		::vk::SampleCountFlagBits::e1,
		::vk::AttachmentLoadOp::eLoad,
		::vk::AttachmentStoreOp::eDontCare,
		::vk::AttachmentLoadOp::eDontCare,
		::vk::AttachmentStoreOp::eDontCare,
		::vk::ImageLayout::eDepthStencilReadOnlyOptimal,
		::vk::ImageLayout::eDepthStencilAttachmentOptimal
	};

	const ::vk::AttachmentReference attachref_render_colour {
		0, ::vk::ImageLayout::eColorAttachmentOptimal
	};

	const ::vk::AttachmentReference attachref_render_depth {
		1, ::vk::ImageLayout::eDepthStencilReadOnlyOptimal
	};

	const ::vk::SubpassDescription subpass_render { ::vk::SubpassDescriptionFlags(),
													::vk::PipelineBindPoint::eGraphics,
													{},
													attachref_render_colour,
													{},
													&attachref_render_depth };

	const ::vk::SubpassDependency depends_render {
		VK_SUBPASS_EXTERNAL,
		0,
		::vk::PipelineStageFlagBits::eBottomOfPipe,
		::vk::PipelineStageFlagBits::eColorAttachmentOutput,
		::vk::AccessFlagBits::eMemoryRead,
		::vk::AccessFlagBits::eColorAttachmentRead |
			::vk::AccessFlagBits::eColorAttachmentWrite
	};

	const std::array attachments_render { attach_render_main, attach_render_depth };

	const ::vk::RenderPassCreateInfo ci_render { ::vk::RenderPassCreateFlags(),
												 attachments_render, subpass_render,
												 depends_render };

	std::pair<::vk::RenderPass, ::vk::RenderPass> ret = {
		device.createRenderPass(ci_depth), device.createRenderPass(ci_render)
	};

	set_debug_name(ret.first, "MXN: Depth Pre-pass");
	set_debug_name(ret.second, "MXN: Render Pass");
	return ret;
}

::vk::RenderPass context::create_imgui_renderpass() const
{
	const ::vk::AttachmentDescription attach_render_main {
		::vk::AttachmentDescriptionFlags(), imgformat,
		::vk::SampleCountFlagBits::e1,		::vk::AttachmentLoadOp::eClear,
		::vk::AttachmentStoreOp::eStore,	::vk::AttachmentLoadOp::eDontCare,
		::vk::AttachmentStoreOp::eDontCare, ::vk::ImageLayout::eUndefined,
		::vk::ImageLayout::ePresentSrcKHR
	};

	const ::vk::AttachmentDescription attach_render_depth {
		::vk::AttachmentDescriptionFlags(),
		depth_format(),
		::vk::SampleCountFlagBits::e1,
		::vk::AttachmentLoadOp::eLoad,
		::vk::AttachmentStoreOp::eDontCare,
		::vk::AttachmentLoadOp::eDontCare,
		::vk::AttachmentStoreOp::eDontCare,
		::vk::ImageLayout::eDepthStencilAttachmentOptimal,
		::vk::ImageLayout::eDepthStencilAttachmentOptimal
	};

	const ::vk::AttachmentReference attachref_render_colour {
		0, ::vk::ImageLayout::eColorAttachmentOptimal
	};

	const ::vk::AttachmentReference attachref_render_depth {
		1, ::vk::ImageLayout::eDepthStencilAttachmentOptimal
	};

	const ::vk::SubpassDescription subpass_render { ::vk::SubpassDescriptionFlags(),
													::vk::PipelineBindPoint::eGraphics,
													{},
													attachref_render_colour,
													{},
													&attachref_render_depth };

	const ::vk::SubpassDependency depends_render {
		VK_SUBPASS_EXTERNAL,
		0,
		::vk::PipelineStageFlagBits::eBottomOfPipe,
		::vk::PipelineStageFlagBits::eColorAttachmentOutput,
		::vk::AccessFlagBits::eMemoryRead,
		::vk::AccessFlagBits::eColorAttachmentRead |
			::vk::AccessFlagBits::eColorAttachmentWrite
	};

	const std::array attachments_render { attach_render_main, attach_render_depth };

	const ::vk::RenderPassCreateInfo ci { ::vk::RenderPassCreateFlags(),
										  attachments_render, subpass_render,
										  depends_render };

	auto ret = device.createRenderPass(ci, nullptr);
	set_debug_name(ret, "MXN: Render Pass, ImGui");
	return ret;
}

::vk::Framebuffer context::create_framebuffer(const ::vk::ImageView& imgview) const
{
	const std::array attachments = { imgview, depth_image.view };

	const ::vk::FramebufferCreateInfo ci(
		::vk::FramebufferCreateFlags(), render_pass, attachments, extent.width,
		extent.height, 1);

	return device.createFramebuffer(ci, nullptr);
}

std::array<::vk::DescriptorSetLayout, 5> context::create_descset_layouts() const
{
	std::array<::vk::DescriptorSetLayout, 5> ret = {};

	const ::vk::DescriptorSetLayoutBinding bind_obj(
		0, ::vk::DescriptorType::eUniformBuffer, 1,
		::vk::ShaderStageFlagBits::eVertex | ::vk::ShaderStageFlagBits::eFragment);

	const ::vk::DescriptorSetLayoutBinding bind_cam(
		0, ::vk::DescriptorType::eUniformBuffer, 1,
		::vk::ShaderStageFlagBits::eVertex | ::vk::ShaderStageFlagBits::eFragment |
			::vk::ShaderStageFlagBits::eCompute);

	const std::array binds_lightcull = {
		// Light culling results
		::vk::DescriptorSetLayoutBinding(
			0, ::vk::DescriptorType::eStorageBuffer, 1,
			::vk::ShaderStageFlagBits::eFragment | ::vk::ShaderStageFlagBits::eCompute),
		// Point light uniform buffer
		::vk::DescriptorSetLayoutBinding(
			1, ::vk::DescriptorType::eUniformBuffer, 1,
			::vk::ShaderStageFlagBits::eFragment | ::vk::ShaderStageFlagBits::eCompute)
	};

	const ::vk::DescriptorSetLayoutBinding bind_inter(
		0, ::vk::DescriptorType::eCombinedImageSampler, 1,
		::vk::ShaderStageFlagBits::eCompute | ::vk::ShaderStageFlagBits::eFragment);

	const std::array binds_mat = { // Uniform
								   ::vk::DescriptorSetLayoutBinding(
									   0, ::vk::DescriptorType::eUniformBuffer, 1,
									   ::vk::ShaderStageFlagBits::eFragment),
								   // Albedo mapping
								   ::vk::DescriptorSetLayoutBinding(
									   1, ::vk::DescriptorType::eCombinedImageSampler, 1,
									   ::vk::ShaderStageFlagBits::eFragment),
								   // Normal mapping
								   ::vk::DescriptorSetLayoutBinding(
									   2, ::vk::DescriptorType::eCombinedImageSampler, 1,
									   ::vk::ShaderStageFlagBits::eFragment)
	};

	ret[0] = device.createDescriptorSetLayout(::vk::DescriptorSetLayoutCreateInfo(
		::vk::DescriptorSetLayoutCreateFlags(), bind_obj));
	ret[1] = device.createDescriptorSetLayout(::vk::DescriptorSetLayoutCreateInfo(
		::vk::DescriptorSetLayoutCreateFlags(), bind_cam));
	ret[2] = device.createDescriptorSetLayout(::vk::DescriptorSetLayoutCreateInfo(
		::vk::DescriptorSetLayoutCreateFlags(), binds_lightcull));
	ret[3] = device.createDescriptorSetLayout(::vk::DescriptorSetLayoutCreateInfo(
		::vk::DescriptorSetLayoutCreateFlags(), bind_inter));
	ret[4] = device.createDescriptorSetLayout(::vk::DescriptorSetLayoutCreateInfo(
		::vk::DescriptorSetLayoutCreateFlags(), binds_mat));

	set_debug_name(ret[0], "MXN: Desc. Set Layout, Object");
	set_debug_name(ret[1], "MXN: Desc. Set Layout, Camera");
	set_debug_name(ret[2], "MXN: Desc. Set Layout, Light Culling");
	set_debug_name(ret[3], "MXN: Desc. Set Layout, Intermediate");
	set_debug_name(ret[4], "MXN: Desc. Set Layout, Material");

	return ret;
}

std::pair<pipeline, pipeline> context::create_graphics_pipelines() const
{
	::vk::Pipeline ppl_d = {}, ppl_r = {};
	::vk::PipelineLayout lo_d = {}, lo_r = {};

	const ::vk::ShaderModule sm_depth = create_shader("shaders/depth.vert.spv"),
							 sm_render_v = create_shader("shaders/fwdplus.vert.spv"),
							 sm_render_f = create_shader("shaders/fwdplus.frag.spv");

	// Shared state ////////////////////////////////////////////////////////////

	const ::vk::PipelineInputAssemblyStateCreateInfo inasm(
		::vk::PipelineInputAssemblyStateCreateFlags(),
		::vk::PrimitiveTopology::eTriangleList, false);

	const ::vk::Viewport viewp(
		0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height),
		0.0f, 1.0f);
	const ::vk::Rect2D scissor({ 0, 0 }, extent);

	const ::vk::PipelineViewportStateCreateInfo viewpstate(
		::vk::PipelineViewportStateCreateFlags(), viewp, scissor);

	const ::vk::PipelineRasterizationStateCreateInfo raster(
		::vk::PipelineRasterizationStateCreateFlags(), false, false,
		::vk::PolygonMode::eFill, ::vk::CullModeFlagBits::eBack,
		::vk::FrontFace::eCounterClockwise, // Inverted Y during proj. matrix
		false, 0.0f, 0.0f, 0.0f,
		1.0f); // Requires `wideLines` feature enabled when > 1.0

	const ::vk::PipelineDepthStencilStateCreateInfo depthstencil(
		::vk::PipelineDepthStencilStateCreateFlags(), true,
		false, // Not true because depth pre-pass
		::vk::CompareOp::eLessOrEqual, // Not `eLess` because depth pre-pass
		false, false);

	// No multisampling for now
	const ::vk::PipelineMultisampleStateCreateInfo multisampling(
		::vk::PipelineMultisampleStateCreateFlags(), ::vk::SampleCountFlagBits::e1, false,
		1.0f, nullptr, false, false);

	// Depth pre-pass //////////////////////////////////////////////////////////

	{
		const ::vk::VertexInputBindingDescription vertbind(
			0, sizeof(vertex::pos), ::vk::VertexInputRate::eVertex);

		const std::array vertattrs = { ::vk::VertexInputAttributeDescription(
			0, 0, ::vk::Format::eR32G32B32Sfloat, offsetof(vertex, pos)) };

		const ::vk::PipelineVertexInputStateCreateInfo vertinput(
			::vk::PipelineVertexInputStateCreateFlags(), vertbind, vertattrs);

		::vk::PipelineDepthStencilStateCreateInfo depthstencil_prepass(depthstencil);
		depthstencil_prepass.depthCompareOp = ::vk::CompareOp::eLess;
		depthstencil_prepass.depthWriteEnable = true;

		::vk::PipelineShaderStageCreateInfo stage(
			::vk::PipelineShaderStageCreateFlags(), ::vk::ShaderStageFlagBits::eVertex,
			sm_depth, "main");

		const std::array dsls = { dsl_obj, dsl_cam };

		const ::vk::PipelineLayoutCreateInfo layout_ci(
			::vk::PipelineLayoutCreateFlags(), dsls, {});

		lo_d = device.createPipelineLayout(layout_ci);

		const ::vk::GraphicsPipelineCreateInfo ppl_ci(
			::vk::PipelineCreateFlagBits::eAllowDerivatives, stage, &vertinput, &inasm,
			nullptr, &viewpstate, &raster, &multisampling, &depthstencil_prepass, nullptr,
			nullptr, lo_d, depth_prepass, 0, ::vk::Pipeline(), -1);

		const auto res = device.createGraphicsPipeline(::vk::PipelineCache(), ppl_ci);

		if (res.result != ::vk::Result::eSuccess)
		{
			throw std::runtime_error(fmt::format(
				"(VK) Depth pre-pass pipeline creation failed: {}",
				magic_enum::enum_name(res.result)));
		}

		ppl_d = res.value;
	}

	// Render //////////////////////////////////////////////////////////////////

	{
		const ::vk::VertexInputBindingDescription vertbind(
			0, sizeof(vertex), ::vk::VertexInputRate::eVertex);

		const std::array vertattrs = {
			::vk::VertexInputAttributeDescription(
				0, 0, ::vk::Format::eR32G32B32Sfloat, offsetof(vertex, pos)),
			::vk::VertexInputAttributeDescription(
				1, 0, ::vk::Format::eR32G32B32Sfloat, offsetof(vertex, colour)),
			::vk::VertexInputAttributeDescription(
				2, 0, ::vk::Format::eR32G32Sfloat, offsetof(vertex, uv)),
			::vk::VertexInputAttributeDescription(
				3, 0, ::vk::Format::eR32G32B32Sfloat, offsetof(vertex, normal))
		};

		const ::vk::PipelineVertexInputStateCreateInfo vertinput(
			::vk::PipelineVertexInputStateCreateFlags(), vertbind, vertattrs);

		const std::array stages = {
			::vk::PipelineShaderStageCreateInfo(
				::vk::PipelineShaderStageCreateFlags(),
				::vk::ShaderStageFlagBits::eVertex, sm_render_v, "main"),
			::vk::PipelineShaderStageCreateInfo(
				::vk::PipelineShaderStageCreateFlags(),
				::vk::ShaderStageFlagBits::eFragment, sm_render_f, "main")
		};

		const ::vk::PipelineColorBlendAttachmentState cba(
			true, ::vk::BlendFactor::eSrcAlpha, ::vk::BlendFactor::eOneMinusSrcAlpha,
			::vk::BlendOp::eAdd, ::vk::BlendFactor::eOne, ::vk::BlendFactor::eZero,
			::vk::BlendOp::eAdd,
			::vk::ColorComponentFlagBits::eR | ::vk::ColorComponentFlagBits::eG |
				::vk::ColorComponentFlagBits::eB | ::vk::ColorComponentFlagBits::eA);

		const ::vk::PipelineColorBlendStateCreateInfo cbs(
			::vk::PipelineColorBlendStateCreateFlags(), false, ::vk::LogicOp::eCopy, cba,
			{ 0.0f, 0.0f, 0.0f, 0.0f });

		const std::array<::vk::DynamicState, 0> dynstates = {};

		// Parameters which can be freely changed without pipeline rebuild
		const ::vk::PipelineDynamicStateCreateInfo dynstate(
			::vk::PipelineDynamicStateCreateFlags(), dynstates);

		const ::vk::PushConstantRange pcr(
			::vk::ShaderStageFlagBits::eFragment, 0, sizeof(pushconst));

		const std::array dsls = { dsl_obj, dsl_cam, dsl_lightcull, dsl_inter, dsl_mat };

		const ::vk::PipelineLayoutCreateInfo layout_ci(
			::vk::PipelineLayoutCreateFlags(), dsls, pcr);

		lo_r = device.createPipelineLayout(layout_ci);

		const ::vk::GraphicsPipelineCreateInfo ppl_ci(
			::vk::PipelineCreateFlagBits::eAllowDerivatives, stages, &vertinput, &inasm,
			nullptr, &viewpstate, &raster, &multisampling, &depthstencil, &cbs, &dynstate,
			lo_r, render_pass, 0, ::vk::Pipeline(), -1);

		const auto res = device.createGraphicsPipeline(::vk::PipelineCache(), ppl_ci);

		if (res.result != ::vk::Result::eSuccess)
		{
			throw std::runtime_error(fmt::format(
				"(VK) Render pipeline creation failed: {}",
				magic_enum::enum_name(res.result)));
		}

		ppl_r = res.value;
	}

	const std::pair<pipeline, pipeline> ret = {
		pipeline(ppl_d, lo_d, { sm_depth }),
		pipeline(ppl_r, lo_r, { sm_render_v, sm_render_f })
	};

	set_debug_name(ret.first.handle, "MXN: Pipeline, Depth Pre-pass");
	set_debug_name(ret.first.layout, "MXN: Pipeline Layout, Depth Pre-pass");
	set_debug_name(ret.second.handle, "MXN: Pipeline, Render");
	set_debug_name(ret.second.layout, "MXN: Pipeline Layout, Render");

	return ret;
}

pipeline context::create_compute_pipeline() const
{
	const auto shader = create_shader("/shaders/lightcull.comp.spv");

	const ::vk::PipelineShaderStageCreateInfo stage(
		::vk::PipelineShaderStageCreateFlags(), ::vk::ShaderStageFlagBits::eCompute,
		shader, "main");

	const ::vk::PushConstantRange pcr(
		::vk::ShaderStageFlagBits::eCompute, 0, static_cast<uint32_t>(sizeof(pushconst)));

	const std::array dsls { dsl_lightcull, dsl_cam, dsl_inter };

	const ::vk::PipelineLayout layout = device.createPipelineLayout(
		::vk::PipelineLayoutCreateInfo(::vk::PipelineLayoutCreateFlags(), dsls, pcr));

	const auto res = device.createComputePipeline(
		::vk::PipelineCache(),
		::vk::ComputePipelineCreateInfo(
			::vk::PipelineCreateFlags(), stage, layout, VK_NULL_HANDLE, -1));

	if (res.result == ::vk::Result::eSuccess)
	{
		const pipeline ret(res.value, layout, { shader });
		set_debug_name(ret.handle, "MXN: Pipeline, Light Culling Compute");
		set_debug_name(ret.layout, "MXN: Pipeline Layout, Light Culling Compute");
		return ret;
	}
	else
	{
		throw std::runtime_error(fmt::format(
			"(VK) Light culling compute pipeline creation failed: {}",
			magic_enum::enum_name(res.result)));
	}
}

vma_image context::create_depth_image() const
{
	const vma_image ret(
		*this,
		::vk::ImageCreateInfo(
			::vk::ImageCreateFlags(), ::vk::ImageType::e2D, depth_format(),
			::vk::Extent3D(extent.width, extent.height, 1), 1, 1,
			::vk::SampleCountFlagBits::e1, ::vk::ImageTiling::eOptimal,
			::vk::ImageUsageFlagBits::eDepthStencilAttachment |
				::vk::ImageUsageFlagBits::eSampled,
			::vk::SharingMode::eExclusive, {}, ::vk::ImageLayout::ePreinitialized),
		::vk::ImageViewCreateInfo(
			::vk::ImageViewCreateFlags(), {}, ::vk::ImageViewType::e2D, depth_format(),
			::vk::ComponentMapping(),
			::vk::ImageSubresourceRange(::vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1)),
		VMA_ALLOC_CREATEINFO_GENERAL, "MXN: Image, Depth");

	auto cmdbuf = begin_onetime_buffer();
	record_image_layout_change(
		cmdbuf, ret.image, ::vk::ImageLayout::eUndefined,
		::vk::ImageLayout::eDepthStencilAttachmentOptimal);
	consume_onetime_buffer(std::move(cmdbuf));

	return ret;
}

void context::create_swapchain(SDL_Window* const window)
{
	std::tie(swapchain, imgformat, extent) = create_swapchain_core(window);
	std::tie(images, imgviews) = create_images_and_views();
	std::tie(depth_prepass, render_pass) = create_passes();
	imgui_pass = create_imgui_renderpass();
	depth_image = create_depth_image();

	for (const auto& imgview : imgviews) framebufs.push_back(create_framebuffer(imgview));

	const std::array dppfb_attachments = { depth_image.view };

	const ::vk::FramebufferCreateInfo dppfb_ci(
		::vk::FramebufferCreateFlags(), depth_prepass, dppfb_attachments, extent.width,
		extent.height, 1);

	prepass_framebuffer = device.createFramebuffer(dppfb_ci, nullptr);

	fullscreen_viewport = ::vk::Viewport(
		0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height),
		0.0f, 1.0f);
	fullscreen_scissor = ::vk::Rect2D({ 0, 0 }, extent);

	update_descset_inter();
	std::tie(ppl_depth, ppl_render) = create_graphics_pipelines();
	ppl_comp = create_compute_pipeline();

	tile_count = update_lightcull_tilecounts();
	lightvis = create_and_write_lightvis_buffer();
	std::tie(cmdbufs_gfx, cmdbuf_lightcull, cmdbuf_prepass) =
		create_and_record_commandbuffers();

	const ::vk::CommandBufferAllocateInfo imgui_cmdbuf_alloc_info(
		cmdpool_gfx, ::vk::CommandBufferLevel::ePrimary, 1);

	cmdbuf_imgui = device.allocateCommandBuffers(imgui_cmdbuf_alloc_info)[0];
}

void context::destroy_swapchain()
{
	ppl_render.destroy(*this);
	ppl_depth.destroy(*this);
	ppl_comp.destroy(*this);

	for (auto& framebuf : framebufs) device.destroyFramebuffer(framebuf, nullptr);
	framebufs.clear();

	device.destroyFramebuffer(prepass_framebuffer);
	lightvis.destroy(*this);

	device.freeCommandBuffers(cmdpool_gfx, cmdbufs_gfx);
	device.freeCommandBuffers(cmdpool_comp, cmdbuf_lightcull);
	device.freeCommandBuffers(cmdpool_gfx, cmdbuf_prepass);
	device.freeCommandBuffers(cmdpool_gfx, cmdbuf_imgui);
	cmdbufs_gfx.clear();

	device.destroyRenderPass(depth_prepass, nullptr);
	device.destroyRenderPass(render_pass, nullptr);
	device.destroyRenderPass(imgui_pass, nullptr);

	for (auto& imgview : imgviews) device.destroyImageView(imgview, nullptr);

	imgviews.clear();
	depth_image.destroy(*this);
	device.destroySwapchainKHR(swapchain);
}

::vk::Format context::depth_format() const
{
	static constexpr std::array CANDIDATES = { ::vk::Format::eD32Sfloat,
											   ::vk::Format::eD32SfloatS8Uint,
											   ::vk::Format::eD24UnormS8Uint };

	for (const auto& c : CANDIDATES)
	{
		const auto fprops = gpu.getFormatProperties(c);

		if (!!(fprops.optimalTilingFeatures &
			   ::vk::FormatFeatureFlagBits::eDepthStencilAttachment))
			return c;
	}

	throw std::runtime_error("(VK) Failed to find suitable depth format.");
}

::vk::DescriptorPool context::create_descpool() const
{
	const std::array<::vk::DescriptorPoolSize, 3> pool_sizes = {
		// Transform/light/camera buffers in compute pipeline
		::vk::DescriptorPoolSize(::vk::DescriptorType::eUniformBuffer, 100),
		// Sampler for colour/normal/depth maps in prepass
		::vk::DescriptorPoolSize(::vk::DescriptorType::eCombinedImageSampler, 100),
		// Light visibility buffer in render and compute pipelines
		::vk::DescriptorPoolSize(::vk::DescriptorType::eStorageBuffer, 3)
	};

	return device.createDescriptorPool(::vk::DescriptorPoolCreateInfo(
		::vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, 200, pool_sizes));
}

std::array<::vk::DescriptorSet, 4> context::create_descsets() const
{
	std::array<::vk::DescriptorSet, 4> ret;
	const std::array dsls = { dsl_obj, dsl_cam, dsl_lightcull, dsl_inter, dsl_mat };
	const ::vk::DescriptorSetAllocateInfo alloc_info(descpool, dsls);
	const auto res = device.allocateDescriptorSets(&alloc_info, ret.data());

	if (res != ::vk::Result::eSuccess)
	{
		throw std::runtime_error(fmt::format(
			"(VK) Failed to allocate descriptor sets: {}", magic_enum::enum_name(res)));
	}

	set_debug_name(ret[0], "MXN: Desc. Set, Object");
	set_debug_name(ret[1], "MXN: Desc. Set, Camera");
	set_debug_name(ret[2], "MXN: Desc. Set, Light Culling");
	set_debug_name(ret[3], "MXN: Desc. Set, Intermediate");

	return ret;
}

void context::update_descset_obj() const
{
	const ::vk::DescriptorBufferInfo dbi(ubo_obj.get_buffer(), 0, ubo_obj.data_size);

	const ::vk::WriteDescriptorSet descwrite(
		descset_obj, 0, 0, ::vk::DescriptorType::eUniformBuffer, NO_DESCIMG_INFO, dbi,
		NO_BUFVIEWS);

	device.updateDescriptorSets(descwrite, {});
}

void context::update_descset_cam() const
{
	const ::vk::DescriptorBufferInfo dbi(ubo_cam.get_buffer(), 0, ubo_cam.data_size);

	const ::vk::WriteDescriptorSet descwrite(
		descset_cam, 0, 0, ::vk::DescriptorType::eUniformBuffer, NO_DESCIMG_INFO, dbi,
		NO_BUFVIEWS);

	device.updateDescriptorSets(descwrite, {});
}

void context::update_descset_inter() const
{
	const ::vk::DescriptorImageInfo dii(
		texture_sampler, depth_image.view,
		::vk::ImageLayout::eDepthStencilReadOnlyOptimal);

	const ::vk::WriteDescriptorSet descwrites(
		descset_inter, 0, 0, ::vk::DescriptorType::eCombinedImageSampler, dii,
		NO_DESCBUF_INFO, NO_BUFVIEWS);

	device.updateDescriptorSets(descwrites, {});
}

glm::uvec2 context::update_lightcull_tilecounts() const
{
	return { (extent.width - 1) / TILE_SIZE + 1, (extent.height - 1) / TILE_SIZE + 1 };
}

vma_buffer context::create_and_write_lightvis_buffer() const
{
	const ::vk::DeviceSize lightvis_bufsize =
		TILE_BUFFERSIZE * tile_count.x * tile_count.y;

	auto ret = vma_buffer(
		*this,
		::vk::BufferCreateInfo(
			::vk::BufferCreateFlags(), lightvis_bufsize,
			::vk::BufferUsageFlagBits::eStorageBuffer),
		VMA_ALLOC_CREATEINFO_GENERAL);

	::vk::DescriptorBufferInfo dbi_lightvis(ret.buffer, 0, lightvis_bufsize),
		dbi_lights(ubo_lights.get_buffer(), 0, ubo_lights.data_size);

	const std::array descwrites = {
		::vk::WriteDescriptorSet(
			descset_lightcull, 0, 0, 1, ::vk::DescriptorType::eStorageBuffer, nullptr,
			&dbi_lightvis, nullptr),
		::vk::WriteDescriptorSet(
			descset_lightcull, 1, 0, 1, ::vk::DescriptorType::eUniformBuffer, nullptr,
			&dbi_lights, nullptr)
	};

	const std::array<::vk::CopyDescriptorSet, 0> desccopies = {};
	device.updateDescriptorSets(descwrites, desccopies);

	return ret;
}

std::tuple<std::vector<::vk::CommandBuffer>, ::vk::CommandBuffer, ::vk::CommandBuffer>
	context::create_and_record_commandbuffers() const
{
	std::tuple<std::vector<::vk::CommandBuffer>, ::vk::CommandBuffer, ::vk::CommandBuffer>
		ret;

	auto& ret_gfx = std::get<0>(ret);
	auto& ret_lightcull = std::get<1>(ret);
	auto& ret_prepass = std::get<2>(ret);

	// Render //////////////////////////////////////////////////////////////////

	{
		const ::vk::CommandBufferAllocateInfo alloc_info(
			cmdpool_gfx, ::vk::CommandBufferLevel::ePrimary,
			static_cast<uint32_t>(framebufs.size()));

		ret_gfx = device.allocateCommandBuffers(alloc_info);

		for (size_t i = 0; i < ret_gfx.size(); i++)
			set_debug_name(ret_gfx[i], fmt::format("MXN: Cmd. Buffer, Render {}", i));
	}

	// Light culling ///////////////////////////////////////////////////////////

	{
		const ::vk::CommandBufferAllocateInfo alloc_info(
			cmdpool_comp, ::vk::CommandBufferLevel::ePrimary, 1);

		ret_lightcull = device.allocateCommandBuffers(alloc_info)[0];
		set_debug_name(ret_lightcull, "MXN: Cmd. Buffer, Light Culling");

		ret_lightcull.begin(::vk::CommandBufferBeginInfo(
			::vk::CommandBufferUsageFlagBits::eSimultaneousUse, nullptr));

		const std::array<::vk::BufferMemoryBarrier, 2> barriers_pre = {
			::vk::BufferMemoryBarrier(
				::vk::AccessFlagBits::eShaderRead, ::vk::AccessFlagBits::eShaderWrite, 0,
				0, lightvis.buffer, 0, TILE_BUFFERSIZE * tile_count.x * tile_count.y),
			::vk::BufferMemoryBarrier(
				::vk::AccessFlagBits::eShaderRead, ::vk::AccessFlagBits::eShaderWrite, 0,
				0, ubo_lights.get_buffer(), 0, ubo_lights.data_size)
		};

		ret_lightcull.pipelineBarrier(
			::vk::PipelineStageFlagBits::eFragmentShader,
			::vk::PipelineStageFlagBits::eComputeShader, ::vk::DependencyFlags(), {},
			barriers_pre, {});

		ret_lightcull.bindDescriptorSets(
			::vk::PipelineBindPoint::eCompute, ppl_comp.layout, 0,
			std::array { descset_lightcull, descset_cam, descset_inter },
			std::array<uint32_t, 0>());

		ret_lightcull.pushConstants<pushconst>(
			ppl_comp.layout, ::vk::ShaderStageFlagBits::eCompute, 0,
			std::array { pushconst { .viewport_size = { extent.width, extent.height },
									 .tile_nums = tile_count,
									 .debugview_index = 0 } });
		ret_lightcull.bindPipeline(::vk::PipelineBindPoint::eCompute, ppl_comp.handle);
		ret_lightcull.dispatch(tile_count.x, tile_count.y, 1);

		ret_lightcull.end();
	}

	// Depth pre-pass //////////////////////////////////////////////////////////

	{
		const ::vk::CommandBufferAllocateInfo alloc_info(
			cmdpool_gfx, ::vk::CommandBufferLevel::ePrimary, 1);

		ret_prepass = device.allocateCommandBuffers(alloc_info)[0];
		set_debug_name(ret_prepass, "MXN: Cmd. Buffer, Depth Pre-pass");
	}

	return ret;
}

// Context, constructor helpers ////////////////////////////////////////////////

::vk::Instance context::ctor_instance(SDL_Window* const window) const
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

::vk::SurfaceKHR context::ctor_surface(SDL_Window* const window) const
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

::vk::PhysicalDevice context::ctor_select_gpu() const
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

uint32_t context::ctor_get_qfam_gfx() const
{
	assert(gpu); // i.e. != VK_NULL_HANDLE

	const auto qfam_props = gpu.getQueueFamilyProperties();

	for (size_t i = 0; i < qfam_props.size(); i++)
		if (suitable_gfx_queue_family(qfam_props[i])) return static_cast<uint32_t>(i);

	return INVALID_QUEUE_FAMILY;
}

uint32_t context::ctor_get_qfam_pres() const
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

uint32_t context::ctor_get_qfam_trans() const
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

::vk::Device context::ctor_device() const
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

::vk::DispatchLoaderDynamic context::ctor_dispatch_loader() const
{
	::vk::DispatchLoaderDynamic dld(vkGetInstanceProcAddr);
	dld.init(inst);
	return dld;
}

VmaAllocator context::ctor_vma() const
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

::vk::DebugUtilsMessengerEXT context::ctor_init_debug_messenger() const
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
