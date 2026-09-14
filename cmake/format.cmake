find_program(CLANG_FORMAT_EXE NAMES clang-format)

set(PEREDUR_FORMAT_GLOBS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/test/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test/*.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/test/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/bench/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/bench/*.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/bench/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/fuzz/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/fuzz/*.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/fuzz/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/*.h
)

file(GLOB_RECURSE PEREDUR_FORMAT_FILES CONFIGURE_DEPENDS ${PEREDUR_FORMAT_GLOBS})

if(CLANG_FORMAT_EXE)
    add_custom_target(format
        COMMAND ${CLANG_FORMAT_EXE} -i ${PEREDUR_FORMAT_FILES}
        COMMENT "Formatting C++ sources with clang-format"
        VERBATIM
    )

    add_custom_target(format-ci
        COMMAND ${CLANG_FORMAT_EXE} --dry-run --Werror ${PEREDUR_FORMAT_FILES}
        COMMENT "Checking C++ sources are formatted with clang-format"
        VERBATIM
    )
else()
    add_custom_target(format
        COMMAND ${CMAKE_COMMAND} -E echo "clang-format was not found on PATH"
        COMMAND ${CMAKE_COMMAND} -E false
        COMMENT "Formatting requires clang-format"
        VERBATIM
    )

    add_custom_target(format-ci
        COMMAND ${CMAKE_COMMAND} -E echo "clang-format was not found on PATH"
        COMMAND ${CMAKE_COMMAND} -E false
        COMMENT "Format check requires clang-format"
        VERBATIM
    )
endif()
