cmake_minimum_required (VERSION 3.29)

project (ChapterOne)

file(GLOB SOURCES *.cpp imgui/*.cpp *.hpp)

find_package(Vulkan REQUIRED)

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

add_subdirectory(extern/sdl)
add_subdirectory(extern/fastgltf)
add_subdirectory(extern/meshoptimizer)

target_link_libraries(ChapterOne PUBLIC Vulkan::Vulkan)
target_link_libraries(ChapterOne PUBLIC SDL3::SDL3-shared)
target_link_libraries(ChapterOne PUBLIC fastgltf::fastgltf)
target_link_libraries(ChapterOne PUBLIC meshoptimizer)

# Annoying script to include Slang
find_library(SLANG_LIB NAMES slang HINTS "$ENV{VULKAN_SDK}/Lib" "$ENV{VULKAN_SDK}/lib")
target_link_libraries(ChapterOne PRIVATE ${SLANG_LIB})

if(WIN32)
    configure_file($ENV{VULKAN_SDK}/Bin/slang.dll ${CMAKE_BINARY_DIR}/slang.dll COPYONLY)
endif()

if(WIN32 AND MINGW)
    get_filename_component(MINGW_BIN_DIR ${CMAKE_CXX_COMPILER} DIRECTORY)

    set(MINGW_DLLS
        libgcc_s_seh-1.dll
        libstdc++-6.dll
        libwinpthread-1.dll
    )

    foreach(DLL ${MINGW_DLLS})
        configure_file(${MINGW_BIN_DIR}/${DLL} ${CMAKE_BINARY_DIR}/${DLL} COPYONLY)
    endforeach()

endif()

set_property(TARGET ChapterOne PROPERTY CXX_STANDARD 20)