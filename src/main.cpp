/** @file main.cpp */

#include "console.hpp"
#include "file.hpp"
#include "log.hpp"
#include "media.hpp"
#include "script.hpp"
#include "src/defines.hpp"
#include "string.hpp"
#include "time.hpp"
#include "vk/context.hpp"

#include <SDL2/SDL.h>
#include <Tracy.hpp>
#include <imgui_impl_sdl.h>
#include <imgui_impl_vulkan.h>

int main(const int arg_c, const char* const argv[])
{
	if (arg_c <= 0)
	{
		throw std::invalid_argument("`main()` requires at least the executable name.");
	}

	tracy::SetThreadName("MXN: Main");

	auto qh_stdout = quill::stdout_handler();
	qh_stdout->set_pattern(
		QUILL_STRING("%(ascii_time) [%(thread)] %(filename):%(lineno) "
					 "%(level_name): %(message)"),
		"%H:%M:%S");
	mxn::console* console =
		dynamic_cast<mxn::console*>(quill::create_handler<mxn::console>("console"));
	console->set_pattern(QUILL_STRING("%(level_name): %(message)"));
	mxn::log_init({ qh_stdout, console });

	MXN_LOGF(
		"Machinate version {}.{}.{}", Machinate_VERSION_MAJOR, Machinate_VERSION_MINOR,
		Machinate_VERSION_PATCH);

	// Seed `rand()` for trivial RNG uses
	const auto curtime = std::chrono::system_clock::now().time_since_epoch().count();
	const auto curtime_uint = static_cast<unsigned int>(curtime);
	srand(curtime_uint);

	mxn::vfs_init(argv[0]);
	mxn::vfs_mount("assets", "/");

	mxn::media_context media;
	mxn::window main_window("Machinate");
	mxn::vk::context vulkan(main_window.get_sdl_window());

	mxn::camera camera;
	mxn::vk::ubo<mxn::vk::camera> vk_cam(vulkan, "MXN: UBO, Camera");

	// Script backend initialisation
	NEED_ALL_DEFAULT_MODULES;
	NEED_MODULE(script_core);
	das::Module::Initialize();

	bool running = true;
	bool draw_imgui_metrics = true;
	const ImGuiIO& imgui_io = ImGui::GetIO();

	// Developer/debug console initialisation
	console->add_command({ .key = "vkdiag",
						   .func = [&](const std::vector<std::string>& args) -> void {
							   vulkan.vkdiag(std::move(args));
						   },
						   .help = [](const std::vector<std::string>& args) -> void {
							   MXN_LOG(
								   "Print information about the Vulkan renderer or this "
								   "system's Vulkan implementation.");
							   MXN_LOG("Usage: vkdiag ext|gpu|queue");
						   } });
	console->add_command(
		{ .key = "file",
		  .func = [&](const std::vector<std::string>& args) -> void {
			  mxn::ccmd_file(args.size() > 1 ? args[1] : "/");
		  },
		  .help = [](const std::vector<std::string>&) -> void {
			  MXN_LOG("List the contents of a directory in the virtual file system.");
		  } });
	console->add_command({ .key = "sound",
						   .func = [&](const std::vector<std::string>& args) -> void {
							   if (args.size() == 1)
								   return;
							   else if (args[1] == "~" || args[1] == "!")
								   media.stop_all_sound();
							   else
								   media.play_sound(args[1]);
						   },
						   .help = [](const std::vector<std::string>&) -> void {
							   MXN_LOGF(
								   "Usage: sound <arg>\n{}",
								   "If <arg> is \"~\" or \"!\", all sound is stopped.");
						   } });
	console->add_command(
		{ .key = "music",
		  .func = [&](const std::vector<std::string>& args) -> void {
			  if (args.size() == 1) { }
			  else if (args[1] == "~" || args[1] == "!")
				  media.stop_music();
			  else
				  media.play_music(args[1]);
		  },
		  .help = [](const std::vector<std::string>&) -> void {
			  MXN_LOGF(
				  "Usage: music <arg>\n{}\n{}",
				  "If no <arg> is given, the path of the current music is printed.",
				  "If <arg> is \"~\" or \"!\", the current music is stopped.");
		  } });

	std::thread render_thread([&]() -> void {
		tracy::SetThreadName("MXN: Render");

		do
		{
			main_window.new_imgui_frame();

			if (draw_imgui_metrics) ImGui::ShowMetricsWindow(&draw_imgui_metrics);

			console->draw();

			vk_cam.data.update(vulkan, camera);
			vk_cam.update(vulkan);

			if (!vulkan.start_render())
				vulkan.rebuild_swapchain(main_window.get_sdl_window());

			vulkan.set_camera(vk_cam);

			vulkan.start_render_record();
			vulkan.end_render_record();

			const auto& sema_depth = vulkan.submit_prepass({});
			const auto& sema_lc = vulkan.compute_lightcull(sema_depth);
			const auto& sema_geom = vulkan.submit_geometry(sema_lc);
			const auto& sema_imgui = vulkan.render_imgui(sema_geom);

			if (!vulkan.present_frame(sema_imgui))
				vulkan.rebuild_swapchain(main_window.get_sdl_window());
		} while (running);

		vulkan.device.waitIdle();
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
				if (!main_window.valid()) running = false;
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
				case SDLK_BACKQUOTE: console->toggle(); break;
				// TODO: Binding immediate termination to escape is temporary
				case SDLK_ESCAPE: running = false; break;
				default: break;
				} // switch (event.key.keysym.sym)
				break;
			}
			default: break;
			} // switch (event.type)
		} // while (SDL_PollEvent(&event) != 0)

		console->run_pending_commands();
	} while (running);

	render_thread.join();

	vk_cam.destroy(vulkan);

	das::Module::Shutdown();
	MXN_LOGF("Runtime duration: {}", mxn::runtime_s());
	mxn::vfs_deinit();
	quill::flush();
	return 0;
}
