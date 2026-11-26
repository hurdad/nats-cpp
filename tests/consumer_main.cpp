// Built by tests/install_consumer and tests/subdirectory_consumer: checks that a project depending on
// natscpp compiles, links (nats.c, OpenSSL, threads come with the target) and runs without a server.
#include <natscpp/natscpp.hpp>

#include <cstdio>

int main() {
  if (!natscpp::nats_CheckCompatibility()) {
    std::fprintf(stderr, "natscpp headers and the linked nats.c disagree\n");
    return 1;
  }
  const auto inbox = natscpp::connection::new_inbox();
  std::printf("natscpp %s consumer OK: nats.c %s, inbox %s\n", natscpp::version(), natscpp::nats_GetVersion(),
              inbox.c_str());
  return inbox.rfind("_INBOX.", 0) == 0 ? 0 : 1;
}
