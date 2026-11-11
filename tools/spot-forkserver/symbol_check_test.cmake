# The duplicate weak-symbol check in cmake/spot_forkserver_objects.cmake, over
# two small objects compiled here: each pair below is compiled into a.o and
# b.o, and the check must find a clash exactly when the pair says so.
#
#   cmake -DCXX=<compiler> -DCMAKE_NM=<nm> -DSOURCE_DIR=<repo> -DWORK=<dir>
#         -P symbol_check_test.cmake

include("${SOURCE_DIR}/cmake/spot_forkserver_objects.cmake")

# Each case: a name, whether it must clash, and one translation unit that is
# compiled twice, as two tools including one header would compile it, with
# TOOL set to tell the copies apart where a case needs them to differ.
set(_shared_spot [=[
namespace spot { struct shared { virtual ~shared() {} }; }
spot::shared* make() { return new spot::shared; }
]=])
set(_shared_std [=[
#include <string>
#include <vector>
std::size_t use(std::vector<int>& v, const std::string& s) { v.push_back(1); return v.size() + s.size(); }
]=])
set(_global_vtable [=[
struct foo { virtual ~foo() {} };
foo* make() { return new foo; }
]=])
set(_global_template_argument [=[
#include <vector>
struct foo { int x; };
std::size_t use(std::vector<foo>& v) { v.push_back(foo{TOOL}); return v.size(); }
]=])
set(_global_inline [=[
struct opt_t { ~opt_t() { volatile int n = TOOL; (void)n; } };
void use() { opt_t o; }
]=])

set(failures "")
foreach(case IN ITEMS
        "shared_spot;FALSE" "shared_std;FALSE" "global_vtable;TRUE"
        "global_template_argument;TRUE" "global_inline;TRUE")
    list(GET case 0 name)
    list(GET case 1 expected)
    set(dir "${WORK}/${name}")
    file(REMOVE_RECURSE "${dir}")
    file(MAKE_DIRECTORY "${dir}")
    file(WRITE "${dir}/case.cpp" "${_${name}}")
    foreach(tool IN ITEMS a b)
        if(tool STREQUAL "a")
            set(value 1)
        else()
            set(value 2)
        endif()
        execute_process(
            COMMAND "${CXX}" -std=c++17 -O0 -c "-DTOOL=${value}"
                "${dir}/case.cpp" -o "${dir}/${tool}.o"
            RESULT_VARIABLE rc
            ERROR_VARIABLE error)
        if(NOT rc EQUAL 0)
            message(FATAL_ERROR "${name}: compiling ${tool}.o failed: ${error}")
        endif()
    endforeach()
    peredur_spot_weak_symbol_clashes(clashes "${dir}/a.o" "${dir}/b.o")
    if(clashes)
        set(found TRUE)
    else()
        set(found FALSE)
    endif()
    if(NOT found STREQUAL expected)
        list(APPEND failures "${name}: expected a clash ${expected}, found ${found} (${clashes})")
    endif()
endforeach()
if(failures)
    list(JOIN failures "\n  " report)
    message(FATAL_ERROR "The weak-symbol check is wrong:\n  ${report}")
endif()
message(STATUS "The weak-symbol check passed all cases")
