cmake_minimum_required (VERSION 3.29)

project (ChapterOne)

file(GLOB SOURCES *.cpp imgui/*.cpp *.hpp)

find_package(Vulkan REQUIRED)

set(SLANG_DIR ${CMAKE_SOURCE_DIR}/extern/slang-2025.22.1-windows-x86_64)
find_package(slang REQUIRED PATHS ${SLANG_DIR})

# Manually copy slang-compiler.dll
configure_file(${SLANG_DIR}/bin/slang-compiler.dll ${CMAKE_BINARY_DIR}/slang-compiler.dll COPYONLY)

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

add_executable(ChapterOne ${SOURCES})
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