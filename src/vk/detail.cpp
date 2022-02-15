/**
 * @file vk/detail.hpp
 * @brief Simple symbols related to Vulkan which don't belong anywhere else.
 */

#include "detail.hpp"

#include "context.hpp"
#include "../media.hpp"

using namespace mxn::vk;

void camera::update(const context& ctxt, const mxn::camera& viewp)
{
	const auto extent = ctxt.get_swapchain_extent();

	camera.view = viewp.view_matrix();
	camera.proj = glm::perspective(glm::radians(45.0f),
		static_cast<float>(extent.width) / static_cast<float>(extent.height),
		0.5f, 100.0f);
	camera.proj[1][1] *= -1.0f; // Vulkan NDC y-axis points downward
	camera.projview = camera.proj * camera.view;
	camera.position = viewp.camera.position;
}
