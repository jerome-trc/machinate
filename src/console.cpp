/**
 * @file console.cpp
 * @brief The developer/debugging console GUI, powered by ImGui.
 */

#include "console.hpp"

#include "colour.hpp"
#include "log.hpp"
#include "string.hpp"

#include <imgui.h>

#ifdef ERROR // WinGDI macro conflicts with line_type_t::ERROR
#undef ERROR
#endif

mxn::console::console()
{
	memset(input_buffer, 0, sizeof(input_buffer));

	const auto cmdfunc_help = [&](const std::vector<std::string> args) -> void {
		if (args.size() <= 1)
		{
			MXN_LOG("Available commands:");

			for (const auto& cmd : this->commands) MXN_LOG(cmd.key);

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

	const auto helpfunc_help = [&](const std::vector<std::string> args) -> void {
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
	clipper.Begin(items.size());

	while (clipper.Step())
	{
		for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++)
		{
			ImVec4 colour = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);

			switch (items[i].type)
			{
			case line_type_t::PLAIN:
				// Keep the pre-initialised white
				break;
			case line_type_t::INFO:
				colour = ImVec4(mxn::OFFWHITE_F[0], mxn::OFFWHITE_F[1], mxn::OFFWHITE_F[2], 1.0f);
				break;
			case line_type_t::ERROR:
				colour = ImVec4(mxn::RED_F[0], mxn::RED_F[1], mxn::RED_F[2], 1.0f);
				break;
			case line_type_t::HELP:
				colour = ImVec4(mxn::CYAN_F[0], mxn::CYAN_F[1], mxn::CYAN_F[2], 1.0f);
				break;
			case line_type_t::INPUT:
				colour = ImVec4(mxn::GREEN_F[0], mxn::GREEN_F[1], mxn::GREEN_F[2], 1.0f);
				break;
			default:
				// Write in pink to indicate an unexpected result
				colour = ImVec4(mxn::PINK_F[0], mxn::PINK_F[1], mxn::PINK_F[2], 1.0f);
				break;
			}

			ImGui::PushStyleColor(ImGuiCol_Text, colour);
			ImGui::TextUnformatted(items[i].text.c_str());
			ImGui::PopStyleColor();
		}
	}

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
		if (s[0])
		{
			std::scoped_lock lock(mutex);
			pending.emplace_back(s);
		}
		strcpy(s, "");
		reclaim_focus = true;
	}

	ImGui::SameLine();
	if (ImGui::Button("Submit"))
	{
		char* s = input_buffer;
		if (s[0])
		{
			std::scoped_lock lock(mutex);
			pending.emplace_back(s);
		}
		strcpy(s, "");
		reclaim_focus = true;
	}

	ImGui::SameLine();
	if (ImGui::Button("Clear"))
	{
		items.clear();
		history.clear();
	}

	// Auto-focus on window apparition
	ImGui::SetItemDefaultFocus();
	if (reclaim_focus) ImGui::SetKeyboardFocusHere(-1); // Auto-focus previous widget

	ImGui::End();
}

void mxn::console::add_command(command cmd)
{
	assert(!cmd.key.empty());
	assert(cmd.func != nullptr);
	commands.push_back(cmd);
}

void mxn::console::run_pending_commands()
{
	std::scoped_lock lock(mutex);

	for (const auto& p : pending) run_command(p);

	pending.clear();
}

void mxn::console::retrieve_logs()
{
	for (const auto& entry :
		 static_cast<log_history_handler*>(quillhandler_history)->unload())
	{
		line_type_t lt = line_type_t::PLAIN;

		switch (entry.level)
		{
		case quill::LogLevel::Warning: lt = line_type_t::WARNING; break;
		case quill::LogLevel::Error: lt = line_type_t::ERROR; break;
		case quill::LogLevel::Critical: lt = line_type_t::CRITICAL; break;
		case quill::LogLevel::Debug: lt = line_type_t::DEBUG; break;
		case quill::LogLevel::Info: lt = line_type_t::INFO; break;
		case quill::LogLevel::None: lt = line_type_t::PLAIN; break;
		default: assert(false); break;
		}

		items.push_back({ .text = entry.text, .type = lt });
	}
}

// Private implementation details //////////////////////////////////////////////

void mxn::console::run_command(const std::string& string)
{
	if (string == "clear")
	{
		items.clear();
		history.clear();
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
			cmd.func(std::move(args));
			scroll_to_bottom = true;
			return;
		}
	}

	// Failed to find the command

	scroll_to_bottom = true;
	MXN_LOGF("Unknown command: {}", args[0]);
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
