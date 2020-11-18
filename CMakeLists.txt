cmake_minimum_required(VERSION 3.14)
project(noname)

option(USE_COLORS "Enable output color support. Requires ANSI-compliant terminal." TRUE)

add_library(libnoname
    src/arena.h
    src/arena.c
    #src/check.h
    #src/check.c
    src/exp.h
    src/exp.c
    src/format.h
    src/format.c
    src/htable.h
    src/htable.c
    src/log.h
    src/log.c
    src/parse.h
    src/parse.c
    src/print.h
    src/print.c
    src/utils.h
    src/utils.c)
set_target_properties(libnoname PROPERTIES C_STANDARD 11 PREFIX "")
if (USE_COLORS)
    target_compile_definitions(libnoname PRIVATE -DUSE_COLORS)
endif ()

add_executable(noname src/main.c)
target_link_libraries(noname PRIVATE libnoname)
