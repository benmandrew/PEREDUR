# The object files tools/spot-forkserver links, taken from Spot's build tree
# by cmake/spot.cmake before it deletes that tree. See "Tool subprocesses" in
# docs/dev/performance.md for what the server is.
#
# Spot's tools were written as separate programs, so linking four of them into
# one binary has two hazards, and this file deals with both:
#
# - Each defines `main`. Each copy is renamed to <tool>_main.
# - Two tools can define different types under one global-scope name, whose
#   inline members are then weak symbols of the same name. The linker keeps one
#   and silently gives every tool that copy. ltlfilt.cc and autfilt.cc both
#   define a `struct opt_t`, and with ltlfilt's destructor every autfilt child
#   crashed on returning from main. clang emits only the base-object destructor
#   (D2), and GCC also the complete-object one (D1) and the comdat group (D5)
#   holding both, so autfilt's copies are renamed by pattern, not by a fixed
#   list. Every remaining weak symbol that two tool objects define then fails
#   the build, unless it comes from a shared header and has the same size in
#   each, which is what catches the next opt_t on a Spot or compiler upgrade.

include_guard(GLOBAL)

set(SPOT_FORKSERVER_TOOLS ltlsynt ltlfilt ltl2tgba autfilt)

# Weak symbols that every tool may define, being instantiated from headers the
# tools share: entities of Spot's namespace, of BuDDy's global `bdd` class, of
# the standard library and its GNU extensions, and their vtables, type
# information, guard variables and function-local statics (the optional
# TV/TI/TS/GV/GVZ/Z after _Z), plus the personality reference and clang's
# terminate helper. A global-namespace type's vtable (_ZTV3foo) matches none.
# <new>'s placement operator new and delete are global functions, not std's.
# GCC emits them as weak symbols where it does not inline them, as in an
# unoptimised object.
set(_spot_shared_weak_symbol
    "^(_Z(TV|TI|TS|GVZ|GV|Z)?(N[VK]*)?(4spot|St|9__gnu_cxx|3bdd)|DW\\.ref\\.|__clang_call_terminate$|_Zn[wa]mPv$|_Zd[la]PvS_$)")
# A shared template instantiated over a global-namespace type, as
# std::vector<foo> is (_ZNSt6vectorI3fooSaIS0_EE...), is the tool's own: its
# first template argument is then a bare length-prefixed name. The global types
# of the headers the tools share are exempt: BuDDy's bdd, and job from Spot's
# bin/common_finput.hh, which every tool reading formulae holds in a vector.
# Later arguments are not checked, since telling them apart needs a demangler.
set(_spot_global_template_argument "I[0-9]")
set(_spot_shared_global_types I3bdd I3job)

# Whether weak symbol `name` may be defined by more than one tool.
function(_spot_weak_symbol_shared name out)
    set(shared FALSE)
    if(name MATCHES "${_spot_shared_weak_symbol}")
        set(rest "${name}")
        foreach(type IN LISTS _spot_shared_global_types)
            string(REPLACE "${type}" "" rest "${rest}")
        endforeach()
        if(NOT rest MATCHES "${_spot_global_template_argument}")
            set(shared TRUE)
        endif()
    endif()
    set(${out} ${shared} PARENT_SCOPE)
endfunction()

# peredur_spot_weak_symbol_clashes(<out> <object>...)
#
# The weak symbols that two of the objects define, other than shared ones of
# one size in each, one "<name> (<a>: size 0x<n>, <b>: size 0x<m>)" each. An
# object is named by its file name without .o.
function(peredur_spot_weak_symbol_clashes out)
    set(clashes "")
    foreach(object IN LISTS ARGN)
        get_filename_component(tool "${object}" NAME_WE)
        _spot_weak_symbols("${object}" weak)
        foreach(entry IN LISTS weak)
            string(REGEX MATCH "^(.*)=([0-9a-fA-F]+)$" _ "${entry}")
            set(name "${CMAKE_MATCH_1}")
            set(size "${CMAKE_MATCH_2}")
            set(key "_spot_weak_${name}")
            if(DEFINED "${key}")
                string(REGEX MATCH "^([^=]*)=(.*)$" _ "${${key}}")
                set(first_tool "${CMAKE_MATCH_1}")
                set(first_size "${CMAKE_MATCH_2}")
                _spot_weak_symbol_shared("${name}" shared)
                if(NOT shared OR NOT size STREQUAL first_size)
                    list(APPEND clashes
                        "${name} (${first_tool}: size 0x${first_size}, ${tool}: size 0x${size})")
                endif()
            else()
                set("${key}" "${tool}=${size}")
            endif()
        endforeach()
    endforeach()
    set(${out} "${clashes}" PARENT_SCOPE)
