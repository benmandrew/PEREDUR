# Run at install time on macOS (cmake/install.cmake). Spot and black are built
# into PEREDUR_THIRD_PARTY_DIR, and dyld finds their libraries by the absolute
# install names and rpaths baked in there, ignoring the LD_LIBRARY_PATH that
# cmake/tool-wrapper.sh.in sets. An installed copy therefore still loaded from
# the build machine's third-party tree. This rewrites each installed Mach-O file
# under DIR so that every reference into OLD_PREFIX names a sibling lib/
# directory relative to the file itself, then re-signs it: arm64 macOS refuses
# to run a binary whose ad-hoc signature install_name_tool has invalidated.
#
# Libraries outside OLD_PREFIX (black's z3 and fmt, at /nix/store or Homebrew
# paths) are not shipped, and are left as they are.
#
#   cmake -DDIR=<installed tool dir> -DOLD_PREFIX=<third-party dir> -P relocate_macho.cmake

find_program(_install_name_tool install_name_tool REQUIRED)
find_program(_otool otool REQUIRED)
find_program(_codesign codesign PATHS /usr/bin REQUIRED)

# OLD_PREFIX goes into regular expressions below; a path holds dots at least.
string(REGEX REPLACE "([][+.*?()^$|])" "\\\\\\1" _prefix_re "${OLD_PREFIX}")

file(GLOB_RECURSE _files LIST_DIRECTORIES FALSE "${DIR}/libexec/*" "${DIR}/lib/*")
foreach(_file IN LISTS _files)
    if(IS_SYMLINK "${_file}")
        continue()
    endif()
    execute_process(COMMAND "${_otool}" -L "${_file}"
        OUTPUT_VARIABLE _deps RESULT_VARIABLE _rc ERROR_QUIET)
    if(NOT _rc EQUAL 0)
        continue()  # not a Mach-O file: a pkgconfig file, a wrapper script
    endif()
    get_filename_component(_parent "${_file}" DIRECTORY)
    get_filename_component(_parent_name "${_parent}" NAME)
    if(_parent_name STREQUAL "lib")
        set(_lib_ref "@loader_path")
    else()
        set(_lib_ref "@loader_path/../lib")
    endif()

    set(_args "")
    string(REPLACE "\n" ";" _dep_lines "${_deps}")
    foreach(_line IN LISTS _dep_lines)
        if(_line MATCHES "^[ \t]+(${_prefix_re}[^ ]*/([^/ ]+)) \\(")
            list(APPEND _args -change "${CMAKE_MATCH_1}" "${_lib_ref}/${CMAKE_MATCH_2}")
        endif()
    endforeach()

    # A library's own id, when it names the third-party tree.
    execute_process(COMMAND "${_otool}" -D "${_file}"
        OUTPUT_VARIABLE _id_out ERROR_QUIET)
    string(REPLACE "\n" ";" _id_lines "${_id_out}")
    list(LENGTH _id_lines _n)
    if(_n GREATER 1)
        list(GET _id_lines 1 _id)
        if(_id MATCHES "^${_prefix_re}.*/([^/]+)$")
            list(APPEND _args -id "@rpath/${CMAKE_MATCH_1}")
        endif()
    endif()

    # Rpaths into the third-party tree, as black's libblack reference uses.
    execute_process(COMMAND "${_otool}" -l "${_file}"
        OUTPUT_VARIABLE _load_cmds ERROR_QUIET)
    string(REGEX MATCHALL "path ${_prefix_re}[^ ]*" _rpaths "${_load_cmds}")
    list(REMOVE_DUPLICATES _rpaths)
    foreach(_rpath IN LISTS _rpaths)
        string(REPLACE "path " "" _rpath "${_rpath}")
        list(APPEND _args -delete_rpath "${_rpath}")
    endforeach()
    # black is built with this rpath already, and install_name_tool refuses a
    # duplicate.
    string(FIND "${_load_cmds}" "path ${_lib_ref} " _has_lib_ref)
    if(_rpaths AND _has_lib_ref EQUAL -1)
        list(APPEND _args -add_rpath "${_lib_ref}")
    endif()

    if(_args)
        execute_process(COMMAND "${_install_name_tool}" ${_args} "${_file}"
            RESULT_VARIABLE _rc ERROR_VARIABLE _err)
        if(NOT _rc EQUAL 0)
            message(FATAL_ERROR "install_name_tool failed on ${_file}: ${_err}")
        endif()
        execute_process(COMMAND "${_codesign}" --force --sign - "${_file}"
            RESULT_VARIABLE _rc ERROR_VARIABLE _err)
        if(NOT _rc EQUAL 0)
            message(FATAL_ERROR "codesign failed on ${_file}: ${_err}")
        endif()
    endif()
endforeach()
