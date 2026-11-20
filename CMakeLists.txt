cmake_minimum_required(VERSION 3.21)

# the dependencies come from vcpkg (vcpkg.json, manifest mode): configure
# with the toolchain of vcpkg, or a preset of CMakePresets.json
#   cmake --preset linux        (VCPKG_ROOT set to the vcpkg folder)
# r4fly is the "viewer" feature of the manifest (SDL2): without it,
# -DVCPKG_MANIFEST_NO_DEFAULT_FEATURES=ON -DR4_BUILD_APPS=OFF
if(DEFINED R4_BUILD_APPS AND NOT R4_BUILD_APPS)
    set(VCPKG_MANIFEST_NO_DEFAULT_FEATURES ON CACHE BOOL "")
endif()

project(r4nkine VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(R4_BUILD_APPS "Build r4fly (needs SDL2)" ON)
option(R4_BUILD_EXAMPLES "Build the examples (with R4_BUILD_APPS)" ON)
option(R4_BUILD_TESTS "Build the tests" ON)

# the library: the GL functions are loaded at run time; glm (the math), stb
# (the images) and cgltf (glTF) are header-only libraries of vcpkg
find_package(glm CONFIG QUIET)
find_package(Stb QUIET)
find_path(CGLTF_INCLUDE_DIR cgltf.h)
if(NOT glm_FOUND OR NOT Stb_FOUND OR NOT CGLTF_INCLUDE_DIR)
    message(FATAL_ERROR "glm, stb or cgltf not found: configure with vcpkg, e.g. "
                        "cmake --preset linux (VCPKG_ROOT set), or "
                        "-DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake")
endif()

add_library(r4nkine
    src/core/math.cpp
    src/gpu/gl/gl_device.cpp
    src/assets/model.cpp
    src/assets/obj_loader.cpp
    src/assets/gobj_loader.cpp
    src/assets/yaodl_loader.cpp
    src/assets/flipbin_loader.cpp
    src/assets/pfb_loader.cpp
    src/assets/dwb_loader.cpp
    src/assets/gset_loader.cpp
    src/assets/flt_loader.cpp
    src/assets/image_stb.cpp
    src/assets/upscale.cpp
    src/assets/gltf_loader.cpp
    src/scene/node.cpp
    src/render/renderer.cpp
    src/ui/ui.cpp
    src/ui/font_data.cpp
    src/ui/controls.cpp
    src/raster/raster.cpp
    src/render/overlay.cpp
    src/render/postprocess.cpp
)
add_library(r4nkine::r4nkine ALIAS r4nkine)
target_include_directories(r4nkine PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_include_directories(r4nkine PRIVATE ${Stb_INCLUDE_DIR} ${CGLTF_INCLUDE_DIR})
# glm in the interface; its vectors at zero and matrices at the identity
target_link_libraries(r4nkine PUBLIC glm::glm)
target_compile_definitions(r4nkine PUBLIC GLM_FORCE_CTOR_INIT)
if(MSVC)
    target_compile_options(r4nkine PRIVATE /W4)
else()
    target_compile_options(r4nkine PRIVATE -Wall -Wextra)
endif()

if(R4_BUILD_APPS)
    find_package(SDL2 CONFIG REQUIRED)
    add_executable(r4fly apps/r4fly/main.cpp apps/r4fly/gui.cpp)
    target_link_libraries(r4fly PRIVATE r4nkine
        $<TARGET_NAME_IF_EXISTS:SDL2::SDL2main>
        $<IF:$<TARGET_EXISTS:SDL2::SDL2>,SDL2::SDL2,SDL2::SDL2-static>)
    # the examples of the README, built with the viewer (SDL2)
    if(R4_BUILD_EXAMPLES)
        foreach(example hello_model forest hierarchy ui_panel cockpit)
            add_executable(${example} examples/${example}.cpp)
            target_link_libraries(${example} PRIVATE r4nkine
                $<TARGET_NAME_IF_EXISTS:SDL2::SDL2main>
                $<IF:$<TARGET_EXISTS:SDL2::SDL2>,SDL2::SDL2,SDL2::SDL2-static>)
            install(TARGETS ${example} RUNTIME DESTINATION bin)
        endforeach()
    endif()
    # the viewer and the DLLs it needs (SDL2 on Windows), side by side
    install(TARGETS r4fly RUNTIME DESTINATION bin)
    if(WIN32)
        install(FILES $<TARGET_RUNTIME_DLLS:r4fly> DESTINATION bin)
    endif()
endif()

if(R4_BUILD_TESTS)
    enable_testing()
    add_executable(r4_tests tests/tests.cpp)
    target_link_libraries(r4_tests PRIVATE r4nkine)
    add_test(NAME r4_tests COMMAND r4_tests ${CMAKE_CURRENT_SOURCE_DIR}/tests/data)
endif()

install(TARGETS r4nkine EXPORT r4nkineTargets ARCHIVE DESTINATION lib LIBRARY DESTINATION lib)
install(DIRECTORY include/r4 DESTINATION include)
install(EXPORT r4nkineTargets NAMESPACE r4nkine:: DESTINATION lib/cmake/r4nkine)
