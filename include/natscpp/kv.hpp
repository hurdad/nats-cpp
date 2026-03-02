#pragma once

#include <nats/nats.h>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <natscpp/connection.hpp>
#include <natscpp/error.hpp>

namespace natscpp {

class kv_not_available : public std::runtime_error {
 public:
  kv_not_available() : std::runtime_error("KeyValue symbols are not available in linked nats.c") {}
};

namespace detail {
inline void destroy_kv_store(kvStore* kv) {
  if (kv != nullptr) {
    kvStore_Destroy(kv);
  }
}

inline void destroy_kv_entry(kvEntry* entry) {
  if (entry != nullptr) {
    kvEntry_Destroy(entry);
  }
}

inline void destroy_kv_context(jsCtx* ctx) {
  if (ctx != nullptr) {
    jsCtx_Destroy(ctx);
  }
}

inline void destroy_kv_watcher(kvWatcher* watcher) {
  if (watcher != nullptr) {
    kvWatcher_Destroy(watcher);
  }
}
}  // namespace detail

class kv_entry {
 public:
  kv_entry() = default;
  explicit kv_entry(kvEntry* entry) : entry_(entry) {}
  ~kv_entry() { detail::destroy_kv_entry(entry_); }

  kv_entry(const kv_entry&) = delete;
  kv_entry& operator=(const kv_entry&) = delete;

  kv_entry(kv_entry&& other) noexcept : entry_(other.entry_) { other.entry_ = nullptr; }
  kv_entry& operator=(kv_entry&& other) noexcept {
    if (this != &other) {
      detail::destroy_kv_entry(entry_);
      entry_ = other.entry_;
      other.entry_ = nullptr;
    }
    return *this;
  }

  [[nodiscard]] bool valid() const noexcept { return entry_ != nullptr; }

  [[nodiscard]] std::string key() const {
    if (!entry_) return {};
    const char* value = kvEntry_Key(entry_);
    return value == nullptr ? std::string{} : std::string{value};
  }

  [[nodiscard]] std::string value() const {
    if (!entry_) return {};
    const char* value = static_cast<const char*>(kvEntry_Value(entry_));
    int len = kvEntry_ValueLen(entry_);
    // Only treat a null pointer as "no value"; len == 0 is a valid empty string.
    if (value == nullptr || len < 0) {
      return {};
    }
    return {value, static_cast<std::size_t>(len)};
  }

  [[nodiscard]] uint64_t revision() const {
    if (!entry_) return 0;
    return kvEntry_Revision(entry_);
  }

  [[nodiscard]] std::string bucket() const {
    if (!entry_) return {};
    const char* value = kvEntry_Bucket(entry_);
    return value == nullptr ? std::string{} : std::string{value};
  }

  [[nodiscard]] int64_t created() const {
    if (!entry_) return 0;
    return kvEntry_Created(entry_);
  }

  [[nodiscard]] uint64_t delta() const {
    if (!entry_) return 0;
    return kvEntry_Delta(entry_);
  }

  [[nodiscard]] kvOperation operation() const {
    if (!entry_) return kvOp_Put;
    return kvEntry_Operation(entry_);
  }

 private:
  kvEntry* entry_{};
};

struct kv_watch_options {
  bool ignore_deletes = false;
  bool include_history = false;
  bool meta_only = false;
  int64_t timeout = 0;
  bool updates_only = false;
};

class kv_watcher {
 public:
  kv_watcher() = default;
  explicit kv_watcher(kvWatcher* watcher) : watcher_(watcher) {}
  ~kv_watcher() { detail::destroy_kv_watcher(watcher_); }

  kv_watcher(const kv_watcher&) = delete;
  kv_watcher& operator=(const kv_watcher&) = delete;

  kv_watcher(kv_watcher&& other) noexcept : watcher_(other.watcher_) { other.watcher_ = nullptr; }
  kv_watcher& operator=(kv_watcher&& other) noexcept {
    if (this != &other) {
      detail::destroy_kv_watcher(watcher_);
      watcher_ = other.watcher_;
      other.watcher_ = nullptr;
    }
    return *this;
  }

  [[nodiscard]] bool valid() const noexcept { return watcher_ != nullptr; }

