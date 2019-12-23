cmake_minimum_required(VERSION 3.10)

project(TupoFS)

add_compile_options(-Wall -Wextra)

add_executable(tupofs_test test.c tupofs.c)
add_executable(tupofs_cli cli.c tupofs.c)

set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/CMake" ${CMAKE_MODULE_PATH})
find_package(FUSE REQUIRED)

include_directories(${FUSE_INCLUDE_DIR})
add_definitions( -D_FILE_OFFSET_BITS=64 )
add_executable(tupofs_fuse fuse.c tupofs.c)
target_link_libraries(tupofs_fuse ${FUSE_LIBRARIES})

set_property(TARGET tupofs_cli PROPERTY C_STANDARD 11)
