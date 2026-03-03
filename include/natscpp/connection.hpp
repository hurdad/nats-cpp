#pragma once

#include <nats/nats.h>

#include <cassert>
#include <chrono>
#include <array>
#include <cstdio>
#include <functional>
#include <future>
#include <limits>
#include <memory>
#include <cstdlib>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <natscpp/awaitable.hpp>
#include <natscpp/detail/deleters.hpp>
#include <natscpp/error.hpp>
#include <natscpp/message.hpp>
#include <natscpp/subscription.hpp>

namespace natscpp {

/**
 * @brief Connection options used to open a NATS connection.
 */
struct connection_options {
  // Primary server URL (used when urls is empty).
  std::string url{"nats://127.0.0.1:4222"};
  // Multiple server URLs for cluster / failover (takes precedence over url when non-empty).
  std::vector<std::string> urls;

  bool retry_on_failed_connect = false;

  // --- Authentication ---
  std::optional<std::string> token;
  std::optional<std::string> user;
  std::optional<std::string> password;
  std::optional<std::string> user_credentials_file;
  std::optional<std::string> user_nkey_seed_file;
  std::optional<std::string> nkey_public;
  natsSignatureHandler nkey_signature_cb = nullptr;
  void* nkey_signature_closure = nullptr;
  // Dynamic token handler (callback invoked on each connect/reconnect).
  natsTokenHandler token_handler_cb = nullptr;
  void* token_handler_closure = nullptr;

  // --- TLS ---
  std::optional<bool> secure;
  std::optional<std::string> ca_trusted_certificates_file;
  std::optional<std::string> ca_certs_path;           // directory of CA certificates
  std::optional<std::string> certificates_chain_file;
  std::optional<std::string> private_key_file;
  std::optional<bool> skip_server_verification;
  std::optional<std::string> expected_hostname;       // TLS SNI override
  std::optional<std::string> ciphers;
  std::optional<std::string> cipher_suites;

  // --- Connection lifecycle callbacks ---
  std::function<void(natsConnection*)> closed_cb;
  std::function<void(natsConnection*)> disconnected_cb;
  std::function<void(natsConnection*)> reconnected_cb;
  std::function<void(natsConnection*, natsSubscription*, natsStatus)> error_handler;
  std::function<void(natsConnection*)> lame_duck_mode_cb;
  std::function<void(natsConnection*)> discovered_servers_cb;

  // --- Reconnect behaviour ---
  std::optional<std::chrono::milliseconds> reconnect_wait;
  std::optional<int> max_reconnect;
  std::optional<bool> allow_reconnect;
  std::optional<bool> no_randomize;
  std::optional<int> max_pings_out;
  std::optional<int> reconnect_buf_size;
  // Jitter added to reconnect wait: {plain_ms, tls_ms}.
  std::optional<std::pair<int64_t, int64_t>> reconnect_jitter;

  // --- Subscription back-pressure ---
  std::optional<int> max_pending_msgs;
  std::optional<int64_t> max_pending_bytes;

  // --- I/O tuning ---
  std::optional<int> io_buf_size;
  std::optional<bool> send_asap;

  // --- Cluster / server discovery ---
  std::optional<bool> fail_requests_on_disconnect;
  std::optional<bool> ignore_discovered_servers;

  // --- Protocol tweaks ---
  std::optional<bool> disable_no_responders;
  std::optional<bool> use_old_request_style;
  std::optional<std::string> custom_inbox_prefix;
  std::optional<bool> pedantic;
  std::optional<bool> verbose;

  // --- Convenience setters ---
  std::optional<std::chrono::milliseconds> ping_interval;
  std::optional<std::chrono::milliseconds> timeout;
  std::optional<std::string> name;
  std::optional<bool> no_echo;

  void set_url(std::string u) { url = std::move(u); }
  void set_urls(std::vector<std::string> server_urls) { urls = std::move(server_urls); }
  void set_token(std::string t) { this->token = std::move(t); }
  void set_token_handler(natsTokenHandler handler, void* closure) {
    token_handler_cb = handler;
    token_handler_closure = closure;
  }
  void set_nkey(std::string nkey_public, natsSignatureHandler sig_cb, void* sig_closure) {
    this->nkey_public = std::move(nkey_public);
    nkey_signature_cb = sig_cb;
    nkey_signature_closure = sig_closure;
  }
  void set_user_credentials_from_files(std::string user_credentials_file, std::string seed_file = {}) {
    this->user_credentials_file = std::move(user_credentials_file);
    user_nkey_seed_file = std::move(seed_file);
  }
  void set_user_info(std::string user, std::string password) {
    this->user = std::move(user);
    this->password = std::move(password);
  }
  void set_secure(bool s) { this->secure = s; }
  void load_ca_trusted_certificates(std::string file_name) { ca_trusted_certificates_file = std::move(file_name); }
  void set_ca_certs_path(std::string path) { ca_certs_path = std::move(path); }
  void load_certificates_chain(std::string certs_file, std::string key_file) {
    certificates_chain_file = std::move(certs_file);
    private_key_file = std::move(key_file);
  }
  void set_skip_server_verification(bool skip) { skip_server_verification = skip; }
  void set_expected_hostname(std::string hostname) { expected_hostname = std::move(hostname); }
  void set_ciphers(std::string c) { ciphers = std::move(c); }
  void set_cipher_suites(std::string c) { cipher_suites = std::move(c); }

