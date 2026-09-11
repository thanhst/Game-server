#include "game/legacy/Identity.h"
#include <stdexcept>
namespace {
void expect(bool condition,const char* message) { if(!condition) throw std::runtime_error(message); }
}
void runIdentityTests() {
    using namespace game::legacy;
    const auto content=ContentSnapshot::load(GAME_HUNR_CONTENT_FILE);
    expect(validateCharacterName("abcd")==CreateCharacterStatus::InvalidLength,"Java rejects four-character name");
    expect(validateCharacterName("abcde")==CreateCharacterStatus::Created,"Java allows five chars despite message saying six");
    expect(validateCharacterName("admin123")==CreateCharacterStatus::ReservedName,"reserved admin substring");
    expect(validateCharacterName("Name123")==CreateCharacterStatus::InvalidName,"character name lowercase only");
    auto earth=makeNewCharacter(content,1,1,"earth01",0,31,1000);
    expect(earth.data.at("gold")==10000000000LL && earth.data.at("gem")==500000,"original new-player currency");
    expect(earth.data.at("head")==31 && earth.data.at("map")==nlohmann::json::array({21,100,336}),"original hair and home");
    expect(earth.data.at("info").at("hp")==200 && earth.data.at("info").at("mp")==100,"earth initial resource values");
    expect(earth.data.at("skill").empty(),"new Java character initially has no learned skills");
    expect(earth.data.at("item_box").at(2).at("quantity")==999999,"source bean stack preserved at creation");
    expect(earth.data.at("magic_tree").at("level")==10,"source tree level");
    const auto saiyan=makeNewCharacter(content,1,1,"saiyan1",2,99,1000);
    expect(saiyan.data.at("head")==6 && saiyan.data.at("info").at("damage")==15,"invalid hair falls back by gender");
    const auto invalidGender=makeNewCharacter(content,1,1,"gender1",-1,99,1000);
    expect(invalidGender.data.at("gender")==0,"source invalid gender falls back to earth");
    SqliteIdentityStore store(":memory:");
    const auto account=store.createAccount("Tester", "first-password");
    expect(!store.authenticate("tester","wrong"),"wrong password must fail");
    expect(store.authenticate("TESTER","first-password")->id==account.id,"username normalization and password verification");
    auto character=makeNewCharacter(content,account.id,1,"tester01",1,29,1000);
    expect(store.createCharacter(character)==CreateCharacterStatus::Created,"create character transaction");
    auto saved=*store.characterFor(account.id);
    auto stale=saved;
    saved.data["future-game-component"]={{"rank",3}};
    expect(store.save(saved),"save first revision");
    stale.data["gold"]=0;
    expect(!store.save(stale),"stale save must not overwrite current character");
    const auto loaded=*store.characterFor(account.id);
    expect(loaded.data.at("gold")==10000000000LL && loaded.data.contains("future-game-component"),"unknown extension data survives storage");
    auto second=makeNewCharacter(content,account.id,1,"tester02",0,64,1000);
    expect(store.createCharacter(second)==CreateCharacterStatus::AccountHasCharacter,"one active character per account contract");
    const auto other=store.createAccount("second","second-password");
    auto sameName=makeNewCharacter(content,other.id,1,"tester01",0,64,1000);
    expect(store.createCharacter(sameName)==CreateCharacterStatus::NameTaken,"global character name uniqueness");
}
