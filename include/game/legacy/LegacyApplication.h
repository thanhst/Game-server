#pragma once
#include "game/application/GameModule.h"
#include "game/legacy/Identity.h"
#include "game/legacy/LegacyCaches.h"
#include "game/legacy/LegacyCodec.h"
#include "game/legacy/MatrixChallenge.h"
#include <array>
#include <functional>
#include <map>

namespace game::legacy {
struct ResourceVersions {
    std::int32_t data = 0;
    Bytes small, background;
};
struct LegacyApplicationConfig {
    application::StreamHostOptions host;
    std::filesystem::path contentFile, identityFile;
    std::string advertisedHost = "127.0.0.1", listServers, website;
    std::int32_t voicePort = 14446, serverId = 1;
    bool redirect = false, maintenance = false, adminOnly = false;
    std::optional<std::int64_t> opensAtMs;
    std::string comingSoonMessage, adminOnlyMessage;
    CacheVersions versions;
    MatrixChallengeConfig matrix;
    std::int64_t partChecksum = 0, itemChecksum = 0;
    std::array<ResourceVersions,4> resources;
    std::vector<std::string> validDlls;
    std::uint32_t maxSessionsPerDevice = 5;
    std::int64_t handshakeTimeoutMs = 15000, partialPacketTimeoutMs = 15000;
    static LegacyApplicationConfig load(const std::filesystem::path&);
};

struct GameplayDelivery { application::SessionId session; Packet packet; };
struct GameplayResult {
    bool supported = false;
    std::string reason;
    std::vector<GameplayDelivery> deliveries;
};
// Compatibility/application boundary. A gameplay adapter owns entering a zone,
// gameplay opcodes and saving live characters. Account/version synchronization
// does not assume a particular world, combat system or database backend.
class LegacyGameplay {
public:
    virtual ~LegacyGameplay() = default;
    virtual GameplayResult enter(application::SessionId, const Account&, CharacterRecord,
                                  std::int64_t nowMs) = 0;
    virtual GameplayResult packet(application::SessionId, const Packet&, std::int64_t nowMs) = 0;
    virtual void leave(application::SessionId, std::int64_t nowMs) = 0;
    virtual std::vector<GameplayDelivery> tick(std::int64_t nowMs) = 0;
};
class LegacyApplication final : public application::GameModule {
public:
    using Entropy = std::function<std::uint64_t()>;
    LegacyApplication(LegacyApplicationConfig, std::shared_ptr<const ContentSnapshot>,
        IdentityStore&, std::unique_ptr<LegacyGameplay> gameplay = {}, Entropy entropy = {});
    ~LegacyApplication() override;
    void connected(application::SessionId,std::int64_t) override;
    void received(application::SessionId,const std::uint8_t*,std::size_t,std::int64_t) override;
    void disconnected(application::SessionId,std::int64_t) override;
    void tick(std::int64_t) override;
    std::vector<application::Delivery> takeDeliveries() override;
private:
    struct Session;
    void dispatch(application::SessionId,Session&,const Packet&,std::int64_t);
    void clientInfo(application::SessionId,Session&,PacketReader&);
    void login(application::SessionId,Session&,PacketReader&,std::int64_t);
    void enter(application::SessionId,Session&,std::int64_t);
    void send(application::SessionId,const Packet&);
    void reserveOutgoing(std::size_t estimatedBytes);
    void close(application::SessionId,std::string reason);
    void dialog(application::SessionId,std::string_view);
    void unsupported(application::SessionId,std::string_view);
    void deliver(const std::vector<GameplayDelivery>&);
    LegacyApplicationConfig config_;
    std::shared_ptr<const ContentSnapshot> content_;
    LegacyCaches caches_;
    IdentityStore& identity_;
    std::unique_ptr<LegacyGameplay> gameplay_;
    Entropy entropy_;
    std::map<application::SessionId,std::unique_ptr<Session>> sessions_;
    std::map<std::string,std::int64_t> lastLogin_;
    std::vector<application::Delivery> outgoing_;
    std::size_t outgoingBytes_ = 0;
};
} // namespace game::legacy
