/**
 * @file vk/pipeline.hpp
 * @brief A wrapper around a pipeline and its layout and shader modules.
 */

#include "pipeline.hpp"

#include "./context.hpp"

using namespace mxn::vk;

pipeline::pipeline(
	::vk::Pipeline handle, ::vk::PipelineLayout layout,
	std::initializer_list<::vk::ShaderModule> shaders)
	: handle(handle), layout(layout), shaders(shaders)
{
	assert(this->handle);
	assert(this->layout);
	for (const auto& sm : this->shaders) { assert(sm); }
}

pipeline::pipeline(const pipeline& other)
{
	handle = other.handle;
	layout = other.layout;
	shaders = other.shaders;
}

pipeline& pipeline::operator=(const pipeline& other)
{
	handle = other.handle;
	layout = other.layout;
	shaders = other.shaders;
	return *this;
}

pipeline::pipeline(pipeline&& other)
	: handle(other.handle), layout(other.layout), shaders(other.shaders)
{
	other.handle = ::vk::Pipeline(nullptr);
	other.layout = ::vk::PipelineLayout(nullptr);
	other.shaders = {};
}

pipeline& pipeline::operator=(pipeline&& other)
{
	handle = other.handle;
	layout = other.layout;
	shaders = other.shaders;
	other.handle = ::vk::Pipeline(nullptr);
	other.layout = ::vk::PipelineLayout(nullptr);
	other.shaders = {};
	return *this;
}

void pipeline::destroy(const context& ctxt)
{
	ctxt.device.destroyPipeline(handle);
	ctxt.device.destroyPipelineLayout(layout);

	for (const auto& sm : shaders) ctxt.device.destroyShaderModule(sm);
}