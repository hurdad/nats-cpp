#include <iostream>

#include <natscpp/connection.hpp>
#include <natscpp/kv.hpp>

int main() {
  natscpp::connection nc({.url = "nats://127.0.0.1:4222"});

  // Bucket must already exist (for example: `nats kv add demo_profiles`).
  natscpp::key_value kv(nc, "demo_profiles");

  auto rev = kv.put("user-1", R"({"name":"Ada"})");
  std::cout << "put revision: " << rev << '\n';

  auto entry = kv.get("user-1");
  std::cout << "kv get key=" << entry.key() << " value=" << entry.value()
            << " revision=" << entry.revision() << '\n';

  kv.erase("user-1");
  std::cout << "deleted key: user-1\n";

  return 0;
}