  void set_closed_cb(std::function<void(natsConnection*)> cb) { closed_cb = std::move(cb); }
  void set_disconnected_cb(std::function<void(natsConnection*)> cb) { disconnected_cb = std::move(cb); }
  void set_reconnected_cb(std::function<void(natsConnection*)> cb) { reconnected_cb = std::move(cb); }
  void set_error_handler(std::function<void(natsConnection*, natsSubscription*, natsStatus)> cb) {
    error_handler = std::move(cb);
  }
  void set_lame_duck_mode_cb(std::function<void(natsConnection*)> cb) { lame_duck_mode_cb = std::move(cb); }
  void set_discovered_servers_cb(std::function<void(natsConnection*)> cb) { discovered_servers_cb = std::move(cb); }

  void set_reconnect_wait(std::chrono::milliseconds rw) { this->reconnect_wait = rw; }
  void set_max_reconnect(int mr) { this->max_reconnect = mr; }
  void set_allow_reconnect(bool allow) { allow_reconnect = allow; }
  void set_no_randomize(bool no_rand) { no_randomize = no_rand; }
  void set_max_pings_out(int max) { max_pings_out = max; }
  void set_reconnect_buf_size(int size) { reconnect_buf_size = size; }
  void set_reconnect_jitter(int64_t jitter_ms, int64_t jitter_tls_ms = 0) {
    reconnect_jitter = {jitter_ms, jitter_tls_ms};
  }

  void set_max_pending_msgs(int max) { max_pending_msgs = max; }
  void set_max_pending_bytes(int64_t max) { max_pending_bytes = max; }
  void set_io_buf_size(int size) { io_buf_size = size; }
  void set_send_asap(bool val) { send_asap = val; }

  void set_fail_requests_on_disconnect(bool val) { fail_requests_on_disconnect = val; }
  void set_ignore_discovered_servers(bool val) { ignore_discovered_servers = val; }

  void set_disable_no_responders(bool val) { disable_no_responders = val; }
  void set_use_old_request_style(bool val) { use_old_request_style = val; }
  void set_custom_inbox_prefix(std::string prefix) { custom_inbox_prefix = std::move(prefix); }
  void set_pedantic(bool val) { pedantic = val; }
  void set_verbose(bool val) { verbose = val; }

  void set_ping_interval(std::chrono::milliseconds pi) { this->ping_interval = pi; }
  void set_timeout(std::chrono::milliseconds t) { this->timeout = t; }
  void set_name(std::string n) { this->name = std::move(n); }
  void set_no_echo(bool ne) { this->no_echo = ne; }
};

/**
 * @brief Owns a natsConnection and provides modern C++ APIs.
 */
class connection {
 public:
  struct statistics {
    uint64_t in_messages = 0;
    uint64_t in_bytes = 0;
    uint64_t out_messages = 0;
    uint64_t out_bytes = 0;
    uint64_t reconnects = 0;
  };

  connection() = default;

  explicit connection(const connection_options& options) {
    connect(options);
  }

  connection(const connection&) = delete;
  connection& operator=(const connection&) = delete;

  connection(connection&&) noexcept = default;
  connection& operator=(connection&&) noexcept = default;

  /**
   * @brief Quick connect to a single URL without building a connection_options.
   */
  [[nodiscard]] static connection connect_to(std::string_view url) {
    connection nc;
    connection_options opts;
    opts.url = std::string(url);
    nc.connect(opts);
    return nc;
  }

  void connect(const connection_options& options = {}) {
    natsConnection* raw{};
    natsOptions* nopts = nullptr;
    throw_on_error(natsOptions_Create(&nopts), "natsOptions_Create");
    std::unique_ptr<natsOptions, void (*)(natsOptions*)> holder(nopts, natsOptions_Destroy);

    // Build handlers on the heap so the pointer stored in nats.c remains valid after a move.
    auto handlers = std::make_shared<callback_handlers>();
    handlers->closed_cb = options.closed_cb;
    handlers->disconnected_cb = options.disconnected_cb;
    handlers->reconnected_cb = options.reconnected_cb;
    handlers->error_handler = options.error_handler;
    handlers->lame_duck_mode_cb = options.lame_duck_mode_cb;
    handlers->discovered_servers_cb = options.discovered_servers_cb;

    apply_nats_options(nopts, options, handlers.get());
    if (options.retry_on_failed_connect) {
      throw_on_error(natsOptions_SetRetryOnFailedConnect(nopts, true, nullptr, nullptr),
                     "natsOptions_SetRetryOnFailedConnect");
    }
    throw_on_error(natsConnection_Connect(&raw, nopts), "natsConnection_Connect");
    // Capture handlers in the deleter so they outlive the natsConnection even when
    // request_async (or any other code) holds conn_ beyond this object's lifetime.
    conn_.reset(raw, [h = handlers](natsConnection* p) { natsConnection_Destroy(p); });
    callback_handlers_ = std::move(handlers);
  }

