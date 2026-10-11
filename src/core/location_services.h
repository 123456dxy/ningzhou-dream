#pragma once
#include "core/model.h"
namespace nz {
struct LocationService {
    string id, name, desc, reason, targetTab;
    int days = 0, cost = 0, stage = 0;
    bool enabled = false;
};
vector<LocationService> locationServices(const Game& g, const string& location);
string locationServiceBegin(Game& g, const string& id, string& targetTab);
void locationServiceComplete(Game& g, const Job& job);
}
