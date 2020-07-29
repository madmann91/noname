cmake_minimum_required(VERSION 3.14)
project(noname)

add_executable(noname
    src/exp.h
    src/exp.c
    src/utils.h
    src/utils.c
    src/main.c)
set_target_properties(noname PROPERTIES C_STANDARD 11)
