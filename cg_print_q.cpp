#include "cg_rf_common.hpp"

#include <iostream>

int main()
{
    BICYCL::RandGen rng = make_secure_randgen();
    CG_AHE::CG_Scheme cg = make_cg_scheme(rng);
    std::cout << cg.cs().cleartext_bound() << "\n";
    return 0;
}
