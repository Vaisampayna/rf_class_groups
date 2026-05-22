cmake_minimum_required(VERSION 3.16)
project(cg_ahe CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Optimisation flags ───────────────────────────────────────
add_compile_options(-O3 -march=native)

# ── Locate GMP and OpenSSL (required by BICYCL) ──────────────
find_package(OpenSSL REQUIRED)
find_library(GMP_LIB gmp REQUIRED)
find_library(GMPXX_LIB gmpxx REQUIRED)
find_path(GMP_INCLUDE gmp.h)

# ── BICYCL is header-only; point to its source tree ──────────
# Set BICYCL_DIR to the cloned BICYCL repo root, or override on
# the cmake command line: cmake -DBICYCL_DIR=/path/to/bicycl ..
if(NOT DEFINED BICYCL_DIR)
    set(BICYCL_DIR "${CMAKE_CURRENT_SOURCE_DIR}"
        CACHE PATH "Path to BICYCL include directory (containing bicycl.hpp)")
endif()

# ── INTERFACE target for BICYCL ──────────────────────────────
add_library(bicycl_iface INTERFACE)
target_include_directories(bicycl_iface INTERFACE ${BICYCL_DIR})
target_link_libraries(bicycl_iface INTERFACE
    OpenSSL::Crypto
    ${GMPXX_LIB}
    ${GMP_LIB}
    pthread
)

# ── Main test executable ──────────────────────────────────────
add_executable(test_cg_ahe test_cg_ahe.cpp)
target_include_directories(test_cg_ahe PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_cg_ahe PRIVATE bicycl_iface)
