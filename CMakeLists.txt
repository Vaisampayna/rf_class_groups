cmake_minimum_required(VERSION 3.16)
project(reverse_firewall_cg CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Optimisation ───────────────────────────────────────────────
# Keep portable CPU flags by default. `-march=native` can emit instructions
# unsupported by another LAN machine if binaries are copied between hosts.
option(CG_ENABLE_NATIVE_ARCH "Build with -march=native for this exact CPU" OFF)
add_compile_options(-O3 -funroll-loops)
if(CG_ENABLE_NATIVE_ARCH)
    add_compile_options(-march=native)
endif()

# ── Dependencies ───────────────────────────────────────────────
find_package(OpenSSL REQUIRED)
find_package(OpenMP REQUIRED)
find_library(GMP_LIB    gmp    REQUIRED)
find_library(GMPXX_LIB  gmpxx  REQUIRED)
option(CG_USE_NTL_POLY "Use NTL for large finite-field polynomial operations" ON)
if(CG_USE_NTL_POLY)
    find_library(NTL_LIB ntl)
    if(NTL_LIB)
        message(STATUS "Using NTL for large PSI polynomial operations: ${NTL_LIB}")
    else()
        message(WARNING "CG_USE_NTL_POLY=ON but libntl was not found; falling back to built-in polynomial code")
    endif()
endif()

# ── Vendored CG-AHE/BICYCL wrapper ─────────────────────────────
set(BICYCL_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/third_party"
    CACHE PATH "Path to directory containing bicycl.hpp and cg_ahe.hpp")
set(CG_AHE_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/cg_ahe")

add_library(bicycl_iface INTERFACE)
target_include_directories(bicycl_iface INTERFACE
    ${BICYCL_INCLUDE}
    ${CG_AHE_INCLUDE}
)
target_link_libraries(bicycl_iface INTERFACE
    OpenSSL::Crypto
    OpenMP::OpenMP_CXX
    ${GMPXX_LIB}
    ${GMP_LIB}
    pthread
)
if(CG_USE_NTL_POLY AND NTL_LIB)
    target_compile_definitions(bicycl_iface INTERFACE CG_USE_NTL_POLY=1)
    target_link_libraries(bicycl_iface INTERFACE ${NTL_LIB})
endif()

# ── Helper function to create a binary ─────────────────────────
function(cg_binary name src)
    add_executable(${name} ${src})
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(${name} PRIVATE bicycl_iface)
endfunction()

# ── 2-Round RF-OLE Binaries ────────────────────────────────────
cg_binary(cg_receiver  cg_receiver.cpp)
cg_binary(cg_rrf       cg_rrf.cpp)
cg_binary(cg_srf       cg_srf.cpp)
cg_binary(cg_sender    cg_sender.cpp)

# ── Benchmark binary (standalone, no network) ──────────────────
cg_binary(cg_bench     cg_bench.cpp)

# ── Older RF-OLE / RF-OPA / RF-PSI demo binaries ───────────────
cg_binary(cg_rf_receiver               cg_rf_receiver.cpp)
cg_binary(cg_rf_receiver_firewall      cg_rf_receiver_firewall.cpp)
cg_binary(cg_rf_sender_firewall        cg_rf_sender_firewall.cpp)
cg_binary(cg_rf_sender                 cg_rf_sender.cpp)
cg_binary(cg_rf_receiver_firewall_opa  cg_rf_receiver_firewall_opa.cpp)
cg_binary(cg_rf_sender_firewall_opa    cg_rf_sender_firewall_opa.cpp)
cg_binary(cg_rf_opa_receiver           cg_rf_opa_receiver.cpp)
cg_binary(cg_rf_opa_sender             cg_rf_opa_sender.cpp)
cg_binary(cg_rf_psi_receiver           cg_rf_psi_receiver.cpp)
cg_binary(cg_rf_psi_sender             cg_rf_psi_sender.cpp)
cg_binary(cg_opa_receiver              cg_opa_receiver.cpp)
cg_binary(cg_opa_sender                cg_opa_sender.cpp)
cg_binary(cg_psi_receiver              cg_psi_receiver.cpp)
cg_binary(cg_psi_sender                cg_psi_sender.cpp)
cg_binary(cg_psi2_receiver             cg_psi2_receiver.cpp)
cg_binary(cg_psi2_sender               cg_psi2_sender.cpp)
cg_binary(cg_rf_psi2_sim               cg_rf_psi2_sim.cpp)
# Current two-way PSI is one-way RF-PSI plus a reveal-back phase, not the older
# two-RF-OPA construction.  The reveal binaries below are the only PSI2 party
# endpoints used by run_rf_cg_psi2_lan_{a,b}.sh and the 2PC benchmark scripts.
cg_binary(cg_rf_psi2_reveal_sender     cg_rf_psi2_reveal_sender.cpp)
cg_binary(cg_rf_psi2_reveal_receiver   cg_rf_psi2_reveal_receiver.cpp)
cg_binary(cg_rf_psi2_reveal_srf        cg_rf_psi2_reveal_srf.cpp)
cg_binary(cg_rf_psi2_reveal_rrf        cg_rf_psi2_reveal_rrf.cpp)
cg_binary(cg_psi_poly_compare          cg_psi_poly_compare.cpp)

# ── Batched OLE test binaries ──────────────────────────────────
cg_binary(cg_batch_ole_receiver        cg_batch_ole_receiver.cpp)
cg_binary(cg_batch_ole_sender          cg_batch_ole_sender.cpp)
cg_binary(cg_rf_batch_ole_receiver     cg_rf_batch_ole_receiver.cpp)
cg_binary(cg_rf_batch_ole_sender       cg_rf_batch_ole_sender.cpp)
cg_binary(cg_ole3_receiver             cg_ole3_receiver.cpp)
cg_binary(cg_ole3_sender               cg_ole3_sender.cpp)
cg_binary(cg_rf_ole3_receiver          cg_rf_ole3_receiver.cpp)
cg_binary(cg_rf_ole3_sender            cg_rf_ole3_sender.cpp)
cg_binary(cg_rf_receiver_firewall_ole3 cg_rf_receiver_firewall_ole3.cpp)
cg_binary(cg_rf_sender_firewall_ole3   cg_rf_sender_firewall_ole3.cpp)
cg_binary(cg_ope_receiver              cg_ope_receiver.cpp)
cg_binary(cg_ope_sender                cg_ope_sender.cpp)
cg_binary(cg_rf_ope_receiver           cg_rf_ope_receiver.cpp)
cg_binary(cg_rf_ope_sender             cg_rf_ope_sender.cpp)
cg_binary(cg_check_batch_ole           cg_check_batch_ole.cpp)
cg_binary(cg_check_ope                 cg_check_ope.cpp)
cg_binary(cg_check_opa                 cg_check_opa.cpp)
cg_binary(cg_print_q                   cg_print_q.cpp)

# ── RF Microbenchmark ──────────────────────────────────────────
cg_binary(cg_rf_microbench             cg_rf_microbench.cpp)
