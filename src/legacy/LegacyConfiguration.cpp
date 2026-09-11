#include "game/legacy/LegacyApplication.h"
#include <fstream>
#include <limits>
#include <stdexcept>

namespace game::legacy {
namespace {
std::int64_t number(const nlohmann::json& j, const char* key, std::int64_t min, std::int64_t max) {
    const auto& field=j.at(key);
    if (!field.is_number_integer()) throw std::invalid_argument(std::string("integer configuration required: ")+key);
    if (field.is_number_unsigned() && field.get<std::uint64_t>() > static_cast<std::uint64_t>(max))
        throw std::invalid_argument(std::string("configuration out of range: ")+key);
    const auto value=field.get<std::int64_t>();
    if(value<min || value>max) throw std::invalid_argument(std::string("configuration out of range: ")+key);
    return value;
}
Bytes versions(const nlohmann::json& value) {
    if (!value.is_array() || value.size()>65535) throw std::invalid_argument("invalid resource version array");
    Bytes result;
    for (const auto& item:value) {
        if (!item.is_number_integer()) throw std::invalid_argument("invalid resource version byte");
        if (item.is_number_unsigned() && item.get<std::uint64_t>() > 255)
            throw std::invalid_argument("invalid resource version byte");
        const auto n=item.get<std::int64_t>();
        if (n<0 || n>255) throw std::invalid_argument("invalid resource version byte");
        result.push_back(static_cast<std::uint8_t>(n));
    }
    return result;
}
} // namespace
LegacyApplicationConfig LegacyApplicationConfig::load(const std::filesystem::path& file) {
    std::ifstream input(file);
    if (!input) throw std::runtime_error("cannot open legacy configuration: "+file.u8string());
    const auto j=nlohmann::json::parse(input);
    LegacyApplicationConfig c;
    auto path=[&](const char* key) {
        auto p=std::filesystem::u8path(j.at(key).get<std::string>());
        return p.is_absolute()? p : file.parent_path()/p;
    };
    c.contentFile=path("content"); c.identityFile=path("identity_database");
    c.host.bindAddress=j.at("bind_address").get<std::string>();
    c.host.port=static_cast<std::uint16_t>(number(j,"port",1,65535));
    c.host.maxConnections=static_cast<std::uint32_t>(number(j,"max_connections",1,4096));
    if (j.contains("max_sessions_per_device"))
        c.maxSessionsPerDevice=static_cast<std::uint32_t>(number(j,"max_sessions_per_device",1,4096));
    c.advertisedHost=j.at("advertised_host").get<std::string>();
    c.voicePort=static_cast<std::int32_t>(number(j,"voice_port",1,65535));
    c.serverId=static_cast<std::int32_t>(number(j,"server_id",0,127));
    c.redirect=j.at("redirect").get<bool>();
    c.listServers=j.at("list_servers").get<std::string>(); c.website=j.at("website").get<std::string>();
    c.maintenance=j.at("maintenance").get<bool>(); c.adminOnly=j.at("admin_only").get<bool>();
    if (!j.at("opens_at_ms").is_null()) c.opensAtMs=number(j,"opens_at_ms",0,std::numeric_limits<std::int64_t>::max());
    c.comingSoonMessage=j.at("coming_soon_message").get<std::string>();
    c.adminOnlyMessage=j.at("admin_only_message").get<std::string>();
    const auto& ver=j.at("cache_versions");
    c.versions.data=static_cast<std::int8_t>(number(ver,"data",-128,127));
    c.versions.map=static_cast<std::int8_t>(number(ver,"map",-128,127));
    c.versions.skill=static_cast<std::int8_t>(number(ver,"skill",-128,127));
    c.versions.itemBase=static_cast<std::int8_t>(number(ver,"item_base",-128,127));
    auto matrix=[&](const char* key) {
        const auto& profile=j.at("matrix").at(key);
        return MatrixProfile{static_cast<std::uint64_t>(number(profile,"modulus",2,std::numeric_limits<std::int64_t>::max())),
            profile.at("version").get<std::string>()};
    };
    c.matrix.pc=matrix("pc"); c.matrix.mobile=matrix("mobile");
    c.partChecksum=number(j,"part_checksum",0,std::numeric_limits<std::int64_t>::max());
    c.itemChecksum=number(j,"item_checksum",0,std::numeric_limits<std::int64_t>::max());
    c.validDlls=j.at("valid_dlls").get<std::vector<std::string>>();
    const auto& resources=j.at("resources");
    if (!resources.is_array() || resources.size()!=4) throw std::invalid_argument("four zoom resource profiles required");
    for (std::size_t i=0;i<4;++i) {
        c.resources[i].data=static_cast<std::int32_t>(number(resources[i],"data_version",INT32_MIN,INT32_MAX));
        c.resources[i].small=versions(resources[i].at("small"));
        c.resources[i].background=versions(resources[i].at("background"));
    }
    return c;
}
} // namespace game::legacy