endfunction()

# The weak symbols one object defines, as a list of <name>=<size>.
function(_spot_weak_symbols object out)
    execute_process(
        COMMAND "${CMAKE_NM}" -S --defined-only "${object}"
        OUTPUT_VARIABLE listing
        RESULT_VARIABLE rc
        ERROR_VARIABLE error)
    if(NOT rc STREQUAL "0")
        message(FATAL_ERROR "${CMAKE_NM} failed on ${object}: ${error}")
    endif()
    string(REPLACE "\n" ";" lines "${listing}")
    set(symbols "")
    foreach(line IN LISTS lines)
        # <address> <size> <type> <name>; a symbol with no size has no second
        # field and cannot be one of the inline definitions this looks for.
        if(line MATCHES "^[0-9a-fA-F]+ ([0-9a-fA-F]+) [WVu] (.+)$")
            list(APPEND symbols "${CMAKE_MATCH_2}=${CMAKE_MATCH_1}")
        endif()
    endforeach()
    set(${out} "${symbols}" PARENT_SCOPE)
endfunction()

# peredur_stash_spot_forkserver_objects(<spot-build-dir> <dest-dir> <log-dir>)
function(peredur_stash_spot_forkserver_objects build_dir dest_dir log_dir)
    if(NOT CMAKE_OBJCOPY OR NOT CMAKE_NM)
        message(FATAL_ERROR
            "The Spot fork server needs objcopy and nm (CMAKE_OBJCOPY "
            "'${CMAKE_OBJCOPY}', CMAKE_NM '${CMAKE_NM}'); binutils provides both.")
    endif()
    file(REMOVE_RECURSE "${dest_dir}")
    file(MAKE_DIRECTORY "${dest_dir}")
    foreach(tool IN LISTS SPOT_FORKSERVER_TOOLS)
        set(source "${build_dir}/bin/${tool}.o")
        set(renames --redefine-sym "main=${tool}_main")
        if(tool STREQUAL "autfilt")
            execute_process(
                COMMAND "${CMAKE_NM}" "${source}"
                OUTPUT_VARIABLE listing
                RESULT_VARIABLE rc)
            if(NOT rc STREQUAL "0")
                message(FATAL_ERROR "${CMAKE_NM} failed on ${source}")
            endif()
            # A length prefix of exactly 5 before opt_t, so a longer name such
            # as opt_terminal_sccs (13opt_terminal...) is never touched. The
            # replacement keeps the length, so the mangling stays valid.
            string(REGEX MATCHALL "[^ \n]*[^0-9]5opt_t[^ \n]*" names "${listing}")
            list(REMOVE_DUPLICATES names)
            foreach(name IN LISTS names)
                string(REGEX REPLACE "([^0-9])5opt_t" "\\15opt_a" renamed "${name}")
                list(APPEND renames --redefine-sym "${name}=${renamed}")
            endforeach()
        endif()
        peredur_run_step("${log_dir}/forkserver-objcopy-${tool}.log"
            COMMAND "${CMAKE_OBJCOPY}" ${renames} "${source}" "${dest_dir}/${tool}.o")
    endforeach()
    file(COPY "${build_dir}/bin/libcommon.a" "${build_dir}/lib/.libs/libgnu.a"
        DESTINATION "${dest_dir}")

    set(objects "")
    foreach(tool IN LISTS SPOT_FORKSERVER_TOOLS)
        list(APPEND objects "${dest_dir}/${tool}.o")
    endforeach()
    peredur_spot_weak_symbol_clashes(clashes ${objects})
    if(clashes)
        list(JOIN clashes "\n  " report)
        message(FATAL_ERROR
            "Two Spot tool objects define the same weak symbol, so the fork "
            "server would run one tool's definition in the other:\n  ${report}\n"
            "Rename the clashing copies in cmake/spot_forkserver_objects.cmake, "
            "as autfilt's opt_t is.")
    endif()
endfunction()
