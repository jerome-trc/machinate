/**
 * @file vk/ubo.ipp
 * @brief Provides implementation of ubo.hpp for inclusion in context.hpp.
 */

#include "../algorithms.hpp"
#include "../log.hpp"

template<typename T, size_t Sz>
mxn::vk::vma_buffer mxn::vk::ubo<T, Sz>::ctor_mkbuf(
	const context& ctxt, const bool staging, const std::vector<uint32_t>& qfams) const
{
	const bool concur = all_elements_unique(qfams.begin(), qfams.end());

	if (!concur)
	{
		const std::vector<uint32_t> no_qfams = {};

		return vma_buffer(
			ctxt,
			::vk::BufferCreateInfo(
				::vk::BufferCreateFlags(), Sz,
				!staging ? ::vk::BufferUsageFlagBits::eUniformBuffer |
							   ::vk::BufferUsageFlagBits::eTransferDst
						 : ::vk::BufferUsageFlagBits::eTransferSrc,
				::vk::SharingMode::eExclusive, no_qfams),
			!staging ? VMA_ALLOC_CREATEINFO_GENERAL : VMA_ALLOC_CREATEINFO_STAGING);
	}
	else
	{
		return vma_buffer(
			ctxt,
			::vk::BufferCreateInfo(
				::vk::BufferCreateFlags(), Sz,
				!staging ? ::vk::BufferUsageFlagBits::eUniformBuffer |
							   ::vk::BufferUsageFlagBits::eTransferDst
						 : ::vk::BufferUsageFlagBits::eTransferSrc,
				::vk::SharingMode::eConcurrent, qfams),
			!staging ? VMA_ALLOC_CREATEINFO_GENERAL : VMA_ALLOC_CREATEINFO_STAGING);
	}
}

template<typename T, size_t Sz>
void mxn::vk::ubo<T, Sz>::ctor_debugnames(const context& ctxt, const std::string& postfix)
{
	if (!postfix.empty())
	{
		ctxt.set_debug_name(buffer.buffer, fmt::format("MXN: UBO, {}", postfix));
		ctxt.set_debug_name(buffer.memory, fmt::format("MXN: UBO Memory, {}", postfix));
		ctxt.set_debug_name(staging.buffer, fmt::format("MXN: UBO Staging, {}", postfix));
		ctxt.set_debug_name(
			staging.buffer, fmt::format("MXN: UBO Staging Memory, {}", postfix));
	}
}

template<typename T, size_t Sz>
mxn::vk::ubo<T, Sz>::ubo(const context& ctxt, const std::string& debug_postfix)
	: buffer(ctor_mkbuf(ctxt, false, {})), staging(ctor_mkbuf(ctxt, true, {}))
{
	ctor_debugnames(ctxt, debug_postfix);
}

template<typename T, size_t Sz>
mxn::vk::ubo<T, Sz>::ubo(
	const context& ctxt, uint32_t shared_queuefamily_a, uint32_t shared_queuefamily_b,
	const std::string& debug_postfix)
	: buffer(ctor_mkbuf(ctxt, false, { shared_queuefamily_a, shared_queuefamily_b })),
	  staging(ctor_mkbuf(ctxt, true, {}))
{
	ctor_debugnames(ctxt, debug_postfix);
}

template<typename T, size_t Sz>
void mxn::vk::ubo<T, Sz>::update(const context& ctxt) requires ubo<T, Sz>::T_inner_ptr
{
	void* d = nullptr;
	const auto res = vmaMapMemory(ctxt.vma, staging.allocation, &d);
	assert(res == VK_SUCCESS);
	memcpy(d, reinterpret_cast<void*>(data.data()), data_size);
	vmaUnmapMemory(ctxt.vma, staging.allocation);
	staging.copy_to(ctxt, buffer, { ::vk::BufferCopy(0, 0, data_size) });
}

template<typename T, size_t Sz>
void mxn::vk::ubo<T, Sz>::update(const context& ctxt)
{
	void* d = nullptr;
	const auto res = vmaMapMemory(ctxt.vma, staging.allocation, &d);
	assert(res == VK_SUCCESS);
	memcpy(d, reinterpret_cast<void*>(&data), data_size);
	vmaUnmapMemory(ctxt.vma, staging.allocation);
	staging.copy_to(ctxt, buffer, { ::vk::BufferCopy(0, 0, data_size) });
}

template<typename T, size_t Sz>
void mxn::vk::ubo<T, Sz>::destroy(const context& ctxt)
{
	staging.destroy(ctxt);
	buffer.destroy(ctxt);
}
