#include "game/application/GameModule.h"
#include "ServerEngine/C/ServerEngine.h"
#include <array>
#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <set>
#include <stdexcept>
#include <thread>

namespace game::application {
namespace {
static_assert(std::atomic_bool::is_always_lock_free,"signal flag must be lock-free");
std::atomic_bool stopped{false};
void stopSignal(int) { stopped.store(true,std::memory_order_relaxed); }
struct Signals {
    using Handler=void(*)(int);
    Handler interrupt,terminate;
    Signals() : interrupt(std::signal(SIGINT,stopSignal)),terminate(std::signal(SIGTERM,stopSignal)) {
        stopped.store(false,std::memory_order_relaxed);
    }
    ~Signals() {
        if(interrupt!=SIG_ERR) std::signal(SIGINT,interrupt);
        if(terminate!=SIG_ERR) std::signal(SIGTERM,terminate);
    }
};
struct Engine {
    se_server_handle handle=0;
    ~Engine() { if(handle) { se_server_stop(handle,nullptr); se_server_destroy(handle,nullptr); } }
};
void require(se_status status,const se_error& error,const char* operation) {
    if(status!=SE_OK) throw std::runtime_error(std::string(operation)+": "+error.message);
}
std::int64_t wallTime() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}
}
int runStreamHost(GameModule& module,const StreamHostOptions& config) {
    if(!config.port || !config.maxConnections) throw std::invalid_argument("invalid stream host options");
    if(se_get_abi_version()!=SE_ABI_VERSION) throw std::runtime_error("ServerEngine ABI mismatch");
    Engine engine;
    se_error error{};
    se_server_options options;
    se_server_options_init(&options);
    options.max_connections=config.maxConnections;
    options.max_message_bytes=8*1024*1024;
    options.max_send_queue_bytes=32*1024*1024;
    options.max_event_queue_count=4096;
    options.max_event_queue_bytes=64*1024*1024;
    options.idle_timeout_ms=120000;
    require(se_server_create(&options,&engine.handle,&error),error,"create engine");
    se_listener_options listener;
    se_listener_options_init(&listener);
    listener.protocol=SE_PROTOCOL_TCP_STREAM;
    listener.security=SE_SECURITY_NONE;
    listener.bind_address=config.bindAddress.c_str(); listener.port=config.port;
    std::uint64_t listenerId=0;
    require(se_server_add_listener(engine.handle,&listener,&listenerId,&error),error,
        "add byte stream listener (requires ServerEngine with SE_PROTOCOL_TCP_STREAM)");
    require(se_server_start(engine.handle,&error),error,"start stream host");
    Signals signals;
    std::set<SessionId> sessions;
    auto close=[&](SessionId id) {
        if(sessions.erase(id)) module.disconnected(id,wallTime());
        se_server_disconnect(engine.handle,id,nullptr);
    };
    auto flush=[&] {
        for(auto& delivery:module.takeDeliveries()) {
            if(!delivery.reason.empty()) std::clog<<"session "<<delivery.session<<": "<<delivery.reason<<'\n';
            if(delivery.close) { close(delivery.session); continue; }
            if(delivery.bytes.empty() || !sessions.count(delivery.session)) continue;
            if(delivery.bytes.size()>options.max_message_bytes ||
               se_server_send(engine.handle,delivery.session,delivery.bytes.data(),
                   static_cast<std::uint32_t>(delivery.bytes.size()),nullptr)!=SE_OK)
                close(delivery.session);
        }
    };
    std::cout<<"Legacy byte-stream host on "<<config.bindAddress<<':'<<config.port
        <<". Ctrl+C stops and saves attached gameplay sessions.\n";
    using Clock=std::chrono::steady_clock;
    auto nextTick=Clock::now();
    // TcpStream emits at most16KiB chunks; framing belongs entirely to module.
    std::array<std::uint8_t,16384> buffer{};
    auto shutdown=[&] {
        const auto ids=sessions;
        for(const auto id:ids) close(id);
    };
    try {
        while(!stopped.load(std::memory_order_relaxed)) {
            for(int n=0;n<64;++n) {
                se_event event;
                se_event_init(&event);
                const auto status=se_server_poll_event(engine.handle,&event,buffer.data(),
                    static_cast<std::uint32_t>(buffer.size()),0,&error);
                if(status==SE_TIMEOUT) break;
                require(status,error,"poll stream event");
                if(event.payload_size>buffer.size()) throw std::runtime_error("oversized stream event");
                if(event.kind==SE_EVENT_OPEN) {
                    if(!sessions.insert(event.session_id).second) throw std::runtime_error("duplicate transport session");
                    module.connected(event.session_id,wallTime());
                } else if(event.kind==SE_EVENT_MESSAGE) {
                    if(sessions.count(event.session_id)) module.received(event.session_id,
                        buffer.data(),event.payload_size,wallTime());
                } else if(event.kind==SE_EVENT_CLOSE) close(event.session_id);
                else if(event.kind==SE_EVENT_ERROR && event.session_id) close(event.session_id);
                else throw std::runtime_error("transport listener/overflow failure");
                flush();
            }
            if(Clock::now()>=nextTick) {
                module.tick(wallTime());
                nextTick=Clock::now()+std::chrono::milliseconds(50);
                flush();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    } catch(...) { shutdown(); throw; }
    shutdown();
    return 0;
}
} // namespace game::application
