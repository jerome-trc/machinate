/**
 * @file vk/context.hpp
 * @brief The class serving as the foundation for all other Vulkan operations.
 */

#pragma once

#include "../ecs.hpp"
#include "../preproc.hpp"
#include "buffer.hpp"
#include "detail.hpp"
#include "image.hpp"
#include "pipeline.hpp"
#include "ubo.hpp"

#include <filesystem>
#include <vulkan/vulkan.hpp>

struct SDL_Window;

struct VmaAllocator_T;
typedef VmaAllocator_T* VmaAllocator;

namespace mxn::vk
{
	struct model;
	struct material;

	class context final
	{
	public:
		const ::vk::Instance inst;
		const ::vk::SurfaceKHR surface;
		const ::vk::PhysicalDevice gpu;
		const uint32_t qfam_gfx = INVALID_QUEUE_FAMILY, qfam_pres = INVALID_QUEUE_FAMILY,
					   qfam_trans = INVALID_QUEUE_FAMILY;
		const ::vk::Device device;
		const ::vk::DispatchLoaderDynamic dispatch_loader;
		const ::vk::DebugUtilsMessengerEXT debug_messenger;
		const VmaAllocator vma = nullptr;
		const ::vk::Queue q_gfx, q_pres, q_comp;
		const ::vk::CommandPool cmdpool_gfx, cmdpool_trans, cmdpool_comp;

		context(SDL_Window* const);
		~context();
		DELETE_COPIERS_AND_MOVERS(context)

		/**
		 * @brief Start a new frame.
		 *
		 * Calls `ImGui::Render()`, resets the context's fence,
		 * and acquires the next swapchain image.
		 *
		 * @returns `false` if the context's swapchain requires re-creation.
		 */
		[[nodiscard]] bool start_render() noexcept;

		void set_camera(const ubo<camera>& uniform);

		void start_render_record() noexcept;
		void bind_material(const mxn::vk::material&) noexcept;
		void record_draw(const mxn::vk::model&) noexcept;
		void end_render_record() noexcept;
		
		[[nodiscard]] const ::vk::Semaphore& submit_prepass(
			const ::vk::ArrayProxyNoTemporaries<const ::vk::Semaphore>& wait_semas
		) noexcept;

		/**
		 * @brief Invokes the light culling computation command buffer.
		 * @returns The semaphore which will signal when computation is complete.
		 */
		[[nodiscard]] const ::vk::Semaphore& compute_lightcull(
			const ::vk::ArrayProxyNoTemporaries<const ::vk::Semaphore>& wait_semas
		) noexcept;

		[[nodiscard]] const ::vk::Semaphore& submit_geometry(
			const ::vk::ArrayProxyNoTemporaries<const ::vk::Semaphore>& wait_semas
		) noexcept;

		/**
		 * @brief Records and submits the commands of `ImGui_ImplVulkan_RenderDrawData()`.
		 * @note Should only be called after `start_frame()` and generally
		 * before `present_frame()`.
		 * @returns The semaphore which will signal when rendering is complete.
		 */
		[[nodiscard]] const ::vk::Semaphore& render_imgui(
			const ::vk::ArrayProxyNoTemporaries<const ::vk::Semaphore>& wait_semas
		) noexcept;

		/**
		 * @brief Submits the current swapchain frame to the present queue.
		 * @param wait_sema The semaphore on which presentation waits.
		 * @returns `false` if the context's swapchain requires re-creation.
		 */
		[[nodiscard]] bool present_frame(const ::vk::Semaphore& wait_sema);

		/// @brief Rebuild the context's swapchain, framebuffers, and command buffer.
		void rebuild_swapchain(SDL_Window* const);

		[[nodiscard]] ::vk::ShaderModule create_shader(
			const std::filesystem::path&, const std::string& debug_name = "") const;

		[[nodiscard]] material create_material(
			const std::filesystem::path& albedo = "", const std::filesystem::path& normal = "",
			const std::string& debug_name = ""
		) const;

		[[nodiscard]] ::vk::CommandBuffer begin_onetime_buffer() const;
		/// @brief Ends, submits, and frees the given buffer.
		/// @remark Only for use with the output of `begin_onetime_buffer()`.
		void consume_onetime_buffer(::vk::CommandBuffer&&) const;

		void record_image_layout_change(
			const ::vk::CommandBuffer&, const ::vk::Image&, ::vk::ImageLayout from,
			::vk::ImageLayout to) const;

		[[nodiscard]] size_t swapchain_image_count() const noexcept
		{
			return images.size();
		}

		[[nodiscard]] constexpr const ::vk::Extent2D& get_swapchain_extent() const noexcept
		{
			return extent;
		}

		template<typename T>
		void set_debug_name(const T& obj, const std::string& name) const
		{
			assert(obj);

			device.setDebugUtilsObjectNameEXT(
				{ T::objectType, reinterpret_cast<uint64_t>(static_cast<T::CType>(obj)),
				  name.c_str() },
				dispatch_loader);
		}

		/** @brief Implements the `vkdiag` console command. */
		void vkdiag(const std::vector<std::string>& args) const;

