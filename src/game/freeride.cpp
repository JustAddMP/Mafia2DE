#include "game/freeride.h"

#include "game/lua51_compile.h"
#include "game/script_res.h"

#include <algorithm>
#include <cstdio>

namespace mc::game::freeride {
namespace {

// Our free ride script. Entry points as in every mission script of the game: onGameInit(self) when the mission's
// script machine starts, onEvent(self, msg) for messages. The start runs as a CommandBuffer sequence (the game's
// script helper: each step may return a sync object the buffer waits for), like the game's FreerideScript.lua:
//   1 the part's season and day preset (the six presets of pc\sds\skies\freeride.sds; any other part: the
//     default), then the season's city (stream map line free_summer_load / free_winter_load of the "Free Raid" game)
//   2 the weather: that day preset
//   3 the player to our start position; the stream line free_joe_load (in the game's script it loads the city part
//     around its start, Little Italy, together with its test characters, which we never activate)
//   4 wait for the city part "italy" (pc\sds\city\italy.sds) to be loaded
//   5 city life: traffic season and speech stage (140 summer / 50 winter), the three radio stations' content, police
//     on (density 0.1, as the game's chapter scripts set it), then control to the player and fade in.
// Left out on purpose: SaveGame (the game's script saves into save slot 1 - a story save - before it starts), the
// test characters Joe and Henry (wingmen, suit changes), the Hunter / Escaper car chase, the garage filled with
// every car model, the relationship change towards the Clemente gang.
// Placeholders: @WEATHER@ (part -> season and day preset), @X@ @Y@ @Z@ (start position).
const char kTemplate[] = R"lua(-- JustAddMP Free Ride for Mafia II: Definitive Edition (replaces /missions/FreerideMain).
-- Single player: the base game's free ride mission without its test content and without saving.

function onGameInit(self)
  registerForMachineMessages(self)
  DelayBuffer:Insert(FreeRideStart, {self}, 100, 1, false)
  game.datastore:SetString("DerekStatus", "Opened")
  game.datastore:SetString("CharlieStatus", "Opened")
  game.datastore:SetString("BruskyStatus", "Opened")
  guid = base.ScriptMachine:getScriptGuid(self.scriptId)
end

function onEvent(self, msg)
  if msg:GetType() == enums.EventType.BASEMESSAGE then
    if msg:GetMessage() == 1 then
      FreeRideStart(self)
    end
  end
end

function FreeRideStart(self)
  if freeRideStarted == true then
    return
  end
  freeRideStarted = true
  id = CommandBuffer:Insert(self, {
    function(self)
      local part = game.datastore:GetNumber("missionPart")
      season = "summer"
@WEATHER@
      if season == "winter" then
        return game.sds:ActivateStreamMapLine("free_winter_load")
      end
      return game.sds:ActivateStreamMapLine("free_summer_load")
    end,
    function(self)
      game.gfx:SetWeatherTemplate(freeRideWeather)
    end,
    function(self)
      player = game.game:GetActivePlayer()
      startPos = Math:newVector()
      startPos:set(@X@, @Y@, @Z@)
      player:SetPos(startPos)
      return game.sds:ActivateStreamMapLine("free_joe_load")
    end,
    function(self)
      return game.sds:GetSyncObjectForLoadSDS("italy")
    end,
    function(self)
      if season == "winter" then
        game.traffic:OpenSeason(50)
        game.speech:LoadStage(50)
        game.radio:SetContent("Empire", "all", "Empire_02004")
        game.radio:SetContent("Classic", "all", "Classic_02004")
        game.radio:SetContent("Delta", "all", "Delta_02004")
      else
        game.traffic:OpenSeason(140)
        game.speech:LoadStage(140)
        game.radio:SetContent("Empire", "all", "Empire_07010")
        game.radio:SetContent("Classic", "all", "Classic_07010")
        game.radio:SetContent("Delta", "all", "Delta_07010")
      end
      game.police:SetDensity(0.1)
      game.traffic:SetPolice(true)
      player:SetControlStyle(enums.ControlStyle.FREE)
      game.game:SoundFadeIn(1000)
      return game.hud:FaderFadeIn(1000)
    end,
  })
end
)lua";

const char kSourceName[] = "=JustAddMP FreeRide";

void Replace(std::string& s, const std::string& from, const std::string& to) {
    for (size_t p = s.find(from); p != std::string::npos; p = s.find(from, p + to.size())) s.replace(p, from.size(), to);
}

std::string Num(float v) {
    char b[32];
    std::snprintf(b, sizeof b, "%.3f", double(v));
    return b;
}

size_t CountFunctions(const lua51::Proto& f) {
    size_t n = 1;
    for (const auto& c : f.p) n += CountFunctions(c);
    return n;
}

size_t CountCode(const lua51::Proto& f) {
    size_t n = f.code.size();
    for (const auto& c : f.p) n += CountCode(c);
    return n;
}

std::string Hex64(uint64_t v) {
    char b[24];
    std::snprintf(b, sizeof b, "%016llX", static_cast<unsigned long long>(v));
    return b;
}

}  // namespace

