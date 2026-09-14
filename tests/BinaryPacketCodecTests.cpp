#include "game/net/BinaryPacketCodec.h"

#include <limits>
#include <stdexcept>

namespace {
using namespace game::legacy;
using namespace game::net;

void expect(bool condition,const char* message) {
    if(!condition) throw std::runtime_error(message);
}
template <typename Action>
void rejected(Action action,const char* message) {
    try { action(); } catch(const ProtocolError&) { return; }
    throw std::runtime_error(message);
}
} // namespace

void runBinaryPacketCodecTests() {
    BinaryPacketCodec codec;
    // Independent engine MESSAGE fixtures. The engine's BE32 length prefix is
    // deliberately absent from both encode output and decode input.
    const Bytes bytes = {0xfb, 0, 0xff, 'A', 'A', '=', '='};
    expect(codec.encode(Packet{-5, {0, 0xff, 'A', 'A', '=', '='}})==bytes,
           "binary codec transformed payload or added another length prefix");
    const auto decoded=codec.decode(bytes);
    expect(decoded.command==-5 && decoded.payload==Bytes({0, 0xff, 'A', 'A', '=', '='}),
           "binary decode changed raw payload or command sign");
    expect(codec.encode(Packet{-109,{}})==Bytes({0x93}),"empty-payload ping must still contain its command");
    expect(codec.decode(Bytes{0x80}).command==-128 && codec.decode(Bytes{0xff}).command==-1,
           "binary command boundary narrowing differs");
    expect(codec.decode(Bytes{0x7f}).command==127 && codec.decode(Bytes{0}).command==0,
           "positive binary command boundary differs");
    for(const auto command : {-74,-67,120,121,115}) {
        const auto packet=codec.encode(Packet{static_cast<std::int8_t>(command),{0,1,2,3}});
        expect(packet.size()==5 && packet[1]==0 && packet[2]==1 && packet[3]==2 && packet[4]==3,
               "resource/auth/batch command received legacy-specific framing");
    }

    const auto hello=makeServerHello("h",1,true,2);
    const Bytes helloFixture={0xe5,0,1,0,0,0,0,0,1,'h',0,0,0,1,1,0,0,0,2};
    expect(hello.command==sessionHandshakeCommand && codec.encode(hello)==helloFixture,
           "version-one server hello schema differs");
    PacketReader helloReader(hello.payload);
    expect(helloReader.readUnsignedShort()==1 && helloReader.readInt()==0,
           "server hello version and flags missing");
    expect(helloReader.readUTF()=="h" && helloReader.readInt()==1 &&
           helloReader.readBoolean() && helloReader.readInt()==2,"server hello endpoint fields differ");
    helloReader.requireEnd();

    BinaryPacketCodec bounded(BinaryPacketLimits{4,4});
    expect(bounded.encode(Packet{1,{2,3,4}})==Bytes({1,2,3,4}),"maximum outgoing message rejected");
    expect(bounded.decode(Bytes{1,2,3,4}).payload==Bytes({2,3,4}),"maximum incoming message rejected");
    rejected([&] { bounded.encode(Packet{1,{2,3,4,5}}); },"outgoing bound excluded command byte");
    rejected([&] { bounded.decode(Bytes{1,2,3,4,5}); },"incoming message limit ignored");
    rejected([&] { bounded.decode(Bytes{}); },"message without command accepted");
    rejected([&] { bounded.decode(nullptr,1); },"null complete message accepted");
    rejected([&] { bounded.decode(bytes.data(),std::numeric_limits<std::size_t>::max()); },
             "oversized length accessed source before bounds check");
    BinaryPacketCodec commandOnly(BinaryPacketLimits{1,1});
    expect(commandOnly.encode(Packet{-27,{}})==Bytes({0xe5}),"command-only limit rejected valid packet");
    rejected([&] { commandOnly.encode(Packet{-27,{0}}); },"command-only limit allowed payload");
    rejected([] { BinaryPacketCodec invalid(BinaryPacketLimits{0,1}); },"zero message limit accepted");
    rejected([] { BinaryPacketCodec invalid(BinaryPacketLimits{1,16*1024*1024+1}); },"limit above engine maximum accepted");
    rejected([] { makeServerHello("h",0,false,1); },"invalid advertised port accepted");
    rejected([] { makeServerHello("h",1,false,65536); },"invalid voice port accepted");

    // No cipher state exists: independent messages always have the same bytes.
    expect(codec.encode(Packet{-5,{0}})==codec.encode(Packet{-5,{0}}),"binary codec retained XOR state");
    const auto raw=codec.decode(Bytes{2,'A','A','=','='});
    expect(raw.payload==Bytes({'A','A','=','='}),"binary payload incorrectly decoded as Base64");
}
