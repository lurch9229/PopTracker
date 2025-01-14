#include "jsonitem.h"
#include "jsonutil.h"
#include "../luaglue/luamethod.h"
using nlohmann::json;

const LuaInterface<JsonItem>::MethodMap JsonItem::Lua_Methods = {
    LUA_METHOD(JsonItem, SetOverlay, const char*),
    LUA_METHOD(JsonItem, SetOverlayBackground, const char*),
    LUA_METHOD(JsonItem, SetOverlayFontSize, int),
};

std::string JsonItem::getCodesString() const {
    // this returns a human-readable list, for debugging
    std::string s;
    for (const auto& code: _codes) {
        if (!s.empty()) s += ", ";
        s += code;
    }
    for (const auto& stage: _stages) {
        if (!s.empty()) s += ", ";
        s += stage.getCodesString();
    }
    return s;
}

const std::list<std::string>& JsonItem::getCodes(int stage) const {
    if (_type == Type::TOGGLE) return _codes;
    if (stage>=0 && (size_t)stage<_stages.size()) return _stages[stage].getCodes();
    return _codes;
}

JsonItem JsonItem::FromJSONString(const std::string& j)
{
    return FromJSON(json::parse(j, nullptr, true, true));
}
JsonItem JsonItem::FromJSON(nlohmann::json&& j)
{
    auto tmp = j;
    return FromJSON(tmp);
}
JsonItem JsonItem::FromJSON(json& j)
{
    JsonItem item;
    
    item._name          = to_string(j["name"], "");
    item._type          = Str2Type(to_string(j["type"], ""));
    
    if (item._type == Type::STATIC) {
        item._allowDisabled = false;
        item._stage1 = 1;
    } else /*if (item._type == Type::TOGGLE)*/ {
        item._allowDisabled = true; // default to true
    }
    
    item._capturable    = to_bool(j["capturable"], false);
    item._loop          = to_bool(j["loop"], false);
    item._allowDisabled = to_bool(j["allow_disabled"], item._allowDisabled);
    item._img           = to_string(j["img"], "");
    item._disabledImg   = to_string(j["disabled_img"], item._img);
    item._maxCount      = to_int(j["max_quantity"], -1);
    
    if (item._type == Type::PROGRESSIVE_TOGGLE) {
        item._type = Type::PROGRESSIVE;
        item._loop = true;
        item._allowDisabled = true;
    }

    item._overlayBackground = to_string(j["overlay_background"], "");
    item._overlayFontSize = to_int(j["overlay_font_size"], to_int(j["badge_font_size"], 0));

    commasplit(to_string(j["codes"], ""), item._codes);
    commasplit(to_string(j["img_mods"], ""), item._imgMods);
    commasplit(to_string(j["disabled_img_mods"], to_string(j["img_mods"], "")+",@disabled"), item._disabledImgMods);
    
    if (j["stages"].type() == json::value_t::array) {
        for (auto& v: j["stages"]) {
            item._stages.push_back(Stage::FromJSON(v));
        }
    }
    
    if (item._type == Type::COMPOSITE_TOGGLE && j["images"].type() == json::value_t::array) {
        // we convert composite toggle to a 4-staged item and only have special
        // code for mouse presses and for linking it to the "parts" in Tracker
        std::string leftCode = j["item_left"];
        std::string rightCode = j["item_right"];
        for (uint8_t n=0; n<4; n++) {
            for (auto& v: j["images"]) {
                if (to_bool(v["left"],false) == !!(n&1) && to_bool(v["right"],false) == !!(n&2)) {
                    v["codes"] = (n==3) ? leftCode+","+rightCode :
                                 (n==2) ? rightCode : leftCode;
                    v["inherit_codes"] = false;
                    item._stages.push_back(Stage::FromJSON(v));
                    break;
                }
            }
        }
        item._stage1 = 1;
    }
    
    item._stage2 = std::max(0,std::min(to_int(j["initial_stage_idx"],0), (int)item._stages.size()-1));
    item._count  = std::max(0,std::min(to_int(j["initial_quantity"],0), item._maxCount));
    if (item._type == Type::CONSUMABLE && item._count > 0) item._stage1=1;
    
    return item;
}


