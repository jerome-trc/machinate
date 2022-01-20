cmake_minimum_required(VERSION 3.16)
project(Machinate
	VERSION 0.0.0
	DESCRIPTION "Transport Tycoon-like game"
	HOMEPAGE_URL "https://github.com/j-martina/machinate"
)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(cmake/CPM.cmake)

if(USE_CCACHE)
	CPMAddPackage(
		NAME Ccache.cmake
		GITHUB_REPOSITORY TheLartians/Ccache.cmake
		VERSION 1.2
	)
endif()

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "In-source builds not allowed. "
	"Please run \"cmake ..\" from the build directory. "
	"You may need to delete \"${CMAKE_SOURCE_DIR}/CMakeCache.txt\" first.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED YES)

# Compiler/linker options ######################################################

set(MXN_COMPILE_OPTIONS
	$<$<CXX_COMPILER_ID:GNU>:-Wall -Wextra -Wpedantic -fexceptions>
	$<$<CXX_COMPILER_ID:MSVC>:/Wall>
	$<$<CXX_COMPILER_ID:GNU>:
		"$<$<CONFIG:DEBUG>:-O0;-g3;-ggdb>"
		"$<$<CONFIG:RELEASE>:-O3;-Os>"
	>
	$<$<CXX_COMPILER_ID:MSVC>:
		"$<$<CONFIG:DEBUG>:/Od>"
		"$<$<CONFIG:RELEASE>:/O2>"
	>
)

# Preprocessor definitions #####################################################

set(MXN_COMPILE_DEFS
	GLM_FORCE_DEPTH_ZERO_TO_ONE # For Vulkan (OpenGL is -1.0 to 1.0)
	GLM_FORCE_RADIANS
	WIN32_LEAN_AND_MEAN
	TRACY_ENABLE
)

if(MSVC)
	# Disable Windows min/max macros
	list(APPEND MXN_COMPILE_DEFS NOMINMAX) 
endif()

# Dependencies: managed packages, embedded includes, subdirectories ############

set(THIRD_PARTY "${CMAKE_SOURCE_DIR}/src/include")

# Defines `MXN_LIBS`
include("${CMAKE_SOURCE_DIR}/cmake/Libraries.cmake")

if(MSVC)
	# winmm and imm32 are required by ImGui
	list(APPEND MXN_LIBS winmm imm32)
endif()

# Targets ######################################################################

add_executable(${PROJECT_NAME}
	"${CMAKE_SOURCE_DIR}/src/console.cpp"
	"${CMAKE_SOURCE_DIR}/src/main.cpp"
	"${CMAKE_SOURCE_DIR}/src/media.cpp"
	"${CMAKE_SOURCE_DIR}/src/utils.cpp"
	"${CMAKE_SOURCE_DIR}/src/vk_mem_alloc.cpp"
	"${CMAKE_SOURCE_DIR}/src/vulkan.cpp"
	"${CMAKE_SOURCE_DIR}/tracy/TracyClient.cpp"
)

target_compile_options(${PROJECT_NAME} PRIVATE ${MXN_COMPILE_OPTIONS})
target_compile_definitions(${PROJECT_NAME} PRIVATE
	${MXN_COMPILE_DEFS}
	MXN_APPNAME="${PROJECT_NAME}"
	MXN_USERPATH="Machinate"
)

target_include_directories(${PROJECT_NAME} PRIVATE ${PROJECT_BINARY_DIR})
target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE
	${THIRD_PARTY}
	# ${BULLET_INCLUDE_DIR}
	"${daScript_SOURCE_DIR}/include"
	${LUAJIT_INCLUDE_DIR}
	${PHYSFS_INCLUDE_DIR}
	"${CMAKE_SOURCE_DIR}/tracy"
)

target_link_libraries(${PROJECT_NAME} PRIVATE ${MXN_LIBS})

# CTest ########################################################################

set(BUILD_TESTING OFF)
include(CTest)

# CPack ########################################################################

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)

configure_file(src/defines.hpp.in src/defines.hpp)
