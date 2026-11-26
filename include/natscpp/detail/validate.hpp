#pragma once

#include <nats/nats.h>

#include <cstddef>
#include <string>
#include <string_view>

#include <natscpp/error.hpp>

// Checks on text that natscpp hands to nats.c.
//
// nats.c takes C strings, so a std::string_view with an embedded NUL would be silently cut short there (a check
// the application made on the whole string no longer matches what is sent). And nats.c writes subjects and header
// keys into the NATS protocol unchecked when publishing: a subject or reply subject containing whitespace or CR/LF,
// or a header key containing CR/LF, could inject protocol lines or headers (publish to other subjects with this
// connection's permissions, or set JetStream headers such as Nats-Msg-Id). These checks reject such input with
// nats_error before it reaches nats.c: NATS_INVALID_SUBJECT for subjects (as nats.c reports an empty one),
// NATS_INVALID_ARG otherwise.

namespace natscpp::detail {

[[noreturn]] inline void invalid_text(natsStatus status, const char* what, std::string_view value, const char* reason) {
  std::string shown;
  for (const char c : value.substr(0, 64)) {
    const auto u = static_cast<unsigned char>(c);
    if (u < 0x20 || u == 0x7f) {
      constexpr std::string_view hex = "0123456789abcdef";
      shown += "\\x";
      shown += hex.at(u >> 4U);
      shown += hex.at(u & 0xfU);
    } else {
      shown += c;
    }
  }
  if (value.size() > 64) shown += "...";
  throw nats_error(status, std::string(what) + ": " + reason + " (\"" + shown + "\")");
}

// For text nats.c only stores or looks up (names, header values, payloads of *_string calls): no NUL bytes.
inline std::string c_string(std::string_view value, const char* what) {
  if (value.find('\0') != std::string_view::npos) invalid_text(NATS_INVALID_ARG, what, value, "contains a NUL byte");
  return std::string(value);
}

// The rules nats.c applies to subscribe subjects (nats_IsSubjectValid), plus no control characters:
// non-empty, no whitespace or control characters, no empty tokens ("a..b", ".a", "a."), and '*' / '>' only as
// whole tokens with '>' last. Wildcards are allowed only where the caller says so (subscriptions, not publishes).
inline std::string subject(std::string_view value, const char* what, bool wildcards_allowed = false) {
  if (value.empty()) invalid_text(NATS_INVALID_SUBJECT, what, value, "subject is empty");
  std::size_t token_start = 0;
  for (std::size_t i = 0; i <= value.size(); ++i) {
    if (i < value.size()) {
      const auto u = static_cast<unsigned char>(value[i]);
      if (u <= 0x20 || u == 0x7f)
        invalid_text(NATS_INVALID_SUBJECT, what, value, "subject contains whitespace or a control character");
      if (value[i] != '.') continue;
    }
    const std::string_view token = value.substr(token_start, i - token_start);
    if (token.empty()) invalid_text(NATS_INVALID_SUBJECT, what, value, "subject has an empty token");
    if (token == "*" || token == ">") {
      if (!wildcards_allowed) invalid_text(NATS_INVALID_SUBJECT, what, value, "wildcards are not allowed here");
      if (token == ">" && i != value.size())
        invalid_text(NATS_INVALID_SUBJECT, what, value, "'>' must be the last token");
    }
    token_start = i + 1;
  }
  return std::string(value);
}

// Header names: visible ASCII except ':' (RFC 7230 field-name characters are a subset). nats.c writes keys into
// the protocol as-is, so CR/LF here would start a new header line.
inline std::string header_key(std::string_view value, const char* what) {
  if (value.empty()) invalid_text(NATS_INVALID_ARG, what, value, "header key is empty");
  for (const char c : value) {
    const auto u = static_cast<unsigned char>(c);
    if (u <= 0x20 || u >= 0x7f || c == ':') {
      invalid_text(NATS_INVALID_ARG, what, value, "header key must be visible ASCII without ':'");
    }
  }
  return std::string(value);
}

// Header values: nats.c replaces CR/LF with spaces when encoding; reject them (and NUL) instead of sending
// something other than what the caller wrote.
inline std::string header_value(std::string_view value, const char* what) {
  for (const char c : value) {
    if (c == '\0' || c == '\r' || c == '\n')
      invalid_text(NATS_INVALID_ARG, what, value, "header value contains NUL, CR or LF");
  }
  return std::string(value);
}

// Stream, consumer (durable) and queue group names. nats.c puts stream and consumer names into JetStream API
// subjects (e.g. $JS.API.STREAM.INFO.<stream>) after checking only for '.' (and, for consumers, ' ', '*', '>'),
// so a name with a tab or CR/LF could inject protocol lines; it also writes them into JSON unescaped. These are
// the server's naming rules: no whitespace or control characters, '.', '*', '>', path separators or quotes.
inline std::string name(std::string_view value, const char* what, bool allow_empty = false) {
  if (value.empty()) {
    if (allow_empty) return {};
    invalid_text(NATS_INVALID_ARG, what, value, "name is empty");
  }
  for (const char c : value) {
    const auto u = static_cast<unsigned char>(c);
    if (u <= 0x20 || u == 0x7f || c == '.' || c == '*' || c == '>' || c == '/' || c == '\\' || c == '"') {
      invalid_text(NATS_INVALID_ARG, what, value,
                   "name must not contain whitespace, control characters, '.', '*', '>', '/', '\\' or '\"'");
    }
  }
  return std::string(value);
}

// Subjects that nats.c writes into JetStream JSON configs and requests without escaping (stream subjects, consumer
// filter and deliver subjects): a valid subject (wildcards allowed) that also has no '"' or '\'.
inline std::string config_subject(std::string_view value, const char* what) {
  std::string out = subject(value, what, true);
  if (value.find_first_of("\"\\") != std::string_view::npos) {
    invalid_text(NATS_INVALID_SUBJECT, what, value, "subject must not contain '\"' or '\\' here");
  }
  return out;
}

// KV key filters (watch, history, keys_with_filters): nats.c builds a consumer filter subject from them and writes
// it into JSON unescaped, without the checks it applies to keys on put/get.
inline std::string kv_key_filter(std::string_view value, const char* what) {
  if (value.empty()) invalid_text(NATS_INVALID_ARG, what, value, "key is empty");
  for (const char c : value) {
    const auto u = static_cast<unsigned char>(c);
    if (u <= 0x20 || u == 0x7f || c == '"') {
      invalid_text(NATS_INVALID_ARG, what, value, "key must not contain whitespace, control characters or '\"'");
    }
  }
  return std::string(value);
}

// Free text that nats.c copies into JSON between quotes without escaping it: the CONNECT fields (user, password,
// token, connection name) and descriptions in stream, consumer and KV configs. Escaping it here makes any text
// arrive intact (a password containing '"' or '\' otherwise never authenticates) and keeps it from adding JSON
// fields or, in CONNECT, protocol lines. NUL is rejected, since nats.c would cut the text there.
inline std::string json_text(std::string_view value, const char* what) {
  std::string out;
  out.reserve(value.size());
  for (const char c : value) {
    const auto u = static_cast<unsigned char>(c);
    if (c == '\0') invalid_text(NATS_INVALID_ARG, what, value, "contains a NUL byte");
    if (c == '"' || c == '\\') {
      out += '\\';
      out += c;
    } else if (u < 0x20 || u == 0x7f) {
      constexpr std::string_view hex = "0123456789abcdef";
      out += "\\u00";
      out += hex.at(u >> 4U);
      out += hex.at(u & 0xfU);
    } else {
      out += c;
    }
  }
  return out;
}

}  // namespace natscpp::detail
