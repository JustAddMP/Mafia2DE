// freeride_test: the pieces of Free Ride (docs/PLAN.md D-FR) that run without the game:
//   - lua51: a made-up chunk (all constant types, nested functions, line info, locals, upvalue names, a source with
//     NUL bytes) written, read back and written again byte-identically; the loader's code rules (Check) catch broken
//     code;
//   - the compiler: exact instructions for small snippets, compile errors for what it does not support;
//   - script_res: a made-up Script resource written and read back, FNV-1 64 hashes;
//   - our Free Ride script: compiles, passes Check, reads back to the same functions (the listing of what the compiler
//     built equals the listing of the bytes read back), contains none of the test content's or the save's names,
//     has the expected constants, pads to an exact size; run in a tiny Lua VM (tests/mini_lua_vm.h) against a
//     recording mock of the game's API: the calls it makes, in order, for every part (season / weather preset);
//   - when the game is installed (read-only): freeraid_main.sds is read, the game's own FreerideScript chunk reads
//     and writes back byte-identically and passes Check, its stored hashes are FNV-1 64 of name and data, and it runs
//     in the same VM with the calls we know from its listing (which checks the VM, and so our script's run, against
//     real compiler output); then the archive is patched: the Script resource keeps its size and prefix word, its
//     data hash matches our chunk, every other resource is byte-identical. Leaves out\freeride\ (the patched
//     archive, our chunk and listings) for inspection.
// Exits non-zero on any failure.
#include "game/freeride.h"
#include "game/lua51.h"
#include "game/lua51_compile.h"
#include "game/script_res.h"
#include "game/sds.h"
#include "mini_lua_vm.h"

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace mc::game;
namespace L = mc::game::lua51;

namespace {

int g_failures = 0;

void Check(bool ok, const std::string& name, const std::string& detail = {}) {
    std::printf("%s  %-74s %s\n", ok ? "PASS" : "FAIL", name.c_str(), ok ? "" : detail.c_str());
    std::fflush(stdout);
    if (!ok) ++g_failures;
}

std::vector<uint8_t> ReadAll(const std::wstring& path) {
    std::ifstream in(path, std::ios::binary);
    return in ? std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>()) : std::vector<uint8_t>{};
}

void WriteAll(const std::wstring& path, const void* p, size_t n) {
    std::ofstream out(path, std::ios::binary);
    out.write(static_cast<const char*>(p), static_cast<std::streamsize>(n));
}
void WriteText(const std::wstring& path, const std::string& s) { WriteAll(path, s.data(), s.size()); }

bool Has(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }

std::string Join(const std::vector<std::string>& v) {
    std::string s;
    for (const auto& x : v) s += "    " + x + "\n";
    return s;
}

// ---- lua51 format ----

L::Const KS(const std::string& s) { L::Const c; c.type = L::Const::String; c.s = s; return c; }
L::Const KN(float n) { L::Const c; c.type = L::Const::Number; c.n = n; return c; }
L::Const KB(bool b) { L::Const c; c.type = L::Const::Bool; c.b = b; return c; }

