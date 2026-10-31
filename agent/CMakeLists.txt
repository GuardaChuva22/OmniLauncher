cmake_minimum_required(VERSION 3.21)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(OmniAgent NONE)
    find_package(Java 17 REQUIRED COMPONENTS Development)
endif()
include(UseJava)
include(cmake/Dependencies.cmake)
set(CMAKE_JAVA_COMPILE_FLAGS --release 17 -encoding UTF-8)
add_jar(omni-bootstrap SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/bootstrap/omni/bridge/Hooks.java"
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}")
file(GLOB_RECURSE omni_agent_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.java")
add_jar(omni-agent SOURCES ${omni_agent_sources} INCLUDE_JARS "${omni_asm_private}"
    MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/MANIFEST.MF" OUTPUT_DIR "${CMAKE_BINARY_DIR}")
add_dependencies(omni-agent omni-bootstrap)
add_custom_command(TARGET omni-agent POST_BUILD
    COMMAND "${Java_JAR_EXECUTABLE}" uf "${CMAKE_BINARY_DIR}/omni-agent.jar"
        -C "${omni_asm_dir}" omni/agent/internal/asm
        -C "${CMAKE_CURRENT_BINARY_DIR}" omni-bootstrap.jar
        -C "${CMAKE_CURRENT_SOURCE_DIR}" LICENSE-ASM.txt
        -C "${CMAKE_CURRENT_SOURCE_DIR}/../assets" fonts/Manrope-Medium.ttf
        -C "${CMAKE_CURRENT_SOURCE_DIR}/../assets" fonts/Manrope-SemiBold.ttf
        -C "${CMAKE_CURRENT_SOURCE_DIR}/../assets" fonts/OFL.txt)
