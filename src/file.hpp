/**
 * @file file.hpp
 * @brief Helper functions for filesystem operations.
 */

#pragma once

#include "log.hpp"

#include <filesystem>
#include <physfs.h>
#include <string>

namespace mxn
{
	using vfs_enumerator =
		PHYSFS_EnumerateCallbackResult (*)(void*, const char*, const char*);

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
	[[nodiscard]] std::string get_userdata_path(const std::string& appname) noexcept;

	void vfs_init(const std::string& argv0);
	void vfs_deinit();
	void vfs_mount(
		const std::filesystem::path& path, const std::filesystem::path& mount_point);

	[[nodiscard]] bool vfs_exists(const std::filesystem::path&) noexcept;
	[[nodiscard]] bool vfs_isdir(const std::filesystem::path&) noexcept;
	[[nodiscard]] uint32_t vfs_count(const std::filesystem::path&) noexcept;

	std::vector<unsigned char> vfs_read(const std::filesystem::path&);
	std::string vfs_readstr(const std::filesystem::path& path);
	void vfs_recur(const std::filesystem::path&, void* userdata, vfs_enumerator);

	void ccmd_file(const std::string& path);
} // namespace mxn
