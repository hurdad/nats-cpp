// The checks below are assert()s, several with side effects (waits, gets); keep them active in Release builds.
#undef NDEBUG

#include "unit_tests.hpp"

#include <natscpp/connection.hpp>

#include <cassert>
#include <chrono>
#include <cstdio>
#include <string>

// TLS and authentication tests against the servers started by tests/ci/start-servers.sh, found through:
//   NATSCPP_TEST_TLS_DIR     certs: root.pem, intA.pem, intB.pem, and ca-path/ (hashed copy of root.pem)
//   NATSCPP_TEST_TLS_URL_A   TLS server whose certificate is issued by intermediate A
//   NATSCPP_TEST_TLS_URL_B   TLS server whose certificate is issued by intermediate B
//   NATSCPP_TEST_AUTH_URL    server requiring user "alice" / password "secret"

#if defined(NATS_HAS_TLS) && __has_include(<openssl/x509.h>)
#define NATSCPP_TEST_TLS 1
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/x509_vfy.h>
#endif

namespace {

using namespace std::chrono_literals;
using unit_tests::env;

// Connects, checks a publish/subscribe roundtrip, and returns true; returns false if connect() throws.
bool connects(const natscpp::connection_options& opts) {
  try {
    natscpp::connection nc(opts);
    auto sub = nc.subscribe_sync("natscpp.tls.ping");
    nc.publish("natscpp.tls.ping", "ok");
    return sub.next_message(2s).data() == "ok";
  } catch (const natscpp::nats_error& e) {
    std::fprintf(stderr, "[natscpp_unit_tests]   (connect refused as: %s)\n", e.what());
    return false;
  }
}

natscpp::connection_options base_opts(const std::string& url) {
  natscpp::connection_options opts;
  opts.set_url(url);
  opts.set_timeout(2s);
  opts.set_allow_reconnect(false);
  return opts;
}

// --- Authentication --------------------------------------------------------

void test_user_password_auth() {
  const std::string url = env("NATSCPP_TEST_AUTH_URL");
  if (url.empty()) {
    unit_tests::skip("auth checks (NATSCPP_TEST_AUTH_URL not set)");
    return;
  }

  auto good = base_opts(url);
  good.set_user_info("alice", "secret");
  assert(connects(good));

  auto bad_password = base_opts(url);
  bad_password.set_user_info("alice", "wrong");
  assert(!connects(bad_password));

  assert(!connects(base_opts(url)));  // no credentials

  // User without a password is now sent (it used to be dropped); the server still rejects alice without one.
  auto user_only = base_opts(url);
  user_only.user = "alice";
  assert(!connects(user_only));
}

// --- TLS -------------------------------------------------------------------

#ifdef NATSCPP_TEST_TLS
X509* g_pinned_issuer = nullptr;  // callback state must be global: nats.c passes no user data

X509* load_cert(const std::string& path) {
  FILE* f = std::fopen(path.c_str(), "r");
  assert(f != nullptr);
  X509* cert = PEM_read_X509(f, nullptr, nullptr, nullptr);
  std::fclose(f);
  assert(cert != nullptr);
  return cert;
}

// Accept only a chain whose leaf was issued by g_pinned_issuer, on top of normal verification.
int pin_issuer_cb(int preverify_ok, void* ctx) {
  if (!preverify_ok) return 0;
  auto* store = static_cast<X509_STORE_CTX*>(ctx);
  if (X509_STORE_CTX_get_error_depth(store) != 0) return 1;
  STACK_OF(X509)* chain = X509_STORE_CTX_get0_chain(store);
  if (chain == nullptr || sk_X509_num(chain) < 2) return 0;
  return X509_cmp(sk_X509_value(chain, 1), g_pinned_issuer) == 0 ? 1 : 0;
}

void test_tls_chain_verification_and_issuer_pinning() {
  const std::string dir = env("NATSCPP_TEST_TLS_DIR");
  const std::string url_a = env("NATSCPP_TEST_TLS_URL_A");
  const std::string url_b = env("NATSCPP_TEST_TLS_URL_B");
  if (dir.empty() || url_a.empty() || url_b.empty()) {
    unit_tests::skip("TLS checks (NATSCPP_TEST_TLS_* not set)");
    return;
  }
  const std::string root = dir + "/root.pem";

  // Plain chain verification: root CA trusts both servers; an unrelated CA trusts neither.
  auto ca_root = [&](const std::string& url) {
    auto o = base_opts(url);
    o.load_ca_trusted_certificates(root);
    return o;
  };
  assert(connects(ca_root(url_a)));
  assert(connects(ca_root(url_b)));
  {
    auto o = base_opts(url_a);
    o.load_ca_trusted_certificates(dir + "/other-root.pem");
    assert(!connects(o));
  }

  // ca_certs_path (a hashed directory) and cipher_suites need NATS_HAS_TLS; they used to always throw.
  {
    auto o = base_opts(url_a);
    o.set_ca_certs_path(dir + "/ca-path");
    assert(connects(o));
  }
  {
    auto o = ca_root(url_a);
    o.set_cipher_suites("TLS_AES_128_GCM_SHA256:TLS_AES_256_GCM_SHA384:TLS_CHACHA20_POLY1305_SHA256");
    assert(connects(o));
  }

#if NATS_VERSION_NUMBER >= 0x030A00
  X509* int_a = load_cert(dir + "/intA.pem");
  X509* int_b = load_cert(dir + "/intB.pem");

  // Pinned to A: server A passes, server B is refused although its chain verifies against root.
  g_pinned_issuer = int_a;
  {
    auto o = ca_root(url_a);
    o.set_ssl_verification_callback(&pin_issuer_cb);
    assert(connects(o));
  }
  {
    auto o = ca_root(url_b);
    o.set_ssl_verification_callback(&pin_issuer_cb);
    assert(!connects(o));
  }
  // The callback re-enables verification even with skip_server_verification(true).
  {
    auto o = ca_root(url_b);
    o.set_skip_server_verification(true);
    o.set_ssl_verification_callback(&pin_issuer_cb);
    assert(!connects(o));
  }

  // Pinned to B: the reverse.
  g_pinned_issuer = int_b;
  {
    auto o = ca_root(url_a);
    o.set_ssl_verification_callback(&pin_issuer_cb);
    assert(!connects(o));
  }
  {
    auto o = ca_root(url_b);
    o.set_ssl_verification_callback(&pin_issuer_cb);
    assert(connects(o));
  }

  g_pinned_issuer = nullptr;
  X509_free(int_a);
  X509_free(int_b);
#endif
}
#endif  // NATSCPP_TEST_TLS

}  // namespace

namespace unit_tests {

void run_tls_and_auth_tests() {
  test_user_password_auth();
#ifdef NATSCPP_TEST_TLS
  test_tls_chain_verification_and_issuer_pinning();
#else
  unit_tests::skip("TLS checks (nats.c built without TLS, or OpenSSL headers missing)");
#endif
}

}  // namespace unit_tests
