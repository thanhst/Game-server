#include "game/legacy/Identity.h"
#include <sqlite3.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>

namespace game::legacy {
namespace {
constexpr int passwordIterations = 210000;
using Salt = std::array<unsigned char, 16>;
using Hash = std::array<unsigned char, 32>;
void sqlCheck(sqlite3* db, int status) {
    if (status != SQLITE_OK) throw std::runtime_error(std::string("identity database: ") + sqlite3_errmsg(db));
}
class Statement {
public:
    Statement(sqlite3* db, const char* text) : db_(db) {
        sqlCheck(db_, sqlite3_prepare_v2(db_, text, -1, &value_, nullptr));
    }
    ~Statement() { sqlite3_finalize(value_); }
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
    void bind(int index, std::int64_t value) { sqlCheck(db_, sqlite3_bind_int64(value_,index,value)); }
    void bind(int index, const std::string& value) {
        if (value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
            throw std::length_error("identity field too large");
        sqlCheck(db_, sqlite3_bind_text(value_, index, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT));
    }
    template<std::size_t N> void bind(int index, const std::array<unsigned char,N>& value) {
        sqlCheck(db_, sqlite3_bind_blob(value_, index, value.data(), static_cast<int>(N), SQLITE_TRANSIENT));
    }
    int step() {
        const auto code = sqlite3_step(value_);
        if (code != SQLITE_ROW && code != SQLITE_DONE && code != SQLITE_CONSTRAINT)
            throw std::runtime_error(std::string("identity statement: ") + sqlite3_errmsg(db_));
        return code;
    }
    std::int64_t integer(int column) const { return sqlite3_column_int64(value_,column); }
    std::string text(int column) const {
        const auto* ptr = sqlite3_column_text(value_, column);
        const int size = sqlite3_column_bytes(value_,column);
        return ptr ? std::string(reinterpret_cast<const char*>(ptr),static_cast<std::size_t>(size)) : std::string();
    }
    bool isNull(int column) const { return sqlite3_column_type(value_,column) == SQLITE_NULL; }
    template<std::size_t N> std::array<unsigned char,N> blob(int column) const {
        if (sqlite3_column_bytes(value_,column) != N) throw std::runtime_error("invalid stored password digest");
        std::array<unsigned char,N> result{};
        const auto* ptr = static_cast<const unsigned char*>(sqlite3_column_blob(value_,column));
        if (!ptr) throw std::runtime_error("missing stored password digest");
        std::copy_n(ptr,N,result.begin());
        return result;
    }
private:
    sqlite3* db_;
    sqlite3_stmt* value_ = nullptr;
};
Hash derive(const std::string& password, const Salt& salt, int iterations) {
    if (password.size() > 65535 || iterations < 10000 || iterations > 1000000)
        throw std::invalid_argument("invalid password derivation parameters");
    Hash result{};
    if (PKCS5_PBKDF2_HMAC(password.data(), static_cast<int>(password.size()), salt.data(),
        static_cast<int>(salt.size()), iterations, EVP_sha256(), static_cast<int>(result.size()), result.data()) != 1)
        throw std::runtime_error("password derivation failed");
    return result;
}
} // namespace
struct SqliteIdentityStore::Impl {
    sqlite3* db = nullptr;
    ~Impl() { if (db) sqlite3_close_v2(db); }
    void execute(const char* sql) { sqlCheck(db, sqlite3_exec(db,sql,nullptr,nullptr,nullptr)); }
};
SqliteIdentityStore::SqliteIdentityStore(const std::filesystem::path& file) : impl_(std::make_unique<Impl>()) {
    if (file.has_parent_path()) std::filesystem::create_directories(file.parent_path());
    const auto filename = file.u8string();
    const auto code = sqlite3_open_v2(filename.c_str(), &impl_->db,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr);
    if (code != SQLITE_OK) throw std::runtime_error("cannot open identity database");
    sqlite3_busy_timeout(impl_->db,5000);
    impl_->execute("PRAGMA foreign_keys=ON; PRAGMA journal_mode=WAL; PRAGMA synchronous=FULL;");
    // Create only this adapter's namespaced schema; never mutate nr_* tables.
    impl_->execute("CREATE TABLE IF NOT EXISTS gs_accounts_v1 ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, username TEXT NOT NULL UNIQUE,"
        "salt BLOB NOT NULL, digest BLOB NOT NULL, iterations INTEGER NOT NULL,"
        "status INTEGER NOT NULL DEFAULT 0, role INTEGER NOT NULL DEFAULT 0,"
        "activated INTEGER NOT NULL DEFAULT 0, gold_bar INTEGER NOT NULL DEFAULT 0, lock_until_ms INTEGER);"
        "CREATE TABLE IF NOT EXISTS gs_characters_v1 ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, account_id INTEGER NOT NULL UNIQUE REFERENCES gs_accounts_v1(id),"
        "name TEXT NOT NULL UNIQUE, revision INTEGER NOT NULL DEFAULT 1, data TEXT NOT NULL);");
}
SqliteIdentityStore::~SqliteIdentityStore() = default;
Account SqliteIdentityStore::createAccount(std::string username, const std::string& password) {
    username = normalizeUsername(std::move(username));
    if (username.empty() || username.size() > 128 || !isLegacyUsername(username) || password.empty())
        throw std::invalid_argument("invalid account name/password");
    Salt salt{};
    if (RAND_bytes(salt.data(),static_cast<int>(salt.size())) != 1) throw std::runtime_error("secure randomness unavailable");
    auto digest = derive(password,salt,passwordIterations);
    Statement query(impl_->db,"INSERT INTO gs_accounts_v1(username,salt,digest,iterations) VALUES(?,?,?,?)");
    query.bind(1,username); query.bind(2,salt); query.bind(3,digest); query.bind(4,passwordIterations);
    OPENSSL_cleanse(digest.data(),digest.size());
    if (query.step() != SQLITE_DONE) throw std::runtime_error("account already exists");
    Account result;
    result.id = sqlite3_last_insert_rowid(impl_->db);
    result.username = std::move(username);
    return result;
}
std::optional<Account> SqliteIdentityStore::authenticate(const std::string& username, const std::string& password) {
    Statement query(impl_->db,"SELECT id,username,status,role,activated,gold_bar,lock_until_ms,salt,digest,iterations "
        "FROM gs_accounts_v1 WHERE username=?");
    query.bind(1,normalizeUsername(username));
    if (query.step() != SQLITE_ROW) return std::nullopt;
    const auto salt = query.blob<16>(7);
    const auto expected = query.blob<32>(8);
    const auto iterations = query.integer(9);
    if (iterations < 10000 || iterations > 1000000) throw std::runtime_error("invalid stored password iterations");
    auto actual = derive(password,salt,static_cast<int>(iterations));
    const bool matches = CRYPTO_memcmp(actual.data(),expected.data(),actual.size()) == 0;
    OPENSSL_cleanse(actual.data(),actual.size());
    if (!matches) return std::nullopt;
    Account result;
    result.id=query.integer(0); result.username=query.text(1);
    result.status=static_cast<std::int32_t>(query.integer(2));
    result.role=static_cast<std::int32_t>(query.integer(3));
    result.activated=static_cast<std::int32_t>(query.integer(4));
    result.goldBar=static_cast<std::int32_t>(query.integer(5));
    if (!query.isNull(6)) result.lockUntilMs=query.integer(6);
    return result;
}
std::optional<CharacterRecord> SqliteIdentityStore::characterFor(std::int64_t accountId) {
    Statement query(impl_->db,"SELECT id,account_id,name,revision,data FROM gs_characters_v1 WHERE account_id=?");
    query.bind(1,accountId);
    if (query.step() != SQLITE_ROW) return std::nullopt;
    return CharacterRecord{query.integer(0),query.integer(1),query.integer(3),query.text(2),
        nlohmann::json::parse(query.text(4))};
}
CreateCharacterStatus SqliteIdentityStore::createCharacter(CharacterRecord& character) {
    const auto validation = validateCharacterName(character.name);
    if (validation != CreateCharacterStatus::Created) return validation;
    Statement query(impl_->db,"INSERT INTO gs_characters_v1(account_id,name,data) VALUES(?,?,?)");
    query.bind(1,character.accountId); query.bind(2,character.name); query.bind(3,character.data.dump());
    if (query.step() == SQLITE_DONE) {
        character.id = sqlite3_last_insert_rowid(impl_->db);
        character.revision = 1;
        return CreateCharacterStatus::Created;
    }
    if (characterFor(character.accountId)) return CreateCharacterStatus::AccountHasCharacter;
    Statement exists(impl_->db,"SELECT id FROM gs_characters_v1 WHERE name=?");
    exists.bind(1,character.name);
    return exists.step() == SQLITE_ROW ? CreateCharacterStatus::NameTaken : CreateCharacterStatus::StorageError;
}
bool SqliteIdentityStore::save(CharacterRecord& character) {
    if (character.revision <= 0 || character.revision == std::numeric_limits<std::int64_t>::max())
        throw std::invalid_argument("invalid character revision");
    Statement query(impl_->db,"UPDATE gs_characters_v1 SET data=?,revision=revision+1 "
        "WHERE id=? AND account_id=? AND name=? AND revision=?");
    query.bind(1,character.data.dump()); query.bind(2,character.id); query.bind(3,character.accountId);
    query.bind(4,character.name); query.bind(5,character.revision);
    if (query.step() != SQLITE_DONE || sqlite3_changes(impl_->db) != 1) return false;
    ++character.revision;
    return true;
}
} // namespace game::legacy
