include(FetchContent)

set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(EIGEN_BUILD_DOC OFF CACHE BOOL "" FORCE)
set(EIGEN_BUILD_PKGCONFIG OFF CACHE BOOL "" FORCE)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)

# Each dependency is a release archive pinned by SHA-256, so a re-published
# or altered artefact fails the download instead of being built. A git tag can
# be moved; a hash cannot.
FetchContent_Declare(
    cpptrace
    URL https://github.com/jeremy-rifkin/cpptrace/archive/refs/tags/v0.7.5.tar.gz
    URL_HASH SHA256=7df0cae3d7da9be2dc82616292cc86a4a08a8209898716231aef80477a8ca35d
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

# cpptrace fetches zstd and libdwarf itself. libdwarf is pinned to a commit
# (CPPTRACE_LIBDWARF_TAG); zstd is a URL with no hash, so it is declared here
# first, with one. The first declaration of a name wins, and SOURCE_SUBDIR
# matches cpptrace's own.
FetchContent_Declare(
    zstd
    URL https://github.com/facebook/zstd/releases/download/v1.5.6/zstd-1.5.6.tar.gz
    URL_HASH SHA256=8c29e06cf42aacc1eafc4077ae2ec6c6fcb96a626157e0593d5e82a34fd403c1
    SOURCE_SUBDIR build/cmake
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

FetchContent_Declare(
    eigen
    URL https://gitlab.com/libeigen/eigen/-/archive/3.4.0/eigen-3.4.0.tar.gz
    URL_HASH SHA256=8586084f71f9bde545ee7fa6d00288b264a2b7ac3607b974e54d13e7162c1c72
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

# Must be set before FetchContent_MakeAvailable(cpptrace) so cpptrace's
# CMakeLists sees the value when it first runs.
set(CPPTRACE_UNWIND_WITH_LIBUNWIND ON CACHE BOOL "" FORCE)

# Eigen's blas/ subdir unconditionally calls check_language(Fortran).
# On macOS, gfortran hangs during the link test. Pre-setting this cache
# variable to empty makes check_language return "not found" immediately
# without running any compiler test, so enable_language(Fortran) is skipped.
set(CMAKE_Fortran_COMPILER "" CACHE FILEPATH "" FORCE)
FetchContent_MakeAvailable(eigen)
unset(CMAKE_Fortran_COMPILER CACHE)
FetchContent_MakeAvailable(cpptrace)

FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

FetchContent_Declare(
    tomlplusplus
    URL https://github.com/marzer/tomlplusplus/archive/refs/tags/v3.4.0.tar.gz
    URL_HASH SHA256=8517f65938a4faae9ccf8ebb36631a38c1cadfb5efa85d9a72e15b9e97d25155
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(tomlplusplus)