  [[nodiscard]] kv_entry next(int64_t timeout_ms = -1) const {
    kvEntry* entry = nullptr;
    throw_on_error(kvWatcher_Next(&entry, watcher_, timeout_ms), "kvWatcher_Next");
    return kv_entry{entry};
  }

  void stop() const {
    throw_on_error(kvWatcher_Stop(watcher_), "kvWatcher_Stop");
  }

 private:
  kvWatcher* watcher_{};
};

class key_value {
 public:
  struct bucket_status {
    std::string bucket;
    uint64_t values = 0;
    uint64_t history = 0;
    uint64_t bytes = 0;
    int64_t ttl = 0;
    int replicas = 0;
  };

  key_value() = default;

  static key_value create(const connection& conn, std::string_view bucket, int64_t history = 1) {
    key_value out;
    throw_on_error(natsConnection_JetStream(&out.ctx_, conn.native_handle(), nullptr), "natsConnection_JetStream");
    kvConfig cfg{};
    throw_on_error(kvConfig_Init(&cfg), "kvConfig_Init");
    std::string bucket_name(bucket);
    cfg.Bucket = bucket_name.c_str();
    cfg.History = history;

    try {
      throw_on_error(js_CreateKeyValue(&out.kv_, out.ctx_, &cfg), "js_CreateKeyValue");
    } catch (...) {
      out.destroy_self();
      throw;
    }
    return out;
  }

  static void delete_bucket(const connection& conn, std::string_view bucket) {
    jsCtx* ctx = nullptr;
    throw_on_error(natsConnection_JetStream(&ctx, conn.native_handle(), nullptr), "natsConnection_JetStream");
    try {
      throw_on_error(js_DeleteKeyValue(ctx, std::string(bucket).c_str()), "js_DeleteKeyValue");
    } catch (...) {
      detail::destroy_kv_context(ctx);
      throw;
    }
    detail::destroy_kv_context(ctx);
  }

  key_value(const connection& conn, std::string_view bucket) {
    throw_on_error(natsConnection_JetStream(&ctx_, conn.native_handle(), nullptr), "natsConnection_JetStream");
    try {
      throw_on_error(js_KeyValue(&kv_, ctx_, std::string(bucket).c_str()), "js_KeyValue");
    } catch (...) {
      detail::destroy_kv_context(ctx_);
      ctx_ = nullptr;
      throw;
    }
  }

  ~key_value() {
    destroy_self();
  }

  key_value(const key_value&) = delete;
  key_value& operator=(const key_value&) = delete;

  key_value(key_value&& other) noexcept : ctx_(other.ctx_), kv_(other.kv_) {
    other.ctx_ = nullptr;
    other.kv_ = nullptr;
  }
  key_value& operator=(key_value&& other) noexcept {
    if (this != &other) {
      destroy_self();
      ctx_ = other.ctx_;
      kv_ = other.kv_;
      other.ctx_ = nullptr;
      other.kv_ = nullptr;
    }
    return *this;
  }

  [[nodiscard]] bool valid() const noexcept { return kv_ != nullptr; }

  [[nodiscard]] kv_watcher watch(std::string_view key, const kv_watch_options* options = nullptr) const {
    kvWatcher* watcher{};
    auto native = to_native_watch_options(options);
    throw_on_error(kvStore_Watch(&watcher, kv_, std::string(key).c_str(), native), "kvStore_Watch");
    return kv_watcher{watcher};
  }

  [[nodiscard]] kv_watcher watch_multi(const std::vector<std::string>& keys,
                                       const kv_watch_options* options = nullptr) const {
    kvWatcher* watcher{};
    std::vector<const char*> key_ptrs;
    key_ptrs.reserve(keys.size());
    for (const auto& key : keys) {
      key_ptrs.push_back(key.c_str());
    }

    auto native = to_native_watch_options(options);
    throw_on_error(kvStore_WatchMulti(&watcher, kv_, key_ptrs.data(), static_cast<int>(key_ptrs.size()), native),
                   "kvStore_WatchMulti");
    return kv_watcher{watcher};
  }

