/**
 * @file console.hpp
 * @brief The developer/debugging console GUI, powered by ImGui.
 */

#pragma once

#include "preproc.hpp"

#include <concurrentqueue/concurrentqueue.h>
#include <mutex>
#include <functional>
#include <quill/Quill.h>
#include <string>
#include <vector>

struct ImGuiInputTextCallbackData;

namespace mxn
{
	class console final : public quill::Handler
	{
	public:
		using command_t = std::function<void(const std::vector<std::string>&)>;

		struct entry final
		{
			const std::string text;
			const quill::LogLevel level;
		};

	private:
		struct command final
		{
			/// The string given by the user to invoke this command.
			const std::string key = nullptr;
			/// Takes `cmd arg1 arg2` split into { `cmd`, `arg1`, `arg2` }.
			const command_t func;
			/// Takes `cmd arg1 arg2` split into { `cmd`, `arg1`, `arg2` }.
			const command_t help;
		};

		bool is_open = false, just_opened = false;

		char input_buffer[256];
		int history_pos = -1;
		bool auto_scroll = true, scroll_to_bottom = true;

		/// Locked by the Quill backend during writes and by `draw()`.
		std::mutex log_mtx;
		
		/// Commands submitted by the render thread, pending execution.
		moodycamel::ConcurrentQueue<std::string> cmd_queue;

		/// Allow user to quickly re-run past commands.
		std::vector<std::string> history;

		/// Stores everything written to the Quill logger.
		std::vector<entry> entries;

		std::vector<command> commands;

		void run_command(const std::string& key);
		void clear_storage();
		int text_edit(ImGuiInputTextCallbackData* const);

		void write(const fmt::memory_buffer&, std::chrono::nanoseconds, quill::LogLevel) override;

		/// @brief Called periodically, or when no more LOG_* writes remain to process.
		void flush() noexcept override { std::cout << std::flush; }

	public:
		console();
		~console() = default;
		DELETE_COPIERS_AND_MOVERS(console)

		/// @note The given command's key must have no whitespace.
		void add_command(command);
		void toggle() noexcept;
		void draw();

		/// @brief Call from the logic thread rather than the render thread.
		void run_pending_commands();
	};
}
