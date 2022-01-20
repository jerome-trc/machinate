/**
 * @file file.hpp
 * @brief Helper functions for filesystem operations.
 */

#pragma once

#include "log.hpp"

#include <filesystem>
#include <string>

namespace mxn
{
	/** @brief Absolute path to the executable's directory. Ends with a path
	 * separator. */
	extern const std::string base_path;
	/** @brief Absolute path to the client userdata folder. Ends with a path
	 * separator. */
	extern const std::string user_path;

	/**
	 * @brief Get the current application's user data path as per
	 * `SDL_GetPrefPath`.
	 * @sa https://wiki.libsdl.org/SDL_GetPrefPath
	 */
	std::string get_userdata_path(const std::string& appname) noexcept;

	void vfs_init(const std::string& argv0);
	void vfs_deinit();
	void vfs_mount(
		const std::filesystem::path& path,
		const std::filesystem::path& mount_point);
} // namespace mxn
