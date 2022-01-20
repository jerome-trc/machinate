/**
 * @file media.cpp
 * @brief Interfaces for SDL2 and ImGui.
 */

#include "media.hpp"

#include "log.hpp"

#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
#include <cassert>
#include <imgui_impl_sdl.h>
#include <imgui_impl_vulkan.h>
#include <stdexcept>

static constexpr uint32_t SDL_INIT_FLAGS = SDL_INIT_VIDEO | SDL_INIT_EVENTS;

mxn::window::window(const std::string& name, int res_x, int res_y) noexcept
{
	assert(SDL_WasInit(SDL_INIT_FLAGS) == SDL_INIT_FLAGS);

	windowptr = SDL_CreateWindow(
		name.c_str(), SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, res_x, res_y,
		SDL_WINDOW_RESIZABLE | SDL_WINDOW_VULKAN);

	id = SDL_GetWindowID(windowptr);
	SDL_Vulkan_GetDrawableSize(windowptr, &size_x, &size_y);

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui::StyleColorsDark();
	ImGui::GetIO().FontAllowUserScaling = true;
}

mxn::window::~window() noexcept { SDL_DestroyWindow(windowptr); }

void mxn::window::new_imgui_frame()
{
	ImGui_ImplVulkan_NewFrame();
	ImGui_ImplSDL2_NewFrame(windowptr);
	ImGui::NewFrame();
}

void mxn::window::handle_event(const SDL_Event& event) noexcept
{
	assert(event.type == SDL_EventType::SDL_WINDOWEVENT);
	if (event.window.windowID != id) return;

	switch (event.window.type)
	{
	case SDL_WindowEventID::SDL_WINDOWEVENT_CLOSE:
		SDL_DestroyWindow(windowptr);
		windowptr = nullptr;
		break;
	case SDL_WindowEventID::SDL_WINDOWEVENT_RESIZED:
	case SDL_WindowEventID::SDL_WINDOWEVENT_SIZE_CHANGED:
		SDL_Vulkan_GetDrawableSize(windowptr, &size_x, &size_y);
		break;
	default: break;
	}
}

mxn::media_context::media_context()
{
	// Latch
	assert(SDL_WasInit(0) == 0);

	const bool success = SDL_Init(SDL_INIT_FLAGS) >= 0;
	if (!success)
	{
		throw std::runtime_error(
			fmt::format("SDL2 initialisation failed: {}", SDL_GetError()));
	}

	key_states = SDL_GetKeyboardState(&keystate_c);
}

mxn::media_context::~media_context() noexcept
{
	assert(SDL_WasInit(SDL_INIT_FLAGS) == SDL_INIT_FLAGS);
	ImGui_ImplSDL2_Shutdown();
	SDL_Quit();
}
