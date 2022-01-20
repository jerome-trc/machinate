/** @file main.cpp */

#include "console.hpp"
#include "file.hpp"
#include "log.hpp"
#include "media.hpp"
#include "script.hpp"
#include "src/defines.hpp"
#include "string.hpp"
#include "time.hpp"
#include "vulkan.hpp"

#include <SDL2/SDL.h>
#include <imgui_impl_sdl.h>
#include <imgui_impl_vulkan.h>
#include <Tracy.hpp>

int main(const int arg_c, const char* const argv[])
{
	tracy::SetThreadName("Main");
	mxn::log_init();
	MXN_LOGF(
		"Machinate version {}.{}.{}", Machinate_VERSION_MAJOR, Machinate_VERSION_MINOR,
		Machinate_VERSION_PATCH);

	mxn::vfs_init(argv[0]);
	mxn::media_context media;
	mxn::window main_window("Machinate");
	mxn::vk::context renderer(main_window.get_sdl_window());

	// Script backend initialisation
	NEED_ALL_DEFAULT_MODULES;
	NEED_MODULE(script_core);
	das::Module::Initialize();

	bool running = true;
	bool draw_imgui_metrics = true;
	const ImGuiIO& imgui_io = ImGui::GetIO();

	// Developer/debug console initialisation
	mxn::console console;
	console.add_command(
		{ .key = "vkdiag",
		  .func = [&](const std::vector<std::string> args) -> void {
			  renderer.vkdiag(std::move(args));
		  },
		  .help = [&](const std::vector<std::string> args) -> void {
				MXN_LOG(
					"Print information about the Vulkan renderer or this "
					"system's Vulkan implementation.");
				MXN_LOG("Usage: vkdiag ext|gpu|queue");
		  } });

	std::thread render_thread([&]() -> void {
		tracy::SetThreadName("Render");
		do
		{
			main_window.new_imgui_frame();

			if (draw_imgui_metrics)
				ImGui::ShowMetricsWindow(&draw_imgui_metrics);

			console.draw();

			renderer.start_render();

			if (!renderer.finish_render())
				renderer.rebuild_swapchain(main_window.get_sdl_window());
		} while (running);
	});

	SDL_Event event = {};

	do
	{
		while (SDL_PollEvent(&event) != 0)
		{
			ImGui_ImplSDL2_ProcessEvent(&event);

			switch (event.type)
			{
			case SDL_QUIT: running = false; break;
			case SDL_WINDOWEVENT:
				main_window.handle_event(event);
				running = main_window.valid();
				break;
			case SDL_MOUSEMOTION:
			{
				if (!imgui_io.WantCaptureMouse) { } // TODO
				break;
			}
			case SDL_KEYDOWN:
			{
				if (imgui_io.WantCaptureKeyboard || imgui_io.WantTextInput) break;

				switch (event.key.keysym.sym)
				{
				case SDLK_BACKQUOTE:
					console.toggle();
					break;
				// TODO: Binding immediate termination to escape is temporary
				case SDLK_ESCAPE:
					running = false;
					break;
				default: break;
				} // switch (event.key.keysym.sym)
				break;
			}
			default: break;
			} // switch (event.type)
		} // while (SDL_PollEvent(&event) != 0)

		console.retrieve_logs();
		console.run_pending_commands();
	} while (running);

	render_thread.join();
	das::Module::Shutdown();
	MXN_LOGF("Runtime duration: {}", mxn::runtime_s());
	mxn::vfs_deinit();
	quill::flush();
	return 0;
}
