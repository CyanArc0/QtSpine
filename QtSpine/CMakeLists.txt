cmake_minimum_required(VERSION 3.18)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(QtSpine VERSION 2.1 LANGUAGES CXX)
endif()

find_package(Qt6 REQUIRED COMPONENTS Quick Gui GuiPrivate ShaderTools)

# 如果使用方没有提前提供 spine-cpp，则自动获取
if(NOT TARGET spine-cpp)
    include(FetchContent)
    FetchContent_Declare(
        spine-cpp
        GIT_REPOSITORY https://github.com/esotericsoftware/spine-runtimes.git
        GIT_TAG 4.2
        SOURCE_SUBDIR spine-cpp
    )
    FetchContent_MakeAvailable(spine-cpp)
endif()

qt_add_library(QtSpine STATIC
    QtTextureLoader.hpp
    getDefaultExtension.cpp
    error_handling.hpp
    qspine_cast.hpp
)
add_library(QtSpine::QtSpine ALIAS QtSpine)

target_compile_features(QtSpine PUBLIC cxx_std_20)

qt_add_qml_module(QtSpine
    URI QtSpine
    VERSION 2.1
    SOURCES
        SpinePlayer.h
        SpinePlayer.cpp
        SpineRenderer.h
        SpineRenderer.cpp
        SpineVertex.h
        SpineUniforms.h
        SpineShader.hpp
)

# 编译着色器为 .qsb
qt6_add_shaders(QtSpine "spine_shaders"
    PREFIX "/shaders"
    BASE "SpineShaders"
    FILES
        SpineShaders/spine.vert
        SpineShaders/spine.frag
)

target_include_directories(QtSpine PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

target_link_libraries(QtSpine
    PUBLIC Qt6::Quick Qt6::Gui
    PRIVATE Qt6::GuiPrivate    # QRhi 在 Qt::GuiPrivate 中
    PUBLIC spine-cpp
)