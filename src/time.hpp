/** @file time.hpp */

#pragma once

#include <chrono>

/** @brief std::localtime() protected by a mutex for thread safety. */
[[nodiscard]] const tm* localtime_ts(const time_t* time) noexcept;

namespace mxn
{
	extern const std::chrono::system_clock::time_point start_time;

	template<typename T>
	[[nodiscard]] T runtime()
	{
		return std::chrono::duration_cast<T>(
			std::chrono::system_clock::now() - start_time);
	}

#define runtime_s runtime<std::chrono::seconds>
#define runtime_ms runtime<std::chrono::milliseconds>
} // namespace mxn
