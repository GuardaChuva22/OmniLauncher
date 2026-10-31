cmake_minimum_required(VERSION 3.21)

# This directory can also be configured alone for a quick persistence check.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(OmniBackendChecks LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Network Qml)
    enable_testing()
endif()

get_filename_component(OMNI_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
add_executable(catalog-smoke
    catalog_smoke.cpp
    "${OMNI_ROOT}/src/core/ModManager.cpp"
    "${OMNI_ROOT}/src/core/ModManager.h"
    "${OMNI_ROOT}/src/core/Downloader.cpp"
    "${OMNI_ROOT}/src/core/Downloader.h"
)
set_target_properties(catalog-smoke PROPERTIES AUTOMOC ON)
target_include_directories(catalog-smoke PRIVATE "${OMNI_ROOT}/src/core")
target_link_libraries(catalog-smoke PRIVATE Qt6::Core Qt6::Network Qt6::Qml)
if(MSVC)
    target_compile_options(catalog-smoke PRIVATE /utf-8 /W4)
endif()
if(WIN32)
    add_test(NAME catalog-smoke COMMAND "${CMAKE_COMMAND}" -E env
        "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
        "$<TARGET_FILE:catalog-smoke>"
    )
else()
    add_test(NAME catalog-smoke COMMAND catalog-smoke)
endif()

add_executable(backend-smoke
    backend_smoke.cpp
    "${OMNI_ROOT}/src/app/AppModel.cpp"
    "${OMNI_ROOT}/src/app/AppModel.h"
)
set_target_properties(backend-smoke PROPERTIES AUTOMOC ON)
target_include_directories(backend-smoke PRIVATE "${OMNI_ROOT}/src/app")
target_link_libraries(backend-smoke PRIVATE Qt6::Core Qt6::Gui)
if(MSVC)
    target_compile_options(backend-smoke PRIVATE /utf-8 /W4)
endif()

if(WIN32)
    add_test(NAME backend-smoke COMMAND "${CMAKE_COMMAND}" -E env
        "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
        "$<TARGET_FILE:backend-smoke>"
    )
else()
    add_test(NAME backend-smoke COMMAND backend-smoke)
endif()

find_package(Qt6 REQUIRED COMPONENTS ZlibPrivate)
add_executable(nbt-smoke
    nbt_smoke.cpp
    "${OMNI_ROOT}/src/core/Nbt.cpp"
    "${OMNI_ROOT}/src/core/Nbt.h"
    "${OMNI_ROOT}/src/core/Worlds.cpp"
    "${OMNI_ROOT}/src/core/Worlds.h"
    "${OMNI_ROOT}/src/core/WorldMap.cpp"
    "${OMNI_ROOT}/src/core/WorldMap.h"
    "${OMNI_ROOT}/src/core/WorldData.cpp"
    "${OMNI_ROOT}/src/core/WorldData.h"
    "${OMNI_ROOT}/src/core/BlockModels.cpp"
    "${OMNI_ROOT}/src/core/BlockModels.h"
    "${OMNI_ROOT}/src/core/ChunkMesher.cpp"
    "${OMNI_ROOT}/src/core/ChunkMesher.h"
    "${OMNI_ROOT}/src/core/EntityModels.cpp"
    "${OMNI_ROOT}/src/core/EntityModels.h"
)
set_target_properties(nbt-smoke PROPERTIES AUTOMOC ON)
target_include_directories(nbt-smoke PRIVATE "${OMNI_ROOT}/src/core")
target_link_libraries(nbt-smoke PRIVATE Qt6::Core Qt6::Gui Qt6::ZlibPrivate)
if(MSVC)
    target_compile_options(nbt-smoke PRIVATE /utf-8 /W4)
endif()
if(WIN32)
    add_test(NAME nbt-smoke COMMAND "${CMAKE_COMMAND}" -E env
        "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
        "$<TARGET_FILE:nbt-smoke>"
    )
else()
    add_test(NAME nbt-smoke COMMAND nbt-smoke)
endif()

# The Omni agent link: token check and the State frame (what the agent sends).
add_executable(link-smoke
    link_smoke.cpp
    "${OMNI_ROOT}/src/core/OmniLink.cpp"
    "${OMNI_ROOT}/src/core/OmniLink.h"
)
set_target_properties(link-smoke PROPERTIES AUTOMOC ON)
target_include_directories(link-smoke PRIVATE "${OMNI_ROOT}/src/core")
target_link_libraries(link-smoke PRIVATE Qt6::Core Qt6::Network)
if(MSVC)
    target_compile_options(link-smoke PRIVATE /utf-8 /W4)
endif()
if(WIN32)
    add_test(NAME link-smoke COMMAND "${CMAKE_COMMAND}" -E env
        "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
        "$<TARGET_FILE:link-smoke>"
    )
else()
    add_test(NAME link-smoke COMMAND link-smoke)
endif()
