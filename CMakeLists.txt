cmake_minimum_required (VERSION 3.29)

project (ChapterOne)

file(GLOB SOURCES *.cpp imgui/*.cpp *.hpp)

find_package(Vulkan REQUIRED)
find_package(Slang REQUIRED PATHS extern/slang/windows)

# GLSL to SPIR-V
file(GLOB SHADERS shaders/*.frag shaders/*.mesh shaders/*.task shaders/*.comp shaders/debug/*.frag)
set(SPIRV_VERSION "1.4")
foreach(SHADER ${SHADERS})
    get_filename_component(FILENAME ${SHADER} NAME)
    add_custom_command(
        OUTPUT  ${CMAKE_BINARY_DIR}/shaders/${FILENAME}.spv
        COMMAND ${Vulkan_GLSLC_EXECUTABLE} ${SHADER} -o ${CMAKE_BINARY_DIR}/shaders/${FILENAME}.spv --target-spv=spv${SPIRV_VERSION}
        DEPENDS ${SHADER}
    )
    list(APPEND SPIRV_BINARY_FILES ${CMAKE_BINARY_DIR}/shaders/${FILENAME}.spv)
endforeach()
add_custom_target(Shaders ALL DEPENDS ${SPIRV_BINARY_FILES})

# Copy assets to build.
set(ASSETS_DIR ${CMAKE_SOURCE_DIR}/assets)
set(TARGET_ASSETS_DIR ${CMAKE_BINARY_DIR}/assets)
add_custom_target(CopyAssets
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${ASSETS_DIR} ${TARGET_ASSETS_DIR}
)

# Copy shaders to build.
set(SHADERS_DIR ${CMAKE_SOURCE_DIR}/shaders)
set(TARGET_SHADERS_DIR ${CMAKE_BINARY_DIR}/shaders)
add_custom_target(CopyShaders
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${SHADERS_DIR} ${TARGET_SHADERS_DIR}
)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/shaders/compiled)

add_executable(ChapterOne ${SOURCES} "shaders/common.h" "shaders/common.slang" "GPUBuffer.hpp" "Shader.cpp")
add_dependencies(ChapterOne CopyAssets)
add_dependencies(ChapterOne CopyShaders)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(BUILD_SHARED_LIBS ON)

add_subdirectory(extern/SDL)
add_subdirectory(extern/fastgltf)
add_subdirectory(extern/meshoptimizer)

target_link_libraries(ChapterOne PUBLIC Vulkan::Vulkan)
target_link_libraries(ChapterOne PUBLIC SDL3::SDL3-shared)
target_link_libraries(ChapterOne PUBLIC fastgltf::fastgltf)
target_link_libraries(ChapterOne PUBLIC meshoptimizer)
target_link_libraries(ChapterOne PUBLIC slang::slang)

set_property(TARGET ChapterOne PROPERTY CXX_STANDARD 20)