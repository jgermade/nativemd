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

# --- Musashi (MIT, third_party/musashi; used only by the oracle and tools) ----

set(MUSASHI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/musashi)
set(MUSASHI_GEN ${CMAKE_CURRENT_BINARY_DIR}/musashi)
file(MAKE_DIRECTORY ${MUSASHI_GEN})
add_executable(m68kmake ${MUSASHI_DIR}/m68kmake.c)
target_compile_options(m68kmake PRIVATE -w)
add_custom_command(
    OUTPUT ${MUSASHI_GEN}/m68kops.c ${MUSASHI_GEN}/m68kops.h
    COMMAND m68kmake ${MUSASHI_GEN} ${MUSASHI_DIR}/m68k_in.c
    DEPENDS m68kmake ${MUSASHI_DIR}/m68k_in.c
    COMMENT "Generating Musashi opcode tables")

# Disassembler only (no CPU callbacks needed besides m68k_read_disassembler_*).
add_library(musashi_dasm STATIC ${MUSASHI_DIR}/m68kdasm.c)
target_include_directories(musashi_dasm PUBLIC ${MUSASHI_DIR})
target_compile_options(musashi_dasm PRIVATE -w)

# CPU core; the program linking it provides the memory callbacks.
add_library(musashi_cpu STATIC
    ${MUSASHI_DIR}/m68kcpu.c
    ${MUSASHI_DIR}/softfloat/softfloat.c
    ${MUSASHI_GEN}/m68kops.c)
target_include_directories(musashi_cpu PUBLIC ${MUSASHI_DIR} ${MUSASHI_GEN})
target_compile_options(musashi_cpu PRIVATE -w)
target_link_libraries(musashi_cpu PUBLIC musashi_dasm m)

# --- Tools ------------------------------------------------------------------

add_executable(nmd-tracediff tools/tracediff.c)
target_link_libraries(nmd-tracediff PRIVATE nativemd)
target_compile_options(nmd-tracediff PRIVATE ${NMD_WARNINGS})

add_executable(nmd-statediff tools/statediff.c)
target_compile_options(nmd-statediff PRIVATE ${NMD_WARNINGS})

add_executable(nmd-disasm tools/disasm.c)
target_link_libraries(nmd-disasm PRIVATE nativemd musashi_dasm)
target_compile_options(nmd-disasm PRIVATE ${NMD_WARNINGS})

add_executable(nmd-oracle harness/oracle/nmd_oracle.c)
target_link_libraries(nmd-oracle PRIVATE nativemd musashi_cpu)
target_compile_options(nmd-oracle PRIVATE ${NMD_WARNINGS})

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

    set(HELLO_DIR ${CMAKE_CURRENT_SOURCE_DIR}/tests/hello)
    set(OUT ${CMAKE_CURRENT_BINARY_DIR})
    set(HELLO_ROM ${OUT}/hello.bin)
    set(HELLO_SYM ${OUT}/hello.sym)
    add_custom_command(OUTPUT ${HELLO_ROM} ${HELLO_SYM}
        COMMAND mkhello ${HELLO_ROM} ${HELLO_SYM}
        DEPENDS mkhello
        COMMENT "Generating hello.bin")
    add_custom_target(hello_rom ALL DEPENDS ${HELLO_ROM} ${HELLO_SYM})

    add_executable(test_hello tests/hello/test_hello.c tests/hello/hello_game.c)
    target_link_libraries(test_hello PRIVATE nativemd)
    target_include_directories(test_hello PRIVATE tests/hello)
    target_compile_options(test_hello PRIVATE ${NMD_WARNINGS})
    add_dependencies(test_hello hello_rom)

    # The encoding of hello.s, checked by disassembling it.
    add_test(NAME hello_disasm
        COMMAND nmd-disasm ${HELLO_ROM} --from 200 --to 500 --sym ${HELLO_SYM} --out ${OUT}/hello.lst)
    add_test(NAME hello_disasm_listing
        COMMAND ${CMAKE_COMMAND} -E compare_files ${HELLO_DIR}/hello.lst ${OUT}/hello.lst)
    set_tests_properties(hello_disasm_listing PROPERTIES DEPENDS hello_disasm)

    # The C on the engine: checks, trace and final state.
    add_test(NAME hello COMMAND test_hello ${HELLO_ROM} ${OUT}/hello.trace ${OUT}/hello.nmds)
    add_test(NAME hello_trace
        COMMAND nmd-tracediff ${HELLO_DIR}/hello.trace ${OUT}/hello.trace)
    set_tests_properties(hello_trace PROPERTIES DEPENDS hello)

    # The original 68000 code on the oracle must make the same accesses.
    add_test(NAME hello_oracle
        COMMAND nmd-oracle ${HELLO_ROM} --frames 10 --input ${HELLO_DIR}/hello.input
                --sym ${HELLO_SYM} --idle WaitLoop --trace ${OUT}/oracle.trace
                --state-out ${OUT}/oracle.nmds --coverage ${OUT}/hello.cov)
    add_test(NAME hello_oracle_trace
        COMMAND nmd-tracediff ${OUT}/oracle.trace ${OUT}/hello.trace)
    add_test(NAME hello_oracle_state
        COMMAND nmd-statediff --ignore-ram FFFA00-FFFDFF ${OUT}/oracle.nmds ${OUT}/hello.nmds)
    set_tests_properties(hello_oracle_trace hello_oracle_state PROPERTIES DEPENDS "hello;hello_oracle")

    # Per-routine harness: same starting state, call VInt on both sides.
    add_test(NAME hello_routine_oracle
        COMMAND nmd-oracle ${HELLO_ROM} --state ${OUT}/hello.nmds --call 000420 --irq
                --trace ${OUT}/vint.oracle.trace --state-out ${OUT}/vint.oracle.nmds)
    add_test(NAME hello_routine_candidate
        COMMAND test_hello ${HELLO_ROM} --call 000420 ${OUT}/hello.nmds ${OUT}/vint.c.trace)
    add_test(NAME hello_routine_trace
        COMMAND nmd-tracediff ${OUT}/vint.oracle.trace ${OUT}/vint.c.trace)
    set_tests_properties(hello_routine_oracle hello_routine_candidate PROPERTIES DEPENDS hello)
    set_tests_properties(hello_routine_trace PROPERTIES
        DEPENDS "hello_routine_oracle;hello_routine_candidate")
endif()