const std::vector<Preset>& Presets() {
    static const std::vector<Preset> k = {
        {1, "SUMMER DAY", "DTFreeRideDay", false},       {2, "SUMMER RAIN", "DTFreeRideDayRain", false},
        {3, "SUMMER NIGHT", "DTFreerideNight", false},   {4, "WINTER DAY", "DTFreeRideDayWinter", true},
        {5, "WINTER SNOW", "DTFreeRideDaySnow", true},   {6, "WINTER NIGHT", "DTFreeRideNightSnow", true}};
    return k;
}

const Preset* PresetOf(unsigned part) {
    for (const auto& p : Presets())
        if (p.part == part) return &p;
    return nullptr;
}

std::string Source() {
    std::string s = kTemplate;
    // part -> season and day preset; any other part (0, 7, ...) gets the default preset (a summer one)
    const Preset* fallback = PresetOf(kDefaultPart);
    std::string weather = std::string("      freeRideWeather = \"") + fallback->dayPreset + "\"\n";
    bool first = true;
    for (const auto& p : Presets()) {
        if (&p == fallback) continue;
        weather += std::string(first ? "      if" : "      elseif") + " part == " + std::to_string(p.part) + " then\n";
        if (p.winter) weather += "        season = \"winter\"\n";
        weather += std::string("        freeRideWeather = \"") + p.dayPreset + "\"\n";
        first = false;
    }
    weather += "      end";
    Replace(s, "@WEATHER@", weather);
    Replace(s, "@X@", Num(kStart.x));
    Replace(s, "@Y@", Num(kStart.y));
    Replace(s, "@Z@", Num(kStart.z));
    return s;
}

bool Compile(lua51::Proto& out, std::string* error) { return lua51::Compile(Source(), out, error); }

bool BuildChunk(size_t size, std::vector<uint8_t>& out, std::string* error) {
    lua51::Proto main;
    if (!Compile(main, error)) return false;
    main.hasSource = false;
    main.source.clear();
    const size_t bare = lua51::Write(main).size();
    if (bare > size) {
        if (error) *error = "our script (" + std::to_string(bare) + " bytes) is bigger than the game's (" + std::to_string(size) + ")";
        return false;
    }
    if (bare < size) {  // a source string costs 4 + length + 1 bytes against 4 for none
        main.hasSource = true;
        main.source.assign(kSourceName, std::min(sizeof kSourceName - 1, size - bare - 1));
        main.source.resize(size - bare - 1, '\0');
    }
    out = lua51::Write(main);
    if (out.size() != size) {
        if (error) *error = "internal: padded chunk is " + std::to_string(out.size()) + " bytes, not " + std::to_string(size);
        return false;
    }
    return true;
}

Result PatchArchive(sds::Archive& a) {
    Result res;
    auto log = [&](const std::string& s) { res.log.push_back(s); };
    for (auto& r : a.resources) {
        if (r.typeName != "Script") continue;
        script_res::Resource sr;
        std::string err;
        if (!script_res::Read(r.data, sr, &err)) {
            log("FAILED: script resource: " + err);
            return res;
        }
        for (auto& s : sr.scripts) {
            if (s.name != kScriptName) continue;
            lua51::Proto original;
            if (!lua51::Read(s.data.data(), s.data.size(), original, &err)) {
                log("FAILED: the game's " + s.name + " is not a chunk we understand: " + err);
                return res;
            }
            std::vector<uint8_t> chunk;
            if (!BuildChunk(s.data.size(), chunk, &err)) {
                log("FAILED: " + err);
                return res;
            }
            const uint64_t oldHash = s.dataHash, oldName = s.nameHash;
            const size_t oldSize = r.data.size();
            s.data = chunk;
            script_res::UpdateHashes(sr);
            if (s.nameHash != oldName) log("note: the stored name hash " + Hex64(oldName) + " is not FNV-1 64 of the name; kept");
            if (s.nameHash != oldName) s.nameHash = oldName;
            std::vector<uint8_t> data = script_res::Write(sr);
            if (data.size() != oldSize) {
                log("FAILED: internal: the script resource would change size (" + std::to_string(data.size()) + " vs " + std::to_string(oldSize) + ")");
                return res;
            }
            r.data = std::move(data);
            lua51::Proto ours;
            lua51::Read(chunk.data(), chunk.size(), ours, &err);
            res.listing = lua51::List(ours);
            log(s.name + ": our script (" + std::to_string(CountFunctions(ours)) + " functions, " +
                std::to_string(CountCode(ours)) + " instructions, padded to " + std::to_string(chunk.size()) +
                " bytes) replaces the game's (" + std::to_string(CountFunctions(original)) + " functions, " +
                std::to_string(CountCode(original)) + " instructions); data hash " + Hex64(oldHash) + " -> " + Hex64(s.dataHash) + "; resource " +
                std::to_string(oldSize) + " bytes and prefix word kept");
            res.ok = true;
            return res;
        }
    }
    log(std::string("FAILED: no Script resource with ") + kScriptName);
    return res;
}

}  // namespace mc::game::freeride
