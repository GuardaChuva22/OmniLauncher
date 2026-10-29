cmake_minimum_required(VERSION 3.21)
project(OmniLauncher VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Network Qml Quick QuickControls2 Widgets)
qt_standard_project_setup(REQUIRES 6.5)
if(Qt6_VERSION VERSION_GREATER_EQUAL 6.8)
    qt_policy(SET QTP0004 NEW)
endif()

qt_add_executable(OmniLauncher WIN32
    src/app/main.cpp
    src/app/AppModel.h
    src/app/AppModel.cpp
    src/app/Language.h
    src/app/Language.cpp
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
    src/core/DiscordPresence.h
    src/core/DiscordPresence.cpp
    src/core/ServerManager.h
    src/core/ServerManager.cpp
    src/core/UiSound.h
    src/core/UiSound.cpp
    src/core/Wrapped.h
    src/core/Wrapped.cpp
    src/core/PackCloud.h
    src/core/PackCloud.cpp
    src/core/SteamTunnel.h
    src/core/SteamTunnel.cpp
    src/app/app.rc
)

# Discord application (client) ID for Rich Presence. Public; OMNI_DISCORD_CLIENT_ID in the
# environment overrides it at runtime.
set(OMNI_DISCORD_CLIENT_ID "" CACHE STRING "Discord application ID for Rich Presence (empty: OmniLauncher's own)")
set(omni_discord_client_id "${OMNI_DISCORD_CLIENT_ID}")
if(omni_discord_client_id STREQUAL "")
    set(omni_discord_client_id "1565183680786202865")
endif()
target_compile_definitions(OmniLauncher PRIVATE OMNI_DISCORD_CLIENT_ID="${omni_discord_client_id}")

# OmniLauncher Cloud (cloud/worker.js) for shared modpacks. OMNI_CLOUD_URL in the
# environment overrides it at runtime (e.g. a local `wrangler dev`).
set(OMNI_CLOUD_URL "" CACHE STRING "Base URL of the OmniLauncher Cloud worker (empty: OmniLauncher's own)")
set(omni_cloud_url "${OMNI_CLOUD_URL}")
if(omni_cloud_url STREQUAL "")
    set(omni_cloud_url "https://cloud.omnilauncher.workers.dev")
endif()
target_compile_definitions(OmniLauncher PRIVATE OMNI_CLOUD_URL="${omni_cloud_url}")

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
    target_link_libraries(OmniLauncher PRIVATE advapi32 winmm dwmapi)
endif()

# Steam friends: invites and a tunnel over Valve's relay network to the servers hosted in
# the launcher. Needs the Steamworks SDK, which stays out of the repository: point
# STEAMWORKS_SDK_ROOT at it (or put it in .tools/steamworks). Without it the launcher
# builds without the feature. steam_api64.dll is delay-loaded, so a missing DLL only
# turns the feature off. App ID 480 (Valve's Spacewar test app) is for testing only.
set(STEAMWORKS_SDK_ROOT "$ENV{STEAMWORKS_SDK_ROOT}" CACHE PATH "Steamworks SDK folder (with public/ and redistributable_bin/)")
if(NOT STEAMWORKS_SDK_ROOT AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.tools/steamworks/public/steam/steam_api.h")
    set(STEAMWORKS_SDK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.tools/steamworks" CACHE PATH "" FORCE)
endif()
set(OMNI_STEAM_APP_ID "480" CACHE STRING "Steam App ID for friends and the relay tunnel (480: Spacewar, testing only)")
if(WIN32 AND STEAMWORKS_SDK_ROOT AND EXISTS "${STEAMWORKS_SDK_ROOT}/public/steam/steam_api.h")
    message(STATUS "Steam tunnel: Steamworks SDK at ${STEAMWORKS_SDK_ROOT}, App ID ${OMNI_STEAM_APP_ID}")
    target_include_directories(OmniLauncher SYSTEM PRIVATE "${STEAMWORKS_SDK_ROOT}/public")
    target_link_libraries(OmniLauncher PRIVATE "${STEAMWORKS_SDK_ROOT}/redistributable_bin/win64/steam_api64.lib" delayimp)
    target_link_options(OmniLauncher PRIVATE "/DELAYLOAD:steam_api64.dll")
    target_compile_definitions(OmniLauncher PRIVATE OMNI_STEAM=1 OMNI_STEAM_APP_ID=${OMNI_STEAM_APP_ID})
    add_custom_command(TARGET OmniLauncher POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${STEAMWORKS_SDK_ROOT}/redistributable_bin/win64/steam_api64.dll"
                "$<TARGET_FILE_DIR:OmniLauncher>")
else()
    message(STATUS "Steam tunnel: off (no Steamworks SDK)")
endif()

# The prototype's QML/assets are authored alongside the native shell. CONFIGURE_DEPENDS
# keeps newly added components visible while the first Home composition is evolving.
file(GLOB_RECURSE OMNI_QML_FILES CONFIGURE_DEPENDS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
    "qml/*.qml"
)
file(GLOB_RECURSE OMNI_ASSET_FILES CONFIGURE_DEPENDS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
    "assets/*.png" "assets/*.jpg" "assets/*.jpeg" "assets/*.webp"
    "assets/*.svg" "assets/*.ttf" "assets/*.otf" "assets/*.wav" "assets/*.json"
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
    Qt6::Core Qt6::Gui Qt6::Network Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Widgets
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