JsonItem::Stage JsonItem::Stage::FromJSONString(const std::string& j)
{
    return FromJSON(json::parse(j, nullptr, true, true));
}
JsonItem::Stage JsonItem::Stage::FromJSON(nlohmann::json&& j)
{
    auto tmp = j;
    return FromJSON(tmp);
}
JsonItem::Stage JsonItem::Stage::FromJSON(json& j)
{
    Stage stage;
    
    stage._img          = to_string(j["img"], "");
    stage._disabledImg  = to_string(j["disabled_img"], stage._img);
    stage._inheritCodes = to_bool(j["inherit_codes"], true);
    
    commasplit(to_string(j["codes"], ""), stage._codes);
    commasplit(to_string(j["secondary_codes"], ""), stage._secondaryCodes);
    commasplit(to_string(j["img_mods"], ""), stage._imgMods);
    commasplit(to_string(j["disabled_img_mods"], to_string(j["img_mods"], "")+",@disabled"), stage._disabledImgMods);
    
    return stage;
}

std::string JsonItem::Stage::getCodesString() const {
    // this returns a human-readable list, for debugging
    std::string s;
    for (const auto& code: _codes) {
        if (!s.empty()) s += ", ";
        s += code;
    }
    return s;
}


bool JsonItem::_changeStateImpl(BaseItem::Action action) {
    if (_type == Type::TOGGLE) {
        // left,fwd = on, right,back = off, middle = toggle
        if (action == Action::Primary || action == Action::Next) {
            if (_stage1 > 0) return false;
            _stage1 = 1;
        } else if (action == Action::Secondary || action == Action::Prev) {
            if (_stage1 < 1) return false;
            _stage1 = 0;
        } else { // middle mouse = toggle
            _stage1 = !_stage1;
        }
    } else if (_type == Type::PROGRESSIVE && !_allowDisabled) {
        // left,fwd = next, right,back = prev, middle = nothing
        _stage1 = 1;
        int n = _stage2;
        if (action == Action::Primary || action == Action::Next) {
            n = _stage2+1;
            if (n>=(int)_stages.size()) {
                if (_loop) n = 0;
                else n--;
            }
        } else if (action == Action::Secondary || action == Action::Prev) {
            n = _stage2-1;
            if (n<0) {
                if (_loop) n = (int)_stages.size()-1;
                else n++;
            }
        }
        if (n == _stage2) return false;
        _stage2 = n;
    } else if (_type == Type::PROGRESSIVE/* && _allowDisabled*/) {
        // left,middle = toggle, right,fwd = next, back = prev
        if (action == Action::Primary || action == Action::Toggle) {
            _stage1 = !_stage1;
        } else if (action == Action::Secondary || action == Action::Next) {
            int n = _stage2+1;
            if (n>=(int)_stages.size()) {
                if (_loop || action == Action::Secondary) n = 0; // always loop for progressive + allowDisable + right mouse button
                else n--;
            }
            if (n == _stage2) return false;
            _stage2 = n;
        } else {
            int n = _stage2-1;
            if (n<0) {
                if (_loop) n = _stages.size()-1;
                else n++;
            }
            if (n == _stage2) return false;
            _stage2 = n;
        }
    } else if (_type == Type::CONSUMABLE) {
        // left,fwd = +1, right,back = -1
        if (action == Action::Primary || action == Action::Next) {
            int n = _count+1;
            if (_maxCount>=0 && n>_maxCount) return false;
            _count = n;
            _stage1 = 1;
        } else if (action == Action::Secondary || action == Action::Prev) {
            int n = _count-1;
            if (n<0) return false;
            _count = n;
            _stage1 = (n>0);
        }
    } else if (_type == Type::COMPOSITE_TOGGLE) {
        unsigned n = (unsigned)_stage2;
        if (action == Action::Primary) {
            n ^= 1;
        } else if (action == Action::Secondary) {
            n ^= 2;
        }
        if (n >= _stages.size()) return false;
        _stage1 = 1;
        _stage2 = (int)n;
    } else {
        // not implemented
        printf("Unimplemented item action for type=%s\n", Type2Str(_type).c_str());
        return false;
    }
    return true;
}

