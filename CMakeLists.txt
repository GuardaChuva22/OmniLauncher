cmake_minimum_required(VERSION 3.21)
project(OmniLauncher VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2)
qt_standard_project_setup(REQUIRES 6.5)
if(Qt6_VERSION VERSION_GREATER_EQUAL 6.8)
    qt_policy(SET QTP0004 NEW)
endif()

qt_add_executable(OmniLauncher WIN32
    src/app/main.cpp
    src/app/AppModel.h
    src/app/AppModel.cpp
)

# The prototype's QML/assets are authored alongside the native shell. CONFIGURE_DEPENDS
# keeps newly added components visible while the first Home composition is evolving.
file(GLOB_RECURSE OMNI_QML_FILES CONFIGURE_DEPENDS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
    "qml/*.qml"
)
file(GLOB_RECURSE OMNI_ASSET_FILES CONFIGURE_DEPENDS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
    "assets/*.png" "assets/*.jpg" "assets/*.jpeg" "assets/*.webp"
    "assets/*.svg" "assets/*.ttf" "assets/*.otf"
)
foreach(qml_file IN LISTS OMNI_QML_FILES)
    if(qml_file MATCHES "(^|/)Theme\\.qml$")
        set_source_files_properties("${qml_file}" PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
    endif()
endforeach()

qt_add_qml_module(OmniLauncher
    URI OmniLauncher
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    QML_FILES ${OMNI_QML_FILES}
    RESOURCES ${OMNI_ASSET_FILES}
)

target_link_libraries(OmniLauncher PRIVATE
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2
)
if(MSVC)
    target_compile_options(OmniLauncher PRIVATE /W4 /utf-8 /permissive-)
else()
    target_compile_options(OmniLauncher PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(GNUInstallDirs)
install(TARGETS OmniLauncher RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

option(OMNI_BUILD_TESTS "Build the local backend persistence checks" OFF)
if(OMNI_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
