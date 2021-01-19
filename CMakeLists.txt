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
    src/utils/log.h
    src/utils/log.c
    src/lang/ast.h
    src/lang/parse.c
    #src/lang/print.c
    src/lang/bind.c
    src/lang/emit.c
    src/ir/exp.h
    src/ir/exp.c
    src/ir/simplify.c
    #src/ir/parse.h
    #src/ir/parse.c
    src/ir/print.h
    src/ir/print.c)
set_target_properties(libnoname PROPERTIES C_STANDARD 11 PREFIX "")
target_include_directories(libnoname PUBLIC src)
if (USE_COLORS)
    target_compile_definitions(libnoname PRIVATE -DUSE_COLORS)
endif ()

add_executable(noname src/main.c)
target_link_libraries(noname PRIVATE libnoname)

include(CTest)
if (BUILD_TESTING)
    add_executable(test_htable      test/htable.c)
    add_executable(test_htable_perf test/htable_perf.c)
    target_link_libraries(test_htable PUBLIC libnoname)
    target_link_libraries(test_htable_perf PUBLIC libnoname)
    add_test(NAME htable      COMMAND test_htable)
    add_test(NAME htable_perf COMMAND test_htable_perf)
endif ()

include(CheckIPOSupported)
check_ipo_supported(RESULT IPO_SUPPORTED)
if (IPO_SUPPORTED)
    set_property(TARGET noname    PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
    set_property(TARGET libnoname PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
endif ()