  /**
   * @brief Triggers a reconnect to the current server.
   */
  void reconnect() {
    throw_on_error(natsConnection_Reconnect(conn_.get()), "natsConnection_Reconnect");
  }

  [[nodiscard]] bool connected() const noexcept { return conn_ != nullptr; }
  [[nodiscard]] natsConnection* native_handle() const noexcept { return conn_.get(); }
  [[nodiscard]] bool is_closed() const noexcept { return conn_ == nullptr || natsConnection_IsClosed(conn_.get()); }
  [[nodiscard]] bool is_reconnecting() const noexcept {
    return conn_ != nullptr && natsConnection_IsReconnecting(conn_.get());
  }
  [[nodiscard]] bool is_draining() const noexcept { return conn_ != nullptr && natsConnection_IsDraining(conn_.get()); }
  [[nodiscard]] natsConnStatus status() const noexcept {
    return conn_ != nullptr ? natsConnection_Status(conn_.get()) : NATS_CONN_STATUS_CLOSED;
  }

  [[nodiscard]] uint64_t buffered_bytes() const noexcept {
    if (conn_ == nullptr) return 0;
    const int n = natsConnection_Buffered(conn_.get());
    return n >= 0 ? static_cast<uint64_t>(n) : 0;
  }

  void publish(std::string_view subject, std::string_view payload) {
    assert(payload.size() <= static_cast<std::size_t>(std::numeric_limits<int>::max()) &&
           "publish: payload exceeds INT_MAX bytes");
    throw_on_error(natsConnection_Publish(conn_.get(), std::string(subject).c_str(), payload.data(),
                                          static_cast<int>(payload.size())),
                   "natsConnection_Publish");
  }

  void publish_string(std::string_view subject, std::string_view payload) {
    throw_on_error(natsConnection_PublishString(conn_.get(), std::string(subject).c_str(), std::string(payload).c_str()),
                   "natsConnection_PublishString");
  }

  void publish_request(std::string_view subject, std::string_view reply_to, std::string_view payload) {
    throw_on_error(natsConnection_PublishRequest(conn_.get(), std::string(subject).c_str(), std::string(reply_to).c_str(),
                                                 payload.data(), static_cast<int>(payload.size())),
                   "natsConnection_PublishRequest");
  }

  void publish_request_string(std::string_view subject, std::string_view reply_to, std::string_view payload) {
    throw_on_error(natsConnection_PublishRequestString(conn_.get(), std::string(subject).c_str(),
                                                       std::string(reply_to).c_str(), std::string(payload).c_str()),
                   "natsConnection_PublishRequestString");
  }

  void publish(message msg) {
    natsMsg* raw = msg.release();
    natsStatus status = natsConnection_PublishMsg(conn_.get(), raw);
    natsMsg_Destroy(raw);  // natsConnection_PublishMsg never takes ownership
    throw_on_error(status, "natsConnection_PublishMsg");
  }

  void flush(std::chrono::milliseconds timeout) {
    throw_on_error(natsConnection_FlushTimeout(conn_.get(), static_cast<int64_t>(timeout.count())),
                   "natsConnection_FlushTimeout");
  }

  void flush() { throw_on_error(natsConnection_Flush(conn_.get()), "natsConnection_Flush"); }
  void close() { natsConnection_Close(conn_.get()); }
  [[nodiscard]] std::array<unsigned char, 64> sign(std::string_view message) {
    assert(message.size() <= static_cast<std::size_t>(std::numeric_limits<int>::max()) &&
           "sign: message exceeds INT_MAX bytes");
    std::array<unsigned char, 64> sig{};
    throw_on_error(natsConnection_Sign(conn_.get(), reinterpret_cast<const unsigned char*>(message.data()),
                                       static_cast<int>(message.size()), sig.data()),
                   "natsConnection_Sign");
    return sig;
  }

  void process_read_event() { natsConnection_ProcessReadEvent(conn_.get()); }
  void process_write_event() { natsConnection_ProcessWriteEvent(conn_.get()); }

  void drain() { throw_on_error(natsConnection_Drain(conn_.get()), "natsConnection_Drain"); }
  void drain(std::chrono::milliseconds timeout) {
    throw_on_error(natsConnection_DrainTimeout(conn_.get(), static_cast<int64_t>(timeout.count())),
                   "natsConnection_DrainTimeout");
  }

  [[nodiscard]] int64_t max_payload() const noexcept {
    return conn_ != nullptr ? natsConnection_GetMaxPayload(conn_.get()) : 0;
  }

  [[nodiscard]] statistics get_statistics() const {
    natsStatistics* raw{};
    throw_on_error(natsStatistics_Create(&raw), "natsStatistics_Create");
    std::unique_ptr<natsStatistics, void (*)(natsStatistics*)> holder(raw, natsStatistics_Destroy);
    throw_on_error(natsConnection_GetStats(conn_.get(), holder.get()), "natsConnection_GetStats");
    statistics stats;
    throw_on_error(natsStatistics_GetCounts(holder.get(), &stats.in_messages, &stats.in_bytes, &stats.out_messages,
                                            &stats.out_bytes, &stats.reconnects),
                   "natsStatistics_GetCounts");
    return stats;
  }

