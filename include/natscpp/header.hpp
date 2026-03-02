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

  [[nodiscard]] natsHeader* native_handle() const noexcept { return header_.get(); }
  [[nodiscard]] natsHeader* release() noexcept { return header_.release(); }
  void reset(natsHeader* raw = nullptr) noexcept { header_.reset(raw); }
  [[nodiscard]] bool valid() const noexcept { return header_ != nullptr; }

 private:
  std::unique_ptr<natsHeader, detail::header_deleter> header_;
};

inline natsStatus natsHeader_New(header& new_header) {
  natsHeader* raw = nullptr;
  const natsStatus status = ::natsHeader_New(&raw);
  if (status == NATS_OK) {
    new_header.reset(raw);
  }
  return status;
}

inline natsStatus natsHeader_Set(header& h, std::string_view key, std::string_view value) {
  return ::natsHeader_Set(h.native_handle(), std::string(key).c_str(), std::string(value).c_str());
}

inline natsStatus natsHeader_Add(header& h, std::string_view key, std::string_view value) {
  return ::natsHeader_Add(h.native_handle(), std::string(key).c_str(), std::string(value).c_str());
}

inline natsStatus natsHeader_Get(const header& h, std::string_view key, const char** value) {
  return ::natsHeader_Get(h.native_handle(), std::string(key).c_str(), value);
}

inline natsStatus natsHeader_Values(const header& h, std::string_view key, const char*** values, int* count) {
  return ::natsHeader_Values(h.native_handle(), std::string(key).c_str(), values, count);
}

inline natsStatus natsHeader_Keys(const header& h, const char*** keys, int* count) {
  return ::natsHeader_Keys(h.native_handle(), keys, count);
}

inline int natsHeader_KeysCount(const header& h) { return ::natsHeader_KeysCount(h.native_handle()); }

inline natsStatus natsHeader_Delete(header& h, std::string_view key) {
  return ::natsHeader_Delete(h.native_handle(), std::string(key).c_str());
}

inline void natsHeader_Destroy(header& h) { h.reset(); }

}  // namespace natscpp
