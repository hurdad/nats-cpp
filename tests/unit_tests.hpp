#pragma once

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace unit_tests {

// One test function. group is what it needs: "unit" (nothing), "integration" (the JetStream nats-server at
// nats://127.0.0.1:4222) or "tls" (the TLS and auth servers described by NATSCPP_TEST_* variables).
struct test_case {
  std::string_view group;
  std::string_view function;  // test_<name>
  void (*run)();
};
#define NATSCPP_TEST_CASE(group, fn) \
  ::unit_tests::test_case { group, #fn, &(fn) }

// Each test file lists its tests in the order they run.
std::vector<test_case> core_unit_tests();
std::vector<test_case> api_surface_unit_tests();
std::vector<test_case> offline_regression_tests();
std::vector<test_case> integration_tests();
std::vector<test_case> server_feature_tests();
std::vector<test_case> server_regression_tests();
std::vector<test_case> tls_and_auth_tests();
std::vector<test_case> security_unit_tests();
std::vector<test_case> security_server_tests();

// Set by skip(); the runner exits with skip_exit_code when a test skipped, so CTest reports it as skipped.
inline std::atomic<bool> skipped{false};
inline constexpr int skip_exit_code = 77;

// Environment variable value, or "" when unset.
inline std::string env(const char* name) {
  const char* v = std::getenv(name);
  return v != nullptr ? std::string{v} : std::string{};
}

// Report a skipped test. With NATSCPP_REQUIRE_SERVER set (as in CI), a skip is a failure: it means
// a server the test expected was not reachable or not configured.
inline void skip(std::string_view what) {
  std::cerr << "[natscpp_unit_tests] skipping " << what << "\n";
  skipped = true;
  const std::string required = env("NATSCPP_REQUIRE_SERVER");
  if (!required.empty() && required != "0") {
    std::cerr << "[natscpp_unit_tests] NATSCPP_REQUIRE_SERVER is set: treating the skip as a failure\n";
    std::abort();
  }
}

}  // namespace unit_tests