  // Buffer sized at 4096 to accommodate long URLs; natsConnection_GetConnectedUrl
  // uses snprintf and silently truncates without returning an error.
  [[nodiscard]] std::string connected_url() const {
    std::array<char, 4096> buf{};
    throw_on_error(natsConnection_GetConnectedUrl(conn_.get(), buf.data(), static_cast<int>(buf.size())),
                   "natsConnection_GetConnectedUrl");
    return std::string(buf.data());
  }

  [[nodiscard]] std::string connected_server_id() const {
    std::array<char, 4096> buf{};
    throw_on_error(natsConnection_GetConnectedServerId(conn_.get(), buf.data(), static_cast<int>(buf.size())),
                   "natsConnection_GetConnectedServerId");
    return std::string(buf.data());
  }

  [[nodiscard]] std::vector<std::string> servers() const { return fetch_server_list(&natsConnection_GetServers); }
  [[nodiscard]] std::vector<std::string> discovered_servers() const {
    return fetch_server_list(&natsConnection_GetDiscoveredServers);
  }

  [[nodiscard]] std::string last_error() const {
    const char* value = nullptr;
    throw_on_error(natsConnection_GetLastError(conn_.get(), &value), "natsConnection_GetLastError");
    return value != nullptr ? std::string(value) : std::string{};
  }

  [[nodiscard]] uint64_t client_id() const {
    uint64_t value = 0;
    throw_on_error(natsConnection_GetClientID(conn_.get(), &value), "natsConnection_GetClientID");
    return value;
  }

  [[nodiscard]] std::string client_ip() const {
    char* value = nullptr;
    throw_on_error(natsConnection_GetClientIP(conn_.get(), &value), "natsConnection_GetClientIP");
    std::string out = value != nullptr ? value : "";
    std::free(value);
    return out;
  }

  [[nodiscard]] std::chrono::microseconds rtt() const {
    int64_t value = 0;
    throw_on_error(natsConnection_GetRTT(conn_.get(), &value), "natsConnection_GetRTT");
    return std::chrono::microseconds(value);
  }

  [[nodiscard]] bool has_header_support() const noexcept {
    return conn_ != nullptr && natsConnection_HasHeaderSupport(conn_.get());
  }

  [[nodiscard]] std::pair<std::string, int> local_ip_and_port() const {
    char* ip = nullptr;
    int port = 0;
    throw_on_error(natsConnection_GetLocalIPAndPort(conn_.get(), &ip, &port), "natsConnection_GetLocalIPAndPort");
    std::pair<std::string, int> out{ip != nullptr ? ip : "", port};
    std::free(ip);
    return out;
  }

  [[nodiscard]] subscription subscribe_sync(std::string_view subject) {
    natsSubscription* raw{};
    throw_on_error(natsConnection_SubscribeSync(&raw, conn_.get(), std::string(subject).c_str()),
                   "natsConnection_SubscribeSync");
    return subscription{raw};
  }

  [[nodiscard]] subscription subscribe_async_timeout(std::string_view subject, std::chrono::milliseconds timeout,
                                                     std::function<void(message)> handler) {
    std::string subj(subject);
    return register_async_subscription(
        std::move(handler), "natsConnection_SubscribeTimeout",
        [this, subj, timeout](natsSubscription** raw, natsMsgHandler cb, void* closure) {
          return natsConnection_SubscribeTimeout(raw, conn_.get(), subj.c_str(),
                                                 static_cast<int64_t>(timeout.count()), cb, closure);
        });
  }

  [[nodiscard]] subscription subscribe_queue_sync(std::string_view subject, std::string_view queue) {
    natsSubscription* raw{};
    throw_on_error(
        natsConnection_QueueSubscribeSync(&raw, conn_.get(), std::string(subject).c_str(), std::string(queue).c_str()),
        "natsConnection_QueueSubscribeSync");
    return subscription{raw};
  }

  [[nodiscard]] subscription subscribe_queue_async_timeout(std::string_view subject, std::string_view queue,
                                                           std::chrono::milliseconds timeout,
                                                           std::function<void(message)> handler) {
    std::string subj(subject), q(queue);
    return register_async_subscription(
        std::move(handler), "natsConnection_QueueSubscribeTimeout",
        [this, subj, q, timeout](natsSubscription** raw, natsMsgHandler cb, void* closure) {
          return natsConnection_QueueSubscribeTimeout(raw, conn_.get(), subj.c_str(), q.c_str(),
                                                      static_cast<int64_t>(timeout.count()), cb, closure);
        });
  }

  [[nodiscard]] subscription subscribe_queue_async(std::string_view subject, std::string_view queue,
                                                   std::function<void(message)> handler) {
    std::string subj(subject), q(queue);
    return register_async_subscription(
        std::move(handler), "natsConnection_QueueSubscribe",
        [this, subj, q](natsSubscription** raw, natsMsgHandler cb, void* closure) {
          return natsConnection_QueueSubscribe(raw, conn_.get(), subj.c_str(), q.c_str(), cb, closure);
        });
  }

