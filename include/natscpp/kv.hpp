#pragma once

#include <nats/nats.h>

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <natscpp/connection.hpp>
#include <natscpp/error.hpp>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace natscpp {

class kv_not_available : public std::runtime_error {
 public:
  kv_not_available() : std::runtime_error("KeyValue symbols are not available in linked nats.c") {}
};

namespace detail {
inline void* resolve_kv_symbol(const char* name) {
#if defined(_WIN32)
  auto module = GetModuleHandleA(nullptr);
  FARPROC proc = GetProcAddress(module, name);
  void* result = nullptr;
  static_assert(sizeof(FARPROC) == sizeof(void*), "FARPROC and void* must be the same size");
  std::memcpy(&result, &proc, sizeof(result));
  return result;
#else
  return dlsym(RTLD_DEFAULT, name);
#endif
}

// These helpers are called from noexcept destructors/move-assigns, so they
// silently skip when the symbol is unavailable rather than throwing.
inline void destroy_kv_store(kvStore* kv) {
  using destroy_fn = void (*)(kvStore*);
  static auto* fn = reinterpret_cast<destroy_fn>(resolve_kv_symbol("kvStore_Destroy"));
  if (fn != nullptr && kv != nullptr) {
    fn(kv);
  }
}

inline void destroy_kv_entry(kvEntry* entry) {
  using destroy_fn = void (*)(kvEntry*);
  static auto* fn = reinterpret_cast<destroy_fn>(resolve_kv_symbol("kvEntry_Destroy"));
  if (fn != nullptr && entry != nullptr) {
    fn(entry);
  }
}

inline void destroy_kv_context(jsCtx* ctx) {
  using destroy_fn = void (*)(jsCtx*);
  static auto* fn = reinterpret_cast<destroy_fn>(resolve_kv_symbol("jsCtx_Destroy"));
  if (fn != nullptr && ctx != nullptr) {
    fn(ctx);
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
    using key_fn = const char* (*)(kvEntry*);
    static auto* fn = reinterpret_cast<key_fn>(detail::resolve_kv_symbol("kvEntry_Key"));
    if (fn == nullptr) {
      throw kv_not_available();
    }
    const char* value = fn(entry_);
    return value == nullptr ? std::string{} : std::string{value};
  }

  [[nodiscard]] std::string value() const {
    using value_fn = const void* (*)(kvEntry*);
    using len_fn = int (*)(kvEntry*);
    static auto* value_getter = reinterpret_cast<value_fn>(detail::resolve_kv_symbol("kvEntry_Value"));
    static auto* len_getter = reinterpret_cast<len_fn>(detail::resolve_kv_symbol("kvEntry_ValueLen"));
    if (value_getter == nullptr || len_getter == nullptr) {
      throw kv_not_available();
    }

    const char* value = static_cast<const char*>(value_getter(entry_));
    int len = len_getter(entry_);
    // Only treat a null pointer as "no value"; len == 0 is a valid empty string.
    if (value == nullptr || len < 0) {
      return {};
    }
    return {value, static_cast<std::size_t>(len)};
  }

  [[nodiscard]] uint64_t revision() const {
    using rev_fn = uint64_t (*)(kvEntry*);
    static auto* fn = reinterpret_cast<rev_fn>(detail::resolve_kv_symbol("kvEntry_Revision"));
    if (fn == nullptr) {
      throw kv_not_available();
    }
    return fn(entry_);
  }

  [[nodiscard]] std::string bucket() const {
    using fn_t = const char* (*)(kvEntry*);
    static auto* fn = reinterpret_cast<fn_t>(detail::resolve_kv_symbol("kvEntry_Bucket"));
    if (fn == nullptr) {
      throw kv_not_available();
    }
    const char* value = fn(entry_);
    return value == nullptr ? std::string{} : std::string{value};
  }

  [[nodiscard]] int64_t created() const {
    using fn_t = int64_t (*)(kvEntry*);
    static auto* fn = reinterpret_cast<fn_t>(detail::resolve_kv_symbol("kvEntry_Created"));
    if (fn == nullptr) {
      throw kv_not_available();
    }
    return fn(entry_);
  }

  [[nodiscard]] uint64_t delta() const {
    using fn_t = uint64_t (*)(kvEntry*);
    static auto* fn = reinterpret_cast<fn_t>(detail::resolve_kv_symbol("kvEntry_Delta"));
    if (fn == nullptr) {
      throw kv_not_available();
    }
    return fn(entry_);
  }

  [[nodiscard]] kvOperation operation() const {
    using fn_t = kvOperation (*)(kvEntry*);
    static auto* fn = reinterpret_cast<fn_t>(detail::resolve_kv_symbol("kvEntry_Operation"));
    if (fn == nullptr) {
      throw kv_not_available();
    }
    return fn(entry_);
  }

 private:
  kvEntry* entry_{};
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

  static key_value create(const connection& conn, std::string_view bucket) {
    using js_create_fn = natsStatus (*)(jsCtx**, natsConnection*, jsOptions*);
    using kv_create_fn = natsStatus (*)(kvStore**, jsCtx*, kvConfig*);

    static auto* create_js = reinterpret_cast<js_create_fn>(detail::resolve_kv_symbol("natsConnection_JetStream"));
    static auto* kv_cfg_init = reinterpret_cast<natsStatus (*)(kvConfig*)>(detail::resolve_kv_symbol("kvConfig_Init"));
    static auto* create_kv = reinterpret_cast<kv_create_fn>(detail::resolve_kv_symbol("js_CreateKeyValue"));
    if (create_js == nullptr || kv_cfg_init == nullptr || create_kv == nullptr) {
      throw kv_not_available();
    }

    key_value out;
    throw_on_error(create_js(&out.ctx_, conn.native_handle(), nullptr), "natsConnection_JetStream");
    kvConfig cfg{};
    throw_on_error(kv_cfg_init(&cfg), "kvConfig_Init");
    std::string bucket_name(bucket);
    cfg.Bucket = bucket_name.c_str();

    try {
      throw_on_error(create_kv(&out.kv_, out.ctx_, &cfg), "js_CreateKeyValue");
    } catch (...) {
      out.destroy_self();
      throw;
    }
    return out;
  }

  static void delete_bucket(const connection& conn, std::string_view bucket) {
    using js_create_fn = natsStatus (*)(jsCtx**, natsConnection*, jsOptions*);
    using delete_fn = natsStatus (*)(jsCtx*, const char*);
    static auto* create_js = reinterpret_cast<js_create_fn>(detail::resolve_kv_symbol("natsConnection_JetStream"));
    static auto* delete_kv = reinterpret_cast<delete_fn>(detail::resolve_kv_symbol("js_DeleteKeyValue"));
    if (create_js == nullptr || delete_kv == nullptr) {
      throw kv_not_available();
    }

    jsCtx* ctx = nullptr;
    throw_on_error(create_js(&ctx, conn.native_handle(), nullptr), "natsConnection_JetStream");
    try {
      throw_on_error(delete_kv(ctx, std::string(bucket).c_str()), "js_DeleteKeyValue");
    } catch (...) {
      detail::destroy_kv_context(ctx);
      throw;
    }
    detail::destroy_kv_context(ctx);
  }

  key_value(const connection& conn, std::string_view bucket) {
    using js_create_fn = natsStatus (*)(jsCtx**, natsConnection*, jsOptions*);
    using kv_open_fn = natsStatus (*)(kvStore**, jsCtx*, const char*);

    static auto* create_js = reinterpret_cast<js_create_fn>(detail::resolve_kv_symbol("natsConnection_JetStream"));
    static auto* open_kv = reinterpret_cast<kv_open_fn>(detail::resolve_kv_symbol("js_KeyValue"));
    if (create_js == nullptr || open_kv == nullptr) {
      throw kv_not_available();
    }

    throw_on_error(create_js(&ctx_, conn.native_handle(), nullptr), "natsConnection_JetStream");
    try {
      throw_on_error(open_kv(&kv_, ctx_, std::string(bucket).c_str()), "js_KeyValue");
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

  [[nodiscard]] kv_entry get(std::string_view key) const {
    using get_fn = natsStatus (*)(kvEntry**, kvStore*, const char*);
    static auto* fn = reinterpret_cast<get_fn>(detail::resolve_kv_symbol("kvStore_Get"));
    if (fn == nullptr) {
      throw kv_not_available();
    }

    kvEntry* entry{};
    throw_on_error(fn(&entry, kv_, std::string(key).c_str()), "kvStore_Get");
    return kv_entry{entry};
  }

  [[nodiscard]] uint64_t put(std::string_view key, std::string_view value) {
    using put_fn = natsStatus (*)(uint64_t*, kvStore*, const char*, const void*, int);
    static auto* fn = reinterpret_cast<put_fn>(detail::resolve_kv_symbol("kvStore_Put"));
    if (fn == nullptr) {
      throw kv_not_available();
    }

    uint64_t rev{};
    throw_on_error(fn(&rev, kv_, std::string(key).c_str(), value.data(), static_cast<int>(value.size())),
                   "kvStore_Put");
    return rev;
  }

  [[nodiscard]] uint64_t create(std::string_view key, std::string_view value) {
    using fn_t = natsStatus (*)(uint64_t*, kvStore*, const char*, const void*, int);
    static auto* fn = reinterpret_cast<fn_t>(detail::resolve_kv_symbol("kvStore_Create"));
    if (fn == nullptr) {
      throw kv_not_available();
    }

    uint64_t rev{};
    throw_on_error(fn(&rev, kv_, std::string(key).c_str(), value.data(), static_cast<int>(value.size())), "kvStore_Create");
    return rev;
  }

  [[nodiscard]] uint64_t update(std::string_view key, std::string_view value, uint64_t last_revision) {
    using fn_t = natsStatus (*)(uint64_t*, kvStore*, const char*, const void*, int, uint64_t);
    static auto* fn = reinterpret_cast<fn_t>(detail::resolve_kv_symbol("kvStore_Update"));
    if (fn == nullptr) {
      throw kv_not_available();
    }

    uint64_t rev{};
    throw_on_error(fn(&rev, kv_, std::string(key).c_str(), value.data(), static_cast<int>(value.size()), last_revision),
                   "kvStore_Update");
    return rev;
  }

  [[nodiscard]] kv_entry get_revision(std::string_view key, uint64_t revision) const {
    using fn_t = natsStatus (*)(kvEntry**, kvStore*, const char*, uint64_t);
    static auto* fn = reinterpret_cast<fn_t>(detail::resolve_kv_symbol("kvStore_GetRevision"));
    if (fn == nullptr) {
      throw kv_not_available();
    }

    kvEntry* entry{};
    throw_on_error(fn(&entry, kv_, std::string(key).c_str(), revision), "kvStore_GetRevision");
    return kv_entry{entry};
  }

  void erase(std::string_view key) {
    using delete_fn = natsStatus (*)(kvStore*, const char*);
    static auto* fn = reinterpret_cast<delete_fn>(detail::resolve_kv_symbol("kvStore_Delete"));
    if (fn == nullptr) {
      throw kv_not_available();
    }

    throw_on_error(fn(kv_, std::string(key).c_str()), "kvStore_Delete");
  }

  void purge(std::string_view key) {
    using fn_t = natsStatus (*)(kvStore*, const char*, kvPurgeOptions*);
    static auto* fn = reinterpret_cast<fn_t>(detail::resolve_kv_symbol("kvStore_Purge"));
    if (fn == nullptr) {
      throw kv_not_available();
    }

    throw_on_error(fn(kv_, std::string(key).c_str(), nullptr), "kvStore_Purge");
  }

  [[nodiscard]] std::vector<std::string> keys() const {
    using fn_t = natsStatus (*)(kvKeysList*, kvStore*, kvWatchOptions*);
    static auto* fn = reinterpret_cast<fn_t>(detail::resolve_kv_symbol("kvStore_Keys"));
    static auto* list_destroy = reinterpret_cast<void (*)(kvKeysList*)>(detail::resolve_kv_symbol("kvKeysList_Destroy"));
    if (fn == nullptr || list_destroy == nullptr) {
      throw kv_not_available();
    }

    kvKeysList list{};
    throw_on_error(fn(&list, kv_, nullptr), "kvStore_Keys");
    std::vector<std::string> out;
    out.reserve(static_cast<std::size_t>(list.Count));
    for (int i = 0; i < list.Count; ++i) {
      out.emplace_back(list.Keys[i] != nullptr ? list.Keys[i] : "");
    }
    list_destroy(&list);
    return out;
  }

  [[nodiscard]] std::string bucket() const {
    using fn_t = const char* (*)(kvStore*);
    static auto* fn = reinterpret_cast<fn_t>(detail::resolve_kv_symbol("kvStore_Bucket"));
    if (fn == nullptr) {
      throw kv_not_available();
    }
    const char* value = fn(kv_);
    return value == nullptr ? std::string{} : std::string{value};
  }

  [[nodiscard]] bucket_status status() const {
    using status_fn = natsStatus (*)(kvStatus**, kvStore*);
    static auto* get_status = reinterpret_cast<status_fn>(detail::resolve_kv_symbol("kvStore_Status"));
    static auto* destroy_status = reinterpret_cast<void (*)(kvStatus*)>(detail::resolve_kv_symbol("kvStatus_Destroy"));
    static auto* status_bucket = reinterpret_cast<const char* (*)(kvStatus*)>(detail::resolve_kv_symbol("kvStatus_Bucket"));
    static auto* status_values = reinterpret_cast<uint64_t (*)(kvStatus*)>(detail::resolve_kv_symbol("kvStatus_Values"));
    static auto* status_history = reinterpret_cast<uint64_t (*)(kvStatus*)>(detail::resolve_kv_symbol("kvStatus_History"));
    static auto* status_bytes = reinterpret_cast<uint64_t (*)(kvStatus*)>(detail::resolve_kv_symbol("kvStatus_Bytes"));
    static auto* status_ttl = reinterpret_cast<int64_t (*)(kvStatus*)>(detail::resolve_kv_symbol("kvStatus_TTL"));
    static auto* status_replicas = reinterpret_cast<int (*)(kvStatus*)>(detail::resolve_kv_symbol("kvStatus_Replicas"));
    if (get_status == nullptr || destroy_status == nullptr || status_bucket == nullptr || status_values == nullptr ||
        status_history == nullptr || status_bytes == nullptr || status_ttl == nullptr || status_replicas == nullptr) {
      throw kv_not_available();
    }

    kvStatus* raw = nullptr;
    throw_on_error(get_status(&raw, kv_), "kvStore_Status");
    bucket_status out;
    const char* b = status_bucket(raw);
    out.bucket = b != nullptr ? b : "";
    out.values = status_values(raw);
    out.history = status_history(raw);
    out.bytes = status_bytes(raw);
    out.ttl = status_ttl(raw);
    out.replicas = status_replicas(raw);
    destroy_status(raw);
    return out;
  }

 private:
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
