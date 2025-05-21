cmake_minimum_required (VERSION 3.8)

project ("ChapterOne")
file(GLOB SOURCES *.cpp)
add_executable(ChapterOne ${SOURCES})

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(BUILD_SHARED_LIBS ON)

find_package(Vulkan REQUIRED)
add_subdirectory(external/vendored/SDL)
add_subdirectory(external/fastgltf EXCLUDE_FROM_ALL)
#add_subdirectory(external/KTX EXCLUDE_FROM_ALL)

target_link_libraries(ChapterOne PRIVATE Vulkan::Vulkan)
target_link_libraries(ChapterOne PRIVATE SDL3::SDL3-shared)
target_link_libraries(ChapterOne PRIVATE fastgltf::fastgltf)
#target_link_libraries(ChapterOne PRIVATE ktx)

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