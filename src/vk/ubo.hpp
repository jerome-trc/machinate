/**
 * @file vk/ubo.hpp
 * @brief A class wrapping UBO data with its holding and staging buffers.
 */

#pragma once

#include "buffer.hpp"

#include <string>
#include <vector>

template<typename T>
concept like_std_container = requires(T t)
{
	t.data();
	t.size();
};

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
		vma_buffer buffer, staging;

		vma_buffer ctor_mkbuf(
			const context&, bool staging, const std::vector<uint32_t>& qfams) const;
		void ctor_debugnames(const context&, const std::string& postfix = "");

	public:
		ubo() = default;
		ubo(const context&, const std::string& debug_postfix = "");
		ubo(const context&, uint32_t shared_queuefamily_a, uint32_t shared_queuefamily_b,
			const std::string& debug_postfix = "");

		void update(const context&);
		void update(const context&) requires like_std_container<T>;

		/// @note Has no effect on `data`.
		void destroy(const context&);

		[[nodiscard]] constexpr const ::vk::Buffer& get_buffer() const noexcept
		{
			return buffer.buffer;
		}
	};
} // namespace mxn::vk
