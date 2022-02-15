/**
 * @file vk/ubo.hpp
 * @brief A class wrapping UBO data with its holding and staging buffers.
 */

#pragma once

#include "buffer.hpp"

#include <string>
#include <vector>

namespace mxn::vk
{
	class context;

	/// @brief Wraps a uniform buffer object's data, staging buffer, and holding buffer.
	/// @param T Can be a container if it comes with `data()` and size()` methods.
	/// @param Sz The size of buffer allocated.
	template<typename T, size_t Sz = sizeof(T)>
	class ubo final
	{
		static_assert(Sz >= 1u);

	public:
		static constexpr size_t data_size = Sz;
		T data;

	private:
		static constexpr bool T_inner_ptr = requires(const T& t) { t.data(); };
		static constexpr bool T_size_func = requires(const T& t) { t.size(); };

		vma_buffer buffer, staging;

		vma_buffer ctor_mkbuf(
			const context&, bool staging, const std::vector<uint32_t>& qfams) const;
		void ctor_debugnames(const context&, const std::string& postfix = "");

	public:
		ubo() = default;
		ubo(const context&, const std::string& debug_postfix = "");
		ubo(const context&, uint32_t shared_queuefamily_a, uint32_t shared_queuefamily_b,
			const std::string& debug_postfix = "");

		void update(const context&) requires T_inner_ptr;
		void update(const context&);

		/// @note Has no effect on `data`.
		void destroy(const context&);

		[[nodiscard]] constexpr const ::vk::Buffer& get_buffer() const noexcept
		{
			return buffer.buffer;
		}
	};
} // namespace mxn::vk
