# natscpp

Modern, header-only **C++20** wrappers for the official [`nats.c`](https://github.com/nats-io/nats.c) client.

## Features

- Header-only API in the `natscpp` namespace.
- RAII wrappers for core NATS resources.
- Core messaging APIs:
  - connection management
  - publish / subscribe
  - queue-group subscriptions
  - request/reply (sync + async)
- Optional coroutine support (`co_await`) through future awaitables.
- JetStream wrappers (context, publish, push/pull subscriptions).
- Trace propagation helpers using NATS headers (`TraceCarrier` concept).
- Graceful JetStream fallback when linked `nats.c` does not expose JetStream symbols.

## Requirements

- C++20 compiler
- `nats.c` headers and runtime (`nats/nats.h`)

## Installation with CMake

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

## Basic usage

```cpp
#include <chrono>
#include <natscpp/connection.hpp>

int main() {
  natscpp::connection nc({.url = "nats://127.0.0.1:4222"});

  auto sub = nc.subscribe_sync("demo.events");
  nc.publish("demo.events", "hello");

  auto msg = sub.next_message(std::chrono::seconds(1));
  return msg.data() == "hello" ? 0 : 1;
}
```

## Request / reply

```cpp
#include <natscpp/connection.hpp>

natscpp::connection nc;
auto reply = nc.request("svc.echo", "ping");

auto fut = nc.request_async("svc.echo", "ping async");
auto async_reply = fut.get();
```

## JetStream

```cpp
#include <natscpp/jetstream.hpp>

natscpp::connection nc;
natscpp::jetstream js(nc);

js.publish("orders.created", R"({"id":"A-1"})");
auto pull = js.pull_subscribe("orders.created", "orders-worker");
auto msg = pull.next(std::chrono::seconds(1));
```

If JetStream symbols are unavailable in your linked `nats.c`, APIs throw `natscpp::jetstream_not_available`.

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
- `#include <natscpp/trace.hpp>`

## Notes

This project is intentionally header-only. Link your application against `nats` as required by your environment.