  [[nodiscard]] kv_watcher watch_all(const kv_watch_options* options = nullptr) const {
    kvWatcher* watcher{};
    auto native = to_native_watch_options(options);
    throw_on_error(kvStore_WatchAll(&watcher, kv_, native), "kvStore_WatchAll");
    return kv_watcher{watcher};
  }

  [[nodiscard]] kv_entry get(std::string_view key) const {
    kvEntry* entry{};
    throw_on_error(kvStore_Get(&entry, kv_, std::string(key).c_str()), "kvStore_Get");
    return kv_entry{entry};
  }

  [[nodiscard]] uint64_t put(std::string_view key, std::string_view value) {
    uint64_t rev{};
    throw_on_error(kvStore_Put(&rev, kv_, std::string(key).c_str(), value.data(), static_cast<int>(value.size())),
                   "kvStore_Put");
    return rev;
  }

  [[nodiscard]] uint64_t create(std::string_view key, std::string_view value) {
    uint64_t rev{};
    throw_on_error(kvStore_Create(&rev, kv_, std::string(key).c_str(), value.data(), static_cast<int>(value.size())),
                   "kvStore_Create");
    return rev;
  }

  [[nodiscard]] uint64_t update(std::string_view key, std::string_view value, uint64_t last_revision) {
    uint64_t rev{};
    throw_on_error(
        kvStore_Update(&rev, kv_, std::string(key).c_str(), value.data(), static_cast<int>(value.size()), last_revision),
        "kvStore_Update");
    return rev;
  }

  [[nodiscard]] kv_entry get_revision(std::string_view key, uint64_t revision) const {
    kvEntry* entry{};
    throw_on_error(kvStore_GetRevision(&entry, kv_, std::string(key).c_str(), revision), "kvStore_GetRevision");
    return kv_entry{entry};
  }

  void erase(std::string_view key) {
    throw_on_error(kvStore_Delete(kv_, std::string(key).c_str()), "kvStore_Delete");
  }

  void purge(std::string_view key) {
    throw_on_error(kvStore_Purge(kv_, std::string(key).c_str(), nullptr), "kvStore_Purge");
  }

  [[nodiscard]] std::vector<std::string> keys() const {
    kvKeysList list{};
    throw_on_error(kvStore_Keys(&list, kv_, nullptr), "kvStore_Keys");
    std::vector<std::string> out;
    out.reserve(static_cast<std::size_t>(list.Count));
    for (int i = 0; i < list.Count; ++i) {
      out.emplace_back(list.Keys[i] != nullptr ? list.Keys[i] : "");
    }
    kvKeysList_Destroy(&list);
    return out;
  }

  [[nodiscard]] std::string bucket() const {
    const char* value = kvStore_Bucket(kv_);
    return value == nullptr ? std::string{} : std::string{value};
  }

  [[nodiscard]] bucket_status status() const {
    kvStatus* raw = nullptr;
    throw_on_error(kvStore_Status(&raw, kv_), "kvStore_Status");
    bucket_status out;
    const char* b = kvStatus_Bucket(raw);
    out.bucket = b != nullptr ? b : "";
    out.values = kvStatus_Values(raw);
    out.history = kvStatus_History(raw);
    out.bytes = kvStatus_Bytes(raw);
    out.ttl = kvStatus_TTL(raw);
    out.replicas = kvStatus_Replicas(raw);
    kvStatus_Destroy(raw);
    return out;
  }

 private:
  [[nodiscard]] static const kvWatchOptions* to_native_watch_options(const kv_watch_options* options) {
    if (options == nullptr) {
      return nullptr;
    }

    thread_local kvWatchOptions native{};
    throw_on_error(kvWatchOptions_Init(&native), "kvWatchOptions_Init");
    native.IgnoreDeletes = options->ignore_deletes;
    native.IncludeHistory = options->include_history;
    native.MetaOnly = options->meta_only;
    native.Timeout = options->timeout;
    native.UpdatesOnly = options->updates_only;
    return &native;
  }

  void destroy_self() noexcept {
    detail::destroy_kv_store(kv_);
    kv_ = nullptr;
    detail::destroy_kv_context(ctx_);
    ctx_ = nullptr;
  }

  jsCtx* ctx_{};
  kvStore* kv_{};
};

}  // namespace natscpp
