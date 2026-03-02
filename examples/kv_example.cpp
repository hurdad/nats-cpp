#include <iostream>

#include <natscpp/connection.hpp>
#include <natscpp/kv.hpp>

int main() {
  natscpp::connection nc({.url = "nats://127.0.0.1:4222"});

  auto kv = natscpp::key_value::create(nc, "demo_profiles");

  auto rev = kv.put("user-1", R"({"name":"Ada"})");
  std::cout << "put revision: " << rev << '\n';

  auto entry = kv.get("user-1");
  std::cout << "kv get key=" << entry.key() << " value=" << entry.value()
            << " revision=" << entry.revision() << '\n';

  natscpp::kv_watch_options watch_opts;
  watch_opts.updates_only = true;
  auto watcher = kv.watch("user-1", &watch_opts);
  auto watch_rev = kv.put("user-1", R"({"name":"Ada","active":true})");
  std::cout << "watch update revision: " << watch_rev << "\n";

  auto updated = watcher.next(2000);
  std::cout << "watch update key=" << updated.key() << " value=" << updated.value()
            << " revision=" << updated.revision() << '\n';
  watcher.stop();

  kv.erase("user-1");
  std::cout << "deleted key: user-1\n";

  natscpp::key_value::delete_bucket(nc, "demo_profiles");
  std::cout << "deleted bucket: demo_profiles\n";

  return 0;
}
