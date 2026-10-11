#pragma once
#include "core/model.h"
namespace nz {
struct NpcLesson {
    string kind, id, name, dao, reason;
    int stage = 0, days = 2, creditCost = 0, stoneCost = 0, affinityNeed = 20;
    bool learned = false, enabled = false;
};
vector<NpcLesson> npcLessons(const Game& g, const string& npcId);
string npcLearnBegin(Game& g, const string& npcId, const string& kind, const string& id);
void npcLearnComplete(Game& g, const Job& job);
string personPortrait(const string& gender, const string& sect, bool player = false);
string beastPortrait(int stage);
}