void TestFormat() {
    L::Proto inner;
    inner.lineDefined = 3;
    inner.lastLineDefined = 5;
    inner.nups = 1;
    inner.numParams = 1;
    inner.maxStack = 3;
    inner.code = {L::ABC(L::GETUPVAL, 1, 0, 0), L::ABC(L::RETURN, 1, 2, 0), L::ABC(L::RETURN, 0, 1, 0)};
    inner.lineInfo = {4, 4, 5};
    inner.locVars = {{"self", 0, 3}};
    inner.upvalues = {"up"};
    L::Proto main;
    main.hasSource = true;
    main.source = std::string("=made up\0\0pad", 13);
    main.isVararg = 2;
    main.maxStack = 2;
    main.k = {KS("x"), KN(-21.26f), KB(true), L::Const{}};
    main.code = {L::ABx(L::LOADK, 0, 1), L::ABx(L::SETGLOBAL, 0, 0), L::ABx(L::CLOSURE, 1, 0),
                 L::ABC(L::MOVE, 0, 0, 0), L::ABC(L::RETURN, 0, 1, 0)};
    main.p = {inner};
    const auto bytes = L::Write(main);
    L::Proto back;
    std::string err;
    size_t used = 0;
    const bool ok = L::Read(bytes.data(), bytes.size(), back, &err, &used);
    Check(ok && used == bytes.size(), "lua51: a written chunk reads back", err);
    Check(back == main, "lua51: same functions, constants, debug info after the round trip");
    Check(L::Write(back) == bytes, "lua51: writing it again gives the same bytes");
    Check(std::memcmp(bytes.data(), "\x1bLuaQ\x00\x01\x04\x04\x04\x04\x00", 12) == 0, "lua51: header = Lua 5.1, little endian, 4-byte int / size_t / instruction / float");
    Check(L::Check(main).empty(), "lua51: Check accepts valid code", L::Check(main));
    Check(Has(L::List(main), "SETGLOBAL _G[\"x\"] := R0") && Has(L::List(main), "-21.2600002") &&
              Has(L::List(main), "source \"=made up\" + 5 bytes from a NUL on"),
          "lua51: the listing resolves constants and shows the source", L::List(main));

    auto broken = [&](auto mutate) {
        L::Proto p = main;
        mutate(p);
        return L::Check(p);
    };
    Check(!broken([](L::Proto& p) { p.code.back() = L::ABC(L::MOVE, 0, 0, 0); }).empty(), "lua51: Check refuses code without a final RETURN");
    Check(!broken([](L::Proto& p) { p.code[0] = L::ABx(L::LOADK, 5, 1); }).empty(), "lua51: Check refuses a register beyond the stack");
    Check(!broken([](L::Proto& p) { p.code[0] = L::ABx(L::LOADK, 0, 9); }).empty(), "lua51: Check refuses a constant out of range");
    Check(!broken([](L::Proto& p) { p.code[1] = L::ABx(L::SETGLOBAL, 0, 1); }).empty(), "lua51: Check refuses a global name that is not a string");
    Check(!broken([](L::Proto& p) { p.code[3] = L::AsBx(L::JMP, 0, 7); }).empty(), "lua51: Check refuses a jump out of the function");
    Check(!broken([](L::Proto& p) { p.code[3] = L::ABC(L::EQ, 0, 0, 0); }).empty(), "lua51: Check refuses a test not followed by JMP");
    Check(!broken([](L::Proto& p) { p.code[2] = L::ABx(L::CLOSURE, 1, 3); }).empty(), "lua51: Check refuses a closure of a missing function");
    Check(!broken([](L::Proto& p) { p.code[3] = L::ABC(L::CALL, 0, 1, 0); }).empty(), "lua51: Check refuses an open call not followed by an op that takes it");
    Check(!L::Read(bytes.data(), bytes.size() - 3, back, &err), "lua51: a truncated chunk is refused");
    std::vector<uint8_t> dbl = bytes;
    dbl[10] = 8;
    Check(!L::Read(dbl.data(), dbl.size(), back, &err), "lua51: a chunk with 8-byte numbers (stock Lua) is refused");
}

// ---- compiler ----

bool CodeIs(const std::string& src, const std::vector<uint32_t>& want, std::string& why) {
    L::Proto p;
    if (!L::Compile(src, p, &why)) return false;
    if (p.code != want) {
        why = L::List(p);
        return false;
    }
    return true;
}

