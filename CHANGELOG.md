# Changelog

## 0.2.0

### Security
- **NATS protocol injection through subjects.** nats.c checks subjects when subscribing but not when
  publishing, so a subject or reply subject containing a space, tab or CR/LF (for example one built from
  untrusted input) was written into the protocol as-is and could publish additional messages to other subjects
  with the connection's permissions. Every publish and request path (`publish*`, `request*`, `publish(message)`,
  `message::create`, `jetstream::publish`/`publish_msg`) now rejects such subjects, and wildcards, with
  `nats_error` `NATS_INVALID_SUBJECT`.
- **Header injection.** nats.c writes header keys into the protocol unchecked, so a key containing CR/LF could
  add headers of the caller's choosing, including JetStream ones such as `Nats-Msg-Id`. Header keys must now be
  visible ASCII without `:`, and values may not contain CR, LF or NUL (`NATS_INVALID_ARG`).
- **Silent truncation at NUL bytes.** Every `std::string_view` argument passed to nats.c as a C string was cut
  at its first NUL byte, so what was sent, looked up or signed (`nats_Sign`) could differ from what the
  application checked. Such arguments are now rejected with `NATS_INVALID_ARG`.

### Fixed
- `connection::last_error()` threw whenever the connection had an error, so it could never return one. It now
  returns the error text (empty when there is none), using `natsConnection_ReadLastError`.
- `process_read_event()` / `process_write_event()` on a connection that is not initialized crashed in nats.c;
  they now throw `NATS_INVALID_ARG`.
- `message::create()` rejects payloads over `INT_MAX` bytes instead of truncating the length.
- `message::create()` before any connection crashed when the message was destroyed: nats.c frees messages
  through its library state, which only opening a connection initialized. `create()` now opens the library.
- **Use-after-free in async subscriptions.** Destroying, move-assigning or unsubscribing a `subscription`
  freed its handler even while nats.c was still running it on its delivery thread, including when a handler
  unsubscribed itself. The handler now stays alive until nats.c reports the subscription complete.
- **Use-after-free in on-complete callbacks.** The callback was freed with the `subscription` object, before
  nats.c ran it. It now lives until it has run.
- **Use-after-free in connection callbacks.** `closed_cb` (and the other connection callbacks) were freed when
  the `connection` was destroyed, but nats.c runs `closed_cb` afterwards on its own thread. They are now
  released after `closed_cb` has run.
- `set_user_credentials_from_files(creds)` stored an empty seed file name, so nats.c tried to open `""` and
  every connection to a server using JWT/nkey authentication failed.
- `connection::has_header_support()` returned the opposite of the truth.
- Error messages could end with an unrelated earlier nats.c error; the last error is now only appended when it
  matches the status being reported.
- JetStream option structs are initialized with nats.c's `*_Init` functions instead of zeroing, so unset
  consumer/subscription policies are left to nats.c and the server.
- `library.hpp` no longer `#undef`s nats.c's `nats_CheckCompatibility` macro.
- `add_subdirectory(natscpp)` no longer builds natscpp's tests and examples or forces nats.c options into the
  parent project's cache.

### Added
- `js_pull_consumer::fetch(batch, timeout)` for batch pulls; `next()` now uses it.
- `jetstream::subscribe_async(subject, durable, handler, manual_ack)`: callback-based push consumers.
- `native_handle()` on `js_pull_consumer` and `js_push_consumer`.
- Input checks: setting both `filter_subject` and `filter_subjects`; JetStream and KV payloads over `INT_MAX`.
- Examples: queue groups, headers and trace propagation, lifecycle callbacks and drain, TLS/auth, coroutines.
  All examples read `$NATS_URL`, check their results, report errors, and clean up after themselves.
- Each test function is its own CTest test (`<group>.<name>`, labelled `unit`/`integration`/`tls`, run in
  its own process), so CI and Codecov report results per test; unreachable servers show as skipped.
- CI: TSan, Clang, C++23, warnings as errors, clang-format, clang-tidy, shellcheck, actionlint, install and
  `add_subdirectory` consumers, example runs, coverage thresholds, and test results on Codecov.

### Changed (may need code changes)
- **nats.c 3.15.0 or newer is required** (the bundled version, and the recommended one). Support for older
  nats.c is dropped: `auto` skips an older system `cnats` for the bundled one, `system` and the installed
  natscpp package reject it, and the headers fail to compile against it. Distribution packages such as
  Ubuntu's `libnats-dev` are usually older.
- `connection::connected()` now reports whether the connection is up; it used to only report whether a handle
  existed (true even after `close()`).
- `future_awaitable` is move-only. A copy could cancel the pending resume and hang the coroutine.
- `subscription::set_on_complete_callback()` throws `NATS_INVALID_SUBSCRIPTION` for sync subscriptions and for
  subscriptions that already completed.
- `connection::new_inbox()` is static (calls through an instance still compile).
- `key_value::bucket_status::replicas` is `int64_t`, matching nats.c.
- `nats_error`'s constructor takes `const std::string&`.
- New CMake options `NATSCPP_BUILD_TESTS`, `NATSCPP_BUILD_EXAMPLES` (both default to ON only when natscpp is
  the top-level project) and `NATSCPP_WARNINGS_AS_ERRORS`.
