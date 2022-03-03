/**
 * @file log.hpp
 * @brief Quill-based logger, shortcut macros, related symbols.
 */

#pragma once

#include "preproc.hpp"

#include <quill/Quill.h>

namespace mxn
{
	extern quill::Logger* qlog;

	/**
	 * @brief Prepare for logging via Quill.
	 *
	 * Calls `quill::enable_console_colours()`, `quill::start()`,
	 * and `quill::preallocate()` before finally constructing handlers and the
	 * logger object.
	 */
	void log_init(std::initializer_list<quill::Handler*>);
} // namespace mxn

/**
 * @brief Print to cout and console; prepend with "INFO: "
 * @remark Use to record information from the average user's runtime wherever
 * that information may be useful to developer/modder debugging.
 */
#define MXN_LOG(msg) LOG_INFO(mxn::qlog, "{}", msg)
/**
 * @brief Print to cout and console; prepend with "WARN: "
 * @remark Use to inform that something undesirable has happened but the user's
 * experience is unlikely to be affected.
 */
#define MXN_WARN(msg) LOG_WARNING(mxn::qlog, "{}", msg)
/**
 * @brief Print to cout and console; prepend with "ERROR: "
 * @remark Use to inform that an error has occurred but the application's state
 * is recoverable.
 */
#define MXN_ERR(msg) LOG_ERROR(mxn::qlog, "{}", msg)
/**
 * @brief Print to cout and console; prepend with "CRITICAL: "
 * @remark Use to inform that the application is in a compromised state, and is
 * either liable to crash or not capable of being useful anymore.
 */
#define MXN_CRIT(msg) LOG_CRITICAL(mxn::qlog, "{}", msg)

/** @brief Like `MXN_LOG`, but allows {fmt}-style variadic formatting. */
#define MXN_LOGF(msg, ...) LOG_INFO(mxn::qlog, msg, __VA_ARGS__)
/** @brief Like `MXN_WARN`, but allows {fmt}-style variadic formatting. */
#define MXN_WARNF(msg, ...) LOG_WARNING(mxn::qlog, msg, __VA_ARGS__)
/** @brief Like `MXN_ERR`, but allows {fmt}-style variadic formatting. */
#define MXN_ERRF(msg, ...) LOG_ERROR(mxn::qlog, msg, __VA_ARGS__)
/** @brief Like `MXN_CRIT`, but allows {fmt}-style variadic formatting. */
#define MXN_CRITF(msg, ...) LOG_CRITICAL(mxn::qlog, msg, __VA_ARGS__)

#ifndef NDEBUG

/**
 * @brief Print to cout and console; prepend with "DEBUG: "
 * @note Preprocessed away when `NDEBUG` is set (i.e., in the Release build).
 * @remark Should only be used to generate a record of information which may be
 * useful in future debugging efforts by developers or modders.
 */
#define MXN_DEBUG(msg) LOG_DEBUG(mxn::qlog, "{}", msg)
/** @brief Like `MXN_DEBUG`, but allows {fmt}-style variadic formatting. */
#define MXN_DEBUGF(msg, ...) LOG_DEBUG(mxn::qlog, msg, ##__VA_ARGS__)

#else

#define MXN_DEBUG(msg)
#define MXN_DEBUGF(msg, ...)

#endif