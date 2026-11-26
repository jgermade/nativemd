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
    src/render.c
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

# --- Harness ----------------------------------------------------------------

add_library(nmd_compare STATIC harness/compare.c)
target_include_directories(nmd_compare PUBLIC harness)
target_link_libraries(nmd_compare PUBLIC nativemd)
target_compile_options(nmd_compare PRIVATE ${NMD_WARNINGS})

# Oracle (Musashi + the engine's hardware), suites and hybrid execution.
add_library(nmd_harness STATIC harness/oracle/oracle.c harness/suite.c harness/hybrid.c harness/png.c)
target_include_directories(nmd_harness PUBLIC harness)
target_link_libraries(nmd_harness PUBLIC nativemd nmd_compare musashi_cpu)
target_compile_options(nmd_harness PRIVATE ${NMD_WARNINGS})

# Desktop host and interactive play (optional: SDL2).
find_package(SDL2 QUIET)
if(SDL2_FOUND)
    # The HUD's font (Inter, OFL), embedded as a C array.
    set(NMD_FONT ${CMAKE_CURRENT_SOURCE_DIR}/third_party/inter/Inter-Medium.ttf)
    add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/inter_medium.c
        COMMAND ${CMAKE_COMMAND} -DIN=${NMD_FONT} -DOUT=${CMAKE_CURRENT_BINARY_DIR}/inter_medium.c
                -DNAME=nmd_inter_medium -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed.cmake
        DEPENDS ${NMD_FONT} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed.cmake)
    add_library(nmd_sdl STATIC host/sdl/sdl_host.c host/sdl/hud_text.c harness/play.c
                ${CMAKE_CURRENT_BINARY_DIR}/inter_medium.c)
    target_include_directories(nmd_sdl PUBLIC host harness PRIVATE third_party)
    target_link_libraries(nmd_sdl PUBLIC nmd_harness SDL2::SDL2)
    target_compile_options(nmd_sdl PRIVATE ${NMD_WARNINGS})
    add_executable(nmd-play harness/play_main.c)
    target_link_libraries(nmd-play PRIVATE nmd_sdl)
    target_compile_options(nmd-play PRIVATE ${NMD_WARNINGS})
else()
    message(STATUS "SDL2 not found: no desktop host (nmd-play)")
endif()

add_executable(nmd-oracle harness/oracle/oracle_main.c)
target_link_libraries(nmd-oracle PRIVATE nmd_harness)
target_compile_options(nmd-oracle PRIVATE ${NMD_WARNINGS})

add_executable(nmd-hybrid harness/hybrid_main.c)
target_link_libraries(nmd-hybrid PRIVATE nmd_harness)
target_compile_options(nmd-hybrid PRIVATE ${NMD_WARNINGS})

add_executable(nmd-tracediff tools/tracediff.c)
target_link_libraries(nmd-tracediff PRIVATE nmd_compare)
target_compile_options(nmd-tracediff PRIVATE ${NMD_WARNINGS})

add_executable(nmd-statediff tools/statediff.c)
target_link_libraries(nmd-statediff PRIVATE nmd_compare)
target_compile_options(nmd-statediff PRIVATE ${NMD_WARNINGS})

add_executable(nmd-statefuzz tools/statefuzz.c)
target_link_libraries(nmd-statefuzz PRIVATE nativemd)
target_compile_options(nmd-statefuzz PRIVATE ${NMD_WARNINGS})

