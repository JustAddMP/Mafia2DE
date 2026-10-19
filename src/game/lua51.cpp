#include "game/lua51.h"

#include <cstdio>
#include <cstring>

namespace mc::game::lua51 {
namespace {

const char* const kOpNames[kNumOps] = {
    "MOVE", "LOADK", "LOADBOOL", "LOADNIL", "GETUPVAL", "GETGLOBAL", "GETTABLE", "SETGLOBAL", "SETUPVAL", "SETTABLE",
    "NEWTABLE", "SELF", "ADD", "SUB", "MUL", "DIV", "MOD", "POW", "UNM", "NOT", "LEN", "CONCAT", "JMP", "EQ", "LT",
    "LE", "TEST", "TESTSET", "CALL", "TAILCALL", "RETURN", "FORLOOP", "FORPREP", "TFORLOOP", "SETLIST", "CLOSE",
    "CLOSURE", "VARARG"};

// Operand modes of the Lua 5.1 opcodes (the public lopcodes.c table): T = a test (the next instruction must be a
// JMP), A = sets register A, B / C argument kinds, instruction format.
enum ArgMode : uint8_t { N, U, R, K };  // not used, used, register / jump offset, register or constant
enum Format : uint8_t { iABC, iABx, iAsBx };
struct Mode {
    bool test, setsA;
    ArgMode b, c;
    Format fmt;
};
const Mode kModes[kNumOps] = {
    {0, 1, R, N, iABC},  {0, 1, K, N, iABx},  {0, 1, U, U, iABC},  {0, 1, R, N, iABC},  {0, 1, U, N, iABC},
    {0, 1, K, N, iABx},  {0, 1, R, K, iABC},  {0, 0, K, N, iABx},  {0, 0, U, N, iABC},  {0, 0, K, K, iABC},
    {0, 1, U, U, iABC},  {0, 1, R, K, iABC},  {0, 1, K, K, iABC},  {0, 1, K, K, iABC},  {0, 1, K, K, iABC},
    {0, 1, K, K, iABC},  {0, 1, K, K, iABC},  {0, 1, K, K, iABC},  {0, 1, R, N, iABC},  {0, 1, R, N, iABC},
    {0, 1, R, N, iABC},  {0, 1, R, R, iABC},  {0, 0, R, N, iAsBx}, {1, 0, K, K, iABC},  {1, 0, K, K, iABC},
    {1, 0, K, K, iABC},  {1, 1, R, U, iABC},  {1, 1, R, U, iABC},  {0, 1, U, U, iABC},  {0, 1, U, U, iABC},
    {0, 0, U, N, iABC},  {0, 1, R, N, iAsBx}, {0, 1, R, N, iAsBx}, {1, 0, N, U, iABC},  {0, 0, U, U, iABC},
    {0, 0, N, N, iABC},  {0, 1, U, N, iABx},  {0, 1, U, N, iABC}};

constexpr int kMaxStack = 250;      // MAXSTACK
constexpr int kVarargHasArg = 1;    // VARARG_HASARG
constexpr int kVarargIsVararg = 2;  // VARARG_ISVARARG
constexpr int kVarargNeedsArg = 4;  // VARARG_NEEDSARG

// ---- reading ----

struct In {
    const uint8_t* p;
    size_t n, pos = 0;
    bool ok = true;
    bool Need(size_t k) {
        if (!ok || pos + k > n) ok = false;
        return ok;
    }
    uint8_t U8() { return Need(1) ? p[pos++] : 0; }
    int32_t I32() {
        int32_t v = 0;
        if (Need(4)) std::memcpy(&v, p + pos, 4), pos += 4;
        return v;
    }
    uint32_t U32() { return static_cast<uint32_t>(I32()); }
    float F32() {
        float v = 0;
        if (Need(4)) std::memcpy(&v, p + pos, 4), pos += 4;
        return v;
    }
    // size_t (4) length incl. the NUL, then the bytes; 0 = no string
    bool Str(std::string& s, bool* present = nullptr) {
        const uint32_t len = U32();
        if (present) *present = len != 0;
        s.clear();
        if (!len) return ok;
        if (!Need(len)) return false;
        s.assign(reinterpret_cast<const char*>(p + pos), len - 1);
        pos += len;
        return ok;
    }
};

bool ReadFunction(In& in, Proto& f, int depth, std::string& err) {
    if (depth > 200) return err = "functions nested too deep", false;
    in.Str(f.source, &f.hasSource);
    f.lineDefined = in.I32();
    f.lastLineDefined = in.I32();
    f.nups = in.U8();
    f.numParams = in.U8();
    f.isVararg = in.U8();
    f.maxStack = in.U8();
    const int32_t ncode = in.I32();
    if (!in.ok || ncode < 0 || size_t(ncode) * 4 > in.n - in.pos) return err = "bad code size", false;
    f.code.resize(size_t(ncode));
    for (auto& i : f.code) i = in.U32();
    const int32_t nk = in.I32();
    if (!in.ok || nk < 0 || size_t(nk) > in.n - in.pos) return err = "bad constant count", false;
    f.k.resize(size_t(nk));
    for (auto& c : f.k) {
        c.type = static_cast<Const::Type>(in.U8());
        switch (c.type) {
            case Const::Nil: break;
            case Const::Bool: c.b = in.U8() != 0; break;
            case Const::Number: c.n = in.F32(); break;
            case Const::String: {
                bool present = false;
                in.Str(c.s, &present);
                if (!present) return err = "string constant without data", false;
                break;
            }
            default: return err = "unknown constant type " + std::to_string(int(c.type)), false;
        }
    }
    const int32_t np = in.I32();
    if (!in.ok || np < 0 || size_t(np) > in.n - in.pos) return err = "bad function count", false;
    f.p.resize(size_t(np));
    for (auto& c : f.p)
        if (!ReadFunction(in, c, depth + 1, err)) return false;
    const int32_t nli = in.I32();
    if (!in.ok || nli < 0 || size_t(nli) * 4 > in.n - in.pos) return err = "bad line info size", false;
    f.lineInfo.resize(size_t(nli));
    for (auto& l : f.lineInfo) l = in.I32();
    const int32_t nloc = in.I32();
    if (!in.ok || nloc < 0 || size_t(nloc) > in.n - in.pos) return err = "bad local count", false;
    f.locVars.resize(size_t(nloc));
    for (auto& l : f.locVars) {
        in.Str(l.name);
        l.startPc = in.I32();
        l.endPc = in.I32();
    }
    const int32_t nup = in.I32();
    if (!in.ok || nup < 0 || size_t(nup) > in.n - in.pos) return err = "bad upvalue name count", false;
    f.upvalues.resize(size_t(nup));
    for (auto& u : f.upvalues) in.Str(u);
    if (!in.ok) return err = "chunk truncated", false;
    return true;
}

// ---- writing ----

void Put8(std::vector<uint8_t>& b, uint8_t v) { b.push_back(v); }
void Put32(std::vector<uint8_t>& b, uint32_t v) {
    const auto* q = reinterpret_cast<const uint8_t*>(&v);
    b.insert(b.end(), q, q + 4);
}
void PutStr(std::vector<uint8_t>& b, const std::string& s, bool present = true) {
    if (!present) return Put32(b, 0);
    Put32(b, static_cast<uint32_t>(s.size() + 1));
    b.insert(b.end(), s.begin(), s.end());
    b.push_back(0);
}

void WriteFunction(std::vector<uint8_t>& b, const Proto& f) {
    PutStr(b, f.source, f.hasSource);
    Put32(b, static_cast<uint32_t>(f.lineDefined));
    Put32(b, static_cast<uint32_t>(f.lastLineDefined));
    Put8(b, f.nups);
    Put8(b, f.numParams);
    Put8(b, f.isVararg);
    Put8(b, f.maxStack);
    Put32(b, static_cast<uint32_t>(f.code.size()));
    for (uint32_t i : f.code) Put32(b, i);
    Put32(b, static_cast<uint32_t>(f.k.size()));
    for (const auto& c : f.k) {
        Put8(b, c.type);
        if (c.type == Const::Bool) Put8(b, c.b ? 1 : 0);
        else if (c.type == Const::Number) {
            uint32_t bits;
            std::memcpy(&bits, &c.n, 4);
            Put32(b, bits);
        } else if (c.type == Const::String) PutStr(b, c.s);
    }
    Put32(b, static_cast<uint32_t>(f.p.size()));
    for (const auto& c : f.p) WriteFunction(b, c);
    Put32(b, static_cast<uint32_t>(f.lineInfo.size()));
    for (int32_t l : f.lineInfo) Put32(b, static_cast<uint32_t>(l));
    Put32(b, static_cast<uint32_t>(f.locVars.size()));
    for (const auto& l : f.locVars) {
        PutStr(b, l.name);
        Put32(b, static_cast<uint32_t>(l.startPc));
        Put32(b, static_cast<uint32_t>(l.endPc));
    }
    Put32(b, static_cast<uint32_t>(f.upvalues.size()));
    for (const auto& u : f.upvalues) PutStr(b, u);
}

// ---- listing ----

std::string Quote(const std::string& s) {
    std::string o = "\"";
    for (unsigned char ch : s) {
        if (ch == '"' || ch == '\\') o += '\\', o += char(ch);
        else if (ch == '\n') o += "\\n";
        else if (ch < 32 || ch >= 127) {
            char b[8];
            std::snprintf(b, sizeof b, "\\%03d", ch);
            o += b;
        } else o += char(ch);
    }
    return o + "\"";
}

std::string Num(float v) {
    char b[48];
    std::snprintf(b, sizeof b, "%.9g", double(v));
    return b;
}

std::string KText(const Proto& f, int idx) {
    if (idx < 0 || size_t(idx) >= f.k.size()) return "K?" + std::to_string(idx);
    const Const& c = f.k[size_t(idx)];
    switch (c.type) {
        case Const::Nil: return "nil";
        case Const::Bool: return c.b ? "true" : "false";
        case Const::Number: return Num(c.n);
        default: return Quote(c.s);
    }
}

std::string RK(const Proto& f, int x) { return x & kBitRK ? KText(f, x & ~kBitRK) : "R" + std::to_string(x); }

std::string Operands(const Proto& f, int pc, uint32_t i) {
    const int op = OpOf(i), a = A(i), b = B(i), c = C(i), bx = Bx(i), sbx = SBx(i);
    const std::string ra = "R" + std::to_string(a);
    switch (op) {
        case MOVE: return ra + " := R" + std::to_string(b);
        case LOADK: return ra + " := " + KText(f, bx);
        case LOADBOOL: return ra + " := " + (b ? "true" : "false") + (c ? "; skip next" : "");
        case LOADNIL: return "R" + std::to_string(a) + ".." + "R" + std::to_string(b) + " := nil";
        case GETUPVAL: return ra + " := U" + std::to_string(b);
        case GETGLOBAL: return ra + " := _G[" + KText(f, bx) + "]";
        case GETTABLE: return ra + " := R" + std::to_string(b) + "[" + RK(f, c) + "]";
        case SETGLOBAL: return "_G[" + KText(f, bx) + "] := " + ra;
        case SETUPVAL: return "U" + std::to_string(b) + " := " + ra;
        case SETTABLE: return ra + "[" + RK(f, b) + "] := " + RK(f, c);
        case NEWTABLE: return ra + " := {} (array " + std::to_string(b) + ", hash " + std::to_string(c) + ")";
        case SELF:
            return "R" + std::to_string(a + 1) + " := R" + std::to_string(b) + "; " + ra + " := R" + std::to_string(b) +
                   "[" + RK(f, c) + "]";
        case ADD: case SUB: case MUL: case DIV: case MOD: case POW:
            return ra + " := " + RK(f, b) + " " + OpName(op) + " " + RK(f, c);
        case UNM: case NOT: case LEN: return ra + " := " + OpName(op) + " R" + std::to_string(b);
        case CONCAT: return ra + " := R" + std::to_string(b) + " .. R" + std::to_string(c);
        case JMP: return "to " + std::to_string(pc + 1 + sbx);
        case EQ: case LT: case LE:
            return "if (" + RK(f, b) + " " + OpName(op) + " " + RK(f, c) + ") ~= " + std::to_string(a) + " then skip next";
        case TEST: return "if not (" + ra + " <=> " + std::to_string(c) + ") then skip next";
        case TESTSET:
            return "if (R" + std::to_string(b) + " <=> " + std::to_string(c) + ") then " + ra + " := R" +
                   std::to_string(b) + " else skip next";
        case CALL: case TAILCALL:
            return ra + "(" + (b ? std::to_string(b - 1) + " args" : std::string("args to top")) + ") -> " +
                   (c ? std::to_string(c - 1) + " results" : std::string("all results"));
        case RETURN: return b ? std::to_string(b - 1) + " values from " + ra : "values from " + ra + " to top";
        case FORLOOP: case FORPREP: return ra + ", to " + std::to_string(pc + 1 + sbx);
        case TFORLOOP: return ra + ", " + std::to_string(c) + " results";
        case SETLIST: return ra + "[...] := " + std::to_string(b) + " values, block " + std::to_string(c);
        case CLOSE: return "close from " + ra;
        case CLOSURE: return ra + " := closure(F" + std::to_string(bx) + ")";
        case VARARG: return ra + " := ... (" + std::to_string(b) + ")";
        default: return "?";
    }
}

void ListFunction(std::string& out, const Proto& f, const std::string& path) {
    char head[256];
    std::snprintf(head, sizeof head,
                  "function %s (lines %d-%d, %d params, %d upvalues, vararg %d, stack %d, %zu instructions, "
                  "%zu constants, %zu functions)\n",
                  path.c_str(), f.lineDefined, f.lastLineDefined, f.numParams, f.nups, f.isVararg, f.maxStack,
                  f.code.size(), f.k.size(), f.p.size());
    out += head;
    if (f.hasSource) {
        const size_t z = f.source.find('\0');
        out += "  source " + Quote(f.source.substr(0, z)) +
               (z == std::string::npos ? "" : " + " + std::to_string(f.source.size() - z) + " bytes from a NUL on") + "\n";
    }
    for (size_t pc = 0; pc < f.code.size(); ++pc) {
        const uint32_t i = f.code[pc];
        const int op = OpOf(i);
        char line[32];
        if (pc < f.lineInfo.size()) std::snprintf(line, sizeof line, "%4d", f.lineInfo[pc]);
        else std::snprintf(line, sizeof line, "   -");
        char b[64];
        std::snprintf(b, sizeof b, "  %4zu [%s] %-9s ", pc, line, op < kNumOps ? OpName(op) : "?");
        out += b;
        out += op < kNumOps ? Operands(f, int(pc), i) : "invalid";
        out += "\n";
    }
    for (size_t k = 0; k < f.p.size(); ++k)
        ListFunction(out, f.p[k], path == "main" ? std::to_string(k) : path + "/" + std::to_string(k));
}

// ---- checking (Lua 5.1 lundump/ldebug rules) ----

struct Checker {
    const Proto& f;
    std::string path;
    std::string err;
    bool Fail(int pc, const std::string& why) {
        if (err.empty()) err = "function " + path + (pc >= 0 ? " pc " + std::to_string(pc) : "") + ": " + why;
        return false;
    }
    bool Reg(int pc, int r) { return r < f.maxStack ? true : Fail(pc, "register " + std::to_string(r) + " >= stack " + std::to_string(f.maxStack)); }
    bool RKok(int pc, int r) {
        if (r & kBitRK) return size_t(r & ~kBitRK) < f.k.size() ? true : Fail(pc, "constant " + std::to_string(r & ~kBitRK) + " out of range");
        return Reg(pc, r);
    }
    bool Arg(int pc, int v, ArgMode m) {
        if (m == N) return v == 0 ? true : Fail(pc, "unused operand not 0");
        if (m == R) return Reg(pc, v);
        if (m == K) return RKok(pc, v);
        return true;
    }
    // an op that takes "all results" from the previous open call
    bool OpenOp(int pc) {
        if (size_t(pc + 1) >= f.code.size()) return Fail(pc, "open call at the end");
        const uint32_t n = f.code[size_t(pc + 1)];
        switch (OpOf(n)) {
            case CALL: case TAILCALL: case RETURN: case SETLIST:
                return B(n) == 0 ? true : Fail(pc, "open call followed by an op with a fixed count");
            default: return Fail(pc, "open call not followed by CALL, TAILCALL, RETURN or SETLIST");
        }
    }
    bool Run() {
        const int n = int(f.code.size());
        if (f.maxStack > kMaxStack) return Fail(-1, "stack too big");
        if (f.numParams + (f.isVararg & kVarargHasArg) > f.maxStack) return Fail(-1, "parameters do not fit the stack");
        if ((f.isVararg & kVarargNeedsArg) && !(f.isVararg & kVarargHasArg)) return Fail(-1, "bad vararg flags");
        if (!f.upvalues.empty() && f.upvalues.size() != f.nups) return Fail(-1, "upvalue names do not match");
        if (!f.lineInfo.empty() && f.lineInfo.size() != f.code.size()) return Fail(-1, "line info size");
        if (n == 0 || OpOf(f.code[size_t(n - 1)]) != RETURN) return Fail(-1, "does not end with RETURN");
        for (int pc = 0; pc < n; ++pc) {
            const uint32_t i = f.code[size_t(pc)];
            const int op = OpOf(i);
            if (op >= kNumOps) return Fail(pc, "invalid opcode");
            const Mode& m = kModes[op];
            int a = A(i), b = 0, c = 0;
            if (!Reg(pc, a)) return false;
            switch (m.fmt) {
                case iABC:
                    b = B(i), c = C(i);
                    if (!Arg(pc, b, m.b) || !Arg(pc, c, m.c)) return false;
                    break;
                case iABx:
                    b = Bx(i);
                    if (m.b == K && size_t(b) >= f.k.size()) return Fail(pc, "constant out of range");
                    break;
                case iAsBx:
                    b = SBx(i);
                    if (m.b == R) {
                        const int dest = pc + 1 + b;
                        if (dest < 0 || dest >= n) return Fail(pc, "jump out of range");
                        if (dest > 0 && OpOf(f.code[size_t(dest - 1)]) == SETLIST && C(f.code[size_t(dest - 1)]) == 0)
                            return Fail(pc, "jump into a SETLIST count");
                    }
                    break;
            }
            if (m.test) {
                if (pc + 2 >= n) return Fail(pc, "test at the end");
                if (OpOf(f.code[size_t(pc + 1)]) != JMP) return Fail(pc, "test not followed by JMP");
            }
            switch (op) {
                case LOADBOOL:
                    if (c == 1 && pc + 2 >= n) return Fail(pc, "LOADBOOL skip at the end");
                    break;
                case LOADNIL:
                    if (!Reg(pc, b)) return false;
                    break;
                case GETUPVAL: case SETUPVAL:
                    if (b >= f.nups) return Fail(pc, "upvalue out of range");
                    break;
                case GETGLOBAL: case SETGLOBAL:
                    if (f.k[size_t(b)].type != Const::String) return Fail(pc, "global name is not a string");
                    break;
                case SELF:
                    if (!Reg(pc, a + 1)) return false;
                    break;
                case CONCAT:
                    if (b >= c) return Fail(pc, "CONCAT needs at least two operands");
                    break;
                case TFORLOOP:
                    if (c < 1 || !Reg(pc, a + 2 + c)) return Fail(pc, "TFORLOOP results");
                    break;
                case FORLOOP: case FORPREP:
                    if (!Reg(pc, a + 3)) return false;
                    break;
                case CALL: case TAILCALL:
                    if (b != 0 && !Reg(pc, a + b - 1)) return false;
                    if (c == 0) {
                        if (!OpenOp(pc)) return false;
                    } else if (c - 1 != 0 && !Reg(pc, a + c - 2)) return false;
                    break;
                case RETURN:
                    if (b - 1 > 0 && !Reg(pc, a + b - 2)) return false;
                    break;
                case SETLIST:
                    if (b > 0 && !Reg(pc, a + b)) return false;
                    if (c == 0) ++pc;  // the next word is the block number
                    break;
                case CLOSURE: {
                    if (size_t(b) >= f.p.size()) return Fail(pc, "closure of a missing function");
                    const int nup = f.p[size_t(b)].nups;
                    if (pc + nup >= n) return Fail(pc, "closure upvalues past the end");
                    for (int j = 1; j <= nup; ++j) {
                        const int o = OpOf(f.code[size_t(pc + j)]);
                        if (o != GETUPVAL && o != MOVE) return Fail(pc, "closure upvalue not GETUPVAL / MOVE");
                    }
                    pc += nup;
                    break;
                }
                case VARARG:
                    if (!(f.isVararg & kVarargIsVararg) || (f.isVararg & kVarargNeedsArg)) return Fail(pc, "VARARG in a fixed-argument function");
                    if (b - 1 == -1 && !OpenOp(pc)) return false;
                    if (b - 1 > 0 && !Reg(pc, a + b - 2)) return false;
                    break;
                default: break;
            }
        }
        return true;
    }
};

std::string CheckAll(const Proto& f, const std::string& path) {
    Checker c{f, path};
    if (!c.Run()) return c.err;
    for (size_t k = 0; k < f.p.size(); ++k) {
        const std::string e = CheckAll(f.p[k], path == "main" ? std::to_string(k) : path + "/" + std::to_string(k));
        if (!e.empty()) return e;
    }
    return {};
}

void CollectStrings(const Proto& f, std::vector<std::string>& out) {
    for (const auto& c : f.k)
        if (c.type == Const::String) out.push_back(c.s);
    for (const auto& p : f.p) CollectStrings(p, out);
}

}  // namespace

const char* OpName(int op) { return op >= 0 && op < kNumOps ? kOpNames[op] : "?"; }

bool Const::operator==(const Const& o) const {
    if (type != o.type) return false;
    switch (type) {
        case Bool: return b == o.b;
        case Number: return std::memcmp(&n, &o.n, 4) == 0;
        case String: return s == o.s;
        default: return true;
    }
}

const std::vector<uint8_t>& Header() {
    static const std::vector<uint8_t> h = {0x1B, 'L', 'u', 'a', 0x51, 0, 1, 4, 4, 4, 4, 0};
    return h;
}

bool Read(const uint8_t* data, size_t size, Proto& main, std::string* error, size_t* used) {
    const auto& h = Header();
    if (size < h.size() || std::memcmp(data, h.data(), h.size()) != 0) {
        if (error) *error = "not a Lua 5.1 chunk with 4-byte floats (header)";
        return false;
    }
    In in{data, size, h.size()};
    std::string err;
    main = Proto{};
    if (!ReadFunction(in, main, 0, err)) {
        if (error) *error = err.empty() ? "chunk truncated" : err;
        return false;
    }
    if (used) *used = in.pos;
    return true;
}

std::vector<uint8_t> Write(const Proto& main) {
    std::vector<uint8_t> b = Header();
    WriteFunction(b, main);
    return b;
}

std::string List(const Proto& main) {
    std::string out;
    ListFunction(out, main, "main");
    return out;
}

std::string Check(const Proto& main) { return CheckAll(main, "main"); }

std::vector<std::string> Strings(const Proto& main) {
    std::vector<std::string> out;
    CollectStrings(main, out);
    return out;
}

}  // namespace mc::game::lua51
