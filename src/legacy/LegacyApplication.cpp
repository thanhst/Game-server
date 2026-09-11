#include "game/legacy/LegacyApplication.h"
#include "game/legacy/EccChallenge.h"
#include <openssl/rand.h>
#include <algorithm>
#include <limits>
#include <set>
#include <stdexcept>
#include <utility>

namespace game::legacy {
using application::SessionId;
namespace {
constexpr std::size_t maxQueuedBytes=64*1024*1024;
constexpr std::size_t maxQueuedDeliveries=8192;
std::uint64_t secureSeed() {
    std::uint64_t value=0;
    if (RAND_bytes(reinterpret_cast<unsigned char*>(&value),sizeof(value)) != 1)
        throw std::runtime_error("secure session entropy unavailable");
    return value;
}
Packet versionArray(std::int8_t command,const Bytes& bytes) {
    if(bytes.size()>65535) throw ProtocolError("resource version array exceeds unsigned-short length");
    return PacketWriter().writeShort(static_cast<std::int32_t>(bytes.size())).writeBytes(bytes).packet(command);
}
std::shared_ptr<const ContentSnapshot> checkedContent(std::shared_ptr<const ContentSnapshot> value) {
    if (!value) throw std::invalid_argument("legacy application needs content");
    return value;
}
}
struct LegacyApplication::Session {
    explicit Session(const MatrixChallengeConfig& config,std::int64_t now) : matrix(config), openedAt(now) {}
    LegacyCodec codec{CodecLimits{256*1024,8*1024*1024,512*1024,50}};
    MatrixChallenge matrix;
    EccChallenge ecc;
    std::int64_t openedAt;
    std::optional<std::int64_t> partialSince;
    bool clientInfo=false, closing=false, closeQueued=false, entered=false;
    std::string closeReason;
    int zoom=0,device=0;
    std::string version;
    std::optional<std::string> deviceInfo;
    std::optional<Account> account;
    std::set<std::string> reportedUnsupported;
};
LegacyApplication::LegacyApplication(LegacyApplicationConfig config,
    std::shared_ptr<const ContentSnapshot> content,IdentityStore& identity,
    std::unique_ptr<LegacyGameplay> gameplay,Entropy entropy)
    : config_(std::move(config)),content_(checkedContent(std::move(content))),
      caches_(LegacyCaches::build(*content_,config_.versions)),identity_(identity),
      gameplay_(std::move(gameplay)),entropy_(entropy?std::move(entropy):Entropy(secureSeed)) {
    if (config_.host.maxConnections==0 || config_.host.maxConnections>4096 ||
        config_.maxSessionsPerDevice==0 || config_.maxSessionsPerDevice>4096 ||
        config_.handshakeTimeoutMs<=0 || config_.partialPacketTimeoutMs<=0)
        throw std::invalid_argument("invalid legacy application limits");
    auto shortString=[](const std::string& value) {
        if(value.size()>65535) throw std::invalid_argument("legacy configuration string exceeds wire limit");
    };
    shortString(config_.advertisedHost); shortString(config_.listServers); shortString(config_.website);
    shortString(config_.comingSoonMessage); shortString(config_.adminOnlyMessage);
    if(config_.validDlls.size()>10000) throw std::invalid_argument("legacy DLL allow-list exceeds configured limit");
    for(const auto& value:config_.validDlls) shortString(value);
    for(const auto& resource:config_.resources) {
        if(resource.small.size()>65535 || resource.background.size()>65535)
            throw std::invalid_argument("legacy resource version array exceeds wire limit");
    }
    (void)MatrixChallenge(config_.matrix); // Validate profiles before accepting connections.
}
LegacyApplication::~LegacyApplication() = default;
void LegacyApplication::connected(SessionId id,std::int64_t now) {
    if (sessions_.count(id)) throw std::logic_error("duplicate session ID");
    if (sessions_.size()>=config_.host.maxConnections) {
        reserveOutgoing(0);
        outgoing_.push_back({id,{},true,"connection limit"}); return;
    }
    sessions_.emplace(id,std::make_unique<Session>(config_.matrix,now));
}
void LegacyApplication::reserveOutgoing(std::size_t estimatedBytes) {
    if(outgoing_.size()>=maxQueuedDeliveries || outgoingBytes_>maxQueuedBytes ||
       estimatedBytes>maxQueuedBytes-outgoingBytes_)
        throw ProtocolError("application send queue limit");
    if(outgoing_.capacity()<outgoing_.size()+1) {
        const auto capacity=std::min(maxQueuedDeliveries,std::max<std::size_t>(16,outgoing_.capacity()*2));
        outgoing_.reserve(capacity);
    }
}
void LegacyApplication::send(SessionId id,const Packet& packet) {
    const auto it=sessions_.find(id);
    if (it==sessions_.end() || it->second->closing) return;
    if(packet.payload.size()>(maxQueuedBytes-16)/2) throw ProtocolError("application payload exceeds queue limit");
    // Reserve the actual vector slot before encoding commits the XOR cursor.
    reserveOutgoing(packet.payload.size()*2+16);
    auto bytes=it->second->codec.encode(packet);
    outgoingBytes_+=bytes.size();
    outgoing_.push_back({id,std::move(bytes),false,{}});
}
void LegacyApplication::close(SessionId id,std::string reason) {
    const auto it=sessions_.find(id);
    if(it==sessions_.end() || it->second->closing) return;
    it->second->closing=true;
    it->second->closeReason=std::move(reason);
    // takeDeliveries emits at most one close per live session, even when the
    // packet queue is full. A send-limit failure must not lose its close event.
}
void LegacyApplication::dialog(SessionId id,std::string_view text) {
    send(id,PacketWriter().writeUTF(text).packet(-26));
}
void LegacyApplication::unsupported(SessionId id,std::string_view feature) {
    auto& session=*sessions_.at(id);
    if (session.reportedUnsupported.size()>=128) { close(id,"unsupported command flood"); return; }
    if (!session.reportedUnsupported.insert(std::string(feature)).second) return;
    dialog(id,"Chức năng này hiện chưa sẵn sàng.");
    reserveOutgoing(0);
    outgoing_.push_back({id,{},false,"unsupported: "+std::string(feature)});
}
void LegacyApplication::deliver(const std::vector<GameplayDelivery>& packets) {
    for(const auto& packet:packets) {
        try { send(packet.session,packet.packet); }
        catch(const ProtocolError&) { close(packet.session,"gameplay delivery exceeds protocol/queue limit"); }
    }
}
void LegacyApplication::received(SessionId id,const std::uint8_t* bytes,std::size_t size,std::int64_t now) {
    const auto it=sessions_.find(id);
    if (it==sessions_.end() || it->second->closing) return;
    auto& session=*it->second;
    try {
        session.codec.feed(bytes,size);
        std::size_t decoded=0;
        while (auto packet=session.codec.tryDecode()) {
            if (++decoded>1024) throw ProtocolError("packet burst limit");
            session.partialSince.reset();
            dispatch(id,session,*packet,now);
            if(session.closing) return;
        }
        if (session.codec.bufferedBytes()>0 && !session.partialSince) session.partialSince=now;
    } catch (const ProtocolError&) {
        close(id,"malformed/over-limit legacy packet");
    }
}
void LegacyApplication::clientInfo(SessionId id,Session& s,PacketReader& reader) {
    if(s.clientInfo) return;
    const auto zoom=reader.readByte();
    reader.readInt(); reader.readInt(); // width and height are not trusted world coordinates
    const auto device=reader.readByte();
    const auto version=reader.readUTF();
    if(zoom<=1) return; // Source accepts only zoom 2,3,4; <=1 returns without setting info.
    if(zoom>4) { close(id,"unsupported client zoom"); return; }
    reader.requireEnd();
    if(!s.matrix.acceptsVersion(device,version)) {
        dialog(id,std::string(device==0?"PC: ":"Mobile: ")+"Vui lòng tải phiên bản ["+
            s.matrix.profile(device).version+"] tại "+config_.website);
        return;
    }
    s.zoom=zoom; s.device=device; s.version=version; s.clientInfo=true;
    PacketWriter servers;
    servers.writeByte(2).writeUTF(config_.listServers).writeInt(5);
    for (const auto type:{35,36,37,21,38}) servers.writeInt(type);
    servers.writeLong(config_.partChecksum).writeLong(config_.itemChecksum);
    send(id,servers.packet(-29));
    send(id,PacketWriter().writeByte(0).writeInt(config_.resources.at(s.zoom-1).data).packet(-74));
    PacketWriter valid;
    valid.writeByte(3).writeInt(static_cast<std::int32_t>(config_.validDlls.size()));
    for(const auto& value:config_.validDlls) valid.writeUTF(value);
    send(id,valid.packet(-29));
    send(id,s.matrix.issue(device,entropy_()));
}
void LegacyApplication::login(SessionId id,Session& s,PacketReader& reader,std::int64_t now) {
    if(!s.clientInfo) { close(id,"login before client info"); return; }
    if(!s.matrix.verified()) { dialog(id,"Xác thực thất bại"); return; }
    auto username=reader.readUTF();
    const auto normalized=normalizeUsername(username);
    const auto last=lastLogin_.find(normalized);
    if(last!=lastLogin_.end() && now>=last->second && now-last->second<15000) {
        dialog(id,"Vui lòng thử lại sau "+std::to_string((15000-(now-last->second))/1000)+" giây"); return;
    }
    auto version=reader.readUTF();
    auto password=reader.readUTF();
    reader.requireEnd();
    if(username.compare(0,14,"@guest.ingame_")==0) password="a";
    if(config_.maintenance) { dialog(id,"Máy chủ đang tiến hành bảo trì, vui lòng quay lại sau."); return; }
    if(!isLegacyUsername(normalized)) { dialog(id,"Tài khoản không được chứa ký tự đặc biệt"); return; }
    const auto account=identity_.authenticate(normalized,password);
    std::fill(password.begin(),password.end(),'\0');
    if(!account) { dialog(id,"Tài khoản hoặc mật khẩu không chính xác!"); return; }
    if(account->role!=1 && config_.opensAtMs && now<*config_.opensAtMs) {
        dialog(id,config_.comingSoonMessage); return;
    }
    if(account->role!=1 && config_.adminOnly) { dialog(id,config_.adminOnlyMessage); return; }
    bool duplicate=false;
    for (const auto& other:sessions_) {
        if(other.first!=id && other.second->account && other.second->account->id==account->id) {
            close(other.first,"duplicate account login"); duplicate=true;
        }
    }
    if(duplicate) { close(id,"duplicate account login"); return; }
    if(account->status==1 || (account->lockUntilMs && now<*account->lockUntilMs)) {
        dialog(id,"Tài khoản của bạn đã bị khóa. Vui lòng liên hệ quản trị viên để biết thêm chi tiết."); return;
    }
    s.account=*account; s.version=std::move(version); lastLogin_[normalized]=now;
    const auto& resources=config_.resources.at(s.zoom-1);
    send(id,versionArray(-77,resources.small));
    send(id,versionArray(-93,resources.background));
    send(id,caches_.versionPacket());
}
void LegacyApplication::enter(SessionId id,Session& s,std::int64_t now) {
    if(!s.account || s.entered) return;
    auto character=identity_.characterFor(s.account->id);
    if(!character) { send(id,Packet{2,{}}); return; } // Service.createChar uses global +2.
    if(!s.deviceInfo) return; // Source waits until device-info packet was received.
    std::size_t matchingDevices=0;
    for(const auto& other:sessions_) {
        if(!other.second->closing && other.second->deviceInfo && *other.second->deviceInfo==*s.deviceInfo)
            ++matchingDevices;
    }
    if(matchingDevices>config_.maxSessionsPerDevice) return; // SessionManager.deviceInvalid
    if(!gameplay_) { unsupported(id,"enter-world adapter (map grids and remaining Player.enter rules)"); return; }
    const auto result=gameplay_->enter(id,*s.account,std::move(*character),now);
    if(!result.supported) { unsupported(id,result.reason); return; }
    s.entered=true;
    deliver(result.deliveries);
}
void LegacyApplication::dispatch(SessionId id,Session& s,const Packet& packet,std::int64_t now) {
    if(packet.command==getSessionIdCommand) {
        if(s.codec.connected()) return;
        reserveOutgoing(config_.advertisedHost.size()*2+48);
        auto bytes=s.codec.encodeHandshake(Bytes{static_cast<std::uint8_t>(entropy_())},
            config_.advertisedHost,config_.host.port,config_.redirect,config_.voicePort);
        outgoingBytes_+=bytes.size(); outgoing_.push_back({id,std::move(bytes),false,{}}); return;
    }
    if(!s.codec.connected()) return;
    PacketReader reader(packet.payload);
    if(packet.command==126) { // Cmd.ANDROID_PACK -> Session.setDeviceInfo
        auto deviceInfo=reader.readUTF();
        reader.requireEnd();
        s.deviceInfo=std::move(deviceInfo);
        return;
    }
    if(packet.command==-29) {
        if(s.account) return;
        const auto sub=reader.readByte();
        if(sub==2) clientInfo(id,s,reader);
        else if(sub==0) login(id,s,reader,now);
        else unsupported(id,"NOT_LOGIN/"+std::to_string(sub));
        return;
    }
    if(packet.command==120) {
        if(!s.clientInfo || !s.matrix.issued()) { close(id,"unsolicited matrix response"); return; }
        const auto result=s.matrix.verifyResponse(packet);
        if(result==MatrixVerification::rejected) return; // Java permits retry; login remains gated.
        if(result==MatrixVerification::accepted) send(id,s.ecc.issue());
        return;
    }
    if(packet.command==-109) { send(id,Packet{-109,{}}); return; }
    if(packet.command==-28) {
        if(!s.account) return;
        const auto sub=reader.readByte();
        if(sub==6) { send(id,caches_.mapPacket()); return; }
        if(sub==7) { send(id,caches_.skillPacket()); return; }
        if(sub==8) { for(const auto type:{0,1,2,100}) send(id,caches_.itemPacket(type)); return; }
        if(sub==13 || sub==17) return; // CLIENT_OK is commented out; CLEAR_TASK is telemetry only.
        if(sub==2) {
            if(s.entered) return;
            const auto name=reader.readUTF(); const auto gender=reader.readByte(); const auto hair=reader.readByte();
            reader.requireEnd();
            auto status=validateCharacterName(name);
            if(status==CreateCharacterStatus::Created) {
                auto character=makeNewCharacter(*content_,s.account->id,config_.serverId,name,gender,hair,now);
                status=identity_.createCharacter(character);
            }
            switch(status) {
            case CreateCharacterStatus::Created: enter(id,s,now); break;
            case CreateCharacterStatus::InvalidLength: dialog(id,"Tên nhân vật từ 6 đến 15 ký tự."); break;
            case CreateCharacterStatus::InvalidName: dialog(id,"Tên nhân vật không được có ký tự đặc biệt."); break;
            case CreateCharacterStatus::NameTaken: dialog(id,"Tên nhân vật đã tồn tại."); break;
            case CreateCharacterStatus::ReservedName: dialog(id,"Tên nhân vật không được chứa các từ này."); break;
            default: dialog(id,"Có lỗi xảy ra."); break;
            }
            return;
        }
        if(!s.entered) { unsupported(id,"NOT_MAP/"+std::to_string(sub)); return; }
    }
    if(packet.command==-87 && s.account) { send(id,caches_.dataPacket()); return; }
    if(packet.command==-38 && s.account) {
        // Session.finishUpdate verifies diagnostically; the Java enforcement
        // block is commented out. Matrix authentication remains mandatory.
        s.ecc.verifyResponse(packet.payload);
        enter(id,s,now); return;
    }
    if(s.entered && gameplay_) {
        const auto result=gameplay_->packet(id,packet,now);
        if(result.supported) deliver(result.deliveries); else unsupported(id,result.reason);
        return;
    }
    unsupported(id,"command/"+std::to_string(packet.command));
}
void LegacyApplication::disconnected(SessionId id,std::int64_t now) {
    const auto found=sessions_.find(id);
    if(found==sessions_.end()) return;
    auto session=std::move(found->second);
    sessions_.erase(found);
    if(session->account) lastLogin_[session->account->username]=now;
    if(session->entered && gameplay_) gameplay_->leave(id,now);
}
void LegacyApplication::tick(std::int64_t now) {
    for(const auto& pair:sessions_) {
        const auto& s=*pair.second;
        if(!s.codec.connected() && now>=s.openedAt && now-s.openedAt>=config_.handshakeTimeoutMs)
            close(pair.first,"handshake timeout");
        else if(s.partialSince && now>=*s.partialSince && now-*s.partialSince>=config_.partialPacketTimeoutMs)
            close(pair.first,"partial packet timeout");
    }
    for(auto it=lastLogin_.begin();it!=lastLogin_.end();) {
        if(now>=it->second && now-it->second>=15000) it=lastLogin_.erase(it); else ++it;
    }
    if(gameplay_) deliver(gameplay_->tick(now));
}
std::vector<application::Delivery> LegacyApplication::takeDeliveries() {
    std::size_t closeCount=0;
    for(const auto& pair:sessions_) if(pair.second->closing && !pair.second->closeQueued) ++closeCount;
    outgoing_.reserve(outgoing_.size()+closeCount);
    for(const auto& pair:sessions_) {
        auto& session=*pair.second;
        if(session.closing && !session.closeQueued) {
            outgoing_.push_back({pair.first,{},true,std::move(session.closeReason)});
            session.closeQueued=true;
        }
    }
    auto result=std::move(outgoing_); outgoing_.clear(); outgoingBytes_=0; return result;
}
} // namespace game::legacy
