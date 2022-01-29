cmake_minimum_required(VERSION 3.16)
project(Machinate
	VERSION 0.0.0
	DESCRIPTION "Transport Tycoon-like game"
	HOMEPAGE_URL "https://github.com/j-martina/machinate"
)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(cmake/CPM.cmake)

option(MXN_PROFILEMODE "Allows profiling via Tracy." OFF)

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
)

if(MXN_PROFILEMODE)
	list(APPEND MXN_COMPILE_DEFS TRACY_ENABLE)
endif()

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

# Targets: Game client #########################################################

add_executable(${PROJECT_NAME}
	"${CMAKE_SOURCE_DIR}/src/console.cpp"
	"${CMAKE_SOURCE_DIR}/src/main.cpp"
	"${CMAKE_SOURCE_DIR}/src/media.cpp"
	"${CMAKE_SOURCE_DIR}/src/utils.cpp"

	"${CMAKE_SOURCE_DIR}/src/vk/buffer.cpp"
	"${CMAKE_SOURCE_DIR}/src/vk/context.cpp"
	"${CMAKE_SOURCE_DIR}/src/vk/image.cpp"
	"${CMAKE_SOURCE_DIR}/src/vk/model.cpp"
	"${CMAKE_SOURCE_DIR}/src/vk/pipeline.cpp"
	"${CMAKE_SOURCE_DIR}/src/vk/vk_mem_alloc.cpp"

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
	"${daScript_SOURCE_DIR}/include"
	${PHYSFS_INCLUDE_DIR}
	"${CMAKE_SOURCE_DIR}/tracy"
)

target_link_libraries(${PROJECT_NAME} PRIVATE ${MXN_LIBS})

# Targets: Utility for moving assets and compiling shaders #####################

set(MXN_TGT_ASSETS "${PROJECT_NAME}_Assets")
add_custom_target("${PROJECT_NAME}_Assets")

file(GLOB_RECURSE MXN_ASSETS RELATIVE "${CMAKE_SOURCE_DIR}/assets"
	"${CMAKE_SOURCE_DIR}/assets/fonts/*"
	"${CMAKE_SOURCE_DIR}/assets/loc/*"
	"${CMAKE_SOURCE_DIR}/assets/meshes/*"
	"${CMAKE_SOURCE_DIR}/assets/scripts/*"
	"${CMAKE_SOURCE_DIR}/assets/textures/*"
)

file(GLOB_RECURSE MXN_SHADERS RELATIVE "${CMAKE_SOURCE_DIR}/assets/shaders"
	"${CMAKE_SOURCE_DIR}/assets/shaders/*.comp"
	"${CMAKE_SOURCE_DIR}/assets/shaders/*.frag"
	"${CMAKE_SOURCE_DIR}/assets/shaders/*.tese"
	"${CMAKE_SOURCE_DIR}/assets/shaders/*.tesc"
	"${CMAKE_SOURCE_DIR}/assets/shaders/*.vert"
)

foreach(EACH_FILE ${MXN_ASSETS})
	set(SOURCE_FILE "${CMAKE_SOURCE_DIR}/assets/${EACH_FILE}")
	set(DEST_FILE "$<TARGET_FILE_DIR:${PROJECT_NAME}>/assets/${EACH_FILE}")
	if(NOT EXISTS ${DEST_FILE} OR ${SOURCE_FILE} IS_NEWER_THAN ${DEST_FILE})
		get_filename_component(DEST_SUBDIR ${DEST_FILE} DIRECTORY)
		add_custom_command(TARGET ${MXN_TGT_ASSETS} PRE_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy
			"${CMAKE_SOURCE_DIR}/assets/${EACH_FILE}"
			"$<TARGET_FILE_DIR:${PROJECT_NAME}>/assets/${EACH_FILE}"
		)
	endif()
endforeach()

add_custom_command(TARGET ${MXN_TGT_ASSETS} PRE_BUILD COMMAND ${CMAKE_COMMAND}
	-E make_directory "$<TARGET_FILE_DIR:${PROJECT_NAME}>/assets/shaders"
)

if(UNIX)
	foreach(EACH_FILE ${MXN_SHADERS})
		set(SOURCE_FILE "${CMAKE_SOURCE_DIR}/assets/shaders/${EACH_FILE}")
		set(DEST_FILE "$<TARGET_FILE_DIR:${PROJECT_NAME}>/assets/shaders/${EACH_FILE}.spv")
		
		if(EXISTS ${DEST_FILE} OR NOT ${SOURCE_FILE} IS_NEWER_THAN ${DEST_FILE})
			continue()
		endif()

		add_custom_command(TARGET ${MXN_TGT_ASSETS} PRE_BUILD COMMAND
			"glslc" ${SOURCE_FILE} "-o" ${DEST_FILE}
		)
	endforeach()
endif()

# CTest ########################################################################

set(BUILD_TESTING OFF)
include(CTest)

# CPack ########################################################################

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)

configure_file(src/defines.hpp.in src/defines.hpp)
