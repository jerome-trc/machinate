/**
 * @file media.hpp
 * @brief Interfaces for SDL2 and ImGui.
 */

#pragma once

#include "preproc.hpp"

#include <Aulib/Stream.h>
#include <mutex>
#include <filesystem>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/vec3.hpp>
#include <physfs.h>
#include <string>
#include <thread>
#include <vector>

struct SDL_Window;
union SDL_Event;

namespace mxn
{
	struct camera final
	{
		struct
		{
			glm::vec3 position, vel_linear, vel_angular;
			glm::quat rotation;
		} camera;

		// In the future, this may contain shading info., etc.
	
		[[nodiscard]] glm::mat4 view_matrix() const
		{
			return glm::transpose(glm::toMat4(camera.rotation)) *
				   glm::translate(glm::mat4(1.0f), -camera.position);
		}
	};

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

		bool alive;
		std::thread audio_worker;
		std::mutex audio_mutex;
		std::unordered_map<std::string, std::vector<unsigned char>> audiomem;
		std::vector<std::unique_ptr<Aulib::Stream>> sfx;
		std::optional<Aulib::Stream> music;

		static PHYSFS_EnumerateCallbackResult load_audio_memory(
			void* data, const char* orig_dir, const char* fname);

	public:
		media_context();
		~media_context();
		DELETE_COPIERS_AND_MOVERS(media_context)

		void stop_all_sound();
		void play_sound(const std::filesystem::path&,
			float volume = 1.0f, float pan = 0.0f);

		void stop_music();
		void play_music(const std::filesystem::path&);
	};
} // namespace mxn
