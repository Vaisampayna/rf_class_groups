#pragma once
/*
 * Offline artifact I/O helpers.
 *
 * Protocol binaries call these only after timed protocol work is complete.
 * The generated files are consumed by checker binaries/scripts and are never
 * exchanged as part of the protocol.
 */

#include "cg_rf_common.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

inline std::string cg_bench_io_dir()
{
    // Benchmark verification files are intentionally written out-of-band.
    // Protocol processes do not reveal private inputs to each other just to
    // check correctness; a local checker reads these files after termination.
    const char* env = std::getenv("CG_BENCH_IO_DIR");
    return (env && *env) ? std::string(env) : std::string(".");
}

inline std::string cg_bench_io_path(const std::string& name)
{
    std::filesystem::create_directories(cg_bench_io_dir());
    return cg_bench_io_dir() + "/" + name;
}

inline void write_ole_sender_inputs(
    const std::string& filename,
    const std::vector<BICYCL::Mpz>& a_vals,
    const std::vector<BICYCL::Mpz>& b_vals)
{
    // Sender-side private OLE inputs: pairs (a_i,b_i).  The receiver dump
    // contains x_i and y_i; the checker combines both files offline.
    if (a_vals.size() != b_vals.size())
        throw std::runtime_error("OLE sender dump size mismatch");
    std::ofstream out(cg_bench_io_path(filename));
    if (!out) throw std::runtime_error("cannot open OLE sender dump");
    out << "OLE_SENDER_V1\n";
    out << a_vals.size() << "\n";
    for (size_t i = 0; i < a_vals.size(); ++i)
        out << a_vals[i] << " " << b_vals[i] << "\n";
}

inline void write_ole_receiver_io(
    const std::string& filename,
    const BICYCL::Mpz& q,
    const std::vector<BICYCL::Mpz>& x_vals,
    const std::vector<BICYCL::Mpz>& y_vals)
{
    if (x_vals.size() != y_vals.size())
        throw std::runtime_error("OLE receiver dump size mismatch");
    std::ofstream out(cg_bench_io_path(filename));
    if (!out) throw std::runtime_error("cannot open OLE receiver dump");
    out << "OLE_RECEIVER_V1\n";
    out << x_vals.size() << "\n";
    out << q << "\n";
    for (size_t i = 0; i < x_vals.size(); ++i)
        out << x_vals[i] << " " << y_vals[i] << "\n";
}

inline void write_ole_blinds(
    const std::string& filename,
    const std::vector<BICYCL::Mpz>& a_blinds,
    const std::vector<BICYCL::Mpz>& b_blinds)
{
    // Archived firewall variants use both affine blind vectors to verify that
    // the receiver output corresponds to the transformed sender tuple.
    if (a_blinds.size() != b_blinds.size())
        throw std::runtime_error("OLE blind dump size mismatch");
    std::ofstream out(cg_bench_io_path(filename));
    if (!out) throw std::runtime_error("cannot open OLE blind dump");
    out << "OLE_BLINDS_V1\n";
    out << a_blinds.size() << "\n";
    for (size_t i = 0; i < a_blinds.size(); ++i)
        out << a_blinds[i] << " " << b_blinds[i] << "\n";
}

inline void write_ope_sender_coeffs(
    const std::string& filename,
    const BICYCL::Mpz& q,
    const std::vector<BICYCL::Mpz>& coeffs)
{
    // OPE verification recomputes p(alpha) directly from Alice's polynomial
    // and compares it with Bob's protocol output.
    std::ofstream out(cg_bench_io_path(filename));
    if (!out) throw std::runtime_error("cannot open OPE sender dump");
    out << "OPE_SENDER_V1\n";
    out << coeffs.size() << "\n";
    out << q << "\n";
    for (const auto& c : coeffs)
        out << c << "\n";
}

inline void write_ope_receiver_output(
    const std::string& filename,
    const BICYCL::Mpz& q,
    const BICYCL::Mpz& alpha,
    const BICYCL::Mpz& output)
{
    std::ofstream out(cg_bench_io_path(filename));
    if (!out) throw std::runtime_error("cannot open OPE receiver dump");
    out << "OPE_RECEIVER_V1\n";
    out << q << "\n";
    out << alpha << "\n";
    out << output << "\n";
}

inline void write_opa_sender_coeffs(
    const std::string& filename,
    const BICYCL::Mpz& q,
    const std::vector<BICYCL::Mpz>& b_coeffs,
    const std::vector<BICYCL::Mpz>& a_coeffs)
{
    if (b_coeffs.size() != a_coeffs.size())
        throw std::runtime_error("OPA sender dump size mismatch");
    std::ofstream out(cg_bench_io_path(filename));
    if (!out) throw std::runtime_error("cannot open OPA sender dump");
    out << "OPA_SENDER_V1\n";
    out << b_coeffs.size() << "\n";
    out << q << "\n";
    for (const auto& c : b_coeffs)
        out << c << "\n";
    out << "--\n";
    for (const auto& c : a_coeffs)
        out << c << "\n";
}

inline void write_opa_receiver_dump(
    const std::string& filename,
    const BICYCL::Mpz& q,
    const std::vector<BICYCL::Mpz>& pB_coeffs,
    const std::vector<BICYCL::Mpz>& alpha,
    const std::vector<BICYCL::Mpz>& y_vals)
{
    if (alpha.size() != y_vals.size())
        throw std::runtime_error("OPA receiver dump size mismatch");
    std::ofstream out(cg_bench_io_path(filename));
    if (!out) throw std::runtime_error("cannot open OPA receiver dump");
    out << "OPA_RECEIVER_V1\n";
    out << pB_coeffs.size() << "\n";
    out << alpha.size() << "\n";
    out << q << "\n";
    for (const auto& c : pB_coeffs)
        out << c << "\n";
    out << "--\n";
    for (size_t i = 0; i < alpha.size(); ++i)
        out << alpha[i] << " " << y_vals[i] << "\n";
}

inline BICYCL::Mpz read_mpz_text(std::istream& in)
{
    std::string s;
    if (!(in >> s)) throw std::runtime_error("failed to read Mpz text");
    return BICYCL::Mpz(s);
}

inline std::pair<std::vector<BICYCL::Mpz>, std::vector<BICYCL::Mpz>>
read_ole_sender_input_file(
    const std::string& path,
    const BICYCL::Mpz& q,
    const char* role)
{
    std::ifstream in(path);
    if (!in)
        throw std::runtime_error(std::string(role) + ": cannot open OLE sender input file " + path);

    std::vector<BICYCL::Mpz> a_vals;
    std::vector<BICYCL::Mpz> b_vals;
    std::string a_tok, b_tok;
    while (in >> a_tok >> b_tok) {
        BICYCL::Mpz a(a_tok.c_str());
        BICYCL::Mpz b(b_tok.c_str());
        if (a.sgn() < 0 || b.sgn() < 0) {
            std::ostringstream oss;
            oss << role << ": negative OLE sender value in "
                << path;
            throw std::runtime_error(oss.str());
        }
        BICYCL::Mpz::mod(a, a, q);
        BICYCL::Mpz::mod(b, b, q);
        a_vals.push_back(a);
        b_vals.push_back(b);
    }
    if (!in.eof())
        throw std::runtime_error(std::string(role) + ": malformed OLE sender input file " + path);
    if (a_vals.empty())
        throw std::runtime_error(std::string(role) + ": OLE sender input file is empty: " + path);
    return {std::move(a_vals), std::move(b_vals)};
}
