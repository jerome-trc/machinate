/// @file script.hpp
/// @brief Lua scripting interfaces and utilities.

#include <filesystem>

#pragma once

namespace sol
{
	class state;
	class protected_function_result;

	template<bool B>
	class basic_reference;

	using reference = basic_reference<false>;

	template<typename T>
	class basic_object;

	using object = basic_object<reference>;
}

namespace mxn::lua
{
	/// @brief Prepares a Sol2 state for use.
	///
	/// Calls `sol::state::load_libraries()`, sets up logging functions, and then
	/// prepares the import function, Teal compiler, and utility modules.
	void setup_state(sol::state&);

	sol::protected_function_result safe_script_file(
		sol::state&, const std::filesystem::path&);

	sol::object require_file(sol::state&, const std::string& key,
		const std::filesystem::path&, bool create_global = true);
} // namespace mxn
