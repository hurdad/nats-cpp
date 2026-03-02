#pragma once

#include <natscpp/connection.hpp>
#include <natscpp/error.hpp>
#include <natscpp/jetstream.hpp>
#include <natscpp/message.hpp>
#include <natscpp/subscription.hpp>
#include <natscpp/trace.hpp>

/**
 * @file natscpp.hpp
 * @brief Umbrella include and usage examples for the header-only natscpp library.
 *
 * @code
 * // Core publish/subscribe
 * #include <natscpp/natscpp.hpp>
 *
 * natscpp::connection nc({.url = "nats://localhost:4222"});
 * auto sub = nc.subscribe_sync("events.>");
 * nc.publish("events.created", R"({"id":42})");
 * auto msg = sub.next_message(std::chrono::seconds(1));
 * @endcode
 *
 * @code
 * // RPC request/reply
 * natscpp::connection nc;
 * auto reply = nc.request("svc.echo", "hello", std::chrono::seconds(2));
 *
 * auto fut = nc.request_async("svc.echo", "hello async");
 * auto async_reply = fut.get();
 * @endcode
 *
 * @code
 * // JetStream publish + pull consumer
 * natscpp::connection nc;
 * natscpp::jetstream js(nc);
 * js.publish("orders.created", R"({"order_id":"A-1"})");
 * auto pull = js.pull_subscribe("orders.created", "orders-worker");
 * auto pulled_msg = pull.next(std::chrono::seconds(1));
 * @endcode
 *
 * @code
 * // Trace propagation via headers
 * natscpp::connection nc;
 * auto inbox = nc.new_inbox();
 *
 * natsMsg* raw{};
 * natsMsg_Create(&raw, "trace.subject", nullptr, "payload", 7);
 * natscpp::message msg(raw);
 * natscpp::message_trace_carrier carrier(msg);
 *
 * std::vector<std::pair<std::string, std::string>> trace_kv = {
 *   {"traceparent", "00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01"},
 *   {"tracestate", "vendor=value"},
 * };
 * natscpp::inject_trace_context(carrier, trace_kv);
 * @endcode
 */

