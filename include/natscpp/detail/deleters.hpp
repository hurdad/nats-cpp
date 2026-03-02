#pragma once

#include <nats/nats.h>

namespace natscpp::detail {

struct connection_deleter {
  void operator()(natsConnection* ptr) const noexcept {
    if (ptr != nullptr) {
      natsConnection_Destroy(ptr);
    }
  }
};

struct subscription_deleter {
  void operator()(natsSubscription* ptr) const noexcept {
    if (ptr != nullptr) {
      natsSubscription_Destroy(ptr);
    }
  }
};

struct msg_deleter {
  void operator()(natsMsg* ptr) const noexcept {
    if (ptr != nullptr) {
      natsMsg_Destroy(ptr);
    }
  }
};

}  // namespace natscpp::detail
