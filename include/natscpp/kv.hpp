#pragma once

#include <nats/nats.h>

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>

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

inline void destroy_kv_store(kvStore* kv) {
  using destroy_fn = void (*)(kvStore*);
  if (auto* fn = reinterpret_cast<destroy_fn>(resolve_kv_symbol("kvStore_Destroy")); fn != nullptr && kv != nullptr) {
    fn(kv);
  }
}

inline void destroy_kv_entry(kvEntry* entry) {
  using destroy_fn = void (*)(kvEntry*);
  if (auto* fn = reinterpret_cast<destroy_fn>(resolve_kv_symbol("kvEntry_Destroy")); fn != nullptr && entry != nullptr) {
    fn(entry);
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
    auto* fn = reinterpret_cast<key_fn>(detail::resolve_kv_symbol("kvEntry_Key"));
    if (fn == nullptr) {
      throw kv_not_available();
    }
    const char* value = fn(entry_);
    return value == nullptr ? std::string{} : std::string{value};
  }

  [[nodiscard]] std::string value() const {
    using value_fn = const void* (*)(kvEntry*);
    using len_fn = int (*)(kvEntry*);
    auto* value_getter = reinterpret_cast<value_fn>(detail::resolve_kv_symbol("kvEntry_Value"));
    auto* len_getter = reinterpret_cast<len_fn>(detail::resolve_kv_symbol("kvEntry_ValueLen"));
    if (value_getter == nullptr || len_getter == nullptr) {
      throw kv_not_available();
    }

    const char* value = static_cast<const char*>(value_getter(entry_));
    int len = len_getter(entry_);
    if (value == nullptr || len <= 0) {
      return {};
    }
    return {value, static_cast<std::size_t>(len)};
  }

  [[nodiscard]] uint64_t revision() const {
    using rev_fn = uint64_t (*)(kvEntry*);
    auto* fn = reinterpret_cast<rev_fn>(detail::resolve_kv_symbol("kvEntry_Revision"));
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
  key_value() = default;

  key_value(connection& conn, std::string_view bucket) {
    using js_create_fn = natsStatus (*)(jsCtx**, natsConnection*, jsOptions*);
    using kv_open_fn = natsStatus (*)(kvStore**, jsCtx*, const char*);

    auto* create_js = reinterpret_cast<js_create_fn>(detail::resolve_kv_symbol("natsConnection_JetStream"));
    auto* open_kv = reinterpret_cast<kv_open_fn>(detail::resolve_kv_symbol("js_KeyValue"));
    if (create_js == nullptr || open_kv == nullptr) {
      throw kv_not_available();
    }

    throw_on_error(create_js(&ctx_, conn.native_handle(), nullptr), "natsConnection_JetStream");
    throw_on_error(open_kv(&kv_, ctx_, std::string(bucket).c_str()), "js_KeyValue");
  }

  ~key_value() {
    detail::destroy_kv_store(kv_);
    using destroy_ctx_fn = void (*)(jsCtx*);
    if (auto* fn = reinterpret_cast<destroy_ctx_fn>(detail::resolve_kv_symbol("jsCtx_Destroy")); fn != nullptr && ctx_ != nullptr) {
      fn(ctx_);
    }
  }

  key_value(const key_value&) = delete;
  key_value& operator=(const key_value&) = delete;

  key_value(key_value&& other) noexcept : ctx_(other.ctx_), kv_(other.kv_) {
    other.ctx_ = nullptr;
    other.kv_ = nullptr;
  }
  key_value& operator=(key_value&& other) noexcept {
    if (this != &other) {
      detail::destroy_kv_store(kv_);
      using destroy_ctx_fn = void (*)(jsCtx*);
      if (auto* fn = reinterpret_cast<destroy_ctx_fn>(detail::resolve_kv_symbol("jsCtx_Destroy")); fn != nullptr && ctx_ != nullptr) {
        fn(ctx_);
      }

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
    auto* fn = reinterpret_cast<get_fn>(detail::resolve_kv_symbol("kvStore_Get"));
    if (fn == nullptr) {
      throw kv_not_available();
    }

    kvEntry* entry{};
    throw_on_error(fn(&entry, kv_, std::string(key).c_str()), "kvStore_Get");
    return kv_entry{entry};
  }

  [[nodiscard]] uint64_t put(std::string_view key, std::string_view value) {
    using put_fn = natsStatus (*)(uint64_t*, kvStore*, const char*, const void*, int);
    auto* fn = reinterpret_cast<put_fn>(detail::resolve_kv_symbol("kvStore_Put"));
    if (fn == nullptr) {
      throw kv_not_available();
    }

    uint64_t rev{};
    throw_on_error(fn(&rev, kv_, std::string(key).c_str(), value.data(), static_cast<int>(value.size())), "kvStore_Put");
    return rev;
  }

  void erase(std::string_view key) {
    using delete_fn = natsStatus (*)(kvStore*, const char*);
    auto* fn = reinterpret_cast<delete_fn>(detail::resolve_kv_symbol("kvStore_Delete"));
    if (fn == nullptr) {
      throw kv_not_available();
    }

    throw_on_error(fn(kv_, std::string(key).c_str()), "kvStore_Delete");
  }

 private:
  jsCtx* ctx_{};
  kvStore* kv_{};
};

}  // namespace natscpp
