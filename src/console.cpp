/**
 * @file console.cpp
 * @brief The developer/debugging console GUI, powered by ImGui.
 */

#include "console.hpp"

#include "colour.hpp"
#include "log.hpp"
#include "string.hpp"

#include <imgui.h>

mxn::console::console()
{
	memset(input_buffer, 0, sizeof(input_buffer));

	const auto cmdfunc_help = [&](const std::vector<std::string>& args) -> void {
		if (args.size() <= 1)
		{
			std::string output = "Available commands:";

			for (const auto& cmd : this->commands)
			{
				output += '\n';
				output += '\t';
				output += cmd.key;
			}

			MXN_LOG(output);
			return;
		}

		const auto iter =
			std::find_if(commands.begin(), commands.end(), [&args](command& cmd) -> bool {
				return cmd.key == args[1];
			});

		if (iter == commands.end())
		{
			MXN_LOGF("Command `{}` not found.", args[1]);
			return;
		}

		std::vector<std::string> new_args { args[0], iter->key };

		for (size_t i = 1; i < args.size(); i++) new_args.push_back(args[i]);

		iter->help(new_args);
	};

	const auto helpfunc_help = [&](const std::vector<std::string>& args) -> void {
		if (args.size() <= 1)
		{
			MXN_LOG(
				"Lists all available console commands. Add the name of "
				"another command afterward to print help on that command.");
			return;
		}
	};

	commands.push_back({ .key = "help", .func = cmdfunc_help, .help = helpfunc_help });
	commands.push_back({ .key = "?", .func = cmdfunc_help, .help = helpfunc_help });
}

void mxn::console::toggle() noexcept
{
	is_open = !is_open;
	if (is_open) just_opened = true;
}

void mxn::console::draw()
{
	if (!is_open) return;

	ImGui::SetNextWindowSize(ImVec2(520, 600), ImGuiCond_FirstUseEver);

	if (!ImGui::Begin("Console", &is_open))
	{
		ImGui::End();
		return;
	}

	if (ImGui::BeginPopupContextItem())
	{
		if (ImGui::MenuItem("Close")) is_open = false;
		ImGui::EndPopup();
	}

	// Reserve enough left-over height for 1 separator + 1 input text
	const float footer_height_to_reserve =
		ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing();
	ImGui::BeginChild(
		"ScrollingRegion", ImVec2(0.0f, -footer_height_to_reserve), false,
		ImGuiWindowFlags_HorizontalScrollbar);

	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 1.0f));

	ImGuiListClipper clipper;
	log_mtx.lock();
	clipper.Begin(entries.size());

	while (clipper.Step())
	{
		for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++)
		{
			ImVec4 colour = ImVec4(0.8f, 0.8f, 0.8f, 1.0f);

			switch (entries[i].level)
			{
			case quill::LogLevel::Info:
				colour.x = mxn::GREEN_F[0];
				colour.y = mxn::GREEN_F[1];
				colour.z = mxn::GREEN_F[2];
				break;
			case quill::LogLevel::Warning:
				colour.x = mxn::YELLOW_F[0];
				colour.y = mxn::YELLOW_F[1];
				colour.z = mxn::YELLOW_F[2];
				break;
			case quill::LogLevel::Error:
				colour.x = mxn::RED_F[0];
				colour.y = mxn::RED_F[1];
				colour.z = mxn::RED_F[2];
				break;
			case quill::LogLevel::Critical:
				colour.x = mxn::PINK_F[0];
				colour.y = mxn::PINK_F[1];
				colour.z = mxn::PINK_F[2];
				break;
			case quill::LogLevel::Backtrace:
			case quill::LogLevel::TraceL3:
			case quill::LogLevel::TraceL2:
			case quill::LogLevel::TraceL1:
			case quill::LogLevel::Debug:
				colour.x = mxn::TEAL_F[0];
				colour.y = mxn::TEAL_F[1];
				colour.z = mxn::TEAL_F[2];
				break;
			case quill::LogLevel::None:
			default: break;
			}

			ImGui::PushStyleColor(ImGuiCol_Text, colour);
			ImGui::TextUnformatted(entries[i].text.c_str());
			ImGui::PopStyleColor();
		}
	}
	log_mtx.unlock();

	if (scroll_to_bottom ||
		(auto_scroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()))
		ImGui::SetScrollHereY(1.0f);

	scroll_to_bottom = false;

	ImGui::PopStyleVar();
	ImGui::EndChild();
	ImGui::Separator();

	// Input field begins
	// Focus on text input when console is first opened
	bool reclaim_focus = false;
	if (just_opened)
	{
		reclaim_focus = true;
		just_opened = false;
	}

	static constexpr ImGuiInputTextFlags INPUT_TEXT_FLAGS =
		ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackCompletion |
		ImGuiInputTextFlags_CallbackHistory;

	if (ImGui::InputText(
			"##", input_buffer, std::size(input_buffer), INPUT_TEXT_FLAGS,
			[](ImGuiInputTextCallbackData* data) -> int {
				auto console = reinterpret_cast<mxn::console*>(data->UserData);
				return console->text_edit(data);
			},
			(void*)this))
	{
		char* s = input_buffer;
		if (s[0]) cmd_queue.enqueue(s);
		strcpy(s, "");
		reclaim_focus = true;
	}

	ImGui::SameLine();
	if (ImGui::Button("Submit"))
	{
		char* s = input_buffer;
		if (s[0]) cmd_queue.enqueue(s);
		strcpy(s, "");
		reclaim_focus = true;
	}

	ImGui::SameLine();
	if (ImGui::Button("Clear"))
	{
		clear_storage();
	}

	// Auto-focus on window apparition
	ImGui::SetItemDefaultFocus();
	if (reclaim_focus) ImGui::SetKeyboardFocusHere(-1); // Auto-focus previous widget

	ImGui::End();
}

