cmake_minimum_required(VERSION 3.10)

project(TupoFS)

add_compile_options(-Wall -Wextra)

add_executable(tupofs_test test.c tupofs.c)
add_executable(tupofs_cli cli.c tupofs.c)

set_property(TARGET tupofs_cli PROPERTY C_STANDARD 11)
