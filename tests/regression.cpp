#include "cg_bench_io.hpp"
#include "cg_rf_ole_batch.hpp"
#include <cassert>
#include <sys/wait.h>
#include <fstream>

template<class Fn> void rejects(Fn fn) {
    bool rejected = false;
    try { fn(); } catch (const std::exception&) { rejected = true; }
    assert(rejected);
}

void transport_test(const std::string& test) {
    int fds[2]; assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
    const bool mismatch = test == "count" || test == "wrong-domain";
    auto pid = fork(); assert(pid >= 0);
    if (!pid) {
        ::close(fds[0]);
        if (test == "wrong-domain") setenv("CG_USE_NTT_POLY", "0", 1);
        try {
            CGNet::set_tcp_opts(fds[1]);
            if (test == "count" || test == "wrong-domain") {
                check_rf_metadata(fds[1], 2, test == "count" ? 9 : 8);
                send_rf_metadata(fds[1], 2, 8);
            } else {
                assert(CGNet::recv_u32(fds[1]) == 42);
                CGNet::send_u32(fds[1], 43);
            }
            CGNet::close_tcp(fds[1]); _exit(mismatch ? 1 : 0);
        } catch (...) { CGNet::close_tcp(fds[1]); _exit(mismatch ? 0 : 1); }
    }
    ::close(fds[1]);
    bool rejected = false;
    try {
        CGNet::set_tcp_opts(fds[0]);
        if (test == "count" || test == "wrong-domain") {
            send_rf_metadata(fds[0], 2, 8); check_rf_metadata(fds[0], 2, 8);
        } else {
            CGNet::send_u32(fds[0], 42); assert(CGNet::recv_u32(fds[0]) == 43);
        }
    } catch (...) { rejected = true; }
    CGNet::close_tcp(fds[0]);
    int status; assert(waitpid(pid, &status, 0) == pid);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0 && rejected == mismatch);
}

int main(int argc, char** argv) {
    setenv("CG_RF_THREADS", "2", 1);
    setenv("CG_FIXED_Q", CG_NTT128_PRIME_DEC, 1);
    setenv("CG_IO_TIMEOUT_S", "2", 1);
    if (argc > 2 && std::string(argv[1]) == "transport") {
        transport_test(argv[2]); return 0;
    }
    const BICYCL::Mpz q(17UL);
    for (auto text : {"-1", "+1", " 1", "1x", "18446744073709551616", "0"})
        rejects([&] { cg_parse_size(text); });
    assert(cg_parse_u64("18446744073709551615") == UINT64_MAX);
    rejects([&] { validate_psi_set({q}, q, "test"); });
    rejects([&] { validate_psi_set({BICYCL::Mpz(1UL), BICYCL::Mpz(1UL)}, q, "test"); });
    validate_psi_set({BICYCL::Mpz(0UL), BICYCL::Mpz(1UL)}, q, "test");
    const std::string directory = argv[1];
    { std::ofstream file(directory + "/pair"); file << "2 7\n9\n"; }
    rejects([&] { read_ole_sender_input_file(directory + "/pair", q, "test"); });
    { std::ofstream file(directory + "/pair"); file << "2 7 9 1\n"; }
    rejects([&] { read_ole_sender_input_file(directory + "/pair", q, "test"); });
    { std::ofstream file(directory + "/poly"); file << "-1\n1\n"; }
    auto coefficients = read_mpz_coeff_file(directory + "/poly", q, "test");
    assert(coefficients[0] == BICYCL::Mpz(16UL));
    setenv("CG_BENCH_INPUT_BITS", "1", 1);
    auto rng = make_seeded_benchmark_randgen(1, 1);
    std::vector<BICYCL::Mpz> out;
    std::unordered_set<std::string> seen;
    rejects([&] { sample_unique_field_elements(2, q, rng, out, seen); });
    setenv("CG_BENCH_INPUT_BITS", "4", 1);
    for (uint64_t seed = 0; seed < 50; ++seed) {
        std::vector<BICYCL::Mpz> a, b;
        make_random_psi_sets_full_field(5, 5, seed, q, a, b);
        size_t overlap = 0;
        for (const auto& x : a) if (std::find(b.begin(), b.end(), x) != b.end()) ++overlap;
        assert(overlap == 1);
    }
    // Oversized lengths are rejected before allocation; sign encodings too.
    for (auto pair : {std::pair<uint32_t,uint32_t>{0, UINT32_MAX}, {2, 0}, {1, 0}}) {
        int fds[2]; assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
        CGNet::send_u32(fds[0], pair.first); CGNet::send_u32(fds[0], pair.second);
        rejects([&] { CGNet::recv_mpz(fds[1]); });
        CGNet::close_tcp(fds[0]); CGNet::close_tcp(fds[1]);
    }
    // A failing lane must shut down the other blocked lane and throw to main.
    int first[2], second[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, first) == 0);
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, second) == 0);
    ::close(first[1]);
    std::vector<int> fds{first[0], second[0]};
    std::vector<CG_AHE::CipherText> values(2);
    rejects([&] { recv_ct_lanes(fds, values, 2); });
    close_rf_lanes(fds); ::close(second[1]);
    CGThreadPool pool(2);
    rejects([&] { pool.parallel_for(0, 10, [](size_t) { throw std::runtime_error("test"); }); });
    size_t sum = 0;
    pool.parallel_for(0, 1, [&](size_t) { sum = 42; }); assert(sum == 42);
    std::cout << "C++ validation, sampler, encoding, lane failure, and pool regressions passed\n";
}
