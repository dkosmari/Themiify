# imgui
find_package(PkgConfig REQUIRED)
pkg_check_modules(sdl2 REQUIRED IMPORTED_TARGET sdl2)
pkg_check_modules(freetype2 REQUIRED IMPORTED_TARGET freetype2)

add_library(imgui STATIC
    imgui/imgui.cpp
    imgui/imgui_draw.cpp
    imgui/imgui_tables.cpp
    imgui/imgui_widgets.cpp
    imgui/imgui_demo.cpp 

    imgui/backends/imgui_impl_sdl2.cpp
    imgui/backends/imgui_impl_sdlrenderer2.cpp

    imgui/misc/cpp/imgui_stdlib.cpp
    imgui/misc/freetype/imgui_freetype.cpp
)

target_include_directories(imgui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui/backends
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui/misc/cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui/misc/freetype
)
target_compile_definitions(imgui PUBLIC IMGUI_USER_CONFIG="custom_imconfig.h")

target_link_libraries(imgui PUBLIC PkgConfig::sdl2 PkgConfig::freetype2)

# glaze
add_library(glaze INTERFACE)

target_include_directories(glaze INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/glaze/include)
