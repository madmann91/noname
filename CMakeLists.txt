cmake_minimum_required(VERSION 3.14)
project(noname)

add_executable(noname
    src/arena.h
    src/arena.c
    src/exp.h
    src/exp.c
    src/format.h
    src/format.c
    src/htable.h
    src/htable.c
    src/print.h
    src/print.c
    src/utils.h
    src/utils.c
    src/main.c)
set_target_properties(noname PROPERTIES C_STANDARD 11)
