#pragma once

#include <nats/nats.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

#ifdef nats_CheckCompatibility
#undef nats_CheckCompatibility
#endif

namespace natscpp {

inline bool nats_CheckCompatibility() {
  return ::nats_CheckCompatibilityImpl(NATS_VERSION_REQUIRED_NUMBER, NATS_VERSION_NUMBER, NATS_VERSION_STRING);
}

[[nodiscard]] inline bool nats_CheckCompatibilityImpl(uint32_t req_ver_number, uint32_t ver_number, std::string_view ver_string) {
  return ::nats_CheckCompatibilityImpl(req_ver_number, ver_number, std::string(ver_string).c_str());
}

inline void nats_Close() { ::nats_Close(); }

inline natsStatus nats_CloseAndWait(int64_t timeout) { return ::nats_CloseAndWait(timeout); }

inline natsStatus nats_CloseAndWait(std::chrono::milliseconds timeout) {
  return nats_CloseAndWait(static_cast<int64_t>(timeout.count()));
}

inline natsStatus nats_GetLastErrorStack(char* buffer, size_t buf_len) {
  return ::nats_GetLastErrorStack(buffer, buf_len);
}

inline const char* nats_GetVersion() { return ::nats_GetVersion(); }

inline uint32_t nats_GetVersionNumber() { return ::nats_GetVersionNumber(); }

inline int64_t nats_Now() { return ::nats_Now(); }

inline int64_t nats_NowInNanoSeconds() { return ::nats_NowInNanoSeconds(); }

inline int64_t nats_NowMonotonicInNanoSeconds() { return ::nats_NowMonotonicInNanoSeconds(); }

inline natsStatus nats_Open(int64_t lock_spin_count) { return ::nats_Open(lock_spin_count); }

inline natsStatus nats_OpenWithConfig(natsClientConfig* config) { return ::nats_OpenWithConfig(config); }

inline void nats_PrintLastErrorStack(FILE* file) { ::nats_PrintLastErrorStack(file); }

inline void nats_ReleaseThreadMemory() { ::nats_ReleaseThreadMemory(); }

inline natsStatus nats_SetMessageDeliveryPoolSize(int max) { return ::nats_SetMessageDeliveryPoolSize(max); }

inline natsStatus nats_Sign(std::string_view encoded_seed, std::string_view input, unsigned char** signature,
                            int* signature_length) {
  return ::nats_Sign(std::string(encoded_seed).c_str(), std::string(input).c_str(), signature, signature_length);
}

inline void nats_Sleep(int64_t sleep_time) { ::nats_Sleep(sleep_time); }

inline void nats_Sleep(std::chrono::milliseconds sleep_time) {
  nats_Sleep(static_cast<int64_t>(sleep_time.count()));
}

}  // namespace natscpp
