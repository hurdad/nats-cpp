#include "unit_tests.hpp"

#include <natscpp/library.hpp>

#include <chrono>
#include <string>
#include <string_view>
#include <vector>

// Usage:
//   natscpp_unit_tests [all|unit|integration|tls]   run every test, or one group, in this process
//   natscpp_unit_tests <group>.<name>               run one test (CTest runs each test this way)
//   natscpp_unit_tests --list                       print "<group>.<name>" for every test
// Groups: unit needs no server; integration needs a nats-server with JetStream at nats://127.0.0.1:4222;
// tls needs the TLS and auth servers described by NATSCPP_TEST_* variables (see tests/ci/start-servers.sh).
// Exits 0 on success, 77 when a test skipped because its server is unavailable, and aborts on a failure.

namespace {

std::vector<unit_tests::test_case> all_tests() {
  std::vector<unit_tests::test_case> tests;
  for (auto list :
       {unit_tests::core_unit_tests, unit_tests::api_surface_unit_tests, unit_tests::offline_regression_tests,
        unit_tests::integration_tests, unit_tests::server_feature_tests, unit_tests::server_regression_tests,
        unit_tests::tls_and_auth_tests}) {
    for (const auto& t : list()) tests.push_back(t);
  }
  return tests;
}

// "<group>.<name>": the function name without its test_ prefix and _if_server_available suffix.
std::string test_name(const unit_tests::test_case& t) {
  std::string_view fn = t.function;
  if (fn.starts_with("test_")) fn.remove_prefix(5);
  if (constexpr std::string_view suffix = "_if_server_available"; fn.ends_with(suffix)) {
    fn.remove_suffix(suffix.size());
  }
  return std::string(t.group) + "." + std::string(fn);
}

int finish() {
  // Wait for nats.c's threads (connections destroyed above close in the background) before exit-time
  // teardown, e.g. OpenSSL's, can pull state out from under them. Not a failure when it times out: nats.c
  // 3.15.0's kvStore_PurgeDeletes leaks a reference (see tests/ci/lsan.supp), so nats.c never reaches zero
  // objects after the KV tests even though every connection is closed.
  if (const natsStatus s = natscpp::nats_CloseAndWait(std::chrono::seconds(2)); s != NATS_OK) {
    std::cerr << "[natscpp_unit_tests] nats_CloseAndWait: " << natsStatus_GetText(s) << " (expected after KV tests)\n";
  }
  return unit_tests::skipped ? unit_tests::skip_exit_code : 0;
}

}  // namespace

int main(int argc, char** argv) {
  const std::string_view arg = argc > 1 ? argv[1] : "all";
  const auto tests = all_tests();

  if (arg == "--list") {
    for (const auto& t : tests) std::cout << test_name(t) << '\n';
    return 0;
  }

  const bool is_group = arg == "all" || arg == "unit" || arg == "integration" || arg == "tls";
  int ran = 0;
  for (const auto& t : tests) {
    if (is_group ? (arg == "all" || t.group == arg) : test_name(t) == arg) {
      t.run();
      ++ran;
    }
  }
  if (ran == 0) {
    std::cerr << "unknown test or group: " << arg << " (see --list)\n";
    return 2;
  }
  return finish();
}
