cmake_minimum_required(VERSION 3.16)
project(zx_sandbox_server C CXX)
set(CMAKE_C_STANDARD 11)

include_directories("${CMAKE_CURRENT_SOURCE_DIR}/../common/include")
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/include")
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/uthash/src")

set(Python_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/venv")
option(CMAKE_USE_WIN32_THREADS_INIT "using WIN32 threads" ON)

find_package(Threads REQUIRED)
find_package(ZLIB REQUIRED)
find_package(Python REQUIRED COMPONENTS Interpreter Development)
include_directories(${Python_INCLUDE_DIRS})

message("Found python: ${Python_EXECUTABLE}")

add_subdirectory(../common sandbox_common)

set(Z80_WITH_EXECUTE TRUE)
set(Z80_WITH_SPECIAL_RESET TRUE)

add_subdirectory(Zeta)
add_subdirectory(Z80)
add_subdirectory(dnslib)

file(GLOB_RECURSE SERVER_SOURCES CONFIGURE_DEPENDS src/*.c)
file(GLOB_RECURSE SERVER_DEBUG_SOURCES CONFIGURE_DEPENDS target/debug/*.c)
file(GLOB_RECURSE SERVER_NON_DEBUG_SOURCES CONFIGURE_DEPENDS target/non_debug/*.c)

add_library(server_common STATIC ${SERVER_SOURCES})
target_link_libraries(server_common PUBLIC ${CMAKE_THREAD_LIBS_INIT} ${Python_LIBRARIES} Python::Module
    channels_proto channels_proto_server sandbox_common Z80 dns)

if(WIN32)
    target_link_libraries(server_common PUBLIC ws2_32)
endif()

add_executable(server ${SERVER_NON_DEBUG_SOURCES})
target_compile_options(server PUBLIC -g)

if(NOT DEFINED ENV{DOCKER_BUILD})
    set(CLIENT_MODULES_BIN "${CMAKE_CURRENT_SOURCE_DIR}/../client/cmake-build-release")

    add_custom_target(
        server_client_modules
        COMMAND CLIENT_BIN=${CLIENT_MODULES_BIN} python3 ${CMAKE_CURRENT_SOURCE_DIR}/../common/tools/databuild.py ./modules.yml ${CMAKE_CURRENT_SOURCE_DIR}/pages/modules
        COMMAND APPEND=1 python3 ${CMAKE_CURRENT_SOURCE_DIR}/../common/tools/databuild.py ./music.yml ${CMAKE_CURRENT_SOURCE_DIR}/pages/modules
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/data
    )
    add_dependencies(server server_client_modules)

    add_custom_target(
        server_client_binary
        COMMAND cp ${CLIENT_MODULES_BIN}/client.bin client.bin
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/pages
    )
    add_dependencies(server server_client_binary)
endif()

target_link_libraries(server PUBLIC server_common)

add_executable(server_debug ${SERVER_DEBUG_SOURCES})
find_package(SDL2)
target_include_directories(server_debug PUBLIC ${SDL2_INCLUDE_DIRS})
target_link_libraries(server_debug PUBLIC server_common)

if(WIN32)
    target_link_libraries(server_debug PUBLIC SDL2main SDL2)
else()
    target_link_libraries(server_debug PUBLIC ${SDL2_LIBRARIES})
endif()
