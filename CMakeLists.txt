cmake_minimum_required(VERSION 3.16)
project(r4nkine VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(R4_BUILD_APPS "Build r4fly (needs SDL2)" ON)
option(R4_BUILD_TESTS "Build the tests" ON)

# the library: no dependency, the GL functions are loaded at run time
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
    src/assets/gltf_loader.cpp
    src/scene/node.cpp
    src/render/renderer.cpp
)
add_library(r4nkine::r4nkine ALIAS r4nkine)
target_include_directories(r4nkine PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
# stb_image and cgltf, single headers (public domain, MIT)
target_include_directories(r4nkine PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
if(MSVC)
    target_compile_options(r4nkine PRIVATE /W4)
else()
    target_compile_options(r4nkine PRIVATE -Wall -Wextra)
endif()

if(R4_BUILD_APPS)
    find_package(SDL2 CONFIG QUIET)
    if(NOT SDL2_FOUND)
        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND)
            pkg_check_modules(SDL2 IMPORTED_TARGET sdl2)
        endif()
    endif()
    if(TARGET SDL2::SDL2)
        set(R4_SDL2 SDL2::SDL2)
    elseif(TARGET PkgConfig::SDL2)
        set(R4_SDL2 PkgConfig::SDL2)
    endif()
    if(R4_SDL2)
        add_executable(r4fly apps/r4fly/main.cpp)
        target_link_libraries(r4fly PRIVATE r4nkine ${R4_SDL2})
        if(TARGET SDL2::SDL2main)
            target_link_libraries(r4fly PRIVATE SDL2::SDL2main)
        endif()
    else()
        message(WARNING "SDL2 not found: r4fly is not built")
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
