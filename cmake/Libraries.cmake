# Brief: Find packages installed via vcpkg and define the list of libraries.

set(MXN_DAS_OPTIONS
	"DAS_BGFX_DISABLED ON"
	"DAS_CLANG_BIND_DISABLED ON"
	"DAS_MINFFT_DISABLED ON"
	"DAS_GLFW_DISABLED ON"
	"DAS_HV_DISABLED ON"
	"DAS_IMGUI_DISABLED ON"
	"DAS_SFML_DISABLED ON"
	"DAS_SOUND_DISABLED ON"
	"DAS_STBIMAGE_DISABLED ON"
	"DAS_STBTRUETYPE_DISABLED ON"
	"DAS_STDDLG_DISABLED ON"
	"DAS_XBYAK_DISABLED ON"

	"DAS_BUILD_PROFILE NO"
	"DAS_BUILD_TEST NO"
	"DAS_BUILD_TUTORIAL NO"
	
	"CMAKE_POSITION_INDEPENDENT_CODE ON"
)

CPMAddPackage(
	NAME daScript
	GITHUB_REPOSITORY GaijinEntertainment/daScript
	GIT_TAG master
	OPTIONS ${MXN_DAS_OPTIONS}
)

find_package(fmt CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(magic_enum CONFIG REQUIRED)
find_package(PhysFS REQUIRED)
find_package(Quill CONFIG REQUIRED)
find_package(SDL2 CONFIG REQUIRED)
find_package(Vulkan REQUIRED)
find_package(unofficial-vulkan-memory-allocator CONFIG REQUIRED)

set(MXN_LIBS
	glm::glm
	imgui::imgui
	libDaScript
	libDaScriptProfile libDaScriptTest Threads::Threads ${DAS_MODULES_LIBS}
	magic_enum::magic_enum
	${PHYSFS_LIBRARY}
	quill::quill
	SDL2::SDL2main SDL2::SDL2-static
	Vulkan::Vulkan
	unofficial::vulkan-memory-allocator::vulkan-memory-allocator
)

if(UNIX)
	list(APPEND MXN_LIBS dl)
endif()
