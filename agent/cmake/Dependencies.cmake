# ASM is downloaded only at build time, verified, and relocated into omni.agent.internal.
# The game needs no extra jar, loader, runtime compiler, or internet connection.
set(omni_asm_version 9.9.1)
set(omni_asm_dir "${CMAKE_CURRENT_BINARY_DIR}/omni-asm")
set(omni_asm_download "${CMAKE_CURRENT_BINARY_DIR}/asm-${omni_asm_version}.jar")
file(DOWNLOAD "https://repo.maven.apache.org/maven2/org/ow2/asm/asm/${omni_asm_version}/asm-${omni_asm_version}.jar"
    "${omni_asm_download}"
    EXPECTED_HASH SHA256=6f3828a215c920059a5efa2fb55c233d6c54ec5cadca99ce1b1bdd10077c7ddd
    TLS_VERIFY ON STATUS omni_asm_status)
list(GET omni_asm_status 0 omni_asm_code)
if(NOT omni_asm_code EQUAL 0)
    message(FATAL_ERROR "Could not fetch ASM for the Omni client: ${omni_asm_status}")
endif()
execute_process(COMMAND "${Java_JAVA_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/Relocate.java"
    "${omni_asm_download}" "${omni_asm_dir}" COMMAND_ERROR_IS_FATAL ANY)
set(omni_asm_private "${CMAKE_CURRENT_BINARY_DIR}/omni-asm-private.jar")
execute_process(COMMAND "${Java_JAR_EXECUTABLE}" cf "${omni_asm_private}" -C "${omni_asm_dir}" omni
    COMMAND_ERROR_IS_FATAL ANY)
