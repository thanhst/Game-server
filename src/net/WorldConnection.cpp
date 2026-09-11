#include "game/net/WorldConnection.h"
#include "game/net/DebugProtocol.h"
#include "ServerEngine/C/ServerEngine.h"

#include <array>
#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>

namespace game::net {
namespace {
static_assert(std::atomic_bool::is_always_lock_free,
    "Signal shutdown requires a lock-free atomic flag");
std::atomic_bool stopRequested{false};
void requestStop(int) { stopRequested.store(true, std::memory_order_relaxed); }
class SignalGuard {
public:
    SignalGuard() {
        stopRequested.store(false, std::memory_order_relaxed);
        previousInterrupt_ = std::signal(SIGINT, requestStop);
        previousTerminate_ = std::signal(SIGTERM, requestStop);
    }
    ~SignalGuard() {
        if (previousInterrupt_ != SIG_ERR) std::signal(SIGINT, previousInterrupt_);
        if (previousTerminate_ != SIG_ERR) std::signal(SIGTERM, previousTerminate_);
    }
private:
    using Handler = void (*)(int);
    Handler previousInterrupt_ = SIG_DFL;
    Handler previousTerminate_ = SIG_DFL;
};
class EngineHandle {
public:
    se_server_handle value = 0;
    EngineHandle() = default;
    EngineHandle(const EngineHandle&) = delete;
    EngineHandle& operator=(const EngineHandle&) = delete;
    ~EngineHandle() {
        if (value) {
            se_server_stop(value, nullptr);
            se_server_destroy(value, nullptr);
        }
    }
};
void require(se_status status, const se_error& error, const char* operation) {
    if (status != SE_OK) throw std::runtime_error(std::string(operation) + ": " +
        std::to_string(status) + " " + std::string(error.message));
}
} // namespace

int runConsole(World& world, std::istream& input, std::ostream& output) {
    DebugProtocol protocol(world, true);
    output << protocol.connect(1).text << '\n';
    output << "Console commands use GAME/1 prefix. TICK advances simulation; QUIT exits.\n";
    bool done = false;
    while (!done) {
        output << "> " << std::flush;
        std::string line;
        bool tooLong = false;
        char character = 0;
        bool read = false;
        while (input.get(character)) {
            read = true;
            if (character == '\n') break;
            if (line.size() < maxDebugMessageBytes) line += character; else tooLong = true;
        }
        if (!read && !input) break;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (tooLong) {
            output << "GAME/1 ERROR message_size\n";
            continue;
        }
        const auto reply = protocol.dispatch(1, line);
        output << reply.text << '\n';
        for (const auto& frame : protocol.takeEventFrames()) output << frame.text << '\n';
        done = reply.close;
    }
    protocol.disconnect(1);
    return 0;
}

int runServer(World& world, std::uint16_t port) {
    if (port == 0) throw std::invalid_argument("port must be in 1..65535");
    // Do this before calling any initializer: V1 layouts are frozen at this ABI.
    if (se_get_abi_version() != SE_ABI_VERSION) throw std::runtime_error("ServerEngine ABI mismatch");
    EngineHandle engine;
    se_server_options options;
    se_server_options_init(&options);
    options.max_connections = 32;
    options.max_message_bytes = static_cast<std::uint32_t>(maxDebugMessageBytes);
    options.max_send_queue_bytes = 65536;
    options.max_event_queue_count = 2048;
    options.max_event_queue_bytes = 1024 * 1024;
    options.idle_timeout_ms = 120000;
    se_error error{};
    require(se_server_create(&options, &engine.value, &error), error, "create");
    se_listener_options listener;
    se_listener_options_init(&listener);
    listener.protocol = SE_PROTOCOL_TCP;
    listener.security = SE_SECURITY_NONE;
    listener.bind_address = "127.0.0.1";
    listener.port = port;
    std::uint64_t listenerId = 0;
    require(se_server_add_listener(engine.value, &listener, &listenerId, &error), error, "add listener");
    require(se_server_start(engine.value, &error), error, "start");

    SignalGuard signalGuard;
    DebugProtocol protocol(world);
    std::set<std::uint64_t> sessions;
    auto close = [&](std::uint64_t session) {
        protocol.disconnect(session);
        sessions.erase(session);
        se_server_disconnect(engine.value, session, nullptr);
    };
    auto send = [&](std::uint64_t session, const std::string& text) {
        if (text.size() > maxDebugMessageBytes || se_server_send(engine.value, session, text.data(),
            static_cast<std::uint32_t>(text.size()), nullptr) != SE_OK) {
            close(session); // Any send failure, including a slow peer, loses ownership immediately.
            return false;
        }
        return true;
    };
    auto broadcast = [&] {
        const auto frames = protocol.takeEventFrames();
        for (const auto& frame : frames) {
            // send may erase the current session; use a snapshot of stable IDs.
            const auto recipients = sessions;
            for (const auto session : recipients)
                if (protocol.canSee(session, frame.map)) send(session, frame.text);
        }
    };
    using Clock = std::chrono::steady_clock;
    const auto tick = std::chrono::milliseconds(50);
    auto nextTick = Clock::now() + tick;
    std::array<char, maxDebugMessageBytes> payload{};
    std::cout << "Development server on 127.0.0.1:" << port
        << " (GAME/1, memory-only, no authentication). Ctrl+C stops.\n";
    while (!stopRequested.load(std::memory_order_relaxed)) {
        // At most 64 events between clock checks; traffic cannot starve simulation.
        for (int count = 0; count < 64 && !stopRequested.load(std::memory_order_relaxed); ++count) {
            se_event event;
            se_event_init(&event);
            const auto status = se_server_poll_event(engine.value, &event, payload.data(),
                static_cast<std::uint32_t>(payload.size()), 0, &error);
            if (status == SE_TIMEOUT) break;
            if (status == SE_STOPPED) throw std::runtime_error("ServerEngine stopped unexpectedly");
            if (status == SE_BUFFER_TOO_SMALL) throw std::runtime_error("event exceeds bounded protocol buffer");
            require(status, error, "poll");
            if (event.payload_size > payload.size()) throw std::runtime_error("invalid event payload size");
            if (event.kind == SE_EVENT_OVERFLOW) throw std::runtime_error("event overflow: server closed to prevent lost state");
            if (event.kind == SE_EVENT_OPEN) {
                if (!sessions.insert(event.session_id).second) { close(event.session_id); continue; }
                send(event.session_id, protocol.connect(event.session_id).text);
            } else if (event.kind == SE_EVENT_MESSAGE) {
                if (!sessions.count(event.session_id)) { close(event.session_id); continue; }
                const auto reply = protocol.dispatch(event.session_id,
                    std::string_view(payload.data(), event.payload_size));
                send(event.session_id, reply.text);
                if (reply.close) close(event.session_id);
            } else if (event.kind == SE_EVENT_CLOSE) {
                protocol.disconnect(event.session_id);
                sessions.erase(event.session_id);
            } else if (event.kind == SE_EVENT_ERROR) {
                if (event.session_id) close(event.session_id);
                else throw std::runtime_error("ServerEngine listener error");
            } else throw std::runtime_error("unexpected transport event");
            broadcast();
        }
        // Catch up in bounded batches, with the same 50ms step under idle and load.
        for (int catchup = 0; catchup < 20 && Clock::now() >= nextTick; ++catchup) {
            if (world.now() > std::numeric_limits<Milliseconds>::max() - 50)
                throw std::runtime_error("simulation clock overflow");
            world.advance(world.now() + 50);
            nextTick += tick;
        }
        broadcast();
        if (Clock::now() < nextTick) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    for (const auto session : sessions) protocol.disconnect(session);
    return 0;
}
} // namespace game::net
