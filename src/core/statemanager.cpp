#include "statemanager.h"
#include "jsonutil.h"
#include "fileutil.h"
#include <json/json.hpp>
using nlohmann::json;

std::map<StateManager::StateID, json> StateManager::_states;
std::string StateManager::_dir;

void StateManager::setDir(const std::string& dir)
{
    _dir = dir;
}

bool StateManager::saveState(Tracker* tracker, ScriptHost*, bool tofile, const std::string& name)
{
    if (!tracker) return false;
    auto pack = tracker->getPack();
    if (!pack) return false;
    
    auto state = tracker->saveState();
    if (!tofile) {
        printf("Saving state \"%s\" to RAM...\n", name.c_str());
        _states[{ pack->getUID(), pack->getVersion(),
                  pack->getVariant(), name }] = state;
        return true;
    } else {
        auto dirname = os_pathcat(_dir, pack->getUID(), pack->getVersion(), pack->getVariant());
        mkdir_recursive(dirname.c_str());
        auto filename = os_pathcat(dirname, name+".json");
        printf("Saving state \"%s\" to file...\n", name.c_str());
        return writeFile(filename, state.dump());
    }    
}
bool StateManager::loadState(Tracker* tracker, ScriptHost* scripthost, bool fromfile, const std::string& name)
{
    if (!tracker) return false;
    auto pack = tracker->getPack();
    if (!pack) return false;
    
    bool res = false;
    if (!fromfile) {
        auto it = _states.find({ pack->getUID(), pack->getVersion(), pack->getVariant(), name });
        printf("Loading state \"%s\" from RAM...", name.c_str());
        if (it == _states.end()) {
            printf(" missing\n");
            return false;
        }
        printf("\n");
        res = tracker->loadState(it->second);
    } else {
        std::string s;
        std::string filename = os_pathcat(_dir, pack->getUID(), pack->getVersion(), pack->getVariant(), name+".json");
        printf("Loading state \"%s\" from file...", name.c_str());
        if (!readFile(filename, s)) {
            printf(" missing\n");
            return false;
        }
        printf("\n");
        auto j = parse_jsonc(s);
        res = tracker->loadState(j);
    }
    if (scripthost) scripthost->resetWatches();
    printf("%s\n", res ? "ok" : "error");
    return res;
}