void TestCompiler() {
    using namespace L;
    const auto ABC = [](int o, int a, int b, int c) { return L::ABC(o, a, b, c); };  // not the wingdi.h struct
    std::string why;
    const int k0 = kBitRK | 0, k1 = kBitRK | 1, k2 = kBitRK | 2;
    Check(CodeIs("x = 1", {ABx(LOADK, 0, 1), ABx(SETGLOBAL, 0, 0), ABC(RETURN, 0, 1, 0)}, why), "compile: global assignment", why);
    Check(CodeIs("a.b:c('s', 2)",
                 {ABx(GETGLOBAL, 0, 0), ABC(GETTABLE, 0, 0, k1), ABC(SELF, 0, 0, k2), ABx(LOADK, 2, 3), ABx(LOADK, 3, 4),
                  ABC(CALL, 0, 4, 1), ABC(RETURN, 0, 1, 0)},
                 why),
          "compile: method call statement (GETGLOBAL, GETTABLE, SELF, args, CALL)", why);
    // if / elseif / else with a comparison against a constant: EQ A=0 + JMP to the next branch, JMP to the end
    Check(CodeIs("if v == 2 then f() elseif v < 4 then g() else h() end",
                 {ABx(GETGLOBAL, 0, 0), ABC(EQ, 0, 0, k1), AsBx(JMP, 0, 3), ABx(GETGLOBAL, 0, 2), ABC(CALL, 0, 1, 1),
                  AsBx(JMP, 0, 8), ABx(GETGLOBAL, 0, 0), ABC(LT, 0, 0, kBitRK | 3), AsBx(JMP, 0, 3),
                  ABx(GETGLOBAL, 0, 4), ABC(CALL, 0, 1, 1), AsBx(JMP, 0, 2), ABx(GETGLOBAL, 0, 5), ABC(CALL, 0, 1, 1),
                  ABC(RETURN, 0, 1, 0)},
                 why),
          "compile: if / elseif / else (EQ, LT, JMP offsets)", why);
    Check(CodeIs("function f(self) return self.x:y(true) end",
                 {ABx(CLOSURE, 0, 0), ABx(SETGLOBAL, 0, 0), ABC(RETURN, 0, 1, 0)}, why),
          "compile: global function = CLOSURE + SETGLOBAL", why);
    {
        Proto p;
        Compile("function f(self) return self.x:y(true) end", p, &why);
        const std::vector<uint32_t> inner = {ABC(GETTABLE, 1, 0, k0), ABC(SELF, 1, 1, k1), ABC(LOADBOOL, 3, 1, 0),
                                             ABC(TAILCALL, 1, 3, 0), ABC(RETURN, 1, 0, 0), ABC(RETURN, 0, 1, 0)};
        Check(p.p.size() == 1 && p.p[0].code == inner && p.p[0].numParams == 1 && p.p[0].maxStack == 4,
              "compile: parameter use, method on a field, returned call = TAILCALL + RETURN A 0", p.p.empty() ? "" : List(p));
    }
    {
        Proto p;
        Compile("function f(self, msg) msg:g(self) end", p, &why);
        const std::vector<uint32_t> inner = {ABC(SELF, 2, 1, k0), ABC(MOVE, 4, 0, 0), ABC(CALL, 2, 3, 1), ABC(RETURN, 0, 1, 0)};
        Check(p.p.size() == 1 && p.p[0].code == inner, "compile: method on a local uses its register in place (SELF A B=local)",
              p.p.empty() ? why : List(p));
    }
    Check(CodeIs("t = {f, 'a'}",
                 {ABC(NEWTABLE, 0, 2, 0), ABx(GETGLOBAL, 1, 1), ABx(LOADK, 2, 2), ABC(SETLIST, 0, 2, 1),
                  ABx(SETGLOBAL, 0, 0), ABC(RETURN, 0, 1, 0)},
                 why),
          "compile: list table (NEWTABLE, items, SETLIST)", why);
    Check(CodeIs("local p = q if p then r = -1.5 end",
                 {ABx(GETGLOBAL, 0, 0), ABC(TEST, 0, 0, 0), AsBx(JMP, 0, 2), ABx(LOADK, 1, 2), ABx(SETGLOBAL, 1, 1),
                  ABC(RETURN, 0, 1, 0)},
                 why),
          "compile: local, truthiness test, negative number", why);
    Check(CodeIs("if a > b then end if a >= 1 then end",
                 {ABx(GETGLOBAL, 0, 0), ABx(GETGLOBAL, 1, 1), ABC(LT, 0, 1, 0), AsBx(JMP, 0, 0), ABx(GETGLOBAL, 0, 0),
                  ABC(LE, 0, k2, 0), AsBx(JMP, 0, 0), ABC(RETURN, 0, 1, 0)},
                 why),
          "compile: > and >= swap the operands of LT / LE", why);
    auto refuses = [](const char* src) {
        Proto p;
        std::string e;
        return !Compile(src, p, &e) && Has(e, "line ");
    };
    Check(refuses("while true do end") && refuses("x = a and b") && refuses("x = 1 + 2") && refuses("x = a .. b") &&
              refuses("t = {a = 1}") && refuses("x, y = 1, 2") && refuses("local u = 1 function f() return u end") &&
              refuses("x = (a == b)") && refuses("f(") && refuses("x = 'open"),
          "compile: unsupported constructs and syntax errors are refused with a line number");
    Proto deep;
    Check(Compile("function a(self) local x = 1 b(function(s) c(s) end) end", deep, &why) && Check(deep).empty(),
          "compile: nested function expressions pass Check", why);
}

