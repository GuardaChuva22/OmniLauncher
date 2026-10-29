cmake_minimum_required(VERSION 3.21)
project(OmniLauncher VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Network Qml Quick QuickControls2)
qt_standard_project_setup(REQUIRES 6.5)
if(Qt6_VERSION VERSION_GREATER_EQUAL 6.8)
    qt_policy(SET QTP0004 NEW)
endif()

qt_add_executable(OmniLauncher WIN32
    src/app/main.cpp
    src/app/AppModel.h
    src/app/AppModel.cpp
    src/core/Downloader.h
    src/core/Downloader.cpp
    src/core/GameRunner.h
    src/core/GameRunner.cpp
    src/core/ModManager.h
    src/core/ModManager.cpp
    src/core/AuthManager.h
    src/core/AuthManager.cpp
    src/core/UpdateManager.h
    src/core/UpdateManager.cpp
)

# Packaged builds know their commit and check the GitHub release for a newer one.
execute_process(COMMAND git rev-parse HEAD WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
                OUTPUT_VARIABLE omni_build_commit OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
option(OMNI_AUTO_UPDATE "Check GitHub releases for launcher updates (packaged builds)" OFF)
target_compile_definitions(OmniLauncher PRIVATE OMNI_BUILD_COMMIT="${omni_build_commit}"
                           OMNI_AUTO_UPDATE=$<BOOL:${OMNI_AUTO_UPDATE}>)
target_include_directories(OmniLauncher PRIVATE src/app src/core)

# Azure app (client) ID for Microsoft sign-in. Public by design; the OMNI_MSA_CLIENT_ID
# environment variable overrides it at runtime.
set(OMNI_MSA_CLIENT_ID "" CACHE STRING "Microsoft (Azure) application client ID for sign-in (empty: OmniLauncher's own)")
set(omni_msa_client_id "${OMNI_MSA_CLIENT_ID}")
if(omni_msa_client_id STREQUAL "")
    set(omni_msa_client_id "ea4003d9-afca-4cd3-9940-4c8ed78b4eb8")
endif()
target_compile_definitions(OmniLauncher PRIVATE OMNI_MSA_CLIENT_ID="${omni_msa_client_id}")
if(WIN32)
    target_link_libraries(OmniLauncher PRIVATE advapi32)
endif()

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
    Qt6::Core Qt6::Gui Qt6::Network Qt6::Qml Qt6::Quick Qt6::QuickControls2
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
