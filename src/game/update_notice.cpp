#include "game/update_notice.h"

#include "core/log.h"
#include "core/update_check.h"
#include "game/game_info.h"

#include <windows.h>

#include <cctype>
#include <mutex>

namespace mc::game::update_notice {
namespace {

std::string EnvValue(const char* key) {
    const std::string name = std::string(GetGameInfo().envPrefix) + key;
    char buf[512];
    const DWORD n = GetEnvironmentVariableA(name.c_str(), buf, sizeof buf);
    return n > 0 && n < sizeof buf ? std::string(buf, n) : std::string();
}

std::mutex _lock;
bool _asked = false;
std::string _line;

}  // namespace

std::string MenuLine(std::string_view latest) {
    if (!core::update::ParseVersion(latest, nullptr)) return {};
    std::string v = core::update::Display(latest);
    if (v.size() > 24) return {};
    for (char& c : v) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return "UPDATE AVAILABLE: " + v + " AT JUSTADDMP.COM";
}

void Start() {
    const GameInfo& gi = GetGameInfo();
    std::string current = gi.modVersion;
    if (std::string as = EnvValue("UPDATE_AS"); !as.empty()) {  // testing: pretend to be another version
        MC_LOG_INFO("update check: checking as version %s (%sUPDATE_AS)", as.c_str(), gi.envPrefix);
        current = std::move(as);
    }
    std::string url = std::string(core::update::kDefaultUrl) + gi.updateId;
    if (std::string other = EnvValue("UPDATE_URL"); !other.empty()) url = std::move(other);
    MC_LOG_INFO("update check: asking %s (this is %s)", url.c_str(), core::update::Display(current).c_str());
    core::update::Start(url, current);
}

std::string Line(unsigned waitMs) {
    std::lock_guard<std::mutex> g(_lock);
    if (_asked) return _line;
    _asked = true;
    const ULONGLONG t0 = GetTickCount64();
    std::string latest;
    core::update::State st = core::update::Poll(&latest);
    while (st == core::update::State::Running && GetTickCount64() - t0 < waitMs) {
        Sleep(20);
        st = core::update::Poll(&latest);
    }
    const unsigned long long waited = GetTickCount64() - t0;
    if (st == core::update::State::Running) {
        MC_LOG_INFO("update check: no answer after waiting %llu ms while the game loads its menus; no notice this time",
                    waited);
    } else if (st == core::update::State::NewerAvailable) {
        _line = MenuLine(latest);
        if (_line.empty())
            MC_LOG_INFO("update check: %s cannot be shown in the menus; no notice", latest.c_str());
        else
            MC_LOG_INFO("update check: the CO-OP pages show \"%s\" (waited %llu ms)", _line.c_str(), waited);
    }
    return _line;
}

}  // namespace mc::game::update_notice