// ---- script resources ----

void TestScriptRes() {
    script_res::Resource r;
    r.word = 0xC223BF41;
    r.prefix = "/scripts/missions/Test/";
    for (int i = 0; i < 2; ++i) {
        script_res::Script s;
        s.name = r.prefix + "Script" + std::to_string(i) + ".lua";
        s.data = {0x1B, 'L', 'u', 'a', uint8_t(i)};
        r.scripts.push_back(s);
    }
    script_res::UpdateHashes(r);
    const auto bytes = script_res::Write(r);
    script_res::Resource back;
    std::string err;
    Check(script_res::Read(bytes, back, &err), "script_res: a written resource reads back", err);
    Check(back.word == r.word && back.prefix == r.prefix && back.scripts.size() == 2 && back.scripts[1].name == r.scripts[1].name &&
              back.scripts[1].data == r.scripts[1].data && back.scripts[0].dataHash == r.scripts[0].dataHash &&
              script_res::Write(back) == bytes,
          "script_res: word, prefix, names, data and hashes kept; same bytes again");
    Check(bytes.size() == 4 + 2 + r.prefix.size() + 4 + 2 * (16 + 2 + 4 + 5) + r.scripts[0].name.size() + r.scripts[1].name.size(),
          "script_res: layout size (word, prefix, count, per script hashes / name / data)");
    Check(script_res::Fnv1_64(std::string("")) == 0xCBF29CE484222325ull && script_res::Fnv1_64(std::string("a")) == 0xAF63BD4C8601B7BEull,
          "script_res: FNV-1 64 (offset basis, one byte)");
    std::vector<uint8_t> cut(bytes.begin(), bytes.end() - 1);
    Check(!script_res::Read(cut, back, &err), "script_res: a truncated resource is refused");
}

// ---- running scripts in the mini VM ----

struct Run {
    std::vector<std::string> trace;
    std::string error;
};

// onGameInit(self) -> the DelayBuffer'd start(self) -> CommandBuffer:Insert(self, steps) -> each step(self) in
// order, with game.datastore:GetNumber("missionPart") = part. Steps' return values are traced as "-> ...".
Run RunMission(const L::Proto& chunk, unsigned part) {
    Run out;
    minivm::Vm vm;
    using minivm::Value;
    for (const char* g : {"game", "enums", "ai", "Math", "common", "base", "registerForMachineMessages", "sendMessage"})
        vm.AutoGlobal(g);
    Value delayed, delayedArgs, steps;
    vm.overrides["game.datastore.GetNumber"] = [&](const std::vector<Value>& a) {
        vm.trace.push_back("game.datastore:GetNumber(" + minivm::Show(a.size() > 1 ? a[1] : Value{}) + ")");
        return std::vector<Value>{Value::N(part)};
    };
    auto buffer = [&](const char* name) {
        Value t = minivm::NewTable();
        Value f;
        f.t = Value::Nat;
        const bool isDelay = std::strcmp(name, "DelayBuffer") == 0;
        f.nat = std::make_shared<minivm::Native>([&, isDelay, name](const std::vector<Value>& a) {
            std::string s = std::string(name) + ":Insert(";
            for (size_t i = 1; i < a.size(); ++i) s += (i > 1 ? ", " : "") + minivm::Show(a[i]);
            vm.trace.push_back(s + ")");
            if (isDelay) {
                delayed = a.size() > 1 ? a[1] : Value{};
                delayedArgs = a.size() > 2 ? a[2] : Value{};
            } else {
                steps = a.size() > 2 ? a[2] : Value{};
            }
            return std::vector<Value>{Value::N(7)};
        });
        t.tab->h["Insert"] = f;
        vm.globals[name] = t;
    };
    buffer("DelayBuffer");
    buffer("CommandBuffer");
    try {
        vm.Run0(chunk);
        Value self = minivm::NewTable(true, "self");
        vm.Call(vm.globals["onGameInit"], {self});
        if (delayed.t == Value::Nil) throw std::runtime_error("onGameInit did not schedule the start");
        std::vector<Value> args;
        if (delayedArgs.t == Value::Tab)
            for (auto& [k, v] : delayedArgs.tab->a) args.push_back(v);
        vm.Call(delayed, args);
        if (steps.t != Value::Tab) throw std::runtime_error("the start did not insert command buffer steps");
        for (auto& [k, step] : steps.tab->a) {
            const auto res = vm.Call(step, {self});
            if (!res.empty() && res[0].t != Value::Nil) vm.trace.push_back("-> " + minivm::Show(res[0]));
        }
    } catch (const std::exception& e) {
        out.error = e.what();
    }
    out.trace = vm.trace;
    return out;
}

