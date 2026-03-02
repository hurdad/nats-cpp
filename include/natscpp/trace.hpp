#pragma once

#include <concepts>
#include <string>
#include <string_view>
#include <utility>

#include <natscpp/message.hpp>

namespace natscpp {

/**
 * @brief A carrier that supports OpenTelemetry-style key/value text map propagation.
 */
template <typename T>
concept TraceCarrier = requires(T carrier, const T const_carrier, std::string_view key, std::string_view value) {
  { carrier.set(key, value) } -> std::same_as<void>;
  { const_carrier.get(key) } -> std::convertible_to<std::string>;
};

/**
 * @brief Trace carrier backed by NATS message headers.
 */
class message_trace_carrier {
 public:
  explicit message_trace_carrier(message& msg) : msg_(msg) {}

  void set(std::string_view key, std::string_view value) { msg_.set_header(key, value); }
  [[nodiscard]] std::string get(std::string_view key) const { return msg_.header(key); }

 private:
  message& msg_;
};

/**
 * @brief Injects text-map trace context into a carrier.
 */
template <TraceCarrier Carrier, typename Pairs>
inline void inject_trace_context(Carrier& carrier, const Pairs& pairs) {
  for (const auto& [key, value] : pairs) {
    carrier.set(key, value);
  }
}

/**
 * @brief Extracts keys from a carrier into an output map-like object.
 */
template <TraceCarrier Carrier, typename OutMap, typename Keys>
inline void extract_trace_context(const Carrier& carrier, const Keys& keys, OutMap& out) {
  for (const auto& key : keys) {
    if (auto value = carrier.get(key); !value.empty()) {
      out.emplace(std::string(key), std::move(value));
    }
  }
}

}  // namespace natscpp