  [[nodiscard]] subscription subscribe_async(std::string_view subject, std::function<void(message)> handler) {
    std::string subj(subject);
    return register_async_subscription(
        std::move(handler), "natsConnection_Subscribe",
        [this, subj](natsSubscription** raw, natsMsgHandler cb, void* closure) {
          return natsConnection_Subscribe(raw, conn_.get(), subj.c_str(), cb, closure);
        });
  }

  [[nodiscard]] subscription subscribe(std::string_view subject, std::function<void(message)> handler) {
    return subscribe_async(subject, std::move(handler));
  }

  [[nodiscard]] subscription subscribe_queue(std::string_view subject, std::string_view queue,
                                             std::function<void(message)> handler) {
    return subscribe_queue_async(subject, queue, std::move(handler));
  }

  [[nodiscard]] message request_sync(std::string_view subject, std::string_view payload,
                                     std::chrono::milliseconds timeout = std::chrono::seconds(2)) {
    assert(payload.size() <= static_cast<std::size_t>(std::numeric_limits<int>::max()) &&
           "request_sync: payload exceeds INT_MAX bytes");
    natsMsg* reply{};
    throw_on_error(natsConnection_Request(&reply, conn_.get(), std::string(subject).c_str(), payload.data(),
                                          static_cast<int>(payload.size()), static_cast<int64_t>(timeout.count())),
                   "natsConnection_Request");
    return message{reply};
  }

  [[nodiscard]] message request_string(std::string_view subject, std::string_view payload,
                                       std::chrono::milliseconds timeout = std::chrono::seconds(2)) {
    natsMsg* reply{};
    throw_on_error(natsConnection_RequestString(&reply, conn_.get(), std::string(subject).c_str(),
                                                std::string(payload).c_str(), static_cast<int64_t>(timeout.count())),
                   "natsConnection_RequestString");
    return message{reply};
  }

  [[nodiscard]] message request(message request_message, std::chrono::milliseconds timeout = std::chrono::seconds(2)) {
    natsMsg* reply{};
    natsMsg* raw = request_message.release();
    natsStatus status = natsConnection_RequestMsg(&reply, conn_.get(), raw, static_cast<int64_t>(timeout.count()));
    natsMsg_Destroy(raw);  // natsConnection_RequestMsg never takes ownership
    throw_on_error(status, "natsConnection_RequestMsg");
    return message{reply};
  }

  [[nodiscard]] message request(std::string_view subject, std::string_view payload,
                                std::chrono::milliseconds timeout = std::chrono::seconds(2)) {
    return request_sync(subject, payload, timeout);
  }

  [[nodiscard]] std::future<message> request_async(
      std::string subject, std::string payload, std::chrono::milliseconds timeout = std::chrono::seconds(2)) {
    assert(payload.size() <= static_cast<std::size_t>(std::numeric_limits<int>::max()) &&
           "request_async: payload exceeds INT_MAX bytes");
    auto conn_ref = conn_;  // extend connection lifetime across the async call
    return std::async(std::launch::async,
                      [conn_ref, subject = std::move(subject), payload = std::move(payload), timeout]() {
                        natsMsg* reply{};
                        throw_on_error(
                            natsConnection_Request(&reply, conn_ref.get(), subject.c_str(), payload.data(),
                                                   static_cast<int>(payload.size()),
                                                   static_cast<int64_t>(timeout.count())),
                            "natsConnection_Request");
                        return message{reply};
                      });
  }

  [[nodiscard]] future_awaitable<message> request_awaitable(
      std::string subject, std::string payload, std::chrono::milliseconds timeout = std::chrono::seconds(2)) {
    return future_awaitable<message>{request_async(std::move(subject), std::move(payload), timeout)};
  }

  /**
   * @brief Generates a NATS inbox subject.
   */
  [[nodiscard]] std::string new_inbox() const {
    natsInbox* inbox = nullptr;
    throw_on_error(natsInbox_Create(&inbox), "natsInbox_Create");
    // RAII guard ensures inbox is freed even if std::string construction throws OOM.
    struct inbox_guard { natsInbox* p; ~inbox_guard() { natsInbox_Destroy(p); } } guard{inbox};
    // natsInbox is typedef char, so natsInbox* is char* — no cast needed.
    return std::string{inbox};
  }

 private:
  // Shared callback dispatched by all async subscriptions. Always null-guards msg
  // (natsConnection_SubscribeTimeout delivers a null msg to signal timeout expiry).
  static void async_msg_callback(natsConnection*, natsSubscription*, natsMsg* msg, void* closure) {
    if (msg == nullptr) { return; }
    auto* fn = static_cast<std::function<void(message)>*>(closure);
    natsMsg* dup{};
    if (natsMsg_Create(&dup, natsMsg_GetSubject(msg), natsMsg_GetReply(msg), natsMsg_GetData(msg),
                       natsMsg_GetDataLength(msg)) != NATS_OK) {
      std::fprintf(stderr, "[natscpp] natsMsg_Create failed: dropping message on subject '%s'\n",
                   natsMsg_GetSubject(msg));
      return;
    }
    // Copy all headers so JetStream metadata and user headers are available in the callback.
    const char** keys = nullptr;
    int key_count = 0;
    if (natsMsgHeader_Keys(msg, &keys, &key_count) == NATS_OK && keys != nullptr) {
      for (int i = 0; i < key_count; ++i) {
        const char** vals = nullptr;
        int val_count = 0;
        if (natsMsgHeader_Values(msg, keys[i], &vals, &val_count) == NATS_OK && vals != nullptr) {
          for (int j = 0; j < val_count; ++j) {
            if (vals[j] != nullptr) {
              natsMsgHeader_Add(dup, keys[i], vals[j]);
            }
          }
          std::free(const_cast<char**>(vals));
        }
      }
      std::free(const_cast<char**>(keys));
    }
    try {
      (*fn)(message{dup});
    } catch (...) {
      std::fprintf(stderr, "[natscpp] exception in message callback, ignoring\n");
    }
  }

