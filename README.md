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
  - [API mapping](#api-mapping)
  - [connection_options](#connection_options)
  - [Message](#message)
  - [Headers](#headers)
  - [Subscription](#subscription)
  - [JetStream](#jetstream)
  - [KeyValue](#keyvalue)
  - [Trace propagation](#trace-propagation)
  - [Library utilities](#library-utilities)
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
- `natscpp_request_reply_server_example`
- `natscpp_request_reply_client_example`
- `natscpp_jetstream_sync_pull_example`
- `natscpp_jetstream_sync_push_example`
- `natscpp_jetstream_async_pull_example`
- `natscpp_jetstream_async_push_example`
- `natscpp_kv_example`

> Most examples expect a local NATS server at `nats://127.0.0.1:4222`.

## API examples

### Sync API example

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

### Async API example

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

### API mapping

- Synchronous subscribe: `subscribe_sync`, `subscribe_queue_sync`
- Asynchronous callback subscribe: `subscribe_async`, `subscribe_queue_async`
  - With timeout: `subscribe_async_timeout`, `subscribe_queue_async_timeout`
  - Short aliases: `subscribe` (→ `subscribe_async`), `subscribe_queue` (→ `subscribe_queue_async`)
- Synchronous request/reply: `request_sync` (alias: `request(subject, payload, timeout)`)
- Asynchronous request/reply: `request_async`, `request_awaitable`
- Message-object request: `request(message, timeout)`, `request_string`

Request/reply example is available in:
- `examples/request_reply_example.cpp`
- `examples/request_reply_server_example.cpp`
- `examples/request_reply_client_example.cpp`

### connection_options

`connection_options` is a plain struct — set fields directly or via convenience setters before passing to the `connection` constructor.

**Authentication**

| Field / setter | Description |
|---|---|
| `token` / `set_token()` | Static token |
| `user`, `password` / `set_user_info()` | Username + password |
| `user_credentials_file` / `set_user_credentials_from_files()` | NKey/JWT credentials file |
| `nkey_public` + `nkey_signature_cb` / `set_nkey()` | NKey public key + signature callback |
| `token_handler_cb` / `set_token_handler()` | Dynamic token callback (invoked on each connect/reconnect) |

**TLS**

| Field / setter | Description |
|---|---|
| `secure` / `set_secure(true)` | Require TLS |
| `ca_trusted_certificates_file` / `load_ca_trusted_certificates()` | CA certificate file |
| `certificates_chain_file` + `private_key_file` / `load_certificates_chain()` | Client cert + key |
| `skip_server_verification` / `set_skip_server_verification()` | Disable hostname check |
| `expected_hostname` / `set_expected_hostname()` | TLS SNI override |

**Lifecycle callbacks**

| Field / setter | Description |
|---|---|
| `closed_cb` / `set_closed_cb()` | Called when connection is permanently closed |
| `disconnected_cb` / `set_disconnected_cb()` | Called on disconnect |
| `reconnected_cb` / `set_reconnected_cb()` | Called after successful reconnect |
| `error_handler` / `set_error_handler()` | Async error handler `(conn, sub, status)` |
| `lame_duck_mode_cb` / `set_lame_duck_mode_cb()` | Server entering lame-duck mode |
| `discovered_servers_cb` / `set_discovered_servers_cb()` | New server discovered in cluster |

**Reconnect and back-pressure**

| Field / setter | Description |
|---|---|
| `retry_on_failed_connect` | Retry initial connect instead of failing immediately |
| `allow_reconnect` / `set_allow_reconnect()` | Enable/disable automatic reconnect |
| `max_reconnect` / `set_max_reconnect()` | Max reconnect attempts |
| `reconnect_wait` / `set_reconnect_wait()` | Delay between reconnect attempts |
| `reconnect_jitter` / `set_reconnect_jitter()` | Jitter added to reconnect wait |
| `max_pending_msgs`, `max_pending_bytes` | Per-subscription back-pressure limits |

**Other notable fields**: `urls` (cluster failover list), `name`, `timeout`, `ping_interval`, `no_echo`, `custom_inbox_prefix`, `disable_no_responders`.

**Connection diagnostics** (methods on `connection`):

| Method | Description |
|---|---|
| `status()` | Raw `natsConnStatus` enum |
| `connected_url()` | URL of currently connected server |
| `connected_server_id()` | Server ID string |
| `servers()`, `discovered_servers()` | Known server URL lists |
| `rtt()` | Round-trip time measurement |
| `has_header_support()` | Whether server supports NATS headers |
| `get_statistics()` | `statistics` struct: in/out message and byte counts, reconnect count |
| `client_id()`, `client_ip()` | Client identity on the server |
| `local_ip_and_port()` | Local socket binding |
| `new_inbox()` | Generate a unique reply-to inbox subject |
| `buffered_bytes()` | Bytes pending in outgoing buffer |
| `is_reconnecting()`, `is_draining()` | Connection state predicates |

### Message

Basic accessors (`subject()`, `reply_to()`, `data()`, `valid()`) are shown in the examples above.

**Headers on a received message:**

```cpp
std::string val = msg.header("Content-Type");
std::vector<std::string> all = msg.header_values("X-Custom");
std::vector<std::string> all_keys = msg.header_keys();
msg.set_header("key", "value");
msg.add_header("key", "value2");  // multi-value
msg.delete_header("key");
```

`msg.is_no_responders()` returns `true` when a request received a no-responders status response.

**JetStream acknowledgments** (only valid on JetStream-delivered messages):

| Method | Description |
|---|---|
| `ack()` | Acknowledge message |
| `ack_sync()` | Acknowledge and wait for server confirmation |
| `nak()` | Negative-acknowledge; server will redeliver |
| `nak_with_delay(ms)` | NAK with a redelivery delay |
| `in_progress()` | Reset the ack timeout (work-in-progress) |
| `term()` | Terminate without redelivery |

**JetStream metadata** (on JetStream messages):

```cpp
auto meta = msg.get_metadata();
// meta.stream, meta.consumer, meta.domain
// meta.sequence_stream, meta.sequence_consumer
// meta.num_delivered, meta.num_pending, meta.timestamp
uint64_t seq = msg.sequence();
int64_t  ts  = msg.timestamp();
```

**Creating a message** (for publishing with headers or for `request(message, timeout)`):

```cpp
auto msg = natscpp::message::create("subject", /*reply_to=*/"", "payload");
msg.set_header("Content-Type", "application/json");
```

### Headers

The `natscpp::header` class wraps a `natsHeader*` for building standalone header objects (e.g., when publishing with `publish_request` or constructing messages manually).

```cpp
#include <natscpp/header.hpp>

auto h = natscpp::header::create();
h.set("Content-Type", "application/json");
h.add("X-Trace", "span-1");

std::string val  = h.get("Content-Type");
auto all_vals    = h.values("X-Trace");
auto all_keys    = h.keys();
int  key_count   = h.keys_count();
h.erase("X-Trace");
```

`header::valid()`, `native_handle()`, and `release()` provide raw access for interoperating with nats.c APIs.

### Subscription

Beyond `next_message()` and `unsubscribe()`, `subscription` exposes:

**Draining** (graceful shutdown — processes in-flight messages before closing):

```cpp
sub.drain();                                     // async; start draining
sub.drain(std::chrono::milliseconds(500));        // with timeout
sub.wait_for_drain_completion(std::chrono::seconds(2));
natsStatus s = sub.drain_completion_status();
sub.set_on_complete_callback([]{ /* done */ });
```

**JetStream batch fetch** (pull consumers):

```cpp
// Fetch up to 10 messages, waiting up to 1 second
std::vector<natscpp::message> msgs =
    sub.fetch(10, std::chrono::seconds(1));
for (auto& m : msgs) {
  // process, then ack
  m.ack();
}
```

**Subscription statistics:**

```cpp
auto s = sub.get_stats();
// s.pending_messages, s.pending_bytes
// s.max_pending_messages, s.max_pending_bytes
// s.delivered_messages, s.dropped_messages

sub.set_pending_limits(1000, 64 * 1024 * 1024);
auto limits = sub.get_pending_limits();
auto pend   = sub.pending();
int64_t delivered = sub.delivered();
int64_t dropped   = sub.dropped();
uint64_t queued   = sub.queued_messages();
```

**Other methods:** `subject()`, `id()`, `is_valid()`, `auto_unsubscribe(n)`, `no_delivery_delay()`, `get_consumer_info()`, `get_sequence_mismatch()`.

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

### Policy enums

| Enum | Values |
|---|---|
| `js_storage_type` | `file`, `memory` |
| `js_retention_policy` | `limits`, `interest`, `work_queue` |
| `js_discard_policy` | `old`, `new_msgs` |
| `js_deliver_policy` | `all`, `last`, `new_msgs`, `by_start_seq`, `by_start_time`, `last_per_subject` |
| `js_ack_policy` | `explicit_`, `none`, `all` |
| `js_replay_policy` | `instant`, `original` |
| `js_consumer_type` | `pull`, `push` |

### js_stream_config fields

| Field | Default | Description |
|---|---|---|
| `name` | — | Stream name (required) |
| `subjects` | — | Subject filters |
| `storage` | `file` | Storage backend |
| `retention` | `limits` | Retention policy |
| `discard` | `old` | Message discard policy when limits hit |
| `max_consumers` | `-1` | Max concurrent consumers (-1 = unlimited) |
| `max_msgs` | `-1` | Max total messages |
| `max_bytes` | `-1` | Max total bytes |
| `max_age_ns` | `0` | Max message age in nanoseconds (0 = unlimited) |
| `max_msgs_per_subject` | `-1` | Per-subject message limit |
| `max_msg_size` | `-1` | Max single message size in bytes |
| `replicas` | `1` | Number of replicas in cluster |
| `duplicates_ns` | `0` | Deduplication window in nanoseconds |
| `no_ack` | `false` | Disable per-message acks |
| `allow_direct` | `false` | Allow direct get |
| `deny_delete` | `false` | Prohibit message deletion |
| `deny_purge` | `false` | Prohibit stream purge |

### js_consumer_config fields

| Field | Default | Description |
|---|---|---|
| `stream` | — | Target stream (required) |
| `durable_name` | — | Durable consumer name |
| `filter_subject` | — | Single subject filter |
| `filter_subjects` | — | Multi-subject filter (alternative to `filter_subject`) |
| `deliver_subject` | — | Push consumer delivery subject |
| `deliver_group` | — | Push consumer queue group |
| `type` | `pull` | `pull` or `push` |
| `deliver_policy` | `all` | Which messages to start delivering |
| `ack_policy` | `explicit_` | Acknowledgment policy |
| `replay_policy` | `instant` | Replay speed |
| `ack_wait_ns` | `0` | Ack timeout in nanoseconds (0 = server default) |
| `max_deliver` | `-1` | Max delivery attempts (-1 = unlimited) |
| `max_ack_pending` | `-1` | Max unacked messages |
| `max_waiting` | `-1` | Max waiting pull requests |
| `headers_only` | `false` | Deliver headers only, no body |
| `opt_start_seq` | `0` | Start sequence for `by_start_seq` |
| `opt_start_time_ns` | `0` | Start time (UTC ns) for `by_start_time` |

### Publish with options

```cpp
natscpp::js_publish_options opts;
opts.msg_id = "dedup-key-1";           // deduplication ID
opts.expected_last_seq = 42;           // optimistic concurrency
opts.msg_ttl_ms = 5000;               // message TTL (nats-server 2.11+)

natscpp::js_pub_ack ack = js.publish("orders.created", R"({})", &opts);
// ack.stream, ack.sequence, ack.domain, ack.duplicate
```

### Stream and consumer management

```cpp
// Streams
js.update_stream({.name = "ORDERS", .max_msgs = 1000000});
js.purge_stream("ORDERS");
natscpp::stream_info si = js.get_stream_info("ORDERS");
std::vector<natscpp::stream_info>  streams = js.list_streams();
std::vector<std::string>           names   = js.list_stream_names();

// Direct message access
natscpp::message m   = js.get_msg("ORDERS", 42);
natscpp::message m   = js.get_last_msg("ORDERS", "orders.created");
js.delete_msg("ORDERS", 42);
js.erase_msg("ORDERS", 42);   // overwrite payload before deletion

// Consumers
js.update_consumer_group({.stream = "ORDERS", .durable_name = "worker", .max_deliver = 5});
natscpp::consumer_info ci = js.get_consumer_group("ORDERS", "worker");
js.delete_consumer_group("ORDERS", "worker");
std::vector<natscpp::consumer_info>  consumers = js.list_consumer_groups("ORDERS");
std::vector<std::string>             cnames    = js.list_consumer_group_names("ORDERS");

// Account
natscpp::js_account_info ai = js.get_account_info();
// ai.memory, ai.store, ai.streams, ai.consumers
```

JetStream examples are available in:
- `examples/jetstream_sync_pull_example.cpp`
- `examples/jetstream_sync_push_example.cpp`
- `examples/jetstream_async_pull_example.cpp`
- `examples/jetstream_async_push_example.cpp`

## KeyValue

```cpp
#include <natscpp/connection.hpp>
#include <natscpp/kv.hpp>

natscpp::connection nc({.url = "nats://127.0.0.1:4222"});
auto kv = natscpp::key_value::create(nc, "profiles");

auto rev = kv.put("user-1", R"({"name":"Ada"})");
auto entry = kv.get("user-1");

natscpp::kv_watch_options watch_opts;
watch_opts.updates_only = true;
auto watcher = kv.watch("user-1", &watch_opts);

kv.put("user-1", R"({"name":"Ada","active":true})");
auto updated = watcher.next(1000);  // timeout in milliseconds
watcher.stop();

kv.erase("user-1");
natscpp::key_value::delete_bucket(nc, "profiles");
```

Watch APIs:
- `watch(key, opts)` for a single key/filter
- `watch_multi(keys, opts)` for multiple keys/filters
- `watch_all(opts)` for all keys in the bucket

`kv_watcher::next()` takes a timeout in milliseconds. It may return an invalid `kv_entry` (`entry.valid() == false`) when the initial snapshot is complete.

Additional CRUD and utility methods on `key_value`:

| Method | Description |
|---|---|
| `put(key, value)` | Store a value, returning the revision |
| `create(key, value)` | Store only if the key does not yet exist |
| `update(key, value, last_revision)` | Compare-and-swap update |
| `get(key)` | Fetch the current entry |
| `get_revision(key, revision)` | Fetch a specific historical revision |
| `erase(key)` | Delete the key (leaves a tombstone) |
| `purge(key)` | Purge all historical revisions of a key |
| `purge_deletes(timeout_ms)` | Remove all delete/purge tombstones from the bucket |
| `keys()` | List all active keys |
| `keys_with_filters(filters, opts)` | List keys matching glob filter patterns |
| `history(key, opts)` | Return the full revision history of a key |
| `status()` | Return bucket metadata (`bucket_status`) |

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

### Library utilities

`library.hpp` exposes thin wrappers around global nats.c functions.

```cpp
#include <natscpp/library.hpp>

// Verify header/library version compatibility (call before anything else).
bool ok = natscpp::nats_CheckCompatibility();

// Initialize the library (optional — nats.c does this lazily on first connect).
natscpp::nats_Open(-1);

// Clean up all library resources.
natscpp::nats_Close();
natscpp::nats_CloseAndWait(std::chrono::seconds(5));

// Version information.
const char* ver = natscpp::nats_GetVersion();
uint32_t    num = natscpp::nats_GetVersionNumber();

// Diagnostics.
char buf[1024];
natscpp::nats_GetLastErrorStack(buf, sizeof(buf));
natscpp::nats_PrintLastErrorStack(stderr);

// Per-thread cleanup (call from worker threads before exit).
natscpp::nats_ReleaseThreadMemory();

// Performance tuning: set shared message delivery pool size.
natscpp::nats_SetMessageDeliveryPoolSize(8);

// Time helpers (milliseconds / nanoseconds since epoch).
int64_t now_ms = natscpp::nats_Now();
int64_t now_ns = natscpp::nats_NowInNanoSeconds();
```

## Includes

- `#include <natscpp/natscpp.hpp>` (umbrella — includes all headers below **except** `awaitable.hpp`)
- `#include <natscpp/connection.hpp>`
- `#include <natscpp/message.hpp>`
- `#include <natscpp/subscription.hpp>`
- `#include <natscpp/header.hpp>`
- `#include <natscpp/awaitable.hpp>` (required for `co_await` / `request_awaitable`; not in umbrella)
- `#include <natscpp/jetstream.hpp>`
- `#include <natscpp/kv.hpp>`
- `#include <natscpp/trace.hpp>`
- `#include <natscpp/error.hpp>`
- `#include <natscpp/library.hpp>`

## Notes

This project is intentionally header-only. Link your application against `nats` as required by your environment.
