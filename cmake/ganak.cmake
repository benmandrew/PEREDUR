set(GANAK_VERSION "2.5.3")

if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(GANAK_OS "mac")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(GANAK_OS "linux")
else()
    message(FATAL_ERROR "Unsupported OS for Ganak: ${CMAKE_SYSTEM_NAME}")
endif()

if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$")
    set(GANAK_ARCH "arm64")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$")
    if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        set(GANAK_ARCH "x86_64")
    else()
        set(GANAK_ARCH "amd64")
    endif()
else()
    message(FATAL_ERROR "Unsupported architecture for Ganak: ${CMAKE_SYSTEM_PROCESSOR}")
endif()

set(GANAK_ARCHIVE_NAME "ganak-${GANAK_OS}-${GANAK_ARCH}.zip")
set(GANAK_DOWNLOAD_URL
    "https://github.com/meelgroup/ganak/releases/download/release/${GANAK_VERSION}/${GANAK_ARCHIVE_NAME}")
set(GANAK_ROOT_DIR "${PEREDUR_THIRD_PARTY_DIR}/ganak")
set(GANAK_ARCHIVE_PATH "${GANAK_ROOT_DIR}/${GANAK_ARCHIVE_NAME}")
set(GANAK_EXTRACT_DIR "${GANAK_ROOT_DIR}/extracted")

file(MAKE_DIRECTORY "${GANAK_ROOT_DIR}")

if(NOT EXISTS "${GANAK_ARCHIVE_PATH}")
    message(STATUS "Downloading Ganak from ${GANAK_DOWNLOAD_URL}")
    file(DOWNLOAD
        "${GANAK_DOWNLOAD_URL}"
        "${GANAK_ARCHIVE_PATH}"
        STATUS ganak_download_status
        TLS_VERIFY ON
    )

    list(GET ganak_download_status 0 ganak_download_code)
    list(GET ganak_download_status 1 ganak_download_message)
    if(NOT ganak_download_code EQUAL 0)
        message(FATAL_ERROR "Failed to download Ganak: ${ganak_download_message}")
    endif()
endif()

if(NOT EXISTS "${GANAK_EXTRACT_DIR}")
    file(MAKE_DIRECTORY "${GANAK_EXTRACT_DIR}")
endif()

file(GLOB_RECURSE GANAK_CANDIDATES
    "${GANAK_EXTRACT_DIR}/ganak"
    "${GANAK_EXTRACT_DIR}/ganak-*"
    "${GANAK_EXTRACT_DIR}/*/ganak"
    "${GANAK_EXTRACT_DIR}/*/ganak-*"
)

if(NOT GANAK_CANDIDATES)
    message(STATUS "Extracting Ganak archive")
    file(ARCHIVE_EXTRACT
        INPUT "${GANAK_ARCHIVE_PATH}"
        DESTINATION "${GANAK_EXTRACT_DIR}"
    )

    file(GLOB_RECURSE GANAK_CANDIDATES
        "${GANAK_EXTRACT_DIR}/ganak"
        "${GANAK_EXTRACT_DIR}/ganak-*"
        "${GANAK_EXTRACT_DIR}/*/ganak"
        "${GANAK_EXTRACT_DIR}/*/ganak-*"
    )
endif()

if(NOT GANAK_CANDIDATES)
    message(FATAL_ERROR "Ganak executable was not found after extraction.")
endif()

list(GET GANAK_CANDIDATES 0 GANAK_EXECUTABLE)

if(UNIX)
    file(CHMOD
        "${GANAK_EXECUTABLE}"
        PERMISSIONS
            OWNER_READ OWNER_WRITE OWNER_EXECUTE
            GROUP_READ GROUP_EXECUTE
            WORLD_READ WORLD_EXECUTE
    )
endif()

set(GANAK_EXECUTABLE "${GANAK_EXECUTABLE}" CACHE FILEPATH "Path to Ganak executable")
message(STATUS "Using Ganak executable: ${GANAK_EXECUTABLE}")