  // Common registration logic shared by all four async subscribe variants.
  // do_subscribe(natsSubscription**, natsMsgHandler, void*) → natsStatus
  template <typename SubscribeFn>
  [[nodiscard]] subscription register_async_subscription(std::function<void(message)> handler,
                                                         const char* context_name,
                                                         SubscribeFn do_subscribe) {
    auto token = std::make_shared<std::function<void(message)>>(std::move(handler));
    auto state = callback_state_;

    {
      std::lock_guard<std::mutex> lock(state->mutex);
      state->callbacks[token.get()] = token;
    }

    natsSubscription* raw{};
    natsStatus status = do_subscribe(&raw, &async_msg_callback, token.get());

    if (status != NATS_OK) {
      std::lock_guard<std::mutex> lock(state->mutex);
      state->callbacks.erase(token.get());
      throw_on_error(status, context_name);
    }

    return subscription{raw, [state = std::weak_ptr<callback_state>(state), key = token.get()] {
      if (const auto locked = state.lock()) {
        std::lock_guard<std::mutex> lock(locked->mutex);
        locked->callbacks.erase(key);
      }
    }};
  }

  [[nodiscard]] std::vector<std::string> fetch_server_list(
      natsStatus (*getter)(natsConnection*, char***, int*)) const {
    char** values = nullptr;
    int count = 0;
    throw_on_error(getter(conn_.get(), &values, &count), "natsConnection_GetServers*");
    // RAII guard: free all strings and the array even if emplace_back throws.
    struct list_guard {
      char** arr; int n;
      ~list_guard() { for (int i = 0; i < n; ++i) std::free(arr[i]); std::free(arr); }
    } guard{values, count};

    std::vector<std::string> out;
    out.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
      out.emplace_back(values[i] != nullptr ? values[i] : "");
    }
    return out;
  }

  struct callback_state {
    std::unordered_map<void*, std::shared_ptr<std::function<void(message)>>> callbacks;
    std::mutex mutex;
  };

  struct callback_handlers {
    std::function<void(natsConnection*)> closed_cb;
    std::function<void(natsConnection*)> disconnected_cb;
    std::function<void(natsConnection*)> reconnected_cb;
    std::function<void(natsConnection*, natsSubscription*, natsStatus)> error_handler;
    std::function<void(natsConnection*)> lame_duck_mode_cb;
    std::function<void(natsConnection*)> discovered_servers_cb;
  };

  static void closed_cb_bridge(natsConnection* nc, void* closure) {
    auto* handlers = static_cast<callback_handlers*>(closure);
    if (handlers->closed_cb) {
      try { handlers->closed_cb(nc); } catch (...) {
        std::fprintf(stderr, "[natscpp] exception in closed callback, ignoring\n");
      }
    }
  }

  static void disconnected_cb_bridge(natsConnection* nc, void* closure) {
    auto* handlers = static_cast<callback_handlers*>(closure);
    if (handlers->disconnected_cb) {
      try { handlers->disconnected_cb(nc); } catch (...) {
        std::fprintf(stderr, "[natscpp] exception in disconnected callback, ignoring\n");
      }
    }
  }

  static void reconnected_cb_bridge(natsConnection* nc, void* closure) {
    auto* handlers = static_cast<callback_handlers*>(closure);
    if (handlers->reconnected_cb) {
      try { handlers->reconnected_cb(nc); } catch (...) {
        std::fprintf(stderr, "[natscpp] exception in reconnected callback, ignoring\n");
      }
    }
  }

  static void error_handler_bridge(natsConnection* nc, natsSubscription* sub, natsStatus err, void* closure) {
    auto* handlers = static_cast<callback_handlers*>(closure);
    if (handlers->error_handler) {
      try { handlers->error_handler(nc, sub, err); } catch (...) {
        std::fprintf(stderr, "[natscpp] exception in error handler callback, ignoring\n");
      }
    }
  }

  static void lame_duck_mode_cb_bridge(natsConnection* nc, void* closure) {
    auto* handlers = static_cast<callback_handlers*>(closure);
    if (handlers->lame_duck_mode_cb) {
      try { handlers->lame_duck_mode_cb(nc); } catch (...) {
        std::fprintf(stderr, "[natscpp] exception in lame duck mode callback, ignoring\n");
      }
    }
  }

