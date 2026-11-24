#include "unit_tests.hpp"

// Usage: natscpp_unit_tests [all|unit|integration|tls]
//   unit         no server needed
//   integration  nats-server with JetStream at nats://127.0.0.1:4222
//   tls          TLS and auth servers described by NATSCPP_TEST_* variables (see tests/ci/start-servers.sh)
int main(int argc, char** argv) {
  const std::string_view group = argc > 1 ? argv[1] : "all";
  const bool all = group == "all";
  if (!all && group != "unit" && group != "integration" && group != "tls") {
    std::cerr << "unknown test group: " << group << "\n";
    return 2;
  }
  if (all || group == "unit") {
    unit_tests::run_core_unit_tests();
    unit_tests::run_api_surface_unit_tests();
  }
  if (all || group == "integration") {
    unit_tests::run_integration_tests();
    unit_tests::run_server_feature_tests();
  }
  if (all || group == "tls") {
    unit_tests::run_tls_and_auth_tests();
  }
  return 0;
}
