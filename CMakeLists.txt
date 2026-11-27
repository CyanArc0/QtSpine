cmake_minimum_required(VERSION 3.18)

project(QtSpine VERSION 2.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 REQUIRED COMPONENTS Quick)

include(FetchContent)
FetchContent_Declare(
   spine-cpp
   GIT_REPOSITORY https://github.com/esotericsoftware/spine-runtimes.git
   GIT_TAG 4.2
   SOURCE_SUBDIR spine-cpp
)
FetchContent_MakeAvailable(spine-cpp)

include(disable-Exceptions.cmake)
include(disable-RTTI.cmake)

qt_standard_project_setup(REQUIRES 6.8)

add_subdirectory(QtSpine)

qt_add_executable(QtSpineExample
    main.cpp
)

qt_add_qml_module(QtSpineExample
    URI QtSpineExample
    QML_FILES
        Main.qml
    RESOURCES
        "resources/p7_bg_gd.png"
)

qt_add_resources(QtSpineExample "resources"
    PREFIX "/"
    BASE "resources/shaonv"
    FILES
        "resources/shaonv/shaonv.json"
        "resources/shaonv/shaonv.atlas"
        "resources/shaonv/shaonv.png"
)

set_target_properties(QtSpineExample PROPERTIES
    WIN32_EXECUTABLE TRUE
    QT_ANDROID_PACKAGE_NAME "CyanArc.QtSpineExample"
    QT_ANDROID_VERSION_CODE 4
    QT_ANDROID_VERSION_NAME "2.1"
)

target_link_libraries(QtSpineExample
    PRIVATE Qt6::Quick
    PRIVATE QtSpine
)

install(TARGETS QtSpineExample
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)