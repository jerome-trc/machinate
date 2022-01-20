/**
 * @file console.hpp
 * @brief The developer/debugging console GUI, powered by ImGui.
 */

#pragma once

#include "preproc.hpp"

#include <mutex>
#include <functional>
#include <string>
#include <vector>

struct ImGuiInputTextCallbackData;

namespace mxn
{
	class console final
	{
	public:
		enum class line_type_t : unsigned char
		{
			PLAIN, /// Text is rendered in white.
			INFO, /// Text is rendered in grey.
			WARNING, /// Text is rendered in yellow.
			USER_ERROR, /// Text is rendered in orange.
			ERROR, /// Text is rendered in red.
			CRITICAL, /// Text is rendered in purple.
			DEBUG, /// Text is rendered in dark teal.
			HELP, /// Text is rendered in cyan.
			INPUT /// Text is rendered in green.
		};

	private:
		struct entry final
		{
			const std::string text = nullptr;
			const line_type_t type = line_type_t::PLAIN;
		};

		struct command final
		{
			const std::string key = nullptr;
			/// Takes `cmd arg1 arg2` split into { `cmd`, `arg1`, `arg2` }.
			const std::function<void(const std::vector<std::string>)> func;
			const std::function<void(const std::vector<std::string>)> help;
		};

		bool is_open = false, just_opened = false;

		char input_buffer[256];
		int history_pos = -1;
		bool auto_scroll = true, scroll_to_bottom = true;

		std::mutex mutex;
		std::vector<std::string> pending, history;
		std::vector<entry> items;
		std::vector<command> commands;

		void run_command(const std::string& key);
		int text_edit(ImGuiInputTextCallbackData* const);

	public:
		console();
		~console() = default;
		DELETE_COPIERS_AND_MOVERS(console)

		void add_command(command);
		void toggle() noexcept;
		void draw();

		/** @brief Call from the logic thread rather than the render thread. */
		void run_pending_commands();
		void retrieve_logs();
	};
}
