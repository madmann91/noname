cmake_minimum_required(VERSION 3.14)
project(noname)

option(USE_COLORS "Enable output color support. Requires ANSI-compliant terminal." TRUE)

add_library(libnoname
    src/utils/arena.h
    src/utils/arena.c
    src/utils/format.h
    src/utils/format.c
    src/utils/htable.h
    src/utils/htable.c
    src/utils/map.h
    src/utils/set.h
    src/utils/vec.h
    src/utils/utils.h
    src/utils/utils.c
    src/exp.h
    src/exp.c
    src/simplify.c
    src/log.h
    src/log.c
    src/parse.h
    src/parse.c
    src/print.h
    src/print.c)
set_target_properties(libnoname PROPERTIES C_STANDARD 11 PREFIX "")
target_include_directories(libnoname PUBLIC src)
if (USE_COLORS)
    target_compile_definitions(libnoname PRIVATE -DUSE_COLORS)
endif ()

add_executable(noname src/main.c)
target_link_libraries(noname PRIVATE libnoname)

include(CheckIPOSupported)
check_ipo_supported(RESULT IPO_SUPPORTED)
if (IPO_SUPPORTED)
    set_property(TARGET noname    PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
    set_property(TARGET libnoname PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
endif ()
