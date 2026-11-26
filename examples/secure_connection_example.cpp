// TLS and authentication, configured from the environment so it runs against any server:
//   NATS_URL            server URL (tls:// for TLS)
//   NATS_CA             CA certificate file to verify the server with
//   NATS_CERT, NATS_KEY client certificate and key (mutual TLS)
//   NATS_USER, NATS_PASSWORD   user/password authentication
//   NATS_TOKEN          token authentication
//   NATS_CREDS          .creds file (JWT + nkey seed)
// For example, against the servers from tests/ci/start-servers.sh:
//   NATS_URL=tls://localhost:4443 NATS_CA=$NATSCPP_TEST_TLS_DIR/root.pem natscpp_secure_connection_example
//   NATS_URL=nats://127.0.0.1:4223 NATS_USER=alice NATS_PASSWORD=secret natscpp_secure_connection_example
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>

#include <natscpp/connection.hpp>

#include "example_common.hpp"

namespace {
std::optional<std::string> env(const char* name) {
  const char* v = std::getenv(name);
  return v != nullptr && *v != '\0' ? std::optional<std::string>{v} : std::nullopt;
}
}  // namespace

int main() {
  return example::run([] {
    natscpp::connection_options opts;
    opts.set_url(example::server_url());
    opts.set_name("natscpp-secure-example");  // shows up in the server's connection list
    opts.set_timeout(std::chrono::seconds(5));

    // TLS: a CA file makes the client verify the server's certificate chain (and require TLS).
    // Never use set_skip_server_verification(true) outside of local testing: it turns verification off.
    if (auto ca = env("NATS_CA")) opts.load_ca_trusted_certificates(*ca);
    if (auto cert = env("NATS_CERT"), key = env("NATS_KEY"); cert && key) opts.load_certificates_chain(*cert, *key);

    // Authentication: pick whichever is configured.
    if (auto creds = env("NATS_CREDS")) opts.set_user_credentials_from_files(*creds);
    if (auto user = env("NATS_USER")) opts.set_user_info(*user, env("NATS_PASSWORD").value_or(""));
    if (auto token = env("NATS_TOKEN")) opts.set_token(*token);

    natscpp::connection nc(opts);
    std::cout << "connected to " << nc.connected_url() << " (server " << nc.connected_server_id() << ")\n";
    std::cout << "round trip: " << nc.rtt().count() << " us\n";
    return nc.connected() ? 0 : 1;
  });
}
