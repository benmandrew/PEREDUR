# Resolves the git commit and writes it into the generated version header.
#
# Runs in script mode (-P) from a custom target, so it re-resolves on every
# build. Doing it at configure time instead would bake in whatever HEAD was
# when cmake last ran, and `cmake --build` after a new commit would keep
# reporting the old hash — which is the failure this whole mechanism exists to
# prevent.
#
# Expects GIT_EXECUTABLE, SOURCE_DIR, TEMPLATE_FILE and OUTPUT_FILE, and
# optionally COMMIT_OVERRIDE / COMMIT_SHORT_OVERRIDE / DIRTY_OVERRIDE (see
# cmake/version.cmake, which passes through -DPEREDUR_GIT_COMMIT and friends).

set(PEREDUR_GIT_COMMIT "unknown")
set(PEREDUR_GIT_COMMIT_SHORT "unknown")
set(PEREDUR_GIT_DIRTY "false")

function(_peredur_git out_var)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${SOURCE_DIR}" ${ARGN}
        OUTPUT_VARIABLE _out
        RESULT_VARIABLE _rc
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(NOT _rc EQUAL 0)
        set(_out "")
    endif()
    set(${out_var} "${_out}" PARENT_SCOPE)
endfunction()

set(_resolved_from_git FALSE)

if(GIT_EXECUTABLE AND EXISTS "${GIT_EXECUTABLE}")
    # A source tree exported from a tarball, or vendored into another
    # checkout, is not a work tree; fall back rather than fail the build.
    _peredur_git(_inside rev-parse --is-inside-work-tree)
    if(_inside STREQUAL "true")
        _peredur_git(_full rev-parse HEAD)
        _peredur_git(_short rev-parse --short HEAD)
        if(_full)
            set(PEREDUR_GIT_COMMIT "${_full}")
            set(_resolved_from_git TRUE)
        endif()
        if(_short)
            set(PEREDUR_GIT_COMMIT_SHORT "${_short}")
        endif()
        # --untracked-files=no on purpose: an untracked file is not compiled
        # into anything, so it cannot explain a binary that disagrees with its
        # commit. Modified tracked files can, and are what the flag reports.
        _peredur_git(_status status --porcelain --untracked-files=no)
        if(NOT _status STREQUAL "")
            set(PEREDUR_GIT_DIRTY "true")
        endif()
    endif()
endif()

# The override exists for builds whose source is a copy rather than a work
# tree — a Docker image built from a COPY of the sources, most of all, where
# shipping .git would put the whole history in a layer and invalidate the cache
# on every commit. It supplies what git cannot answer there; it does not get to
# contradict what git can. A build that has both and disagrees is either a
# mistyped argument or a stale one, and both are the same failure this
# mechanism exists to catch, so it stops the build rather than picking a side.
if(COMMIT_OVERRIDE)
    if(_resolved_from_git AND NOT COMMIT_OVERRIDE STREQUAL PEREDUR_GIT_COMMIT)
        message(FATAL_ERROR
            "PEREDUR_GIT_COMMIT is ${COMMIT_OVERRIDE}, but ${SOURCE_DIR} is a "
            "work tree at ${PEREDUR_GIT_COMMIT}. Drop the override and let git "
            "answer, or build from a source copy that is not a work tree.")
    endif()
    set(PEREDUR_GIT_COMMIT "${COMMIT_OVERRIDE}")
    if(COMMIT_SHORT_OVERRIDE)
        set(PEREDUR_GIT_COMMIT_SHORT "${COMMIT_SHORT_OVERRIDE}")
    else()
        string(SUBSTRING "${COMMIT_OVERRIDE}" 0 7 PEREDUR_GIT_COMMIT_SHORT)
    endif()
    # An override asserts that the source is a clean checkout of the commit it
    # names, which is what the image build guarantees by exporting from one.
    # -DPEREDUR_GIT_DIRTY=true is there for a build that cannot make that
    # promise, so the binary says so rather than the caller having to remember.
    # A work tree that git already read as dirty keeps that verdict: the
    # commits agreeing says nothing about the files on top of them, and this is
    # the one flag a caller must not be able to clear by hand.
    if(DIRTY_OVERRIDE)
        set(PEREDUR_GIT_DIRTY "true")
    elseif(NOT _resolved_from_git)
        set(PEREDUR_GIT_DIRTY "false")
    endif()
endif()

# configure_file leaves the output untouched when the rendered content is
# identical, so a build that changed no commit does not invalidate every
# object file that transitively depends on the header.
configure_file("${TEMPLATE_FILE}" "${OUTPUT_FILE}" @ONLY)
