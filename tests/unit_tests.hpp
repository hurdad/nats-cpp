#pragma once

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

namespace unit_tests {

void run_core_unit_tests();
void run_api_surface_unit_tests();
void run_integration_tests();
void run_server_feature_tests();
void run_tls_and_auth_tests();
void run_offline_regression_tests();
void run_server_regression_tests();

// Environment variable value, or "" when unset.
inline std::string env(const char* name) {
  const char* v = std::getenv(name);
  return v != nullptr ? std::string{v} : std::string{};
}

// Report a skipped test. With NATSCPP_REQUIRE_SERVER set (as in CI), a skip is a failure: it means
// a server the test expected was not reachable or not configured.
inline void skip(std::string_view what) {
  std::cerr << "[natscpp_unit_tests] skipping " << what << "\n";
  const std::string required = env("NATSCPP_REQUIRE_SERVER");
  if (!required.empty() && required != "0") {
    std::cerr << "[natscpp_unit_tests] NATSCPP_REQUIRE_SERVER is set: treating the skip as a failure\n";
    std::abort();
  }
}

}  // namespace unit_tests
