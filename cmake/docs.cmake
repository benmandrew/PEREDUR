find_package(Doxygen)
find_program(SPHINX_BUILD_EXE sphinx-build)

# sphinxcontrib-katex bundles katex.min.js but not katex.min.css, and defaults
# that stylesheet to a jsdelivr URL. Staging the stylesheet and its fonts out of
# the katex package is what keeps the built site free of CDN requests.
find_path(KATEX_DIST_DIR
    NAMES katex.min.css
    PATH_SUFFIXES lib/node_modules/katex/dist share/katex/dist dist
    DOC "Directory containing katex.min.css and its fonts/ subdirectory"
)

if(DOXYGEN_FOUND AND SPHINX_BUILD_EXE AND KATEX_DIST_DIR)
    set(DOXYGEN_OUTPUT_DIR "${CMAKE_BINARY_DIR}/docs/doxygen")
    set(DOXYGEN_XML_DIR "${DOXYGEN_OUTPUT_DIR}/xml")
    set(SPHINX_OUTPUT_DIR "${CMAKE_BINARY_DIR}/docs/sphinx")

    # Sphinx copies the contents of html_static_path into the site's _static, so
    # staging under a katex/ subdirectory lands the stylesheet next to its
    # fonts/ directory -- the relative paths katex.min.css resolves against.
    set(KATEX_STATIC_DIR "${CMAKE_BINARY_DIR}/docs/_static")
    # The Nix store is read-only, and file(COPY) keeps source permissions by
    # default, which leaves a staging copy that rm -rf cannot delete. Copy with
    # default permissions, and make writable any copy an older configure left.
    if(EXISTS "${KATEX_STATIC_DIR}/katex")
        file(CHMOD_RECURSE "${KATEX_STATIC_DIR}/katex"
             FILE_PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ
             DIRECTORY_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
                                   GROUP_READ GROUP_EXECUTE
                                   WORLD_READ WORLD_EXECUTE)
    endif()
    file(COPY "${KATEX_DIST_DIR}/katex.min.css"
         DESTINATION "${KATEX_STATIC_DIR}/katex"
         NO_SOURCE_PERMISSIONS)
    file(COPY "${KATEX_DIST_DIR}/fonts"
         DESTINATION "${KATEX_STATIC_DIR}/katex"
         NO_SOURCE_PERMISSIONS)

    configure_file(
        "${CMAKE_SOURCE_DIR}/docs/Doxyfile.in"
        "${CMAKE_BINARY_DIR}/docs/Doxyfile"
        @ONLY
    )
    configure_file(
        "${CMAKE_SOURCE_DIR}/docs/conf.py.in"
        "${CMAKE_BINARY_DIR}/docs/conf.py"
        @ONLY
    )

    add_custom_target(docs
        COMMAND ${CMAKE_COMMAND} -E make_directory "${DOXYGEN_OUTPUT_DIR}"
        COMMAND ${DOXYGEN_EXECUTABLE} "${CMAKE_BINARY_DIR}/docs/Doxyfile"
        COMMAND ${CMAKE_COMMAND} -E remove_directory "${SPHINX_OUTPUT_DIR}"
        COMMAND ${SPHINX_BUILD_EXE} -q -b html
            -c "${CMAKE_BINARY_DIR}/docs"
            "${CMAKE_SOURCE_DIR}/docs"
            "${SPHINX_OUTPUT_DIR}"
        COMMENT "Generating documentation with Doxygen and Sphinx"
        VERBATIM
    )
else()
    set(_docs_missing "")
    if(NOT DOXYGEN_FOUND)
        list(APPEND _docs_missing "doxygen")
    endif()
    if(NOT SPHINX_BUILD_EXE)
        list(APPEND _docs_missing "sphinx-build")
    endif()
    if(NOT KATEX_DIST_DIR)
        list(APPEND _docs_missing "katex (set KATEX_DIST_DIR to its dist/)")
    endif()
    list(JOIN _docs_missing ", " _docs_missing_str)

    add_custom_target(docs
        COMMAND ${CMAKE_COMMAND} -E echo
            "docs requires: ${_docs_missing_str}"
        COMMAND ${CMAKE_COMMAND} -E false
        COMMENT "Documentation generation requires doxygen, sphinx-build and katex"
        VERBATIM
    )
endif()
