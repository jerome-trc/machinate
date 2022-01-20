/**
 * @file vulkan.hpp
 * @brief Interfaces and helpers for Vulkan functionality.
 */

#pragma once

#include "preproc.hpp"

#include <vector>
#include <vulkan/vulkan.hpp>

struct SDL_Window;
struct VmaAllocator_T;
typedef VmaAllocator_T* VmaAllocator;

namespace mxn
{
	class console;
}

namespace mxn::vk
{
	class context final
	{
		static constexpr uint32_t INVALID_QUEUE_FAMILY =
			std::numeric_limits<uint32_t>::max();

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

		::vk::SwapchainKHR swapchain;
		::vk::Format imgformat;
		::vk::Extent2D extent;
		std::vector<::vk::Image> images;
		std::vector<::vk::ImageView> imgviews;
		::vk::RenderPass render_pass;
		std::vector<::vk::Framebuffer> framebufs;
		::vk::CommandBuffer cmdbuf;
		::vk::Viewport fullscreen_viewport;
		::vk::Rect2D fullscreen_scissor;

		::vk::Semaphore sema_render, sema_present;
		::vk::Fence fence_render;

		::vk::DescriptorPool descpool_imgui;

		// Dynamic data ////////////////////////////////////////////////////////

		size_t frame = 0;
		uint32_t img_idx = 0;

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
			create_swapchain(SDL_Window* const) const;
		[[nodiscard]] std::tuple<std::vector<::vk::Image>, std::vector<::vk::ImageView>>
			create_images_and_views() const;
		[[nodiscard]] ::vk::RenderPass create_renderpass() const;
		[[nodiscard]] ::vk::Framebuffer create_framebuffer(const ::vk::ImageView&) const;
		[[nodiscard]] ::vk::CommandBuffer create_commandbuffer() const;
		void destroy_swapchain();

	public:
		context(SDL_Window* const);
		~context();
		DELETE_COPIERS_AND_MOVERS(context)

		/**
		 * @brief Start a new frame.
		 *
		 * Calls `ImGui::Render()`, resets the context's fences and command buffer,
		 * acquires the next swapchain image, restarts command buffer recording, and
		 * starts the render pass.
		 */
		void start_render() noexcept;

		/**
		 * @brief Finalise rendering a frame.
		 *
		 * Calls `ImGui_ImplVulkan_RenderDrawData()`, ends a render pass and
		 * command buffer recording, submits to the graphics queue, and presents
		 * the new frame.
		 *
		 * @returns `false` if the context's swapchain requires rebuilding.
		 */
		bool finish_render() noexcept;

		/**
		 * @brief Rebuild the context's swapchain, framebuffers, and command buffer.
		 */
		void rebuild_swapchain(SDL_Window* const);

		template<typename T>
		void set_debug_name(const T& obj, const std::string& name)
		{
			assert(obj);

			device.setDebugUtilsObjectNameEXT(
				{ T::objectType, reinterpret_cast<uint64_t>(static_cast<T::CType>(obj)),
				  name.c_str() },
				dispatch_loader);
		}

		/** @brief Implements the `vkdiag` console command.  */
		void vkdiag(const std::vector<std::string> args) const;
	};
} // namespace mxn::vk
