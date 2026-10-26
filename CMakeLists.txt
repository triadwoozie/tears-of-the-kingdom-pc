cmake_minimum_required(VERSION 3.20)
project(TotK-Recomp LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Optimization and vectorization flags for x86_64
if(MSVC)
    add_compile_options(/O2 /arch:AVX2 /W3 /utf-8)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
else()
    add_compile_options(-O2 -Wall -Wextra -mavx2 -mfma -pthread)
endif()

# Global definitions
add_definitions(-DVK_NO_PROTOTYPES -DIMGUI_IMPL_VULKAN_USE_VOLK)

# Third party include directories
set(VOLK_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/volk)
set(VULKAN_HEADERS_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/vulkan-headers/include)
set(VMA_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/vma/include)
set(SIMDE_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/simde)
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui)

# SDL3 Subdirectory
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_SHARED ON CACHE BOOL "" FORCE)
add_subdirectory(third_party/sdl3 EXCLUDE_FROM_ALL)

# Volk Library
add_library(volk STATIC
    third_party/volk/volk.c
    third_party/volk/volk.h
)
target_include_directories(volk PUBLIC
    ${VOLK_INCLUDE_DIR}
    ${VULKAN_HEADERS_INCLUDE_DIR}
)

# ImGui Library (with SDL3 and Vulkan backends)
add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdl3.cpp
    ${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp
)
target_include_directories(imgui PUBLIC
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${VOLK_INCLUDE_DIR}
    ${VULKAN_HEADERS_INCLUDE_DIR}
)
target_link_libraries(imgui PUBLIC SDL3::SDL3 volk)

# Platform Sources
if(WIN32)
    set(PLATFORM_SOURCES src/runtime/platform/windows.c)
else()
    set(PLATFORM_SOURCES src/runtime/platform/linux.c)
endif()

# TotK Runtime Library
add_library(totk_recomp_runtime STATIC
    src/runtime/runtime_config.cpp
    src/runtime/paths.c
    src/runtime/memory_manager.cpp
    src/runtime/kernel_hle.cpp
    src/runtime/threads_hle.cpp
    src/runtime/services_hle.cpp
    src/runtime/filesystem_hle.cpp
    src/runtime/hlr_sead.cpp
    src/runtime/audio_hle.cpp
    src/runtime/video_hle.cpp
    src/runtime/module_loader.cpp
    src/runtime/game_framework_hle.cpp
    src/runtime/hid_hle.cpp
    src/runtime/byml_bfres_parser.cpp
    ${PLATFORM_SOURCES}
)
target_include_directories(totk_recomp_runtime PUBLIC
    src/
    src/runtime/
    ${SIMDE_INCLUDE_DIR}
)
target_link_libraries(totk_recomp_runtime PUBLIC SDL3::SDL3)
if(NOT MSVC)
    target_link_libraries(totk_recomp_runtime PUBLIC m dl pthread zstd)
endif()

# TotK Recompiler Library
add_library(totk_recomp_recompiler STATIC
    src/recompiler/dispatch.cpp
)
target_include_directories(totk_recomp_recompiler PUBLIC
    src/
    src/runtime/
    ${SIMDE_INCLUDE_DIR}
)
target_link_libraries(totk_recomp_recompiler PUBLIC totk_recomp_runtime)

# TotK Graphics Library
add_library(totk_recomp_graphics STATIC
    src/graphics/nvn_vk.cpp
    src/graphics/vk_pipeline_cache.cpp
    src/graphics/hle_nvn_gen.cpp
    src/graphics/nvn_buffer_cache.cpp
    src/graphics/nvn_uniform_snapshot.cpp
    src/graphics/nvn_conditional_draw.cpp
    src/graphics/nvn_texture_cache.cpp
    src/graphics/astc_decoder.cpp
    src/graphics/bnsh_parser.cpp
    src/graphics/maxwell_to_spirv.cpp
    src/graphics/nvn_bindless.cpp
    src/graphics/nvn_render_states.cpp
    src/graphics/bc_decoder.cpp
)
target_include_directories(totk_recomp_graphics PUBLIC
    src/
    ${VOLK_INCLUDE_DIR}
    ${VULKAN_HEADERS_INCLUDE_DIR}
    ${VMA_INCLUDE_DIR}
    ${SIMDE_INCLUDE_DIR}
)
target_link_libraries(totk_recomp_graphics PUBLIC volk SDL3::SDL3)

# TotK UI Library
add_library(totk_recomp_ui STATIC
    src/ui/imgui_style.cpp
    src/ui/studio.cpp
    src/ui/overlay.cpp
)
target_include_directories(totk_recomp_ui PUBLIC
    src/
    ${IMGUI_DIR}
    ${VOLK_INCLUDE_DIR}
    ${VULKAN_HEADERS_INCLUDE_DIR}
)
target_link_libraries(totk_recomp_ui PUBLIC imgui SDL3::SDL3)

# Main Executable
add_executable(totk_recomp
    src/main.cpp
)
target_include_directories(totk_recomp PRIVATE
    src/
    ${VOLK_INCLUDE_DIR}
    ${VULKAN_HEADERS_INCLUDE_DIR}
    ${IMGUI_DIR}
)
target_link_libraries(totk_recomp PRIVATE
    totk_recomp_ui
    totk_recomp_graphics
    totk_recomp_runtime
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wl,--whole-archive>
    totk_recomp_recompiler
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wl,--no-whole-archive>
    imgui
    volk
    SDL3::SDL3
)
if(NOT MSVC)
    target_link_libraries(totk_recomp PRIVATE m dl pthread zstd)
endif()
set_target_properties(totk_recomp PROPERTIES ENABLE_EXPORTS ON)

# ==============================================================================
# Testing Suite (CTest)
# ==============================================================================
enable_testing()

function(add_totk_test TEST_NAME TEST_SRC)
    add_executable(${TEST_NAME} ${TEST_SRC})
    target_include_directories(${TEST_NAME} PRIVATE
        src/
        src/runtime/
        ${IMGUI_DIR}
        ${SIMDE_INCLUDE_DIR}
        ${VOLK_INCLUDE_DIR}
        ${VULKAN_HEADERS_INCLUDE_DIR}
        ${VMA_INCLUDE_DIR}
    )
    target_link_libraries(${TEST_NAME} PRIVATE
        totk_recomp_ui
        totk_recomp_runtime
        totk_recomp_recompiler
        totk_recomp_graphics
        imgui
        volk
        SDL3::SDL3
    )
    if(NOT MSVC)
        target_link_libraries(${TEST_NAME} PRIVATE m dl pthread zstd)
    endif()
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endfunction()

add_totk_test(test_abi_structs tests/test_abi_structs.cpp)
add_totk_test(test_memory_window tests/test_memory_window.cpp)
add_totk_test(test_lifter_neon tests/test_lifter_neon.cpp)
add_totk_test(test_kernel_sync tests/test_kernel_sync.cpp)
add_totk_test(test_nvn_vulkan tests/test_nvn_vulkan.cpp)
add_totk_test(test_audio_hid tests/test_audio_hid.cpp)
add_totk_test(test_module_loader tests/test_module_loader.cpp)
add_totk_test(test_fs_ipc tests/test_fs_ipc.cpp)
add_totk_test(test_astc_decoder tests/test_astc_decoder.cpp)
add_totk_test(test_ui_config tests/test_ui_config.cpp)
add_totk_test(test_maxwell_spirv tests/test_maxwell_spirv.cpp)
add_totk_test(test_byml_bfres tests/test_byml_bfres.cpp)
