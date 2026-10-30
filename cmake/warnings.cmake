# Applies the project's warning flags to every compiled target defined in this
# source tree. Included last from the root CMakeLists, it walks the directory
# tree instead of naming targets, so a new binary or library picks the flags up
# without an edit here. FetchContent dependencies are added as subdirectories
# too, so a directory outside the source tree, or inside the build tree (where
# FetchContent unpacks), is skipped: their warnings are not ours to fix.
# ExternalProject trees build in processes of their own and never appear.

if(MSVC)
    set(PEREDUR_WARNING_FLAGS /W4 /permissive-)
else()
    set(PEREDUR_WARNING_FLAGS -Wall -Wextra -Wpedantic -Werror)
endif()

function(peredur_apply_warnings dir)
    cmake_path(IS_PREFIX CMAKE_SOURCE_DIR "${dir}" NORMALIZE in_source)
    set(in_binary FALSE)
    if(NOT CMAKE_BINARY_DIR STREQUAL CMAKE_SOURCE_DIR)
        cmake_path(IS_PREFIX CMAKE_BINARY_DIR "${dir}" NORMALIZE in_binary)
    endif()
    if(NOT in_source OR in_binary)
        return()
    endif()
    get_property(targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(target IN LISTS targets)
        get_target_property(type ${target} TYPE)
        if(type MATCHES "^(EXECUTABLE|STATIC_LIBRARY|SHARED_LIBRARY|MODULE_LIBRARY|OBJECT_LIBRARY)$")
            target_compile_options(${target} PRIVATE ${PEREDUR_WARNING_FLAGS})
        endif()
    endforeach()
    get_property(subdirs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
    foreach(subdir IN LISTS subdirs)
        peredur_apply_warnings("${subdir}")
    endforeach()
endfunction()

peredur_apply_warnings("${CMAKE_SOURCE_DIR}")