bool AnyHas(const std::vector<std::string>& v, const std::string& needle) {
    for (const auto& s : v)
        if (Has(s, needle)) return true;
    return false;
}

std::vector<std::string> ExpectedTrace(unsigned part) {
    const freeride::Preset* p = freeride::PresetOf(part);
    if (!p) p = freeride::PresetOf(freeride::kDefaultPart);
    const bool winter = p->winter;
    const char* radio = winter ? "_02004" : "_07010";
    const char* season = winter ? "50" : "140";
    const std::string player = "game.game:GetActivePlayer()";
    std::vector<std::string> t = {
        "registerForMachineMessages(self)",
        "DelayBuffer:Insert(function, {table}, 100, 1, false)",
        "game.datastore:SetString(\"DerekStatus\", \"Opened\")",
        "game.datastore:SetString(\"CharlieStatus\", \"Opened\")",
        "game.datastore:SetString(\"BruskyStatus\", \"Opened\")",
        "base.ScriptMachine:getScriptGuid(self.scriptId)",
        "CommandBuffer:Insert(self, {table})",
        "game.datastore:GetNumber(\"missionPart\")",
        std::string("game.sds:ActivateStreamMapLine(\"") + (winter ? "free_winter_load" : "free_summer_load") + "\")",
        std::string("-> game.sds:ActivateStreamMapLine(\"") + (winter ? "free_winter_load" : "free_summer_load") + "\")",
        std::string("game.gfx:SetWeatherTemplate(\"") + p->dayPreset + "\")",
        player,
        "Math:newVector()",
        "Math:newVector():set(129.164, 917.259, -21.26)",
        player + ":SetPos(Math:newVector())",
        "game.sds:ActivateStreamMapLine(\"free_joe_load\")",
        "-> game.sds:ActivateStreamMapLine(\"free_joe_load\")",
        "game.sds:GetSyncObjectForLoadSDS(\"italy\")",
        "-> game.sds:GetSyncObjectForLoadSDS(\"italy\")",
        std::string("game.traffic:OpenSeason(") + season + ")",
        std::string("game.speech:LoadStage(") + season + ")",
        std::string("game.radio:SetContent(\"Empire\", \"all\", \"Empire") + radio + "\")",
        std::string("game.radio:SetContent(\"Classic\", \"all\", \"Classic") + radio + "\")",
        std::string("game.radio:SetContent(\"Delta\", \"all\", \"Delta") + radio + "\")",
        "game.police:SetDensity(0.1)",
        "game.traffic:SetPolice(true)",
        player + ":SetControlStyle(enums.ControlStyle.FREE)",
        "game.game:SoundFadeIn(1000)",
        "game.hud:FaderFadeIn(1000)",
        "-> game.hud:FaderFadeIn(1000)",
    };
    return t;
}

const char* const kForbidden[] = {"SaveGame", "GetSaveGameSyncObject", "CheatAddCar", "GetEntityByName", "Joe", "Henry",
                                  "Hunter", "Escaper", "ChangePlayerModel", "AddWingman", "Follow", "SetRelationship",
                                  "InventoryAddWeapon", "MakeCarOwnership", "HuntCar", "EscapeMode", "CreateScriptActorAction",
                                  "sendMessage", "Activate"};

