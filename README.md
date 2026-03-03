# natscpp

Modern, header-only **C++20** wrappers for the official [`nats.c`](https://github.com/nats-io/nats.c) client.

## Table of contents

- [Features](#features)
- [Requirements](#requirements)
- [Build and test](#build-and-test)
- [Install](#install)
- [Example programs](#example-programs)
- [API examples](#api-examples)
  - [Sync API example](#sync-api-example)
  - [Async API example](#async-api-example)
  - [JetStream](#jetstream)
  - [KeyValue](#keyvalue)
  - [Trace propagation](#trace-propagation)
- [Includes](#includes)
- [Notes](#notes)

## Features

- Header-only API in the `natscpp` namespace.
- RAII wrappers for core NATS resources.
- Core messaging APIs:
  - connection management
  - publish / subscribe
  - queue-group subscriptions
  - request/reply (sync + async)
  - explicit subscribe APIs for both sync and async callback styles
- Optional coroutine support (`co_await`) through future awaitables.
- JetStream wrappers (context, stream/consumer management, push/pull subscriptions).
- Stream + consumer-group creation helpers (`create_stream`, `create_consumer_group`).
- KeyValue wrappers (open bucket, put/get/delete entries, watch key updates).
- Trace propagation helpers using NATS headers (`TraceCarrier` concept).
- Graceful JetStream fallback when linked `nats.c` does not expose JetStream symbols.

## Requirements

- C++20 compiler
- CMake 3.20+
- `nats.c` dependency, resolved in one of two ways:
  - bundled submodule at `third_party/nats.c` (static build wired automatically by CMake), or
  - system install discoverable as `libnats` via `pkg-config`

## Build and test

```bash
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

If `third_party/nats.c` is not initialized, configure will fail unless `libnats` is installed and visible to `pkg-config`.

## Install

Install headers and CMake package metadata:

```bash
cmake -S . -B build
cmake --build build
cmake --install build --prefix /your/install/prefix
```

Then consume via CMake:

```cmake
find_package(natscpp CONFIG REQUIRED)

add_executable(app main.cpp)
target_link_libraries(app PRIVATE natscpp::natscpp)
```

Or embed as a subdirectory:

```cmake
add_subdirectory(path/to/natscpp)
target_link_libraries(app PRIVATE natscpp::natscpp)
```

## Example programs

When `NATSCPP_BUILD_EXAMPLES=ON` (default), the following example targets are built:

- `natscpp_sync_example`
- `natscpp_async_example`
- `natscpp_request_reply_example`
- `natscpp_jetstream_sync_pull_example`
- `natscpp_jetstream_sync_push_example`
- `natscpp_jetstream_async_pull_example`
- `natscpp_jetstream_async_push_example`
- `natscpp_kv_example`

> Most examples expect a local NATS server at `nats://127.0.0.1:4222`.

## API examples

## Sync API example

```cpp
#include <chrono>
#include <natscpp/connection.hpp>

int main() {
  natscpp::connection nc({.url = "nats://127.0.0.1:4222"});

  auto sub = nc.subscribe_sync("demo.sync.events");
  nc.publish("demo.sync.events", "hello");

  auto msg = sub.next_message(std::chrono::seconds(1));
  auto reply = nc.request_sync("svc.echo", "ping");
  return (msg.data() == "hello" && reply.data() == "ping") ? 0 : 1;
}
```

## Async API example

```cpp
#include <chrono>
#include <natscpp/connection.hpp>

int main() {
  natscpp::connection nc({.url = "nats://127.0.0.1:4222"});

  auto sub = nc.subscribe_async("demo.async.events", [](natscpp::message msg) {
    // callback invoked by nats.c dispatcher thread
    (void)msg;
  });

  nc.publish("demo.async.events", "hello async");
  auto fut = nc.request_async("svc.echo", "ping async", std::chrono::seconds(2));
  auto async_reply = fut.get();
  sub.unsubscribe();

  return async_reply.data().empty() ? 1 : 0;
}
```

## API mapping

- Synchronous subscribe: `subscribe_sync`, `subscribe_queue_sync`
- Asynchronous callback subscribe: `subscribe_async`, `subscribe_queue_async`
- Synchronous request/reply: `request_sync` (alias: `request`)
- Asynchronous request/reply: `request_async`, `request_awaitable`

Request/reply example is available in:
- `examples/request_reply_example.cpp`

## JetStream

```cpp
#include <natscpp/jetstream.hpp>

natscpp::connection nc;
natscpp::jetstream js(nc);

js.create_stream({.name = "ORDERS", .subjects = {"orders.>"}});
js.create_consumer_group({
  .stream = "ORDERS",
  .durable_name = "orders-worker",
  .filter_subject = "orders.created",
  .type = natscpp::js_consumer_type::pull,
});

js.publish("orders.created", R"({"id":"A-1"})");
auto pull = js.pull_subscribe("orders.created", "orders-worker");
auto msg = pull.next(std::chrono::seconds(1));
```

JetStream examples are available in:
- `examples/jetstream_sync_pull_example.cpp`
- `examples/jetstream_sync_push_example.cpp`
- `examples/jetstream_async_pull_example.cpp`
- `examples/jetstream_async_push_example.cpp`


If JetStream symbols are unavailable in your linked `nats.c`, APIs throw `natscpp::jetstream_not_available`.


## KeyValue

```cpp
#include <natscpp/kv.hpp>

natscpp::connection nc;
natscpp::key_value kv(nc, "profiles");

kv.put("user-1", R"({"name":"Ada"})");
auto entry = kv.get("user-1");

natscpp::kv_watch_options watch_opts;
watch_opts.updates_only = true;
auto watcher = kv.watch("user-1", &watch_opts);

kv.put("user-1", R"({"name":"Ada","active":true})");
auto updated = watcher.next(std::chrono::seconds(1).count() * 1000);
watcher.stop();

kv.erase("user-1");
```

Watch APIs:
- `watch(key, opts)` for a single key/filter
- `watch_multi(keys, opts)` for multiple keys/filters
- `watch_all(opts)` for all keys in the bucket

`kv_watcher::next()` may return an invalid `kv_entry` (`entry.valid() == false`) when the initial snapshot is complete.

## KeyValue example

```cpp
#include <natscpp/connection.hpp>
#include <natscpp/kv.hpp>

int main() {
  natscpp::connection nc({.url = "nats://127.0.0.1:4222"});

  auto kv = natscpp::key_value::create(nc, "demo_profiles");

  auto rev = kv.put("user-1", R"({"name":"Ada"})");
  auto entry = kv.get("user-1");
  kv.erase("user-1");
  natscpp::key_value::delete_bucket(nc, "demo_profiles");
  return rev > 0 && entry.key() == "user-1" ? 0 : 1;
}
```

KeyValue example is available in:
- `examples/kv_example.cpp`

## Trace propagation

```cpp
#include <unordered_map>
#include <vector>

#include <natscpp/trace.hpp>

natsMsg* raw{};
natsMsg_Create(&raw, "trace.subject", nullptr, "payload", 7);
natscpp::message msg(raw);

natscpp::message_trace_carrier carrier(msg);
std::vector<std::pair<std::string, std::string>> injected = {
  {"traceparent", "00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01"},
};
natscpp::inject_trace_context(carrier, injected);

std::unordered_map<std::string, std::string> extracted;
std::vector<std::string> keys = {"traceparent", "tracestate"};
natscpp::extract_trace_context(carrier, keys, extracted);
```

## Includes

- `#include <natscpp/natscpp.hpp>` (umbrella)
- `#include <natscpp/connection.hpp>`
- `#include <natscpp/message.hpp>`
- `#include <natscpp/subscription.hpp>`
- `#include <natscpp/jetstream.hpp>`
- `#include <natscpp/kv.hpp>`
- `#include <natscpp/trace.hpp>`

## Notes

This project is intentionally header-only. Link your application against `nats` as required by your environment.
