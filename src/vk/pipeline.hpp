/**
 * @file vk/pipeline.hpp
 * @brief A wrapper around a pipeline and its layout and shader modules.
 */

#pragma once

#include <vulkan/vulkan.hpp>

namespace mxn::vk
{
	class context;

	/// @brief A wrapper around a pipeline and its layout and shader modules.
	struct pipeline final
	{
		::vk::Pipeline handle;
		::vk::PipelineLayout layout;
		std::vector<::vk::ShaderModule> shaders;

		pipeline() noexcept = default;

		pipeline(
			::vk::Pipeline, ::vk::PipelineLayout,
			std::initializer_list<::vk::ShaderModule>);

		pipeline(const pipeline&);
		pipeline& operator=(const pipeline&);
		pipeline(pipeline&&);
		pipeline& operator=(pipeline&&);

		void destroy(const context&);
	};
} // namespace mxn::vk
