/**
 * @file script.hpp
 * @brief Interfaces and helpers for daScript.
 */

#pragma once

#include "log.hpp"

#include <daScript/daScript.h>

namespace mxn
{
	class script_core final : public das::Module
	{
		static void log(const char* msg) { MXN_LOG(msg); }
		static void warn(const char* msg) { MXN_WARN(msg); }
		static void err(const char* msg) { MXN_ERR(msg); }

	public:
		script_core() : das::Module("mxn_core")
		{
			das::ModuleLibrary lib;
			lib.addModule(this);
			lib.addBuiltInModule();

			das::addExtern<DAS_BIND_FUN(log)>(
				*this, lib, "mxn_log", das::SideEffects::worstDefault, "log");
			das::addExtern<DAS_BIND_FUN(warn)>(
				*this, lib, "mxn_warn", das::SideEffects::worstDefault, "warn");
			das::addExtern<DAS_BIND_FUN(err)>(
				*this, lib, "mxn_err", das::SideEffects::worstDefault, "err");
		}
	};
} // namespace mxn

REGISTER_MODULE_IN_NAMESPACE(script_core, mxn);
