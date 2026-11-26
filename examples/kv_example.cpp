// KeyValue: put/get, compare-and-set, history, keys, and a watcher that first replays current values.
#include <iostream>
#include <string>

#include <natscpp/connection.hpp>
#include <natscpp/kv.hpp>

#include "example_common.hpp"

namespace {
const char* op_name(kvOperation op) {
  switch (op) {
    case kvOp_Put:
      return "put";
    case kvOp_Delete:
      return "delete";
    case kvOp_Purge:
      return "purge";
    default:
      return "unknown";
  }
}
}  // namespace

int main() {
  return example::run([] {
    natscpp::connection nc({.url = example::server_url()});

    constexpr auto bucket = "demo_profiles";
    auto kv = natscpp::key_value::create(nc, {.bucket = bucket, .history = 5});  // keep 5 revisions per key
    example::scope_exit cleanup([&] { natscpp::key_value::delete_bucket(nc, bucket); });

    const auto rev1 = kv.put("user-1", R"({"name":"Ada"})");
    auto entry = kv.get("user-1");
    std::cout << "get " << entry.key() << " = " << entry.value() << " (revision " << entry.revision() << ")\n";

    // Compare-and-set: update() only succeeds if the key is still at the revision we read.
    const auto rev2 = kv.update("user-1", R"({"name":"Ada","active":true})", rev1);
    try {
      (void)kv.update("user-1", R"({"name":"stale"})", rev1);  // rev1 is no longer current
    } catch (const natscpp::nats_error& e) {
      std::cout << "stale update rejected: " << e.what() << '\n';
    }
    // create() only succeeds if the key does not exist yet.
    (void)kv.create("user-2", R"({"name":"Grace"})");

    for (const auto& h : kv.history("user-1")) {
      std::cout << "history user-1 rev " << h.revision() << ": " << h.value() << '\n';
    }
    for (const auto& key : kv.keys()) {
      std::cout << "key: " << key << '\n';
    }

    // A watcher first delivers the current value of each matching key, then an invalid entry marking the
    // end of that snapshot, then live updates. (updates_only = true skips the snapshot.)
    auto watcher = kv.watch_all();
    for (;;) {
      auto e = watcher.next(2000);
      if (!e.valid()) break;  // snapshot complete
      std::cout << "snapshot " << e.key() << " = " << e.value() << '\n';
    }
    kv.erase("user-2");
    auto update = watcher.next(2000);
    std::cout << "watch: " << op_name(update.operation()) << ' ' << update.key() << " (revision " << update.revision()
              << ")\n";
    watcher.stop();

    auto status = kv.status();
    std::cout << "bucket " << status.bucket << ": " << status.values << " values, history " << status.history << '\n';
    return rev2 > rev1 ? 0 : 1;
  });
}
