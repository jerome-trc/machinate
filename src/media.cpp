/**
 * @file media.cpp
 * @brief Interfaces for SDL2 and ImGui.
 */

#include "media.hpp"

#include "file.hpp"
#include "log.hpp"

#include <Aulib/Decoder.h>
#include <Aulib/ResamplerSpeex.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
#include <Tracy.hpp>
#include <cassert>
#include <imgui_impl_sdl.h>
#include <imgui_impl_vulkan.h>
#include <stdexcept>

static constexpr uint32_t SDL_INIT_FLAGS =
	SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_AUDIO;

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

PHYSFS_EnumerateCallbackResult mxn::media_context::load_audio_memory(
	void* data, const char* orig_dir, const char* fname)
{
	char p[256];
	strcpy(p, orig_dir);
	strcat(p, "/");
	strcat(p, fname);

	const std::filesystem::path path(p);

	if (vfs_isdir(p))
	{
		vfs_recur(p, data, load_audio_memory);
		return PHYSFS_ENUM_OK;
	}

	const auto buf = vfs_read(path);

	SDL_RWops* rw = SDL_RWFromConstMem(
		reinterpret_cast<const void*>(buf.data()),
		buf.size() * sizeof(decltype(buf)::value_type));

	if (Aulib::Decoder::decoderFor(rw) == nullptr)
		return PHYSFS_ENUM_OK;

	auto audiomem = reinterpret_cast<decltype(media_context::audiomem)*>(data);
	(*audiomem)[path.string()] = buf;
	return PHYSFS_ENUM_STOP;
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

	if (!Aulib::init(44100, AUDIO_S16SYS, 2, 8192))
	{ MXN_ERRF("Failed to initialise audio: {}", SDL_GetError()); }

	key_states = SDL_GetKeyboardState(&keystate_c);

	alive = true;

	vfs_recur("", reinterpret_cast<void*>(&audiomem), load_audio_memory);

	audio_worker = std::thread([&]() -> void {
		tracy::SetThreadName("Audio Worker");

		while (alive)
		{
			audio_mutex.lock();

			sfx.erase(std::remove_if(
				sfx.begin(), sfx.end(),
				[](const std::unique_ptr<Aulib::Stream>& stream) -> bool {
					return !stream->isPlaying();
				}), sfx.end());

			audio_mutex.unlock();

			using namespace std::chrono_literals;
			std::this_thread::sleep_for(200ms);
		}
	});
}

mxn::media_context::~media_context()
{
	assert(SDL_WasInit(SDL_INIT_FLAGS) == SDL_INIT_FLAGS);

	alive = false;
	audio_worker.join();

	for (auto& stream : sfx) stream->stop();

	sfx.clear();

	if (music.has_value()) { music->stop(); }

	ImGui_ImplSDL2_Shutdown();
	Aulib::quit();
	SDL_Quit();
}

void mxn::media_context::stop_all_sound()
{
	std::scoped_lock lock(audio_mutex);

	for (auto& stream : sfx) stream->stop();

	sfx.clear();
}

void mxn::media_context::play_sound(const std::filesystem::path& path,
	float volume, float pan)
{
	const auto mem = audiomem.find(path.string());
	if (mem == audiomem.end())
	{
		MXN_ERRF("Tried to play sound from non-existent file: {}", path.string());
		return;
	}

	SDL_RWops* rw = SDL_RWFromConstMem(
		reinterpret_cast<const void*>(mem->second.data()),
		mem->second.size() * sizeof(decltype(mem->second)::value_type));

	auto decoder = Aulib::Decoder::decoderFor(rw);

	if (decoder == nullptr)
	{
		MXN_ERRF("No decoder exists for audio file: {}", path.string());
		return;
	}

	audio_mutex.lock();
	sfx.push_back(std::make_unique<Aulib::Stream>(
		rw, std::move(decoder), std::make_unique<Aulib::ResamplerSpeex>(), true));
	sfx.back()->play();
	sfx.back()->setVolume(volume);
	sfx.back()->setStereoPosition(pan);
	audio_mutex.unlock();
}

void mxn::media_context::stop_music()
{
	music.reset();
}

void mxn::media_context::play_music(const std::filesystem::path& path)
{
	const auto mem = audiomem.find(path.string());
	if (mem == audiomem.end())
	{
		MXN_ERRF("Tried to play music from non-existent file: {}", path.string());
		return;
	}

	SDL_RWops* rw = SDL_RWFromConstMem(
		reinterpret_cast<const void*>(mem->second.data()),
		mem->second.size() * sizeof(decltype(mem->second)::value_type));

	auto decoder = Aulib::Decoder::decoderFor(rw);

	if (decoder == nullptr)
	{
		MXN_ERRF("No decoder exists for audio file: {}", path.string());
		return;
	}

	audio_mutex.lock();
	music.reset();

	auto& stream = music.emplace(
		rw, std::move(decoder), std::make_unique<Aulib::ResamplerSpeex>(), true);
	
	if (!stream.play(0))
		MXN_ERRF("Failed to start music: {}", SDL_GetError());

	audio_mutex.unlock();
}
