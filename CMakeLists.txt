cmake_minimum_required (VERSION 3.8)

project ("ChapterOne")

include_directories(external)
include_directories(external/ktx/include)
include_directories(external/ktx/other_include)

# Fetch external depencies.
include (FetchContent)
FetchContent_Declare(
	fastgltf
	GIT_REPOSITORY https://github.com/spnda/fastgltf.git
	GIT_TAG v0.8.0
)
FetchContent_Declare(
	SDL3
	GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
	GIT_TAG release-3.2.0
)
set(KTX_FEATURE_STATIC_LIBRARY ON CACHE BOOL "Build KTX as a static library" FORCE)
FetchContent_Declare(
    ktx
    GIT_REPOSITORY https://github.com/KhronosGroup/KTX-Software
    GIT_TAG v4.4.0
)
FetchContent_MakeAvailable (fastgltf SDL3 ktx)

find_package(Vulkan REQUIRED)

file(GLOB SOURCES *.cpp)
add_executable(ChapterOne ${SOURCES})
target_include_directories(ChapterOne PRIVATE ${Vulkan_INCLUDE_DIRS} ${ktx_SOURCE_DIR}/include)

target_link_libraries(ChapterOne PRIVATE Vulkan::Vulkan)
target_link_libraries(ChapterOne PRIVATE SDL3::SDL3)
add_custom_command(
        TARGET ChapterOne POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy $<TARGET_FILE:SDL3::SDL3-shared> $<TARGET_FILE_DIR:ChapterOne>
        VERBATIM
    )
target_link_libraries(ChapterOne PRIVATE fastgltf::fastgltf)
add_custom_command(
        TARGET ChapterOne POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy $<TARGET_FILE:fastgltf> $<TARGET_FILE_DIR:ChapterOne>
        VERBATIM
    )
target_link_libraries(ChapterOne PRIVATE ktx)
add_custom_command(
        TARGET ChapterOne POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy $<TARGET_FILE:ktx> $<TARGET_FILE_DIR:ChapterOne>
        VERBATIM
    )

set_property(TARGET ChapterOne PROPERTY CXX_STANDARD 20)

# Collect all shader files
file(GLOB SHADERS *.frag *.mesh)

# Compile each shader using glslc
set(SPIRV_VERSION "1.4")
foreach(SHADER ${SHADERS})
    get_filename_component(FILENAME ${SHADER} NAME)
    add_custom_command(
        OUTPUT ${CMAKE_BINARY_DIR}/shaders/${FILENAME}.spv
        COMMAND ${Vulkan_GLSLC_EXECUTABLE} ${SHADER} -o ${CMAKE_BINARY_DIR}/shaders/${FILENAME}.spv --target-spv=spv${SPIRV_VERSION}
        DEPENDS ${SHADER}
    )
    list(APPEND SPIRV_BINARY_FILES ${CMAKE_BINARY_DIR}/shaders/${FILENAME}.spv)
endforeach()

# Add a custom target to compile all shaders
add_custom_target(Shaders ALL DEPENDS ${SPIRV_BINARY_FILES})

# Add a custom target for all assets
set(ASSETS_DIR ${CMAKE_SOURCE_DIR}/assets)
set(TARGET_ASSETS_DIR ${CMAKE_BINARY_DIR}/assets)
add_custom_target(CopyAssets
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${ASSETS_DIR} ${TARGET_ASSETS_DIR}
)
add_dependencies(ChapterOne CopyAssets)