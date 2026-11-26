# natscpp

[![CI](https://img.shields.io/github/actions/workflow/status/hurdad/nats-cpp/ci.yml?branch=main&logo=github&label=CI)](https://github.com/hurdad/nats-cpp/actions/workflows/ci.yml)
[![codecov](https://codecov.io/gh/hurdad/nats-cpp/branch/main/graph/badge.svg)](https://codecov.io/gh/hurdad/nats-cpp)

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
  - [Coroutines](#coroutines)
  - [Library utilities](#library-utilities)
- [Threading and callback lifetimes](#threading-and-callback-lifetimes)
- [Includes](#includes)
- [Development](#development)

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
- JetStream wrappers (context, stream/consumer management, push/pull subscriptions, callback push
  subscriptions, batch fetch).
- Stream + consumer-group creation helpers (`create_stream`, `create_consumer_group`).
- KeyValue wrappers (open bucket, put/get/delete entries, watch key updates).
- Trace propagation helpers using NATS headers (`TraceCarrier` concept).

## Requirements

- C++20 or C++23 compiler. CI builds with GCC and Clang in both modes; the library only needs C++20, and a
  project compiling as C++23 can use it unchanged.
- CMake 3.20+
- `nats.c` dependency, resolved in one of two ways:
  - bundled submodule at `third_party/nats.c` (static build wired automatically by CMake), or
  - system install that provides the `cnats` CMake package (e.g. Debian/Ubuntu `libnats-dev`, or nats.c's own
    `cmake --install`), found with `find_package(cnats)`; add its prefix to `CMAKE_PREFIX_PATH` if it is not in a
    standard location

## Build and test

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

The `unit` test group needs no server. The `integration` and `tls` groups need nats-servers, and **skip
silently** when none is reachable. To run everything, start the same servers CI uses (needs `nats-server` and
`openssl`; set `NATS_SERVER` to use another binary):

```bash
eval "$(tests/ci/start-servers.sh /tmp/natscpp-servers | sed 's/^/export /')"
NATSCPP_REQUIRE_SERVER=1 ctest --test-dir build --output-on-failure   # a skip is now a failure
tests/ci/run-examples.sh build                                         # every example, checked
tests/ci/stop-servers.sh /tmp/natscpp-servers
```

Build options:

| Option | Default | Effect |
|---|---|---|
| `NATSCPP_NATS_BACKEND` | `auto` | nats.c source: `auto`, `system` or `bundled` (below) |
| `NATSCPP_BUNDLED_WITH_TLS` | ON if OpenSSL is found | Build the bundled nats.c with TLS |
| `NATSCPP_BUILD_TESTS` | ON when top-level | Build the test suite |
| `NATSCPP_BUILD_EXAMPLES` | ON when top-level | Build the examples |
| `NATSCPP_WARNINGS_AS_ERRORS` | OFF | `-Werror` / `/WX` for natscpp's tests and examples (CI turns it on) |

Use `-DCMAKE_CXX_STANDARD=23` to build the tests and examples as C++23.

`NATSCPP_NATS_BACKEND` picks the nats.c source: `auto` (default) uses a system `cnats` package if one is found and
otherwise falls back to the bundled submodule, `system` requires the system package, and `bundled` always uses the
submodule. With `auto`, if `third_party/nats.c` is not initialized and no `cnats` package is found, configure fails.

TLS: the bundled submodule is built with TLS when OpenSSL is found (`NATSCPP_BUNDLED_WITH_TLS`, default ON if
OpenSSL is available; set it OFF to build without). A system nats.c has whatever TLS support it was built with;
`libnats-dev` includes it:

```bash
sudo apt install libnats-dev
cmake -S . -B build -DNATSCPP_NATS_BACKEND=system
```

Without TLS in nats.c, TLS options (including `ssl_verification_callback`) make `connect()` throw `NATS_ILLEGAL_STATE`.
The `natscpp::natscpp` target defines `NATS_HAS_TLS` when the nats.c it links has TLS (probed for a system nats.c);
`ca_certs_path` and `cipher_suites` need it. Define it yourself if you use the headers without that target.

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

As a subdirectory, natscpp builds neither its tests nor its examples (turn them on with
`NATSCPP_BUILD_TESTS` / `NATSCPP_BUILD_EXAMPLES`), and it leaves the parent's cache alone.

`natscpp::natscpp` brings in everything needed to link: nats.c, OpenSSL when nats.c uses it, and threads.
With the bundled backend, `cmake --install` also installs the static nats.c library, its headers (`nats.h`,
`nats/`) and its `cnats` CMake package into the same prefix, because the installed natscpp package depends on
them. Install into a prefix of its own if another nats.c is already installed there.

## Example programs

Each `examples/<name>.cpp` builds a `natscpp_<name>` target. They connect to `$NATS_URL` (default
`nats://127.0.0.1:4222`; JetStream and KV need a server started with `-js`), check their own results, exit
non-zero with a readable message on failure, and clean up any streams or buckets they create.

| Example | Shows |
|---|---|
| `sync_example` | Blocking publish/subscribe and request/reply |
| `async_example` | Callback subscriptions and `request_async` |
| `request_reply_example` | Request/reply in one process |
| `request_reply_server_example` / `request_reply_client_example` | A two-process service with a queue group, graceful shutdown on Ctrl-C, and handling no responders |
| `queue_group_example` | Load balancing across a queue group, and `drain()` |
| `headers_trace_example` | Message headers and trace-context propagation |
| `lifecycle_example` | Reconnect settings, connection callbacks, statistics, and draining a connection |
| `secure_connection_example` | TLS and authentication, configured from environment variables |
| `coroutine_example` | `co_await` on `request_awaitable` |
| `jetstream_sync_pull_example` | Pull consumer: batch `fetch()` and acks |
| `jetstream_async_pull_example` | Pull consumer driven from a worker thread |
| `jetstream_sync_push_example` | Push consumer with `next()`, and message metadata |
| `jetstream_async_push_example` | Callback push consumer (`jetstream::subscribe_async`) with manual acks |
| `kv_example` | KeyValue: put/get, compare-and-set, history, keys, and watchers |

## API examples

### Sync API example

```cpp
#include <chrono>
#include <natscpp/connection.hpp>

int main() {
  natscpp::connection nc({.url = "nats://127.0.0.1:4222"});  // throws natscpp::nats_error if unreachable

  auto sub = nc.subscribe_sync("demo.sync.events");  // subscribe before publishing, or the message is missed
  nc.publish("demo.sync.events", "hello");

  auto msg = sub.next_message(std::chrono::seconds(1));  // throws NATS_TIMEOUT if nothing arrives
  return msg.data() == "hello" ? 0 : 1;
}
```

### Async API example

```cpp
#include <chrono>
#include <natscpp/connection.hpp>

int main() {
  natscpp::connection nc({.url = "nats://127.0.0.1:4222"});

  // An echo responder: the handler runs on a nats.c delivery thread.
  auto echo = nc.subscribe_async("demo.echo", [&nc](natscpp::message req) {
    if (!req.reply_to().empty()) nc.publish(req.reply_to(), req.data());
  });

  auto fut = nc.request_async("demo.echo", "ping async", std::chrono::seconds(2));
  auto reply = fut.get();  // rethrows the request's nats_error, e.g. NATS_NO_RESPONDERS or NATS_TIMEOUT
  return reply.data() == "ping async" ? 0 : 1;
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
| `user_credentials_file` / `set_user_credentials_from_files(creds, seed = "")` | JWT credentials: a chained `.creds` file, or a JWT file plus a separate seed file |
| `nkey_public` + `nkey_signature_cb` / `set_nkey()` | NKey public key + signature callback |
| `token_handler_cb` / `set_token_handler()` | Dynamic token callback (invoked on each connect/reconnect) |

**TLS**

| Field / setter | Description |
|---|---|
| `secure` / `set_secure(true)` | Require TLS |
| `ca_trusted_certificates_file` / `load_ca_trusted_certificates()` | CA certificate file |
| `certificates_chain_file` + `private_key_file` / `load_certificates_chain()` | Client cert + key |
| `skip_server_verification` / `set_skip_server_verification()` | Turn off **all** server certificate verification (testing only) |
| `expected_hostname` / `set_expected_hostname()` | TLS SNI override |
| `ssl_verification_callback` / `set_ssl_verification_callback()` | Custom OpenSSL verify callback (nats.c ≥ 3.10, EXPERIMENTAL there) |

Since nats.c 3.15.0, setting any TLS option (CA, client certificate, ciphers, expected hostname, verification
callback, skip verification) makes the client require TLS: connecting to a plaintext server fails with
`NATS_SECURE_CONNECTION_WANTED`. `set_secure(false)` opts out (natscpp applies `secure` before the other TLS
options, as nats.c requires), and a `tls://` URL always uses TLS. nats.c 3.15.0 also rejects `ws://` and `wss://`
URLs with `NATS_INVALID_ARG`.

`ssl_verification_callback` is a plain `natsSSLVerifyCb` function pointer (`int(int preverify_ok, void* ctx)`,
where `ctx` is an `X509_STORE_CTX*`). nats.c passes no user-data pointer, so any state the callback needs must be
global/static. It replaces nats.c's own verify callback, so return `0` when `preverify_ok` is `0` to keep normal
chain verification. It is applied after the other TLS options: setting it turns server verification back on even
if `skip_server_verification` is `true`. The field only exists when building against nats.c 3.10.0 or newer
(`NATS_VERSION_NUMBER >= 0x030A00`). With a nats.c built without TLS, `connect()` throws `NATS_ILLEGAL_STATE`.

Example: accept the server only if its certificate was issued by one specific intermediate CA:

```cpp
#include <natscpp/connection.hpp>
#include <openssl/x509_vfy.h>

static X509* g_pinned_issuer;  // loaded once at startup, e.g. with PEM_read_X509

static int pin_issuer(int preverify_ok, void* ctx) {
  if (!preverify_ok) return 0;                       // chain must verify against the CA first
  auto* store = static_cast<X509_STORE_CTX*>(ctx);
  if (X509_STORE_CTX_get_error_depth(store) != 0) return 1;
  STACK_OF(X509)* chain = X509_STORE_CTX_get0_chain(store);
  return chain && sk_X509_num(chain) >= 2 && X509_cmp(sk_X509_value(chain, 1), g_pinned_issuer) == 0;
}

natscpp::connection_options opts;
opts.set_url("tls://nats.example:4222");
opts.load_ca_trusted_certificates("root-ca.pem");
opts.set_ssl_verification_callback(&pin_issuer);
```

**Escape hatch**

| Field / setter | Description |
|---|---|
| `customize_options` / `set_customize_options()` | `std::function<void(natsOptions*)>` called after every other option is applied, just before connecting; for nats.c options the wrapper does not cover |

**Lifecycle callbacks**

| Field / setter | Description |
|---|---|
| `closed_cb` / `set_closed_cb()` | Called when connection is permanently closed (may run after the `connection` object is destroyed) |
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
| `connected()` | Whether the connection is currently up (false while connecting, reconnecting, draining or closed) |
| `status()` | Raw `natsConnStatus` enum |
| `connected_url()` | URL of currently connected server |
| `connected_server_id()` | Server ID string |
| `servers()`, `discovered_servers()` | Known server URL lists |
| `rtt()` | Round-trip time measurement |
| `has_header_support()` | Whether server supports NATS headers |
| `get_statistics()` | `statistics` struct: in/out message and byte counts, reconnect count |
| `client_id()`, `client_ip()` | Client identity on the server |
| `local_ip_and_port()` | Local socket binding |
| `new_inbox()` | Generate a unique reply-to inbox subject (static) |
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

`msg.is_no_responders()` returns `true` for the status message the server sends when a request has no
responders. Only an async handler receives that message: `request_sync` / `request_async` throw
`NATS_NO_RESPONDERS` instead, and so does `next_message()` on a sync subscription.

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

The `natscpp::header` class wraps a standalone `natsHeader*`, for nats.c APIs that take one (through
`native_handle()`). To put headers on a message you publish, use `message::set_header` / `add_header` above.

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
```

**Completion callback** (async subscriptions): runs once the subscription is closed — by `unsubscribe()`,
drain, auto-unsubscribe or connection close — and its handler has returned for the last time. Use it to know
when state the handler uses can be freed. A second call replaces the first; sync subscriptions, and
subscriptions that already completed, throw `NATS_INVALID_SUBSCRIPTION`.

```cpp
std::promise<void> done;
sub.set_on_complete_callback([&done] { done.set_value(); });
sub.unsubscribe();
done.get_future().wait();  // the handler will not run again
```

`fetch()` / `fetch_request()` here work on a pull subscription you created with nats.c directly; with
natscpp's own JetStream API use `js_pull_consumer::fetch()` (below).

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

natscpp::connection nc({.url = "nats://127.0.0.1:4222"});  // a default-constructed connection is not connected
natscpp::jetstream js(nc);

(void)js.create_stream({.name = "ORDERS", .subjects = {"orders.>"}});
(void)js.create_consumer_group({
  .stream = "ORDERS",
  .durable_name = "orders-worker",
  .filter_subject = "orders.created",
  .type = natscpp::js_consumer_type::pull,
});

(void)js.publish("orders.created", R"({"id":"A-1"})");
auto pull = js.pull_subscribe("orders.created", "orders-worker");
for (auto& msg : pull.fetch(10, std::chrono::seconds(1))) {  // one pull request for up to 10 messages
  // ... process ...
  msg.ack();  // the default ack policy is explicit: unacked messages are redelivered after the ack wait
}
auto one = pull.next(std::chrono::seconds(1));  // a single message (one round trip each)
```

`fetch()` and `next()` throw `NATS_TIMEOUT` when no message arrives in time.

**Push consumers:** `push_subscribe(subject, durable)` returns a `js_push_consumer` read with `next()`.
`subscribe_async(subject, durable, handler, manual_ack = false)` delivers to a callback instead and returns a
`natscpp::subscription` (same lifetime rules as core async subscriptions). Without `manual_ack`, nats.c acks
each message after the handler returns; with it, the handler calls `ack()` / `nak()` / `term()`. An empty
durable name creates an ephemeral consumer.

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
| `description` | — | Optional description |
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
| `inactive_threshold_ns` | `0` | Delete an inactive consumer after this long (0 = server default) |
| `replicas` | `0` | Consumer replicas (0 = inherit from the stream) |
| `description` | — | Optional description |

`filter_subject` and `filter_subjects` are alternatives: setting both throws `std::invalid_argument`.

### Publish with options

```cpp
natscpp::js_publish_options opts;
opts.msg_id = "dedup-key-1";           // deduplication ID
opts.expected_last_seq = 42;           // optimistic concurrency
opts.msg_ttl_ms = 5000;               // message TTL (nats-server 2.11+)

natscpp::js_pub_ack ack = js.publish("orders.created", R"({})", opts);
// publish_msg(message, opts) publishes a message with headers, with the same options.
// ack.stream, ack.sequence, ack.domain, ack.duplicate
```

### Stream and consumer management

`update_stream` and `update_consumer_group` send a complete configuration, not a patch: fields you leave out
go back to their defaults (an update without `subjects` would try to clear the stream's subjects). Start
from the full configuration you created the stream or consumer with and change what you need.

```cpp
// Streams
natscpp::js_stream_config orders{.name = "ORDERS", .subjects = {"orders.>"}};
orders.max_msgs = 1000000;
(void)js.update_stream(orders);
js.purge_stream("ORDERS");
natscpp::stream_info si = js.get_stream_info("ORDERS");
std::vector<natscpp::stream_info>  streams = js.list_streams();
std::vector<std::string>           names   = js.list_stream_names();

// Direct message access
natscpp::message by_seq  = js.get_msg("ORDERS", 42);
natscpp::message last    = js.get_last_msg("ORDERS", "orders.created");
js.delete_msg("ORDERS", 42);
js.erase_msg("ORDERS", 42);   // overwrite payload before deletion

// Consumers
(void)js.update_consumer_group({.stream = "ORDERS", .durable_name = "worker", .max_deliver = 5});
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
auto kv = natscpp::key_value::create(nc, "profiles");  // or create(nc, kv_config{.bucket = ..., .history = 5})

auto rev = kv.put("user-1", R"({"name":"Ada"})");
auto entry = kv.get("user-1");

natscpp::kv_watch_options watch_opts;
watch_opts.updates_only = true;
auto watcher = kv.watch("user-1", &watch_opts);

kv.put("user-1", R"({"name":"Ada","active":true})");
auto updated = watcher.next(1000);  // timeout in milliseconds; next() with no argument blocks until an entry or stop()
watcher.stop();

kv.erase("user-1");
natscpp::key_value::delete_bucket(nc, "profiles");
```

Watch APIs:
- `watch(key, opts)` for a single key/filter
- `watch_multi(keys, opts)` for multiple keys/filters
- `watch_all(opts)` for all keys in the bucket

`kv_watcher::next()` takes a timeout in milliseconds and throws `NATS_TIMEOUT` when it expires; without an
argument it blocks until an entry arrives or the watcher is stopped. Unless `updates_only` is set, a watcher
first delivers the current value of every matching key, then **one invalid entry** (`entry.valid() == false`)
marking the end of that snapshot, then live updates — so check `valid()` before using an entry.

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

auto msg = natscpp::message::create("trace.subject", /*reply_to=*/"", "payload");

natscpp::message_trace_carrier carrier(msg);
std::vector<std::pair<std::string, std::string>> injected = {
  {"traceparent", "00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01"},
};
natscpp::inject_trace_context(carrier, injected);

std::unordered_map<std::string, std::string> extracted;
std::vector<std::string> keys = {"traceparent", "tracestate"};
natscpp::extract_trace_context(carrier, keys, extracted);
```

### Coroutines

`request_awaitable(subject, payload, timeout)` returns an awaitable for `co_await` in your own coroutine type
(natscpp does not ship one; see `examples/coroutine_example.cpp` for a minimal task):

```cpp
natscpp::message reply = co_await nc.request_awaitable("svc.echo", "ping");  // throws like request_sync
```

The coroutine resumes on a background thread (one per suspension), so this suits occasional requests, not
high-rate traffic. The awaitable is move-only.

### Library utilities

`library.hpp` exposes thin wrappers around global nats.c functions.

```cpp
#include <natscpp/library.hpp>

// Verify header/library version compatibility (call before anything else).
bool ok = natscpp::nats_CheckCompatibility();

// Initialize the library (optional — nats.c does this lazily on first connect).
natscpp::nats_Open(-1);

// Clean up all library resources at exit (call one of these, once, after every natscpp object is gone).
natscpp::nats_CloseAndWait(std::chrono::seconds(5));  // waits for nats.c's threads; or nats_Close() to not wait

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

## Threading and callback lifetimes

- Message handlers and connection callbacks run on nats.c's threads, not yours. Synchronize any state they
  share with your code.
- An async subscription keeps its handler alive until nats.c reports it complete. Destroying, move-assigning
  or unsubscribing a `subscription` — from any thread, including from inside its own handler — never frees a
  handler that is running. The handler may still run briefly after `unsubscribe()` returns, so state it uses
  through references must outlive it: wait for the completion callback (above) or share the state with a
  `shared_ptr`.
- Connection callbacks are owned by the connection inside nats.c and released after `closed_cb`, which can
  run after the `natscpp::connection` object is destroyed. Capture what they need by value or by `shared_ptr`.
- Exceptions thrown by a handler or callback are caught and reported on stderr; they never unwind into nats.c.
- `request_async` runs each request on its own thread (`std::async`).

## Includes

- `#include <natscpp/natscpp.hpp>` (umbrella — includes all headers below)
- `#include <natscpp/connection.hpp>`
- `#include <natscpp/message.hpp>`
- `#include <natscpp/subscription.hpp>`
- `#include <natscpp/header.hpp>`
- `#include <natscpp/awaitable.hpp>` (`future_awaitable`; already included by `connection.hpp`)
- `#include <natscpp/jetstream.hpp>`
- `#include <natscpp/kv.hpp>`
- `#include <natscpp/trace.hpp>`
- `#include <natscpp/error.hpp>`
- `#include <natscpp/library.hpp>`

## Development

This project is intentionally header-only; the CMake target carries the nats.c link dependency.

CI (`.github/workflows/ci.yml`) runs on every push and pull request:

- the full test suite and every example against real nats-servers, under **ASan + UBSan** and **TSan**
  (suppressions for nats.c and OpenSSL internals live in `tests/ci/*.supp`, each with its reason);
- Release builds with GCC as **C++20 and C++23**, a Clang C++23 build, and a build without TLS — all with
  warnings as errors;
- **coverage** of `include/natscpp` (line and branch, with minimum thresholds) and every job's **test
  results** (JUnit), both uploaded to Codecov;
- **lint**: `clang-format` (`.clang-format`), `clang-tidy` (`.clang-tidy`, with the reason for each disabled
  check), `shellcheck` and `actionlint`;
- an **install** check: `find_package(natscpp)` from an installed prefix, and `add_subdirectory` without
  pulling natscpp's tests or examples into the parent build.

Locally:

```bash
clang-format -i $(git ls-files '*.hpp' '*.cpp' ':!:third_party')     # format
cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON && run-clang-tidy -p build tests/ examples/
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
```
