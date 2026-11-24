cmake_minimum_required(VERSION 3.16)
project(nativemd VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(NMD_BUILD_TESTS "Build tests" ON)
option(NMD_WERROR "Treat warnings as errors" OFF)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    set(NMD_WARNINGS -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion)
    if(NMD_WERROR)
        list(APPEND NMD_WARNINGS -Werror)
    endif()
endif()

# --- Engine -----------------------------------------------------------------

add_library(nativemd
    src/bus.c
    src/cart.c
    src/coro_ucontext.c
    src/host_null.c
    src/io.c
    src/machine.c
    src/state.c
    src/trace.c
    src/vdp.c
    src/z80.c
)
target_include_directories(nativemd PUBLIC include)
target_compile_options(nativemd PRIVATE ${NMD_WARNINGS})
target_compile_definitions(nativemd PRIVATE _POSIX_C_SOURCE=200809L)

# --- Tools ------------------------------------------------------------------

add_executable(nmd-tracediff tools/tracediff.c)
target_link_libraries(nmd-tracediff PRIVATE nativemd)
target_compile_options(nmd-tracediff PRIVATE ${NMD_WARNINGS})

add_executable(mkhello tools/mkhello.c)
target_include_directories(mkhello PRIVATE tests/hello)
target_compile_options(mkhello PRIVATE ${NMD_WARNINGS})

# --- Tests ------------------------------------------------------------------

if(NMD_BUILD_TESTS)
    enable_testing()

    add_executable(test_core tests/test_core.c)
    target_link_libraries(test_core PRIVATE nativemd)
    target_compile_definitions(test_core PRIVATE _POSIX_C_SOURCE=200809L)
    add_test(NAME core COMMAND test_core)

    set(HELLO_ROM ${CMAKE_CURRENT_BINARY_DIR}/hello.bin)
    add_custom_command(OUTPUT ${HELLO_ROM}
        COMMAND mkhello ${HELLO_ROM}
        DEPENDS mkhello
        COMMENT "Generating hello.bin")
    add_custom_target(hello_rom ALL DEPENDS ${HELLO_ROM})

    add_executable(test_hello tests/hello/test_hello.c tests/hello/hello_game.c)
    target_link_libraries(test_hello PRIVATE nativemd)
    target_include_directories(test_hello PRIVATE tests/hello)
    target_compile_options(test_hello PRIVATE ${NMD_WARNINGS})
    add_dependencies(test_hello hello_rom)

    add_test(NAME hello COMMAND test_hello ${HELLO_ROM} ${CMAKE_CURRENT_BINARY_DIR}/hello.trace)
    add_test(NAME hello_trace
        COMMAND nmd-tracediff ${CMAKE_CURRENT_SOURCE_DIR}/tests/hello/hello.trace
                              ${CMAKE_CURRENT_BINARY_DIR}/hello.trace)
    set_tests_properties(hello_trace PROPERTIES DEPENDS hello)
endif()