void TestOurScript(const std::wstring& outDir) {
    L::Proto ours;
    std::string err;
    Check(freeride::Compile(ours, &err), "script: our Free Ride script compiles", err);
    if (!err.empty()) {
        std::printf("%s\n", freeride::Source().c_str());
        return;
    }
    Check(L::Check(ours).empty(), "script: it passes the loader's code rules", L::Check(ours));
    const auto bytes = L::Write(ours);
    L::Proto back;
    Check(L::Read(bytes.data(), bytes.size(), back, &err) && back == ours, "script: the written chunk reads back to the same functions", err);
    Check(L::List(back) == L::List(ours), "script: listing of the bytes read back = listing of what the compiler built");
    const auto strings = L::Strings(ours);
    std::string bad;
    for (const auto& s : strings)
        for (const char* f : kForbidden)
            if (s == f || Has(s, "SaveGame") || Has(s, "CheatAddCar")) bad += s + " ";
    Check(bad.empty(), "script: no SaveGame, no test characters / cars / garage fill / relationship names", bad);
    auto hasString = [&](const char* s) {
        for (const auto& x : strings)
            if (x == s) return true;
        return false;
    };
    bool all = true;
    for (const char* s : {"onGameInit", "onEvent", "free_summer_load", "free_winter_load", "free_joe_load", "italy",
                          "OpenSeason", "LoadStage", "SetContent", "SetDensity", "SetPolice", "SetControlStyle", "FREE",
                          "SoundFadeIn", "FaderFadeIn", "missionPart", "registerForMachineMessages", "getScriptGuid"})
        all = all && hasString(s);
    for (const auto& p : freeride::Presets()) all = all && hasString(p.dayPreset);
    Check(all, "script: the stream lines, the six day presets and the city-life calls are in it");
    // the start position constants, exactly as floats
    bool pos[3] = {};
    std::function<void(const L::Proto&)> scan = [&](const L::Proto& f) {
        for (const auto& c : f.k)
            if (c.type == L::Const::Number) {
                pos[0] |= c.n == freeride::kStart.x;
                pos[1] |= c.n == freeride::kStart.y;
                pos[2] |= c.n == freeride::kStart.z;
            }
        for (const auto& p : f.p) scan(p);
    };
    scan(ours);
    Check(pos[0] && pos[1] && pos[2], "script: our start position constants (4-byte floats)");
    Check(ours.p.size() == 3 && ours.p[2].p.size() == 5, "script: onGameInit, onEvent, FreeRideStart with its 5 steps");

    // run it
    for (unsigned part = 0; part <= 7; ++part) {
        const Run r = RunMission(ours, part);
        const auto want = ExpectedTrace(part);
        Check(r.error.empty() && r.trace == want,
              "script run, part " + std::to_string(part) + " (" + (freeride::PresetOf(part) ? freeride::PresetOf(part)->label : "unknown -> default") + "): the expected calls in order",
              r.error + "\n  got:\n" + Join(r.trace) + "  want:\n" + Join(want));
    }
    {   // a second start (onEvent message 1 after the DelayBuffer start) does nothing
        minivm::Vm vm;
        using minivm::Value;
        for (const char* g : {"game", "enums", "registerForMachineMessages", "base", "DelayBuffer", "CommandBuffer"}) vm.AutoGlobal(g);
        vm.Run0(ours);
        Value self = minivm::NewTable(true, "self");
        Value msg = minivm::NewTable();
        Value getType, getMsg;
        getType.t = getMsg.t = Value::Nat;
        Value base = vm.globals["enums"];
        getType.nat = std::make_shared<minivm::Native>([&](const std::vector<Value>&) {
            Value et = minivm::NewTable();
            (void)et;
            // enums.EventType.BASEMESSAGE as the auto table the script will look up
            Value v = vm.Index0(vm.Index0(vm.globals["enums"], "EventType"), "BASEMESSAGE");
            return std::vector<Value>{v};
        });
        getMsg.nat = std::make_shared<minivm::Native>([](const std::vector<Value>&) { return std::vector<Value>{Value::N(1)}; });
        msg.tab->h["GetType"] = getType;
        msg.tab->h["GetMessage"] = getMsg;
        std::string e;
        try {
            vm.Call(vm.globals["onEvent"], {self, msg});
            const size_t n = vm.trace.size();
            vm.Call(vm.globals["onEvent"], {self, msg});
            Check(n == 1 && Has(vm.trace[0], "CommandBuffer:Insert(self, {table})") && vm.trace.size() == 1,
                  "script: onEvent message 1 starts free ride once; a second start does nothing", Join(vm.trace));
        } catch (const std::exception& x) {
            Check(false, "script: onEvent", x.what());
        }
    }

    // padding to an exact size
    std::vector<uint8_t> chunk;
    const size_t bare = bytes.size();
    bool sizesOk = true;
    for (size_t size : {bare, bare + 1, bare + 2, bare + 25, size_t(13417)}) {
        sizesOk = sizesOk && freeride::BuildChunk(size, chunk, &err) && chunk.size() == size;
        L::Proto p;
        sizesOk = sizesOk && L::Read(chunk.data(), chunk.size(), p, &err) && L::Check(p).empty();
        p.hasSource = false;
        p.source.clear();
        sizesOk = sizesOk && L::Write(p) == bytes;
    }
    Check(sizesOk, "script: padded to exact sizes (bare, +1, +2, +25, 13417) by the source name; reads back, same code", err);
    Check(!freeride::BuildChunk(bare - 1, chunk, &err), "script: a size smaller than the script is refused");
    freeride::BuildChunk(13417, chunk, &err);
    WriteAll(outDir + L"\\FreerideScript.ours.luac", chunk.data(), chunk.size());
    WriteText(outDir + L"\\FreerideScript.ours.lst", L::List(ours));
    WriteText(outDir + L"\\FreerideScript.ours.lua", freeride::Source());
}

