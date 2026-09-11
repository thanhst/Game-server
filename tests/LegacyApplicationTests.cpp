#include "game/legacy/LegacyApplication.h"
#include <algorithm>
#include <stdexcept>
namespace {
using namespace game::legacy;
using game::application::SessionId;
void expect(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
class MemoryIdentity final : public IdentityStore {
public:
    int authCalls=0;
    std::optional<CharacterRecord> saved;
    std::optional<Account> authenticate(const std::string& user,const std::string& password) override {
        ++authCalls;
        if(user!="tester" || password!="correct") return std::nullopt;
        Account account; account.id=7; account.username="tester"; return account;
    }
    std::optional<CharacterRecord> characterFor(std::int64_t id) override {
        return saved && saved->accountId==id ? saved : std::nullopt;
    }
    CreateCharacterStatus createCharacter(CharacterRecord& value) override {
        if(saved) return CreateCharacterStatus::AccountHasCharacter;
        value.id=55; value.revision=1; saved=value; return CreateCharacterStatus::Created;
    }
    bool save(CharacterRecord& value) override { saved=value; return true; }
};
class GameplayProbe final : public LegacyGameplay {
public:
    int enters=0,packets=0,leaves=0;
    GameplayResult enter(SessionId id,const Account& account,CharacterRecord record,std::int64_t) override {
        expect(account.id==record.accountId && record.id==55,"session cannot attach another account's character");
        ++enters; return {true,{},{{id,Packet{99,{1}}}}};
    }
    GameplayResult packet(SessionId id,const Packet&,std::int64_t) override {
        ++packets; return {true,{},{{id,Packet{99,{2}}}}};
    }
    void leave(SessionId,std::int64_t) override { ++leaves; }
    std::vector<GameplayDelivery> tick(std::int64_t) override { return {}; }
};
void receive(LegacyApplication& app,SessionId id,const Packet& packet,std::int64_t now=100000) {
    // Test entropy is zero, so live handshake key is one zero byte. A plaintext
    // codec supplies client-direction BE24/Base64, including command120.
    const auto bytes=LegacyCodec().encode(packet);
    app.received(id,bytes.data(),bytes.size(),now);
}
void handshakeAndInfo(LegacyApplication& app,SessionId id,std::int64_t now) {
    app.connected(id,now);
    receive(app,id,Packet{-27,{}},now);
    const auto handshake=app.takeDeliveries();
    expect(handshake.size()==1 && handshake.front().bytes.front()==229,"handshake command before client info");
    receive(app,id,PacketWriter().writeByte(2).writeByte(2).writeInt(800).writeInt(600)
        .writeByte(0).writeUTF("0.0.7").packet(-29),now);
    const auto info=app.takeDeliveries();
    expect(info.size()==4 && info.back().bytes.front()==120,"client info sends server list/resource/valid DLL/matrix");
}
void verifyMatrix(LegacyApplication& app,SessionId id,std::int64_t now) {
    const auto response=MatrixChallenge::computeResponse(MatrixChallenge::generateSecret(4294967291ULL),
        MatrixChallenge::generateChallenge(4294967291ULL,0),4294967291ULL);
    PacketWriter writer;
    for(const auto& row:response) for(const auto n:row) writer.writeLong(static_cast<std::int64_t>(n));
    receive(app,id,writer.packet(120),now);
    const auto packets=app.takeDeliveries();
    expect(packets.size()==1 && packets.front().bytes.front()==121,"matrix success sends ECC challenge");
}
Packet login() { return PacketWriter().writeByte(0).writeUTF("tester").writeUTF("0.0.7").writeUTF("correct").packet(-29); }
}
void runLegacyApplicationTests() {
    using namespace game::legacy;
    const auto content=std::make_shared<const ContentSnapshot>(ContentSnapshot::load(GAME_HUNR_CONTENT_FILE));
    LegacyApplicationConfig config;
    config.listServers="local:127.0.0.1:14445:0,0,0";
    MemoryIdentity identity;
    auto probe=std::make_unique<GameplayProbe>();
    auto* gameplay=probe.get();
    LegacyApplication app(config,content,identity,std::move(probe),[]{return std::uint64_t{0};});
    handshakeAndInfo(app,1,100000);
    receive(app,1,login());
    expect(identity.authCalls==0,"unverified matrix must never call account storage");
    app.takeDeliveries();
    verifyMatrix(app,1,100000);
    receive(app,1,login());
    const auto auth=app.takeDeliveries();
    expect(identity.authCalls==1 && auth.size()==3,"login emits version arrays and template version packet");
    expect(auth[0].bytes.front()==179 && auth[1].bytes.front()==163 && auth[2].bytes.front()==228,"login packet order");
    receive(app,1,PacketWriter().writeUTF("test-device").packet(126));
    app.takeDeliveries();
    receive(app,1,Packet{-38,{}});
    const auto create=app.takeDeliveries();
    expect(create.size()==1 && create[0].bytes.front()==2,"account without character receives global CREATE_PLAYER");
    receive(app,1,PacketWriter().writeByte(2).writeUTF("tester01").writeByte(0).writeByte(31).packet(-28));
    app.takeDeliveries();
    expect(identity.saved && gameplay->enters==1,"create and authenticated gameplay handoff");
    receive(app,1,Packet{99,{}});
    app.takeDeliveries();
    expect(gameplay->packets==1,"entered packets reach replaceable gameplay adapter");
    app.disconnected(1,100100);
    expect(gameplay->leaves==1,"disconnect releases gameplay ownership once");
    app.disconnected(1,100200);
    expect(gameplay->leaves==1,"duplicate close is idempotent");
    app.connected(2,100000);
    app.tick(115000);
    const auto timeout=app.takeDeliveries();
    expect(timeout.size()==1 && timeout[0].close,"silent handshake deadline");
    app.disconnected(2,115000);
    app.connected(3,100000);
    const std::uint8_t partial=229;
    app.received(3,&partial,1,100000);
    app.tick(115001);
    const auto partialTimeout=app.takeDeliveries();
    expect(partialTimeout.size()==1 && partialTimeout[0].close,"partial header cannot keep a session alive indefinitely");
}