int JsonItem::Lua_Index(lua_State *L, const char* key) {
    if (strcmp(key, "AcquiredCount")==0) {
        lua_pushinteger(L, _count);
        return 1;
    } else if (strcmp(key, "Active")==0) {
        lua_pushboolean(L, _stage1);
        return 1;
    } else if (strcmp(key, "CurrentStage")==0) {
        lua_pushinteger(L, _stage2);
        return 1;
    } else if (strcmp(key, "MaxCount")==0) {
        lua_pushinteger(L, _maxCount);
        return 1;
    }
    printf("Get JsonItem(%s).%s unknown\n", _name.c_str(), key);
    return 0;
}
bool JsonItem::Lua_NewIndex(lua_State *L, const char *key) {
    if (strcmp(key, "AcquiredCount")==0) {
        int val = luaL_checkinteger(L, -1);
        if (_count != val) {
            _count = val;
            if (_type == Type::CONSUMABLE)
                _stage1 = (_count>0);
            onChange.emit(this);
        }
        return true;
    } else if (strcmp(key, "Active")==0) {
        bool val = lua_toboolean(L, -1);
        if (_stage1 != val) {
            _stage1 = val;
            onChange.emit(this);
        }
        return true;
    } else if (strcmp(key, "CurrentStage")==0) {
        int val = luaL_checkinteger(L, -1);
        if (val<0) val=0;
        if (_stages.size()>0 && (size_t)val>=_stages.size()) val=(size_t)_stages.size()-1;
        if (_stage2 != val) {
            _stage2 = val;
            onChange.emit(this);
        }
        return true;
    } else if (strcmp(key, "MaxCount")==0) {
        int val = luaL_checkinteger(L, -1);
        if (_maxCount != val) {
            _maxCount = val;
            if (_maxCount<_count) {
                _count = _maxCount;
                onChange.emit(this);
            }
        }
        return true;
    }
    printf("Set JsonItem(%s).%s unknown\n", _name.c_str(), key);
    return false;
}

json JsonItem::save() const
{
    json data = {
        { "overlay", _overlay },
        { "state", { _stage1, _stage2 } },
        { "count", _count },
        { "max_count", _maxCount }
    };
    if (_overlayBackgroundChanged)
        data["overlay_background"] = _overlayBackground;
    if (_overlayFontSizeChanged)
        data["overlay_font_size"] = _overlayFontSize;
    return data;
}
bool JsonItem::load(json& j)
{
    if (j.type() == json::value_t::object) {
        std::string overlay = to_string(j["overlay"], _overlay);
        std::string overlayBackground = to_string(j["overlay_background"], _overlayBackground);
        int overlayFontSize = to_int(j["overlay_font_size"], _overlayFontSize);
        auto state = j["state"];
        int stage1 = _stage1, stage2 = _stage2;
        if (state.type() == json::value_t::array) {
            stage1 = to_int(state[0],stage1);
            stage2 = to_int(state[1],stage2);
        }
        int count = to_int(j["count"],_count);
        int maxCount = to_int(j["max_count"], _maxCount);
        if (_count != count || _maxCount != maxCount
                || _stage1 != stage1 || _stage2 != stage2
                || _overlay != overlay
                || _overlayBackground != overlayBackground
                || _overlayFontSize != overlayFontSize)
        {
            _count = count;
            _maxCount = maxCount;
            _stage1 = stage1;
            _stage2 = stage2;
            _overlay = overlay;
            _overlayBackgroundChanged = _overlayBackground != overlayBackground;
            _overlayBackground = overlayBackground;
            _overlayFontSizeChanged = _overlayFontSize != overlayFontSize;
            _overlayFontSize = overlayFontSize;
            onChange.emit(this);
        }
        return true;
    }
    return false;
}
