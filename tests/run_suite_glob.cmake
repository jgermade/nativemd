# Runs a suite binary on every state file matching GLOB (CTest cannot glob at
# test time). Variables: SUITE, ROM, SYM, ROUTINE, GLOB, optional ARGS.
file(GLOB STATES ${GLOB})
list(LENGTH STATES N)
if(N EQUAL 0)
    message(FATAL_ERROR "no state files match ${GLOB}")
endif()
execute_process(COMMAND ${SUITE} ${ROM} --sym ${SYM} --call ${ROUTINE} --irq ${ARGS} ${STATES}
                RESULT_VARIABLE RESULT OUTPUT_VARIABLE OUT ERROR_VARIABLE OUT)
string(REGEX MATCH "[^\n]*states pass" SUMMARY "${OUT}")
message(STATUS "${SUMMARY}")
if(NOT RESULT EQUAL 0)
    message(FATAL_ERROR "${OUT}")
endif()
