#pragma once
// Lua 5.1 binary chunks in the format Mafia II: Definitive Edition's script VM loads (docs/re-notes.md 5): the
// stock Lua 5.1 layout ("\x1bLua", version 0x51, format 0, little endian, int 4, size_t 4, Instruction 4) except
// that lua_Number is a 4-byte float (header byte 4, integral flag 0) instead of a double. Read, write (a chunk read
// and written again is byte-identical), list (a disassembly for logs and tests) and check (the static rules the
// Lua 5.1 loader applies to code before it runs it). Written from the public Lua 5.1 bytecode format; the game's own
// chunks (pc\sds\missionscript\*.sds) were used to confirm it.
#include <cstdint>
#include <string>
#include <vector>

namespace mc::game::lua51 {

enum Op : uint8_t {
    MOVE, LOADK, LOADBOOL, LOADNIL, GETUPVAL, GETGLOBAL, GETTABLE, SETGLOBAL, SETUPVAL, SETTABLE, NEWTABLE, SELF,
    ADD, SUB, MUL, DIV, MOD, POW, UNM, NOT, LEN, CONCAT, JMP, EQ, LT, LE, TEST, TESTSET, CALL, TAILCALL, RETURN,
    FORLOOP, FORPREP, TFORLOOP, SETLIST, CLOSE, CLOSURE, VARARG,
    kNumOps
};
const char* OpName(int op);

// Instruction fields (Lua 5.1: op 6 bits, A 8, C 9, B 9; Bx = B:C 18 bits; sBx = Bx - 131071).
constexpr int kBitRK = 1 << 8;  // RK operand: constant index | kBitRK
constexpr int kMaxIndexRK = kBitRK - 1;
constexpr int kMaxArgBx = (1 << 18) - 1;
constexpr int kMaxArgSBx = kMaxArgBx >> 1;
inline uint32_t ABC(int op, int a, int b, int c) {
    return uint32_t(op) | (uint32_t(a) << 6) | (uint32_t(c) << 14) | (uint32_t(b) << 23);
}
inline uint32_t ABx(int op, int a, int bx) { return uint32_t(op) | (uint32_t(a) << 6) | (uint32_t(bx) << 14); }
inline uint32_t AsBx(int op, int a, int sbx) { return ABx(op, a, sbx + kMaxArgSBx); }
inline int OpOf(uint32_t i) { return int(i & 0x3F); }
inline int A(uint32_t i) { return int((i >> 6) & 0xFF); }
inline int B(uint32_t i) { return int((i >> 23) & 0x1FF); }
inline int C(uint32_t i) { return int((i >> 14) & 0x1FF); }
inline int Bx(uint32_t i) { return int(i >> 14); }
inline int SBx(uint32_t i) { return Bx(i) - kMaxArgSBx; }

struct Const {
    enum Type : uint8_t { Nil = 0, Bool = 1, Number = 3, String = 4 };
    Type type = Nil;
    bool b = false;
    float n = 0;
    std::string s;
    bool operator==(const Const& o) const;
};

struct LocVar {
    std::string name;
    int32_t startPc = 0, endPc = 0;
    bool operator==(const LocVar& o) const = default;
};

struct Proto {
    bool hasSource = false;  // false: no source string (the game's chunks; nested functions inherit the parent's)
    std::string source;      // may contain NUL bytes
    int32_t lineDefined = 0, lastLineDefined = 0;
    uint8_t nups = 0, numParams = 0, isVararg = 0, maxStack = 2;
    std::vector<uint32_t> code;
    std::vector<Const> k;
    std::vector<Proto> p;
    std::vector<int32_t> lineInfo;  // empty or one line per instruction
    std::vector<LocVar> locVars;
    std::vector<std::string> upvalues;
    bool operator==(const Proto& o) const = default;
};

// The 12-byte header of the game's chunks.
const std::vector<uint8_t>& Header();

// Parses one chunk (header + main function). `used` (optional) gets the bytes consumed.
bool Read(const uint8_t* data, size_t size, Proto& main, std::string* error, size_t* used = nullptr);
std::vector<uint8_t> Write(const Proto& main);

// A readable disassembly: per function its header line, then one line per instruction with constants resolved.
std::string List(const Proto& main);

// The rules the Lua 5.1 loader checks before running code (lundump -> luaG_checkcode / symbexec): register and
// constant ranges, jump targets, a JMP after every test, open calls followed by an op that takes them, CLOSURE
// targets, global names that are strings, a final RETURN, stack and line-info sizes. "" = fine, else the first
// problem ("function 4/2 pc 3: ...").
std::string Check(const Proto& main);

// Every string constant of the chunk, in function order (tests: what a script can reach).
std::vector<std::string> Strings(const Proto& main);

}  // namespace mc::game::lua51
