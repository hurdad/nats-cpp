#pragma once

#include <nats/nats.h>

#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <natscpp/detail/deleters.hpp>

namespace natscpp {

class header {
 public:
  header() = default;
  explicit header(natsHeader* raw) : header_(raw) {}

  static natsStatus create(header& new_header) {
    natsHeader* raw = nullptr;
    const natsStatus status = ::natsHeader_New(&raw);
    if (status == NATS_OK) {
      new_header.reset(raw);
    }
    return status;
  }

  natsStatus set(std::string_view key, std::string_view value) {
    return ::natsHeader_Set(native_handle(), std::string(key).c_str(), std::string(value).c_str());
  }

  natsStatus add(std::string_view key, std::string_view value) {
    return ::natsHeader_Add(native_handle(), std::string(key).c_str(), std::string(value).c_str());
  }

  natsStatus get(std::string_view key, const char** value) const {
    return ::natsHeader_Get(native_handle(), std::string(key).c_str(), value);
  }

  natsStatus values(std::string_view key, const char*** values, int* count) const {
    return ::natsHeader_Values(native_handle(), std::string(key).c_str(), values, count);
  }

  natsStatus keys(const char*** keys, int* count) const { return ::natsHeader_Keys(native_handle(), keys, count); }

  int keys_count() const { return ::natsHeader_KeysCount(native_handle()); }

  natsStatus erase(std::string_view key) { return ::natsHeader_Delete(native_handle(), std::string(key).c_str()); }

  void destroy() { reset(); }

  [[nodiscard]] natsHeader* native_handle() const noexcept { return header_.get(); }
  [[nodiscard]] natsHeader* release() noexcept { return header_.release(); }
  void reset(natsHeader* raw = nullptr) noexcept { header_.reset(raw); }
  [[nodiscard]] bool valid() const noexcept { return header_ != nullptr; }

 private:
  std::unique_ptr<natsHeader, detail::header_deleter> header_;
};

inline natsStatus natsHeader_New(header& new_header) {
  return header::create(new_header);
}

inline natsStatus natsHeader_Set(header& h, std::string_view key, std::string_view value) {
  return h.set(key, value);
}

inline natsStatus natsHeader_Add(header& h, std::string_view key, std::string_view value) {
  return h.add(key, value);
}

inline natsStatus natsHeader_Get(const header& h, std::string_view key, const char** value) {
  return h.get(key, value);
}

inline natsStatus natsHeader_Values(const header& h, std::string_view key, const char*** values, int* count) {
  return h.values(key, values, count);
}

inline natsStatus natsHeader_Keys(const header& h, const char*** keys, int* count) { return h.keys(keys, count); }

inline int natsHeader_KeysCount(const header& h) { return h.keys_count(); }

inline natsStatus natsHeader_Delete(header& h, std::string_view key) {
  return h.erase(key);
}

inline void natsHeader_Destroy(header& h) { h.destroy(); }

}  // namespace natscpp
