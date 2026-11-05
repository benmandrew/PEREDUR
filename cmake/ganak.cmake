# Ganak, a prebuilt release binary. See cmake/third_party.cmake for the install
# protocol. Each version installs into a directory of its own, so a version
# bump fetches the new binary rather than finding the old one in place.

include("${CMAKE_CURRENT_LIST_DIR}/third_party.cmake")

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

# One hash per release asset, for GANAK_VERSION.
set(_ganak_sha256_mac_arm64    "66c1ce0a1c042abf0e55588585b2d6fb40648c0ae7fdd29fc3799e83aae22dc1")
set(_ganak_sha256_mac_x86_64   "f74df89d61036381e60a251eb5f5e50d4db9a670ae586554f0a1658996673401")
set(_ganak_sha256_linux_amd64  "229083515e57fe4e25c5c7f6bfb0b0e84f8b74c276464d5e2f2ddd0ca1ef7140")
set(_ganak_sha256_linux_arm64  "04491b094671f7f780877cbac540147284b37f8112e5d497fe8c2e7ae5d3b2c0")
set(_ganak_sha256 "${_ganak_sha256_${GANAK_OS}_${GANAK_ARCH}}")

set(GANAK_ARCHIVE_NAME "ganak-${GANAK_OS}-${GANAK_ARCH}.zip")
set(GANAK_DOWNLOAD_URL
    "https://github.com/meelgroup/ganak/releases/download/release/${GANAK_VERSION}/${GANAK_ARCHIVE_NAME}")
set(GANAK_INSTALL_DIR "${PEREDUR_THIRD_PARTY_DIR}/ganak/${GANAK_VERSION}")
set(GANAK_EXECUTABLE "${GANAK_INSTALL_DIR}/ganak")
set(_ganak_stamp "ganak ${GANAK_VERSION} ${GANAK_OS}-${GANAK_ARCH}")
set(_ganak_check "${GANAK_EXECUTABLE}" --version)

peredur_lock_tool("${GANAK_INSTALL_DIR}")
peredur_tool_installed(_ganak_ok
    DIR "${GANAK_INSTALL_DIR}" STAMP "${_ganak_stamp}" CHECK ${_ganak_check})
if(NOT _ganak_ok)
    file(REMOVE_RECURSE "${GANAK_INSTALL_DIR}")
    set(_ganak_archive
        "${PEREDUR_DOWNLOAD_DIR}/ganak-${GANAK_VERSION}-${GANAK_OS}-${GANAK_ARCH}.zip")
    peredur_download("${GANAK_DOWNLOAD_URL}" "${_ganak_sha256}" "${_ganak_archive}")
    file(ARCHIVE_EXTRACT INPUT "${_ganak_archive}" DESTINATION "${GANAK_INSTALL_DIR}")
    if(NOT EXISTS "${GANAK_EXECUTABLE}")
        message(FATAL_ERROR
            "${_ganak_archive} holds no top-level ganak binary; the release "
            "layout has changed")
    endif()
    file(CHMOD "${GANAK_EXECUTABLE}"
        PERMISSIONS
            OWNER_READ OWNER_WRITE OWNER_EXECUTE
            GROUP_READ GROUP_EXECUTE
            WORLD_READ WORLD_EXECUTE)
    peredur_mark_installed("${GANAK_INSTALL_DIR}" "${_ganak_stamp}" CHECK ${_ganak_check})
endif()
file(LOCK "${GANAK_INSTALL_DIR}.lock" RELEASE)

peredur_publish_tool_path(GANAK_EXECUTABLE FILEPATH "Path to the Ganak executable")
message(STATUS "Using Ganak executable: ${GANAK_EXECUTABLE}")
