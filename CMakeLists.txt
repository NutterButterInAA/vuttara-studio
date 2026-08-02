cmake_minimum_required(VERSION 3.27)

project(
    VuttaraStudio
    VERSION 0.0.1
    DESCRIPTION "Vuttara Studio streaming software"
    LANGUAGES CXX RC
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
foreach(config Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${config}" config_upper)
    set("CMAKE_RUNTIME_OUTPUT_DIRECTORY_${config_upper}" "${CMAKE_BINARY_DIR}/bin")
endforeach()

find_package(Qt6 6.8 REQUIRED COMPONENTS Widgets Network)
qt_standard_project_setup()

qt_add_executable(VuttaraStudio
    src/main.cpp
    src/MainWindow.cpp
    src/MainWindow.hpp
    src/UpdateManager.cpp
    src/UpdateManager.hpp
    src/AppPaths.cpp
    src/AppPaths.hpp
    src/PreviewWidget.cpp
    src/PreviewWidget.hpp
    src/engine/DisplayEnumerator.cpp
    src/engine/DisplayEnumerator.hpp
    src/engine/AudioDeviceEnumerator.cpp
    src/engine/AudioDeviceEnumerator.hpp
    src/engine/ObsAbi.hpp
    src/engine/WindowEnumerator.cpp
    src/engine/WindowEnumerator.hpp
    src/engine/ObsRuntime.cpp
    src/engine/ObsRuntime.hpp
    src/engine/VuttaraEngine.cpp
    src/engine/VuttaraEngine.hpp
    resources/resources.qrc
    src/windows/VuttaraStudio.rc
)

target_link_libraries(VuttaraStudio PRIVATE Qt6::Widgets Qt6::Network dwmapi ole32 propsys crypt32)

option(VUTTARA_STUDIO_RELEASE_BUILD "Enable the stable clean-rewrite updater" OFF)

target_compile_definitions(VuttaraStudio PRIVATE
    VUTTARA_STUDIO_VERSION="${PROJECT_VERSION}"
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

if(VUTTARA_STUDIO_RELEASE_BUILD)
    target_compile_definitions(VuttaraStudio PRIVATE VUTTARA_STUDIO_RELEASE_BUILD=1)
else()
    target_compile_definitions(VuttaraStudio PRIVATE VUTTARA_STUDIO_LOCAL_DEVELOPMENT=1)
endif()

if(MSVC)
    target_compile_options(VuttaraStudio PRIVATE /W4 /permissive- /EHsc /utf-8)
endif()

set_target_properties(VuttaraStudio PROPERTIES
    OUTPUT_NAME "VuttaraStudio"
    WIN32_EXECUTABLE TRUE
)