std::wstring FindGameDir() {
    wchar_t env[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"M2_GAME_DIR", env, MAX_PATH)) return env;
    for (const wchar_t* p : {L"G:\\SteamLibrary\\steamapps\\common\\Mafia II Definitive Edition",
                             L"C:\\Program Files (x86)\\Steam\\steamapps\\common\\Mafia II Definitive Edition",
                             L"D:\\SteamLibrary\\steamapps\\common\\Mafia II Definitive Edition"}) {
        if (GetFileAttributesW((std::wstring(p) + L"\\pc\\sds\\missionscript\\freeraid_main.sds").c_str()) != INVALID_FILE_ATTRIBUTES)
            return p;
    }
    return {};
}

void TestGameFile(const std::wstring& outDir) {
    const std::wstring game = FindGameDir();
    if (game.empty()) {
        std::printf("SKIP  the game's freeraid_main.sds (set M2_GAME_DIR to the Mafia II Definitive Edition folder)\n");
        return;
    }
    const auto raw = ReadAll(game + L"\\pc\\sds\\missionscript\\freeraid_main.sds");
    sds::Archive a, orig;
    std::string err;
    const int before = g_failures;
    Check(sds::Read(raw, a, &err) && sds::Read(raw, orig, &err), "game: freeraid_main.sds reads", err);
    if (g_failures != before) return;
    const sds::Resource* sr = nullptr;
    for (const auto& r : orig.resources)
        if (r.typeName == "Script") sr = &r;
    script_res::Resource res;
    Check(sr && script_res::Read(sr->data, res, &err) && res.scripts.size() == 1 && res.scripts[0].name == freeride::kScriptName &&
              res.prefix == "/scripts/missions/FreerideMain/",
          "game: one Script resource with /scripts/missions/FreerideMain/FreerideScript.lua", err);
    if (g_failures != before) return;
    const auto& s = res.scripts[0];
    Check(s.nameHash == script_res::Fnv1_64(s.name) && s.dataHash == script_res::Fnv1_64(s.data.data(), s.data.size()),
          "game: its stored hashes are FNV-1 64 of the name and of the data");
    Check(script_res::Write(res) == sr->data, "game: the Script resource writes back byte-identically");
    L::Proto theirs;
    Check(L::Read(s.data.data(), s.data.size(), theirs, &err) && L::Write(theirs) == s.data,
          "game: the game's FreerideScript chunk reads and writes back byte-identically", err);
    Check(L::Check(theirs).empty(), "game: the game's chunk passes our Check (the loader's rules)", L::Check(theirs));
    WriteText(outDir + L"\\FreerideScript.game.lst", L::List(theirs));

    // the game's own script in the VM: what its listing says it does (checks the VM against real compiler output)
    const Run r = RunMission(theirs, 5);
    const bool saves = AnyHas(r.trace, "game.game:SaveGame(1)"), cars = AnyHas(r.trace, "CheatAddCar(\"Moscardo_AS37_pha\", true)"),
               joe = AnyHas(r.trace, "GetEntityByName(\"Joe\")"), winter = AnyHas(r.trace, "ActivateStreamMapLine(\"free_winter_load\")"),
               snow = AnyHas(r.trace, "SetWeatherTemplate(\"DTFreeRideDaySnow\")") && !AnyHas(r.trace, "\"DTFreeRideDay\")"),
               season = AnyHas(r.trace, "game.traffic:OpenSeason(50)"), fade = AnyHas(r.trace, "-> game.hud:FaderFadeIn(1000)");
    Check(r.error.empty() && saves && cars && joe && winter && snow && season && fade,
          "game: its script run (part 5) saves, fills the garage, uses Joe, winter + snow, season 50, fades in",
          r.error + "\n" + Join(r.trace));
    const Run r1 = RunMission(theirs, 1);
    Check(r1.error.empty() && AnyHas(r1.trace, "free_summer_load") && AnyHas(r1.trace, "SetWeatherTemplate(\"DTFreeRideDay\")") &&
              AnyHas(r1.trace, "OpenSeason(140)") && AnyHas(r1.trace, "Empire_07010"),
          "game: its script run (part 1) is summer day, season 140", r1.error);

    // patch the archive
    const auto pr = freeride::PatchArchive(a);
    for (const auto& l : pr.log) std::printf("      freeride: %s\n", l.c_str());
    Check(pr.ok, "game: freeraid_main.sds patched");
    if (!pr.ok) return;
    const auto out = sds::Write(a);
    sds::Archive back;
    Check(sds::Read(out, back, &err) && back.resources.size() == orig.resources.size() && back.index == orig.index,
          "game: the patched archive reads back (same resources, same index)", err);
    WriteAll(outDir + L"\\freeraid_main.sds", out.data(), out.size());
    WriteText(outDir + L"\\freeraid_main.patch.txt", [&] {
        std::string t;
        for (const auto& l : pr.log) t += "# " + l + "\n";
        return t + "\n" + pr.listing;
    }());
    bool othersSame = true, scriptShape = false;
    for (size_t i = 0; i < orig.resources.size() && i < back.resources.size(); ++i) {
        const auto& o = orig.resources[i];
        const auto& b = back.resources[i];
        if (o.typeName == "Script") {
            script_res::Resource nr;
            std::vector<uint8_t> want;
            freeride::BuildChunk(s.data.size(), want, &err);
            scriptShape = b.data.size() == o.data.size() && std::memcmp(b.data.data(), o.data.data(), 4) == 0 &&
                          script_res::Read(b.data, nr, &err) && nr.scripts.size() == 1 && nr.prefix == res.prefix &&
                          nr.word == res.word && nr.scripts[0].name == s.name && nr.scripts[0].nameHash == s.nameHash &&
                          nr.scripts[0].data == want && nr.scripts[0].data.size() == s.data.size() &&
                          nr.scripts[0].dataHash == script_res::Fnv1_64(want.data(), want.size()) && b.ram[0] == o.ram[0] &&
                          b.typeId == o.typeId && b.version == o.version;
        } else {
            othersSame = othersSame && o.data == b.data && o.typeId == b.typeId && o.version == b.version &&
                         std::memcmp(o.ram, b.ram, sizeof o.ram) == 0;
        }
    }
    Check(scriptShape, "game: Script resource: same size, prefix word, path, name and name hash; our chunk; new data hash");
    Check(othersSame, "game: every other resource (frames, actors, scriptname.txt) is byte-identical");
}

}  // namespace

int main(int argc, char** argv) {
    std::wstring outDir = L"freeride";
    wchar_t exe[MAX_PATH];
    if (GetModuleFileNameW(nullptr, exe, MAX_PATH)) {
        std::wstring e = exe;
        outDir = e.substr(0, e.find_last_of(L'\\')) + L"\\freeride";
    }
    CreateDirectoryW(outDir.c_str(), nullptr);
    TestFormat();
    TestCompiler();
    TestScriptRes();
    TestOurScript(outDir);
    if (argc > 1 && std::strcmp(argv[1], "--no-game") == 0)
        std::printf("SKIP  the game's freeraid_main.sds (--no-game)\n");
    else
        TestGameFile(outDir);
    std::printf("%s (%d failure%s)\n", g_failures ? "FAILED" : "ALL PASSED", g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
