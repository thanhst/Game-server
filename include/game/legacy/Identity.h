#pragma once

#include "game/legacy/LegacyContent.h"
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace game::legacy {
struct Account {
    std::int64_t id = 0;
    std::string username;
    std::int32_t status = 0, role = 0, activated = 0, goldBar = 0;
    std::optional<std::int64_t> lockUntilMs;
};
struct CharacterRecord {
    std::int64_t id = 0, accountId = 0, revision = 0;
    std::string name;
    // SQL column names, with embedded JSON decoded once. Unknown fields survive
    // load/save so another game module can add persistent state independently.
    nlohmann::json data;
};
enum class CreateCharacterStatus { Created = 0, InvalidLength = 1, InvalidName = 2,
    StorageError = 3, NameTaken = 4, ReservedName = 5, AccountHasCharacter = 6 };

// Storage owns atomic uniqueness and compare-and-swap, never game rules or wire
// packets. MySQL/account-service adapters can implement this same contract.
class IdentityStore {
public:
    virtual ~IdentityStore() = default;
    virtual std::optional<Account> authenticate(const std::string& username,
                                                 const std::string& password) = 0;
    virtual std::optional<CharacterRecord> characterFor(std::int64_t accountId) = 0;
    virtual CreateCharacterStatus createCharacter(CharacterRecord& character) = 0;
    virtual bool save(CharacterRecord& character) = 0;
};

// Local durable development storage. Its schema is NOT the old MySQL schema.
// Local passwords use salted PBKDF2-HMAC-SHA256; no password appears in packets
// emitted by this class or in source-controlled content.
class SqliteIdentityStore final : public IdentityStore {
public:
    explicit SqliteIdentityStore(const std::filesystem::path& file);
    ~SqliteIdentityStore() override;
    SqliteIdentityStore(const SqliteIdentityStore&) = delete;
    SqliteIdentityStore& operator=(const SqliteIdentityStore&) = delete;
    std::optional<Account> authenticate(const std::string&, const std::string&) override;
    std::optional<CharacterRecord> characterFor(std::int64_t) override;
    CreateCharacterStatus createCharacter(CharacterRecord&) override;
    bool save(CharacterRecord&) override;
    Account createAccount(std::string username, const std::string& password);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

std::string normalizeUsername(std::string username);
bool isLegacyUsername(const std::string& username) noexcept;
CreateCharacterStatus validateCharacterName(const std::string& name) noexcept;
// User.createChar + Info(planet), using actual template default item options.
CharacterRecord makeNewCharacter(const ContentSnapshot&, std::int64_t accountId,
    std::int32_t serverId, std::string name, std::int32_t gender,
    std::int32_t hair, std::int64_t nowMs);
} // namespace game::legacy
