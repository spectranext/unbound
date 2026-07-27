cmake_minimum_required(VERSION 3.17)

include($ENV{SPECTRANEXT_SDK_PATH}/cmake/spectranext_sdk_import.cmake)
spectranext_sdk_init()

project(zx_sandbox_client C ASM)

include_directories("/usr/local/share/z88dk/include/arch/zx")
include_directories("../common/include")
include_directories("../common/proto/include")
include_directories("../common/launcher-api")
include_directories("include")
include_directories("$ENV{SPECTRANEXT_SDK_PATH}/include")
include_directories("include/spectranet")
include_directories("gui/include")

file(GLOB_RECURSE CLIENT_ASM_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.asm")
file(GLOB_RECURSE CLIENT_C_SOURCES RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c")

file(GLOB_RECURSE CLIENT_RELEASE_ASM_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/release/*.asm")
file(GLOB_RECURSE CLIENT_RELEASE_C_SOURCES RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/release/*.c")

file(GLOB_RECURSE CLIENT_DEBUG_ASM_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/debug/*.asm")
file(GLOB_RECURSE CLIENT_DEBUG_C_SOURCES RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/debug/*.c")

add_subdirectory(gui)
add_subdirectory(../common sandbox_common)

zxgui_generate_scenes(${CMAKE_CURRENT_SOURCE_DIR}/src/scenes gui_scenes)
zxgui_generate_tiles(${CMAKE_CURRENT_SOURCE_DIR}/data/gui.png)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_definitions(-DDEBUG)
endif()

set(CLIENT_PRAGMAS
    -pragma-define=CRT_ENABLE_STDIO=0
    -pragma-define=fputc_cons=1
    -pragma-define=nostreams=1
    -pragma-define=CLIB_FOPEN_MAX=-1
    -pragma-define=CLIB_MALLOC_HEAP_SIZE=0
    -pragma-define=REGISTER_SP=65020)

set(CLIENT_EXECUTABLE_SOURCES
    ${CLIENT_ASM_SOURCES}
    ${CLIENT_C_SOURCES}
    src/printf.c
    $<$<CONFIG:Release>:${CLIENT_RELEASE_ASM_SOURCES}>
    $<$<CONFIG:Release>:${CLIENT_RELEASE_C_SOURCES}>
    $<$<CONFIG:Debug>:${CLIENT_DEBUG_ASM_SOURCES}>
    $<$<CONFIG:Debug>:${CLIENT_DEBUG_C_SOURCES}>
)

add_executable(client ${CLIENT_EXECUTABLE_SOURCES})
target_compile_definitions(client PUBLIC -D__SPECTRUM -DINCLUDE_ENV_H)
target_link_options(client PUBLIC -startup=31 -zorg=25000 -create-app -m ${CLIENT_PRAGMAS})
target_link_directories(client PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/libs")
add_dependencies(client zxgui)

target_link_options(client PUBLIC
    -pragma-redirect:fputc_cons=spectranext_fputc_cons
)

target_link_libraries(client PUBLIC
    ndos.lib
    libterm
    libspectranet_np libsocket_np channels_proto channels_proto_asm sandbox_common ${ZXGUI_LIBRARIES})


add_custom_target(client.map
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMAND mkdir -p "${CMAKE_CURRENT_SOURCE_DIR}/build"
    COMMAND cp "${CMAKE_CURRENT_BINARY_DIR}/client.map" "${CMAKE_CURRENT_SOURCE_DIR}/build/client.map"
    COMMAND cp "${CMAKE_CURRENT_BINARY_DIR}/client.tap" "${CMAKE_CURRENT_SOURCE_DIR}/build/client.tap"
    COMMAND cp "${CMAKE_CURRENT_BINARY_DIR}/client.bin" "${CMAKE_CURRENT_SOURCE_DIR}/build/client.bin"
    COMMAND zsh ${CMAKE_CURRENT_SOURCE_DIR}/get_size.sh
    DEPENDS client all_modules)

add_custom_target(client_bin_autoboot_size
    COMMAND zsh ${CMAKE_CURRENT_SOURCE_DIR}/get_size.sh
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    DEPENDS client_bin_autoboot)

set(CLIENT_ASM_LINK "${CMAKE_CURRENT_BINARY_DIR}/client.asm")
set(MODULE_MAIN_ASM "${CMAKE_CURRENT_SOURCE_DIR}/modules/modules.asm")

add_custom_target(
    client_asm_gen
    COMMAND python3 tools/maplink.py ${CMAKE_CURRENT_BINARY_DIR}/client.map ${CLIENT_ASM_LINK}
    BYPRODUCTS ${CLIENT_ASM_LINK}
    DEPENDS client
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

add_custom_target(all_modules)

function(add_client_module)
    file(GLOB_RECURSE MODULE_INTRO_SOURCES RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/modules/${ARGV0}/*.c")
    add_executable(module_${ARGV0} ${MODULE_MAIN_ASM} ${MODULE_INTRO_SOURCES} ${CLIENT_ASM_LINK})
    add_dependencies(module_${ARGV0} client_asm_gen)
    add_dependencies(all_modules module_${ARGV0})
    set_source_files_properties(${MODULE_INTRO_SOURCES} PROPERTIES
        OBJECT_DEPENDS ${CLIENT_ASM_LINK}
    )
    target_link_options(module_${ARGV0} PUBLIC -clib=noclib -zorg=4096 -startup=0 -subtype=bin -nostdlib --no-crt -m)
endfunction()

file(GLOB MODULES_LIST RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}/modules" ${CMAKE_CURRENT_SOURCE_DIR}/modules/*)
foreach(MODULE_NAME ${MODULES_LIST})
    if(IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/modules/${MODULE_NAME})
        add_client_module(${MODULE_NAME})
    endif()
endforeach()

add_custom_target(client.chunks
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/build"
    COMMAND split -b12000 client.bin client
    DEPENDS client all_modules)

if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
add_custom_target(client.tap
    COMMAND open -a FuseX --args "${CMAKE_CURRENT_BINARY_DIR}/client.tap"
    DEPENDS client.map)
endif()

spectranext_set_boot("
10 CLEAR 24999
20 %aload \"client.bin\" CODE 25000
30 RANDOMIZE USR 25000
")

spectranext_add_extra_outputs(client)
