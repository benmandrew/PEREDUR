# Run after every link of spot-forkserver: one ltlfilt request and one autfilt
# request through the server, each of which must come back as the tool's own
# answer. autfilt is here because its child once crashed on return from main
# from a one-definition clash the duplicate-symbol check now catches.
#
#   cmake -DSERVER=<binary> -DREQUEST=<file> -P round_trip.cmake

execute_process(
    COMMAND "${SERVER}"
    INPUT_FILE "${REQUEST}"
    OUTPUT_VARIABLE reply
    ERROR_VARIABLE errors
    RESULT_VARIABLE rc
    TIMEOUT 60)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "spot-forkserver exited ${rc}: ${errors}")
endif()
# ltlfilt echoes the formula; autfilt --is-empty on an automaton accepting
# nothing prints it and exits 0.
if(NOT reply MATCHES "^ready\n0 [0-9]+ 2\na\n0 [0-9]+ [0-9]+\nHOA: v1")
    message(FATAL_ERROR "spot-forkserver failed its round trip:\n${reply}${errors}")
endif()
