#pragma once

#include <nats/nats.h>

#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <natscpp/detail/deleters.hpp>
#include <natscpp/error.hpp>

namespace natscpp {

class header {
 public:
  header() = default;
  explicit header(natsHeader* raw) : header_(raw) {}

  [[nodiscard]] static header create() {
    natsHeader* raw = nullptr;
    throw_on_error(::natsHeader_New(&raw), "natsHeader_New");
    return header{raw};
  }

  void set(std::string_view key, std::string_view value) {
    throw_on_error(::natsHeader_Set(native_handle(), std::string(key).c_str(), std::string(value).c_str()),
                   "natsHeader_Set");
  }

  void add(std::string_view key, std::string_view value) {
    throw_on_error(::natsHeader_Add(native_handle(), std::string(key).c_str(), std::string(value).c_str()),
                   "natsHeader_Add");
  }

  [[nodiscard]] std::string get(std::string_view key) const {
    const char* value = nullptr;
    throw_on_error(::natsHeader_Get(native_handle(), std::string(key).c_str(), &value), "natsHeader_Get");
    return value != nullptr ? std::string{value} : std::string{};
  }

  [[nodiscard]] std::vector<std::string> values(std::string_view key) const {
    const char** vals = nullptr;
    int count = 0;
    throw_on_error(::natsHeader_Values(native_handle(), std::string(key).c_str(), &vals, &count),
                   "natsHeader_Values");
    struct array_free { const char** p; ~array_free() { std::free(const_cast<char**>(p)); } } guard{vals};
    std::vector<std::string> out;
    out.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
      out.emplace_back(vals[i] != nullptr ? vals[i] : "");
    }
    return out;
  }

  [[nodiscard]] std::vector<std::string> keys() const {
    const char** ks = nullptr;
    int count = 0;
    throw_on_error(::natsHeader_Keys(native_handle(), &ks, &count), "natsHeader_Keys");
    struct array_free { const char** p; ~array_free() { std::free(const_cast<char**>(p)); } } guard{ks};
    std::vector<std::string> out;
    out.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
      out.emplace_back(ks[i] != nullptr ? ks[i] : "");
    }
    return out;
  }

  [[nodiscard]] int keys_count() const { return ::natsHeader_KeysCount(native_handle()); }

  void erase(std::string_view key) {
    throw_on_error(::natsHeader_Delete(native_handle(), std::string(key).c_str()), "natsHeader_Delete");
  }

  void destroy() { reset(); }

  [[nodiscard]] natsHeader* native_handle() const noexcept { return header_.get(); }
  [[nodiscard]] natsHeader* release() noexcept { return header_.release(); }
  void reset(natsHeader* raw = nullptr) noexcept { header_.reset(raw); }
  [[nodiscard]] bool valid() const noexcept { return header_ != nullptr; }

 private:
  std::unique_ptr<natsHeader, detail::header_deleter> header_;
};

[[nodiscard]] inline header natsHeader_New() {
  return header::create();
}

inline void natsHeader_Set(header& h, std::string_view key, std::string_view value) {
  h.set(key, value);
}

inline void natsHeader_Add(header& h, std::string_view key, std::string_view value) {
  h.add(key, value);
}

[[nodiscard]] inline std::string natsHeader_Get(const header& h, std::string_view key) {
  return h.get(key);
}

[[nodiscard]] inline std::vector<std::string> natsHeader_Values(const header& h, std::string_view key) {
  return h.values(key);
}

[[nodiscard]] inline std::vector<std::string> natsHeader_Keys(const header& h) {
  return h.keys();
}

[[nodiscard]] inline int natsHeader_KeysCount(const header& h) {
  return h.keys_count();
}

inline void natsHeader_Delete(header& h, std::string_view key) {
  h.erase(key);
}

inline void natsHeader_Destroy(header& h) {
  h.destroy();
}

}  // namespace natscpp
