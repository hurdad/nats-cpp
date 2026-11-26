#include "unit_tests.hpp"

#include <natscpp/library.hpp>

#include <chrono>

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
    unit_tests::run_offline_regression_tests();
  }
  if (all || group == "integration") {
    unit_tests::run_integration_tests();
    unit_tests::run_server_feature_tests();
    unit_tests::run_server_regression_tests();
  }
  if (all || group == "tls") {
    unit_tests::run_tls_and_auth_tests();
  }
  // Wait for nats.c's threads (connections destroyed above close in the background) before exit-time
  // teardown, e.g. OpenSSL's, can pull state out from under them. Not a failure when it times out: nats.c
  // 3.15.0's kvStore_PurgeDeletes leaks a reference (see tests/ci/lsan.supp), so nats.c never reaches zero
  // objects after the KV tests even though every connection is closed.
  if (const natsStatus s = natscpp::nats_CloseAndWait(std::chrono::seconds(2)); s != NATS_OK) {
    std::cerr << "[natscpp_unit_tests] nats_CloseAndWait: " << natsStatus_GetText(s) << " (expected after KV tests)\n";
  }
  return 0;
}
