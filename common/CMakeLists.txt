
add_subdirectory(proto)

file(GLOB_RECURSE COMMON_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c")

add_library(sandbox_common OBJECT ${COMMON_SOURCES})

target_include_directories(sandbox_common PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/proto/include")