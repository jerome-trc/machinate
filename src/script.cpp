/// @file script.cpp
/// @brief Lua scripting interfaces and utilities.

#include "script.hpp"

#include "file.hpp"
#include "log.hpp"

#include <sol/sol.hpp>

static void lua_log_info(const char* msg) { MXN_LOG(msg); }
static void lua_log_warn(const char* msg) { MXN_WARN(msg); }
static void lua_log_err(const char* msg) { MXN_ERR(msg); }
static void lua_log_debug(const char* msg) { MXN_DEBUG(msg); }

void mxn::lua::setup_state(sol::state& lua)
{
	// clang-format off
	lua.open_libraries(
		sol::lib::base, sol::lib::ffi, sol::lib::jit, sol::lib::coroutine,
		sol::lib::string, sol::lib::math, sol::lib::table, sol::lib::bit32
#ifndef NDEBUG
		, sol::lib::debug
#endif
	);
	// clang-format on

	auto _G_mxn = lua["mxn"].get_or_create<sol::table>();
	_G_mxn.set_function("log", lua_log_info);
	_G_mxn.set_function("warn", lua_log_warn);
	_G_mxn.set_function("err", lua_log_err);
	_G_mxn.set_function("debug", lua_log_debug);

	lua.set_function("import", [&lua](const char* path) -> sol::object {
		return mxn::lua::safe_script_file(lua, path);
	});

	auto reg = lua.registry();

	// Load the Teal compiler into the registry

	{
		const auto res = mxn::lua::safe_script_file(lua, "/lua/tl.lua");
		if (!res.valid())
		{
			const sol::error& err = res;
			MXN_ERRF("Failed to load Teal compiler. Details: {}", err.what());
		}
		else
		{
			reg["teal"] = res;
		}
	}

	// Make utils.tl globally available

	{
		const auto res = mxn::lua::safe_script_file(lua, "/lua/utils.tl");
		if (!res.valid())
		{
			const sol::error& err = res;
			MXN_ERRF("Failed to load utils script module. Details: {}", err.what());
		}
	}
}

sol::object mxn::lua::require_file(
	sol::state& lua, const std::string& key, const std::filesystem::path& path,
	bool create_global)
{
	if (!vfs_exists(path))
	{
		MXN_ERRF("Attempted to load non-existent file as a Lua module: {}", path);
		return sol::protected_function_result();
	}

	const std::string buffer = vfs_readstr(path);
	if (buffer.empty())
	{
		MXN_ERRF("Failed to read Lua script from file: {}", path);
		return sol::object();
	}

	if (path.extension() == ".tl")
	{
		const sol::function teal = lua.registry()["teal"]["gen"];
		assert(teal.valid());
		auto result = teal(buffer);

		if (!result.valid())
		{
			const sol::error& err = result;
			MXN_ERRF(
				"Failed to compile Teal file: {}.\n\tError: {}", path.string(),
				err.what());
		}

		return lua.require_script(
			key, static_cast<std::string>(result), create_global,
			sol::detail::default_chunk_name(), sol::load_mode::text);
	}
	else
	{
		return lua.require_script(
			key, buffer, create_global, sol::detail::default_chunk_name(),
			sol::load_mode::text);
	}
}

sol::protected_function_result mxn::lua::safe_script_file(
	sol::state& lua, const std::filesystem::path& path)
{
	if (!vfs_exists(path))
	{
		MXN_ERRF("Attempted to run non-existent file as a Lua script: {}", path);
		return sol::protected_function_result();
	}

	const std::string buffer = vfs_readstr(path);
	if (buffer.empty())
	{
		MXN_ERRF("Failed to read Lua script from file: {}", path);
		return sol::protected_function_result();
	}

	if (path.extension() == ".tl")
	{
		const sol::function teal = lua.registry()["teal"]["gen"];
		assert(teal.valid());
		auto result = teal(buffer);

		if (!result.valid())
		{
			const sol::error& err = result;
			MXN_ERRF(
				"Failed to compile Teal file: {}.\n\tError: {}", path.string(),
				err.what());
		}

		return lua.safe_script(
			static_cast<std::string>(result), sol::detail::default_chunk_name(),
			sol::load_mode::text);
	}
	else
	{
		return lua.safe_script(
			buffer, sol::detail::default_chunk_name(), sol::load_mode::text);
	}
}
