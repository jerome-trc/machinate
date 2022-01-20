/**
 * @file media.hpp
 * @brief Interfaces for SDL2 and ImGui.
 */

#pragma once

#include "preproc.hpp"

#include <string>
#include <vector>

struct SDL_Window;
union SDL_Event;

namespace mxn
{
	class window final
	{
		static constexpr int DEFAULT_WINDOW_WIDTH = 800, DEFAULT_WINDOW_HEIGHT = 600;

		enum class status_t : unsigned char
		{
			NONE = 0,
			MOUSE_FOCUS = 1 << 0,
			KEYBOARD_FOCUS = 1 << 1,
			MINIMISED = 1 << 2,
			SHOWN = 1 << 3
		} status = status_t::NONE;

		SDL_Window* windowptr = nullptr;
		uint32_t id = 0;
		/// Obtained from `SDL_Vulkan_GetDrawableSize()`.
		int size_x = -1, size_y = -1;

	public:
		window(
			const std::string& name, int res_x = DEFAULT_WINDOW_WIDTH,
			int res_y = DEFAULT_WINDOW_HEIGHT) noexcept;
		~window() noexcept;
		DELETE_COPIERS_AND_MOVERS(window)

		[[nodiscard]] constexpr SDL_Window* get_sdl_window() const noexcept
		{
			return windowptr;
		}

		[[nodiscard]] constexpr bool valid() const noexcept
		{
			return windowptr != nullptr;
		}

		void new_imgui_frame();
		void handle_event(const SDL_Event&) noexcept;
	};

	class media_context final
	{
		const uint8_t* key_states = nullptr;
		int keystate_c = 0;

	public:
		media_context();
		~media_context() noexcept;
		DELETE_COPIERS_AND_MOVERS(media_context)
	};
} // namespace mxn
