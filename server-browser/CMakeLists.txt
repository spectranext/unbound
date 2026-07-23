cmake_minimum_required(VERSION 3.17)

include($ENV{SPECTRANEXT_SDK_PATH}/cmake/spectranext_sdk_import.cmake)
spectranext_sdk_init()

project(zx_sandbox_client C ASM)

include_directories("$ENV{SPECTRANEXT_SDK_PATH}/z88dk/include/arch/zx")
include_directories("$ENV{SPECTRANEXT_SDK_PATH}/include")
include_directories("include")
include_directories("../client/gui/include")
include_directories("../common/proto/include")
include_directories("../common/launcher-api")

file(GLOB_RECURSE CLIENT_ASM_SOURCES RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/src/*.asm")
file(GLOB_RECURSE CLIENT_C_SOURCES RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c")

file(GLOB_RECURSE LOADER_C_SOURCES RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/loader/*.c")

add_subdirectory(../client/gui gui)
add_subdirectory(../common/proto proto)

zxgui_generate_scenes(${CMAKE_CURRENT_SOURCE_DIR}/src/scenes gui_scenes)
zxgui_generate_tiles(${CMAKE_CURRENT_SOURCE_DIR}/data/gui.png)

set(CLIENT_PRAGMAS
    -pragma-define=CRT_ENABLE_STDIO=0
    -pragma-define=fputc_cons=1
    -pragma-define=nostreams=1
    -pragma-define=CLIB_FOPEN_MAX=-1
    -pragma-define=CLIB_MALLOC_HEAP_SIZE=0
    -pragma-define=REGISTER_SP=24575)

add_executable(game_loader ${LOADER_C_SOURCES})
target_compile_definitions(game_loader PUBLIC -D__SPECTRUM)
target_link_directories(game_loader PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../client/libs")
target_link_options(game_loader PUBLIC -startup=31 -zorg=4096 --no-crt -m ${CLIENT_PRAGMAS})

target_link_libraries(game_loader PUBLIC libspectranet_np libsocket_np)

add_dependencies(game_loader channels_proto channels_proto_asm)
target_link_libraries(game_loader PUBLIC channels_proto channels_proto_asm)


add_custom_target(game_loader_src_package
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMAND cp "${CMAKE_CURRENT_BINARY_DIR}/game_loader.bin" "${CMAKE_CURRENT_SOURCE_DIR}/game_loader.bin"
    DEPENDS game_loader)

add_custom_target(
    game_loader_map_link
    COMMAND python3 "${CMAKE_CURRENT_SOURCE_DIR}/../client/tools/maplink.py" ${CMAKE_CURRENT_BINARY_DIR}/game_loader.map ${CMAKE_CURRENT_SOURCE_DIR}/game_loader.asm _loader
    BYPRODUCTS ${CLIENT_ASM_LINK}
    DEPENDS game_loader_src_package
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

add_executable(server_browser_debug ${CLIENT_ASM_SOURCES} ${CLIENT_C_SOURCES} "${CMAKE_CURRENT_SOURCE_DIR}/game_loader.asm")
target_compile_definitions(server_browser_debug PUBLIC -D__SPECTRUM -DSERVERS_DEBUG)
target_link_directories(server_browser_debug PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../client/libs")
target_link_options(server_browser_debug PUBLIC -startup=31 -zorg=29000 -create-app -m ${CLIENT_PRAGMAS})
target_link_libraries(server_browser_debug PUBLIC  libspectranet_np ${ZXGUI_LIBRARIES})
add_dependencies(server_browser_debug game_loader_map_link)

add_executable(server_browser ${CLIENT_ASM_SOURCES} ${CLIENT_C_SOURCES} "${CMAKE_CURRENT_SOURCE_DIR}/game_loader.asm")
target_compile_definitions(server_browser PUBLIC -D__SPECTRUM)
target_link_directories(server_browser PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../client/libs")
target_link_options(server_browser PUBLIC -startup=31 -zorg=29000 -create-app -m ${CLIENT_PRAGMAS})
target_link_libraries(server_browser PUBLIC  libspectranet_np ${ZXGUI_LIBRARIES})
add_dependencies(server_browser game_loader_map_link)

spectranext_set_boot("
10 CLEAR 28999
20 %aload \"server_browser_debug.bin\" CODE 29000
30 RANDOMIZE USR 29000
")

spectranext_add_extra_outputs(server_browser)
spectranext_add_extra_outputs(server_browser_debug)
