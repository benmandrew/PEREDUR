# find_program and find_path cache their result and never search again while
# the entry holds a value. Every developer tool here comes from the Nix dev
# shell at a /nix/store path that changes whenever flake.lock moves, so a tree
# configured before a flake update kept linting and formatting with the old
# LLVM, and once that store path was garbage-collected the tool vanished
# without the cache noticing.
#
# peredur_refresh_found(<var>) runs before the find call and drops the entry
# when this project found it itself (its value equals the one recorded after
# the last search) or when the file it names no longer exists. A value the user
# passed with -D<var>=... differs from the recorded one and is kept.
# peredur_record_found(<var>) runs after the find call and records the result.
#
# A tree configured before this file existed has no record. There the entry is
# dropped once, since nothing distinguishes it from a stale search result; a
# fresh tree has no entry unless the user supplied one, so that one is kept.

if(NOT DEFINED _PEREDUR_FIND_TOOL_LEGACY)
    if(DEFINED CACHE{PEREDUR_THIRD_PARTY_DIR}
       AND NOT DEFINED CACHE{_PEREDUR_FIND_TOOL_SEEN})
        set(_PEREDUR_FIND_TOOL_LEGACY TRUE)
    else()
        set(_PEREDUR_FIND_TOOL_LEGACY FALSE)
    endif()
    set(_PEREDUR_FIND_TOOL_SEEN TRUE CACHE INTERNAL "")
endif()

function(peredur_refresh_found var)
    if(NOT DEFINED CACHE{${var}})
        return()
    endif()
    set(value "$CACHE{${var}}")
    if(DEFINED CACHE{_PEREDUR_FOUND_${var}})
        set(stale FALSE)
        if(value STREQUAL "$CACHE{_PEREDUR_FOUND_${var}}")
            set(stale TRUE)
        endif()
    else()
        set(stale ${_PEREDUR_FIND_TOOL_LEGACY})
    endif()
    if(value AND NOT EXISTS "${value}")
        set(stale TRUE)
    endif()
    if(stale)
        unset(${var} CACHE)
    endif()
endfunction()

function(peredur_record_found var)
    set(_PEREDUR_FOUND_${var} "$CACHE{${var}}" CACHE INTERNAL "")
endfunction()

# The common case: refresh, find_program, record.
macro(peredur_find_program var)
    peredur_refresh_found(${var})
    find_program(${var} ${ARGN})
    peredur_record_found(${var})
endmacro()