void mxn::console::add_command(command cmd)
{
	assert(!cmd.key.empty());

	for (size_t i = 0; i < cmd.key.length(); i++)
		assert(!isspace(cmd.key.at(i)));

	assert(cmd.func != nullptr);
	commands.push_back(cmd);
}

void mxn::console::run_pending_commands()
{
	std::string cmd;

	while (cmd_queue.try_dequeue(cmd))
		run_command(cmd);
}

// Private implementation details //////////////////////////////////////////////

void mxn::console::write(const fmt::memory_buffer& rec, std::chrono::nanoseconds,
	quill::LogLevel level)
{
	// Split the given buffer by newlines for the ImGui clipper

	size_t next = 0, last = 0;
	const std::string string(rec.begin(), rec.size() - 1);
	std::string token;

	const std::scoped_lock lock(log_mtx);

	while ((next = string.find('\n', last)) != std::string::npos)
	{
		entries.push_back({ .text = string.substr(last, next - last),
							.level = level });
		last = next + 1;
	}

	entries.push_back({ .text = string.substr(last), .level = level });
}

void mxn::console::run_command(const std::string& string)
{
	if (string == "clear")
	{
		clear_storage();
		return;
	}

	MXN_LOGF("$ {}", string);

	history_pos = -1;

	for (int i = history.size() - 1; i >= 0; i--)
		if (history[i] == string)
		{
			history.erase(history.begin() + i);
			break;
		}

	history.push_back(string);

	std::vector<std::string> args = str_split(string, ' ');
	args[0] = str_tolower(args[0]);

	for (const auto& cmd : commands)
	{
		if (cmd.key == args[0])
		{
			cmd.func(args);
			scroll_to_bottom = true;
			return;
		}
	}

	// Failed to find the command

	scroll_to_bottom = true;
	MXN_LOGF("Unknown command: {}", args[0]);
}

void mxn::console::clear_storage()
{
	const std::scoped_lock lock(log_mtx);
	entries.clear();
	history.clear();
}

int mxn::console::text_edit(ImGuiInputTextCallbackData* const data)
{
	switch (data->EventFlag)
	{
	case ImGuiInputTextFlags_CallbackHistory:
		const int prev_history_pos = history_pos;
		if (data->EventKey == ImGuiKey_UpArrow)
		{
			if (history_pos == -1)
				history_pos = history.size() - 1;
			else if (history_pos > 0)
				history_pos--;
		}
		else if (data->EventKey == ImGuiKey_DownArrow)
		{
			if (history_pos != -1)
				if (static_cast<uint32_t>(++history_pos) >= history.size())
					history_pos = -1;
		}

		// TODO: preserve data on current input line, along with cursor position
		if (prev_history_pos != history_pos)
		{
			const std::string history_str =
				(history_pos >= 0) ? history[history_pos] : "";
			data->DeleteChars(0, data->BufTextLen);
			data->InsertChars(0, history_str.c_str());
		}
		break;
	}
	return 0;
}
