cmake_minimum_required(VERSION 3.21)

# This directory can also be configured alone for a quick persistence check.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(OmniBackendChecks LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui)
    enable_testing()
endif()

get_filename_component(OMNI_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
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