	private:
		// Swapchain components ////////////////////////////////////////////////

		::vk::SwapchainKHR swapchain;
		::vk::Format imgformat;
		::vk::Extent2D extent;
		std::vector<::vk::Image> images;
		std::vector<::vk::ImageView> imgviews;
		::vk::RenderPass depth_prepass, render_pass, imgui_pass;
		std::vector<::vk::Framebuffer> framebufs;
		::vk::Framebuffer prepass_framebuffer;
		::vk::Viewport fullscreen_viewport;
		::vk::Rect2D fullscreen_scissor;
		::vk::DescriptorSetLayout dsl_obj, dsl_cam, dsl_lightcull, dsl_inter, dsl_mat;

		ubo<glm::mat4> ubo_obj;
		ubo<std::vector<point_light>, POINTLIGHT_BUFSIZE> ubo_lights;

		pipeline ppl_render, ppl_depth, ppl_comp;

		vma_image depth_image;
		::vk::Sampler texture_sampler;
		::vk::DescriptorPool descpool;
		::vk::DescriptorSet descset_obj, descset_cam, descset_lightcull, descset_inter;

		/// `x` is per row, `y` is per column.
		glm::uvec2 tile_count;
		vma_buffer lightvis;

		::vk::DescriptorPool descpool_imgui;

		/// One command buffer per framebuffer.
		std::vector<::vk::CommandBuffer> cmdbufs_gfx;
		::vk::CommandBuffer cmdbuf_lightcull, cmdbuf_prepass, cmdbuf_imgui;

		::vk::Semaphore sema_renderdone, sema_imgavail, sema_lightculldone,
			sema_prepassdone, sema_imgui;

		::vk::Fence fence_render;

		// Dynamic data ////////////////////////////////////////////////////////

		size_t frame = 0;
		uint32_t img_idx = 0;

		// Methods /////////////////////////////////////////////////////////////

		[[nodiscard]] ::vk::Instance ctor_instance(SDL_Window* const) const;
		[[nodiscard]] ::vk::SurfaceKHR ctor_surface(SDL_Window* const) const;
		[[nodiscard]] ::vk::PhysicalDevice ctor_select_gpu() const;
		[[nodiscard]] uint32_t ctor_get_qfam_gfx() const;
		[[nodiscard]] uint32_t ctor_get_qfam_pres() const;
		[[nodiscard]] uint32_t ctor_get_qfam_trans() const;
		[[nodiscard]] ::vk::Device ctor_device() const;
		[[nodiscard]] ::vk::DispatchLoaderDynamic ctor_dispatch_loader() const;
		[[nodiscard]] VmaAllocator ctor_vma() const;
		[[nodiscard]] ::vk::DebugUtilsMessengerEXT ctor_init_debug_messenger() const;

		[[nodiscard]] std::tuple<::vk::SwapchainKHR, ::vk::Format, ::vk::Extent2D>
			create_swapchain_core(SDL_Window* const) const;
		[[nodiscard]] std::tuple<std::vector<::vk::Image>, std::vector<::vk::ImageView>>
			create_images_and_views() const;
		/// @brief Returns a depth prepass and render pass (in that order).
		[[nodiscard]] std::pair<::vk::RenderPass, ::vk::RenderPass> create_passes() const;
		[[nodiscard]] ::vk::RenderPass create_imgui_renderpass() const;
		[[nodiscard]] ::vk::Framebuffer create_framebuffer(const ::vk::ImageView&) const;
		/// @brief Creates, in order, descriptor set layouts for the object, camera,
		/// light culling, intermediate, and material uniform buffers.
		[[nodiscard]] std::array<::vk::DescriptorSetLayout, 5> create_descset_layouts()
			const;
		[[nodiscard]] std::pair<pipeline, pipeline> create_graphics_pipelines() const;
		[[nodiscard]] pipeline create_compute_pipeline() const;
		[[nodiscard]] vma_image create_depth_image() const;
		[[nodiscard]] ::vk::DescriptorPool create_descpool() const;
		/// @brief Returns object, camera, light culling, and intermediate
		/// descriptor sets (in that order; performs no writing).
		[[nodiscard]] std::array<::vk::DescriptorSet, 4> create_descsets() const;

		void update_descset_obj() const;
		void update_descset_inter() const;

		[[nodiscard]] glm::uvec2 update_lightcull_tilecounts() const;
		[[nodiscard]] vma_buffer create_and_write_lightvis_buffer() const;

		/// @brief Returns pre-recorded command buffers for graphics,
		/// lightculling, and the depth pre-pass, (in that order).
		/// @note Only the light cull command buffer is pre-recorded.
		[[nodiscard]] std::tuple<
			std::vector<::vk::CommandBuffer>, ::vk::CommandBuffer, ::vk::CommandBuffer>
			create_and_record_commandbuffers() const;

		void create_swapchain(SDL_Window* const);
		void destroy_swapchain();

		[[nodiscard]] ::vk::Format depth_format() const;
	};
} // namespace mxn::vk

#include "ubo.ipp"