  static void discovered_servers_cb_bridge(natsConnection* nc, void* closure) {
    auto* handlers = static_cast<callback_handlers*>(closure);
    if (handlers->discovered_servers_cb) {
      try { handlers->discovered_servers_cb(nc); } catch (...) {
        std::fprintf(stderr, "[natscpp] exception in discovered servers callback, ignoring\n");
      }
    }
  }

  static void apply_nats_options(natsOptions* nopts, const connection_options& options, callback_handlers* handlers) {
    // URL setup: multi-server list takes precedence over single url.
    if (!options.urls.empty()) {
      std::vector<const char*> server_ptrs;
      server_ptrs.reserve(options.urls.size());
      for (const auto& u : options.urls) server_ptrs.push_back(u.c_str());
      throw_on_error(::natsOptions_SetServers(nopts, server_ptrs.data(), static_cast<int>(server_ptrs.size())),
                     "natsOptions_SetServers");
    } else {
      throw_on_error(::natsOptions_SetURL(nopts, options.url.c_str()), "natsOptions_SetURL");
    }

    // Authentication
    if (options.token) {
      throw_on_error(::natsOptions_SetToken(nopts, options.token->c_str()), "natsOptions_SetToken");
    }
    if (options.token_handler_cb) {
      throw_on_error(::natsOptions_SetTokenHandler(nopts, options.token_handler_cb, options.token_handler_closure),
                     "natsOptions_SetTokenHandler");
    }
    if (options.nkey_public) {
      throw_on_error(::natsOptions_SetNKey(nopts, options.nkey_public->c_str(), options.nkey_signature_cb,
                                           options.nkey_signature_closure),
                     "natsOptions_SetNKey");
    }
    if (options.user_credentials_file) {
      const char* seed_file = options.user_nkey_seed_file ? options.user_nkey_seed_file->c_str() : nullptr;
      throw_on_error(::natsOptions_SetUserCredentialsFromFiles(nopts, options.user_credentials_file->c_str(), seed_file),
                     "natsOptions_SetUserCredentialsFromFiles");
    }
    if (options.user && options.password) {
      throw_on_error(::natsOptions_SetUserInfo(nopts, options.user->c_str(), options.password->c_str()),
                     "natsOptions_SetUserInfo");
    }

    // TLS
    if (options.secure) {
      throw_on_error(::natsOptions_SetSecure(nopts, *options.secure), "natsOptions_SetSecure");
    }
    if (options.ca_trusted_certificates_file) {
      throw_on_error(::natsOptions_LoadCATrustedCertificates(nopts, options.ca_trusted_certificates_file->c_str()),
                     "natsOptions_LoadCATrustedCertificates");
    }
    if (options.ca_certs_path) {
#ifdef NATS_HAS_TLS
      throw_on_error(::natsOptions_LoadCATrustedCertificatesPath(nopts, options.ca_certs_path->c_str()),
                     "natsOptions_LoadCATrustedCertificatesPath");
#else
      throw nats_error(NATS_ILLEGAL_STATE, "natsOptions_LoadCATrustedCertificatesPath: TLS not compiled in");
#endif
    }
    if (options.certificates_chain_file && options.private_key_file) {
      throw_on_error(::natsOptions_LoadCertificatesChain(nopts, options.certificates_chain_file->c_str(),
                                                         options.private_key_file->c_str()),
                     "natsOptions_LoadCertificatesChain");
    }
    if (options.skip_server_verification) {
      throw_on_error(::natsOptions_SkipServerVerification(nopts, *options.skip_server_verification),
                     "natsOptions_SkipServerVerification");
    }
    if (options.expected_hostname) {
      throw_on_error(::natsOptions_SetExpectedHostname(nopts, options.expected_hostname->c_str()),
                     "natsOptions_SetExpectedHostname");
    }
    if (options.ciphers) {
      throw_on_error(::natsOptions_SetCiphers(nopts, options.ciphers->c_str()), "natsOptions_SetCiphers");
    }
    if (options.cipher_suites) {
#ifdef NATS_HAS_TLS
      throw_on_error(::natsOptions_SetCipherSuites(nopts, options.cipher_suites->c_str()),
                     "natsOptions_SetCipherSuites");
#else
      throw nats_error(NATS_ILLEGAL_STATE, "natsOptions_SetCipherSuites: TLS not compiled in");
#endif
    }

    // Callbacks
    if (options.closed_cb) {
      throw_on_error(::natsOptions_SetClosedCB(nopts, &closed_cb_bridge, handlers), "natsOptions_SetClosedCB");
    }
    if (options.disconnected_cb) {
      throw_on_error(::natsOptions_SetDisconnectedCB(nopts, &disconnected_cb_bridge, handlers),
                     "natsOptions_SetDisconnectedCB");
    }
    if (options.reconnected_cb) {
      throw_on_error(::natsOptions_SetReconnectedCB(nopts, &reconnected_cb_bridge, handlers),
                     "natsOptions_SetReconnectedCB");
    }
    if (options.error_handler) {
      throw_on_error(::natsOptions_SetErrorHandler(nopts, &error_handler_bridge, handlers),
                     "natsOptions_SetErrorHandler");
    }
    if (options.lame_duck_mode_cb) {
      throw_on_error(::natsOptions_SetLameDuckModeCB(nopts, &lame_duck_mode_cb_bridge, handlers),
                     "natsOptions_SetLameDuckModeCB");
    }
    if (options.discovered_servers_cb) {
      throw_on_error(::natsOptions_SetDiscoveredServersCB(nopts, &discovered_servers_cb_bridge, handlers),
                     "natsOptions_SetDiscoveredServersCB");
    }

    // Reconnect behaviour
    if (options.reconnect_wait) {
      throw_on_error(::natsOptions_SetReconnectWait(nopts, options.reconnect_wait->count()),
                     "natsOptions_SetReconnectWait");
    }
    if (options.max_reconnect) {
      throw_on_error(::natsOptions_SetMaxReconnect(nopts, *options.max_reconnect), "natsOptions_SetMaxReconnect");
    }
    if (options.allow_reconnect) {
      throw_on_error(::natsOptions_SetAllowReconnect(nopts, *options.allow_reconnect),
                     "natsOptions_SetAllowReconnect");
    }
    if (options.no_randomize) {
      throw_on_error(::natsOptions_SetNoRandomize(nopts, *options.no_randomize), "natsOptions_SetNoRandomize");
    }
    if (options.max_pings_out) {
      throw_on_error(::natsOptions_SetMaxPingsOut(nopts, *options.max_pings_out), "natsOptions_SetMaxPingsOut");
    }
    if (options.reconnect_buf_size) {
      throw_on_error(::natsOptions_SetReconnectBufSize(nopts, *options.reconnect_buf_size),
                     "natsOptions_SetReconnectBufSize");
    }
    if (options.reconnect_jitter) {
      throw_on_error(::natsOptions_SetReconnectJitter(nopts, options.reconnect_jitter->first,
                                                      options.reconnect_jitter->second),
                     "natsOptions_SetReconnectJitter");
    }

    // Subscription back-pressure
    if (options.max_pending_msgs) {
      throw_on_error(::natsOptions_SetMaxPendingMsgs(nopts, *options.max_pending_msgs),
                     "natsOptions_SetMaxPendingMsgs");
    }
    if (options.max_pending_bytes) {
      throw_on_error(::natsOptions_SetMaxPendingBytes(nopts, *options.max_pending_bytes),
                     "natsOptions_SetMaxPendingBytes");
    }

    // I/O tuning
    if (options.io_buf_size) {
      throw_on_error(::natsOptions_SetIOBufSize(nopts, *options.io_buf_size), "natsOptions_SetIOBufSize");
    }
    if (options.send_asap) {
      throw_on_error(::natsOptions_SetSendAsap(nopts, *options.send_asap), "natsOptions_SetSendAsap");
    }

    // Cluster / server discovery
    if (options.fail_requests_on_disconnect) {
      throw_on_error(::natsOptions_SetFailRequestsOnDisconnect(nopts, *options.fail_requests_on_disconnect),
                     "natsOptions_SetFailRequestsOnDisconnect");
    }
    if (options.ignore_discovered_servers) {
      throw_on_error(::natsOptions_SetIgnoreDiscoveredServers(nopts, *options.ignore_discovered_servers),
                     "natsOptions_SetIgnoreDiscoveredServers");
    }

    // Protocol tweaks
    if (options.disable_no_responders) {
      throw_on_error(::natsOptions_DisableNoResponders(nopts, *options.disable_no_responders),
                     "natsOptions_DisableNoResponders");
    }
    if (options.use_old_request_style) {
      throw_on_error(::natsOptions_UseOldRequestStyle(nopts, *options.use_old_request_style),
                     "natsOptions_UseOldRequestStyle");
    }
    if (options.custom_inbox_prefix) {
      throw_on_error(::natsOptions_SetCustomInboxPrefix(nopts, options.custom_inbox_prefix->c_str()),
                     "natsOptions_SetCustomInboxPrefix");
    }
    if (options.pedantic) {
      throw_on_error(::natsOptions_SetPedantic(nopts, *options.pedantic), "natsOptions_SetPedantic");
    }
    if (options.verbose) {
      throw_on_error(::natsOptions_SetVerbose(nopts, *options.verbose), "natsOptions_SetVerbose");
    }

    // Connection tuning
    if (options.ping_interval) {
      throw_on_error(::natsOptions_SetPingInterval(nopts, options.ping_interval->count()),
                     "natsOptions_SetPingInterval");
    }
    if (options.timeout) {
      throw_on_error(::natsOptions_SetTimeout(nopts, options.timeout->count()), "natsOptions_SetTimeout");
    }
    if (options.name) {
      throw_on_error(::natsOptions_SetName(nopts, options.name->c_str()), "natsOptions_SetName");
    }
    if (options.no_echo) {
      throw_on_error(::natsOptions_SetNoEcho(nopts, *options.no_echo), "natsOptions_SetNoEcho");
    }
  }

  std::shared_ptr<natsConnection> conn_;
  std::shared_ptr<callback_state> callback_state_ = std::make_shared<callback_state>();
  std::shared_ptr<callback_handlers> callback_handlers_;
};

}  // namespace natscpp