add_executable(nmd-disasm tools/disasm.c)
target_link_libraries(nmd-disasm PRIVATE nativemd musashi_dasm)
target_compile_options(nmd-disasm PRIVATE ${NMD_WARNINGS})

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
    add_executable(test_render tests/test_render.c)
    target_link_libraries(test_render PRIVATE nativemd)
    target_compile_options(test_render PRIVATE ${NMD_WARNINGS})
    add_test(NAME render COMMAND test_render)

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
        COMMAND nmd-statediff --ignore-ram FFFD00-FFFDFF ${OUT}/oracle.nmds ${OUT}/hello.nmds)
    set_tests_properties(hello_oracle_trace hello_oracle_state PROPERTIES DEPENDS "hello;hello_oracle")

    add_executable(hello_suite tests/hello/hello_suite.c tests/hello/hello_game.c)
    target_link_libraries(hello_suite PRIVATE nmd_harness)
    target_include_directories(hello_suite PRIVATE tests/hello)
    target_compile_options(hello_suite PRIVATE ${NMD_WARNINGS})

    # The same suite on a deliberately wrong C must fail (the harness is not a no-op).
    add_executable(hello_suite_bug tests/hello/hello_suite.c tests/hello/hello_game.c)
    target_link_libraries(hello_suite_bug PRIVATE nmd_harness)
    target_include_directories(hello_suite_bug PRIVATE tests/hello)
    target_compile_definitions(hello_suite_bug PRIVATE HELLO_BUG)

    add_executable(test_hello_hybrid tests/hello/test_hello_hybrid.c tests/hello/hello_game.c)
    target_link_libraries(test_hello_hybrid PRIVATE nmd_harness)
    target_include_directories(test_hello_hybrid PRIVATE tests/hello)
    target_compile_options(test_hello_hybrid PRIVATE ${NMD_WARNINGS})

    # Capture real states at the entry of VInt and HInt while the ROM runs.
    set(STATES ${OUT}/hello_states)
    file(MAKE_DIRECTORY ${STATES})
    add_test(NAME hello_capture
        COMMAND nmd-oracle ${HELLO_ROM} --frames 10 --input ${HELLO_DIR}/hello.input
                --sym ${HELLO_SYM} --idle WaitLoop --capture VInt --capture HInt --capture ReadPad
                --capture-dir ${STATES} --capture-max 4)
    set(VINT_STATES ${STATES}/VInt.0000.nmds ${STATES}/VInt.0001.nmds ${STATES}/VInt.0002.nmds ${STATES}/VInt.0003.nmds)
    set(HINT_STATES ${STATES}/HInt.0000.nmds ${STATES}/HInt.0001.nmds ${STATES}/HInt.0002.nmds ${STATES}/HInt.0003.nmds)
    set(PAD_STATES ${STATES}/ReadPad.0000.nmds ${STATES}/ReadPad.0001.nmds ${STATES}/ReadPad.0002.nmds ${STATES}/ReadPad.0003.nmds)

    # Per-routine suites: oracle vs C from each captured state.
    add_test(NAME hello_suite_vint
        COMMAND hello_suite ${HELLO_ROM} --sym ${HELLO_SYM} --call VInt --irq ${VINT_STATES})
    add_test(NAME hello_suite_vint_replay
        COMMAND hello_suite ${HELLO_ROM} --sym ${HELLO_SYM} --call VInt --irq --replay ${VINT_STATES})
    add_test(NAME hello_suite_hint
        COMMAND hello_suite ${HELLO_ROM} --sym ${HELLO_SYM} --call HInt --irq ${HINT_STATES})
    # Fuzzed states: HInt with every value of its counter's low byte.
    set(FUZZ ${OUT}/hello_fuzz)
    file(MAKE_DIRECTORY ${FUZZ})
    add_test(NAME hello_fuzz
        COMMAND nmd-statefuzz ${STATES}/HInt.0000.nmds --out ${FUZZ} --name HInt
                --sweep FF0005.b=0-FF --rand FF0004.b=0-FF --rand d0=0-FFFFFFFF --seed 3)
    add_test(NAME hello_suite_hint_fuzz
        COMMAND ${CMAKE_COMMAND} -DSUITE=$<TARGET_FILE:hello_suite> -DROM=${HELLO_ROM}
                -DSYM=${HELLO_SYM} -DROUTINE=HInt -DGLOB=${FUZZ}/HInt.*.nmds
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_suite_glob.cmake)
    set_tests_properties(hello_fuzz PROPERTIES DEPENDS hello_capture)
    set_tests_properties(hello_suite_hint_fuzz PROPERTIES DEPENDS hello_fuzz)

    # A routine with a register ABI: d0 is compared too.
    add_test(NAME hello_suite_readpad
        COMMAND hello_suite ${HELLO_ROM} --sym ${HELLO_SYM} --call ReadPad --regs d0 ${PAD_STATES})
    add_test(NAME hello_suite_detects_bug
        COMMAND hello_suite_bug ${HELLO_ROM} --sym ${HELLO_SYM} --call VInt --irq ${VINT_STATES})
    set_tests_properties(hello_suite_detects_bug PROPERTIES WILL_FAIL TRUE)
    set_tests_properties(hello_suite_vint hello_suite_vint_replay hello_suite_hint hello_suite_readpad
        hello_suite_detects_bug
        PROPERTIES DEPENDS hello_capture)

    # Hybrid: only HInt in C, the rest runs as original code; same trace as the oracle.
    foreach(MODE hint readpad)
        add_test(NAME hello_hybrid_${MODE}
            COMMAND test_hello_hybrid ${MODE} ${HELLO_ROM} ${HELLO_SYM}
                    ${OUT}/hybrid_${MODE}.trace ${OUT}/hybrid_${MODE}.nmds)
        add_test(NAME hello_hybrid_${MODE}_trace
            COMMAND nmd-tracediff ${OUT}/oracle.trace ${OUT}/hybrid_${MODE}.trace)
        add_test(NAME hello_hybrid_${MODE}_state
            COMMAND nmd-statediff --ignore-ram FFFD00-FFFDFF ${OUT}/oracle.nmds ${OUT}/hybrid_${MODE}.nmds)
        set_tests_properties(hello_hybrid_${MODE}_trace hello_hybrid_${MODE}_state
            PROPERTIES DEPENDS "hello_hybrid_${MODE};hello_oracle")
    endforeach()
endif()
