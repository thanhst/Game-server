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
#include <vector>
#include <exception>

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
int runTcpHost(GameModule& module,const TcpHostOptions& config) {
    if(!config.port || !config.maxConnections) throw std::invalid_argument("invalid TCP host options");
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
    listener.protocol=SE_PROTOCOL_TCP;
    listener.security=SE_SECURITY_NONE;
    listener.bind_address=config.bindAddress.c_str(); listener.port=config.port;
    std::uint64_t listenerId=0;
    require(se_server_add_listener(engine.handle,&listener,&listenerId,&error),error,
        "add framed TCP listener");
    require(se_server_start(engine.handle,&error),error,"start TCP host");
    Signals signals;
    std::set<SessionId> sessions;
    auto close=[&](SessionId id) {
        se_server_disconnect(engine.handle,id,nullptr);
        if(sessions.erase(id)) module.disconnected(id,wallTime());
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
    std::cout<<"Game TCP host on "<<config.bindAddress<<':'<<config.port
        <<". Ctrl+C stops and saves attached gameplay sessions.\n";
    using Clock=std::chrono::steady_clock;
    auto nextTick=Clock::now();
    // The engine emits whole payloads; reserve the declared message limit on
    // the heap once, outside the per-event loop.
    std::vector<std::uint8_t> buffer(options.max_message_bytes);
    auto shutdown=[&] {
        const auto ids=sessions;
        std::exception_ptr failure;
        for(const auto id:ids) {
            try { close(id); } catch(...) { if(!failure) failure=std::current_exception(); }
        }
        if(failure) std::rethrow_exception(failure);
    };
    try {
        while(!stopped.load(std::memory_order_relaxed)) {
            for(int n=0;n<64;++n) {
                se_event event;
                se_event_init(&event);
                const auto status=se_server_poll_event(engine.handle,&event,buffer.data(),
                    static_cast<std::uint32_t>(buffer.size()),0,&error);
                if(status==SE_TIMEOUT) break;
                require(status,error,"poll TCP event");
                if(event.payload_size>buffer.size()) throw std::runtime_error("oversized TCP event");
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
    } catch(...) {
        const auto failure=std::current_exception();
        try { shutdown(); }
        catch(const std::exception& saveError) { std::cerr<<"Shutdown: "<<saveError.what()<<'\n'; }
        catch(...) { std::cerr<<"Shutdown callback failed\n"; }
        std::rethrow_exception(failure);
    }
    shutdown();
    return 0;
}
} // namespace game::application
