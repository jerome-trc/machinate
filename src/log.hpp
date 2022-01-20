/**
 * @file log.hpp
 * @brief Quill-based logger, shortcut macros, related symbols.
 */

#pragma once

#include "preproc.hpp"

#include <quill/Quill.h>

namespace mxn
{
	struct log_entry final
	{
		const std::string text;
		const quill::LogLevel level;
	};

	/** @brief Quill handler; keeps history to print to the ImGui engine log. */
	class log_history_handler final : public quill::Handler
	{
		std::vector<log_entry> entries;
		mutable std::mutex mtx;

	public:
		log_history_handler() = default;
		DELETE_COPIERS_AND_MOVERS(log_history_handler)

		void write(
			fmt::memory_buffer const& fmted_log_rec, std::chrono::nanoseconds,
			quill::LogLevel log_msg_severity) override
		{
			const std::scoped_lock lock(mtx);

			// Split the given buffer by newlines for the ImGui clipper

			size_t next = 0, last = 0;
			const std::string string(fmted_log_rec.begin(), fmted_log_rec.size() - 1);
			std::string token;

			while ((next = string.find('\n', last)) != std::string::npos)
			{
				// token = string.substr(last, next - last);
				entries.push_back({ .text = string.substr(last, next - last),
									.level = log_msg_severity });
				last = next + 1;
			}

			entries.push_back({ .text = string.substr(last), .level = log_msg_severity });
		}

		// Called periodically, or when no more LOG_* writes remain to process.
		void flush() noexcept override { std::cout << std::flush; }

		[[nodiscard]] std::vector<log_entry> unload() noexcept
		{
			const std::scoped_lock lock(mtx);
			auto ret = std::move(entries);
			entries = std::vector<log_entry>();
			return ret;
		}
	};

	extern quill::Handler* quillhandler_file;
	extern quill::Handler* quillhandler_stdout;
	extern quill::Handler* quillhandler_history;

	extern quill::Logger* qlog;

	/**
	 * @brief Prepare for logging via Quill.
	 *
	 * Calls `quill::enable_console_colours()`, `quill::start()`,
	 * and `quill::preallocate()` before finally constructing handlers and the
	 * logger object.
	 */
	void log_init();
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
#define MXN_LOGF(msg, ...) LOG_INFO(mxn::qlog, msg, ##__VA_ARGS__)
/** @brief Like `MXN_WARN`, but allows {fmt}-style variadic formatting. */
#define MXN_WARNF(msg, ...) LOG_WARNING(mxn::qlog, msg, ##__VA_ARGS__)
/** @brief Like `MXN_ERR`, but allows {fmt}-style variadic formatting. */
#define MXN_ERRF(msg, ...) LOG_ERROR(mxn::qlog, msg, ##__VA_ARGS__)
/** @brief Like `MXN_CRIT`, but allows {fmt}-style variadic formatting. */
#define MXN_CRITF(msg, ...) LOG_CRITICAL(mxn::qlog, msg, ##__VA_ARGS__)

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