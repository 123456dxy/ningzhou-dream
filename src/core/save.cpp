// 宁州一梦 — public save orchestration; codec, validation and storage live in persistence/.
#include "core/save.h"
#include "core/persistence/internal.h"

namespace nz {
using json::Value;

std::string saveToJson(const Game& game) {
    return persistence::gameToJson(game).dump(1);
}

static bool loadFromJsonImpl(const std::string& text, Game& out, std::string* err, bool detached) {
    if (err) err->clear();
    if (text.size() > 16u * 1024u * 1024u) { if (err) *err = "存档数据超过大小限制"; return false; }
    std::string parseError;
    Value root = Value::parse(text, &parseError);
    if (!parseError.empty() || !root.isObject()) {
        if (err) *err = parseError.empty() ? "存档不是合法的 JSON 对象" : parseError;
        return false;
    }
    if (root["format"].asString() != SAVE_FORMAT) {
        if (err) *err = "存档格式不匹配（期望 " + std::string(SAVE_FORMAT) + "）";
        return false;
    }
    if (!root["player"].isObject() || !root["world"].isObject() ||
        !root["player"]["name"].isString() || !root["player"]["stage"].isNumber() ||
        !persistence::validSaveNumbers(root) || !persistence::saveShapesValid(root) ||
        (root["version"].isNumber() && !persistence::saveInteger(root["version"], 1, VERSION)) ||
        (!root["combatRulesVersion"].isNull() && !persistence::saveInteger(root["combatRulesVersion"], 1, 5)) ||
        (root["year"].isNumber() && !persistence::saveInteger(root["year"], 0, 100000)) ||
        (root["clock"]["days"].isNumber() && !persistence::saveInteger(root["clock"]["days"], 0, 36000000))) {
        if (err) *err = "存档缺少角色或世界数据，或字段数值无效";
        return false;
    }

    if (!detached && root["player"]["arts"]["battle"]["shadowPlayer"].isObject()) {
        if (err) *err = "天机切磋不能作为角色存档导入";
        return false;
    }
    Game game = persistence::gameFromJson(root);
    persistence::migrateLegacySave(root, game);
    const std::string stateError = persistence::saveStateError(game);
    if (!stateError.empty()) {
        if (err) *err = "存档数据无效：" + stateError;
        return false;
    }
    persistence::completeLoadedSave(root, game, detached);

    // Publish only after every schema check, migration and state check succeeds.
    out = std::move(game);
    return true;
}

bool loadFromJson(const std::string& text, Game& out, std::string* err) {
    return loadFromJsonImpl(text, out, err, false);
}

bool loadDetachedFromJson(const std::string& text, Game& out, std::string* err) {
    return loadFromJsonImpl(text, out, err, true);
}


} // namespace nz
