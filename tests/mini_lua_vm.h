#pragma once
// A tiny interpreter for the Lua 5.1 instructions the game's mission scripts and our compiler use (MOVE, LOADK,
// LOADBOOL, LOADNIL, GET/SETGLOBAL, GET/SETTABLE, NEWTABLE, SETLIST, SELF, EQ, LT, LE, TEST, JMP, CALL, TAILCALL,
// RETURN, CLOSURE), for tests only. The game's API is mocked by "auto" tables: any field of one is another auto
// table (cached, so enums compare equal), and calling one records the call ("game.sds:ActivateStreamMapLine(...)")
// in a trace and returns an auto table named after the call. Chosen paths can be overridden by native functions.
// Running the game's own FreerideScript through it checks the interpreter against real compiler output; running our
// script checks what it calls, in which order, with which constants.
#include "game/lua51.h"

#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace minivm {

using mc::game::lua51::Proto;
namespace op = mc::game::lua51;

struct TableObj;
struct Value;
using Native = std::function<std::vector<Value>(const std::vector<Value>&)>;

struct Value {
    enum T { Nil, Bool, Num, Str, Tab, Fn, Nat } t = Nil;
    bool b = false;
    double n = 0;
    std::string s;
    std::shared_ptr<TableObj> tab;
    const Proto* fn = nullptr;
    std::shared_ptr<Native> nat;
    static Value N(double v) { Value x; x.t = Num; x.n = v; return x; }
    static Value S(const std::string& v) { Value x; x.t = Str; x.s = v; return x; }
    static Value B(bool v) { Value x; x.t = Bool; x.b = v; return x; }
    bool Truthy() const { return !(t == Nil || (t == Bool && !b)); }
};

struct TableObj {
    std::map<std::string, Value> h;  // string keys
    std::map<double, Value> a;       // number keys (array part)
    bool autoTable = false;
    std::string name;
    TableObj* parent = nullptr;
};

inline Value NewTable(bool autoTable = false, const std::string& name = {}, TableObj* parent = nullptr) {
    Value v;
    v.t = Value::Tab;
    v.tab = std::make_shared<TableObj>();
    v.tab->autoTable = autoTable;
    v.tab->name = name;
    v.tab->parent = parent;
    return v;
}

inline bool RawEq(const Value& x, const Value& y) {
    if (x.t != y.t) return false;
    switch (x.t) {
        case Value::Nil: return true;
        case Value::Bool: return x.b == y.b;
        case Value::Num: return float(x.n) == float(y.n);
        case Value::Str: return x.s == y.s;
        case Value::Tab: return x.tab == y.tab;
        case Value::Fn: return x.fn == y.fn;
        case Value::Nat: return x.nat == y.nat;
    }
    return false;
}

inline std::string Show(const Value& v) {
    char b[64];
    switch (v.t) {
        case Value::Nil: return "nil";
        case Value::Bool: return v.b ? "true" : "false";
        case Value::Num: std::snprintf(b, sizeof b, "%.6g", v.n); return b;
        case Value::Str: return "\"" + v.s + "\"";
        case Value::Tab: return v.tab->autoTable ? v.tab->name : "{table}";
        case Value::Fn: return "function";
        case Value::Nat: return "native";
    }
    return "?";
}

class Vm {
public:
    std::map<std::string, Value> globals;
    std::vector<std::string> trace;
    std::map<std::string, Native> overrides;  // by auto path, e.g. "game.datastore.GetNumber"
    long steps = 0;

    Value Auto(const std::string& name) { return NewTable(true, name); }
    void AutoGlobal(const std::string& name) { globals[name] = Auto(name); }

    // Runs a chunk's main function (defines the script's globals).
    void Run0(const Proto& main) { Run(main, {}); }
    Value Index0(const Value& t, const std::string& key) { return Index(t, Value::S(key)); }

    std::vector<Value> Call(const Value& f, const std::vector<Value>& args) {
        if (f.t == Value::Fn) return Run(*f.fn, args);
        if (f.t == Value::Nat) return (*f.nat)(args);
        if (f.t == Value::Tab && f.tab->autoTable) return CallAuto(f, args);
        throw std::runtime_error("attempt to call a " + Show(f) + " value");
    }

private:
    Value Index(const Value& t, const Value& key) {
        if (t.t != Value::Tab) throw std::runtime_error("attempt to index " + Show(t) + " with " + Show(key));
        TableObj& o = *t.tab;
        if (key.t == Value::Str) {
            auto it = o.h.find(key.s);
            if (it != o.h.end()) return it->second;
            if (o.autoTable) {
                const std::string path = o.name + "." + key.s;
                auto ov = overrides.find(path);
                Value v;
                if (ov != overrides.end()) {
                    v.t = Value::Nat;
                    v.nat = std::make_shared<Native>(ov->second);
                } else {
                    v = NewTable(true, path, &o);
                }
                o.h[key.s] = v;
                return v;
            }
            return {};
        }
        if (key.t == Value::Num) {
            auto it = o.a.find(key.n);
            return it == o.a.end() ? Value{} : it->second;
        }
        throw std::runtime_error("unsupported key " + Show(key));
    }

    void SetIndex(const Value& t, const Value& key, const Value& v) {
        if (t.t != Value::Tab) throw std::runtime_error("attempt to index " + Show(t));
        if (key.t == Value::Str) t.tab->h[key.s] = v;
        else if (key.t == Value::Num) t.tab->a[key.n] = v;
        else throw std::runtime_error("unsupported key");
    }

    std::vector<Value> CallAuto(const Value& f, const std::vector<Value>& args) {
        const TableObj& o = *f.tab;
        std::string text;
        size_t first = 0;
        const size_t dot = o.name.rfind('.');
        if (o.parent && !args.empty() && args[0].t == Value::Tab && args[0].tab.get() == o.parent && dot != std::string::npos) {
            text = o.name.substr(0, dot) + ":" + o.name.substr(dot + 1) + "(";
            first = 1;
        } else {
            text = o.name + "(";
        }
        for (size_t i = first; i < args.size(); ++i) text += (i > first ? ", " : "") + Show(args[i]);
        text += ")";
        trace.push_back(text);
        return {NewTable(true, text)};
    }

    Value RK(const Proto& f, std::vector<Value>& R, int x) {
        if (x & op::kBitRK) return K(f, x & ~op::kBitRK);
        return R[size_t(x)];
    }
    static Value K(const Proto& f, int i) {
        const auto& c = f.k[size_t(i)];
        switch (c.type) {
            case op::Const::Bool: return Value::B(c.b);
            case op::Const::Number: return Value::N(c.n);
            case op::Const::String: return Value::S(c.s);
            default: return {};
        }
    }
    static bool Less(const Value& x, const Value& y, bool orEqual) {
        if (x.t == Value::Num && y.t == Value::Num) return orEqual ? float(x.n) <= float(y.n) : float(x.n) < float(y.n);
        if (x.t == Value::Str && y.t == Value::Str) return orEqual ? x.s <= y.s : x.s < y.s;
        throw std::runtime_error("attempt to compare " + Show(x) + " with " + Show(y));
    }

    std::vector<Value> Run(const Proto& f, const std::vector<Value>& args) {
        std::vector<Value> R(size_t(f.maxStack) + 64);
        for (size_t i = 0; i < f.numParams && i < args.size(); ++i) R[i] = args[i];
        int top = 0;  // after an open call: first free register
        for (int pc = 0;;) {
            if (++steps > 1000000) throw std::runtime_error("too many steps");
            if (pc < 0 || size_t(pc) >= f.code.size()) throw std::runtime_error("pc out of range");
            const uint32_t i = f.code[size_t(pc)];
            const int a = op::A(i), b = op::B(i), c = op::C(i);
            switch (op::OpOf(i)) {
                case op::MOVE: R[size_t(a)] = R[size_t(b)]; break;
                case op::LOADK: R[size_t(a)] = K(f, op::Bx(i)); break;
                case op::LOADBOOL:
                    R[size_t(a)] = Value::B(b != 0);
                    if (c) ++pc;
                    break;
                case op::LOADNIL:
                    for (int r = a; r <= b; ++r) R[size_t(r)] = {};
                    break;
                case op::GETGLOBAL: {
                    auto it = globals.find(f.k[size_t(op::Bx(i))].s);
                    R[size_t(a)] = it == globals.end() ? Value{} : it->second;
                    break;
                }
                case op::SETGLOBAL: globals[f.k[size_t(op::Bx(i))].s] = R[size_t(a)]; break;
                case op::GETTABLE: R[size_t(a)] = Index(R[size_t(b)], RK(f, R, c)); break;
                case op::SETTABLE: SetIndex(R[size_t(a)], RK(f, R, b), RK(f, R, c)); break;
                case op::NEWTABLE: R[size_t(a)] = NewTable(); break;
                case op::SETLIST: {
                    int n = b ? b : top - a - 1;
                    if (c == 0) throw std::runtime_error("SETLIST with an extra word");
                    for (int k = 1; k <= n; ++k) SetIndex(R[size_t(a)], Value::N((c - 1) * 50 + k), R[size_t(a + k)]);
                    break;
                }
                case op::SELF: {
                    const Value obj = R[size_t(b)];
                    R[size_t(a + 1)] = obj;
                    R[size_t(a)] = Index(obj, RK(f, R, c));
                    break;
                }
                case op::EQ: case op::LT: case op::LE: {
                    const Value x = RK(f, R, b), y = RK(f, R, c);
                    const int o = op::OpOf(i);
                    const bool res = o == op::EQ ? RawEq(x, y) : Less(x, y, o == op::LE);
                    if (int(res) == a) pc = pc + 1 + op::SBx(f.code[size_t(pc + 1)]);  // the JMP's jump
                    else ++pc;                                                       // skip the JMP
                    break;
                }
                case op::TEST:
                    if (int(!R[size_t(a)].Truthy()) != c) pc = pc + 1 + op::SBx(f.code[size_t(pc + 1)]);
                    else ++pc;
                    break;
                case op::JMP: pc += op::SBx(i); break;
                case op::CALL: case op::TAILCALL: {
                    const int nargs = b ? b - 1 : top - a - 1;
                    std::vector<Value> callArgs(R.begin() + a + 1, R.begin() + a + 1 + nargs);
                    std::vector<Value> res = Call(R[size_t(a)], callArgs);
                    if (op::OpOf(i) == op::TAILCALL) return res;
                    if (c == 0) {
                        if (R.size() < size_t(a) + res.size() + 1) R.resize(size_t(a) + res.size() + 1);
                        for (size_t k = 0; k < res.size(); ++k) R[size_t(a) + k] = res[k];
                        top = a + int(res.size());
                    } else {
                        for (int k = 0; k < c - 1; ++k) R[size_t(a + k)] = size_t(k) < res.size() ? res[size_t(k)] : Value{};
                    }
                    break;
                }
                case op::RETURN: {
                    const int n = b ? b - 1 : top - a;
                    return std::vector<Value>(R.begin() + a, R.begin() + a + n);
                }
                case op::CLOSURE: {
                    Value v;
                    v.t = Value::Fn;
                    v.fn = &f.p[size_t(op::Bx(i))];
                    if (v.fn->nups) throw std::runtime_error("closures with upvalues are not supported");
                    R[size_t(a)] = v;
                    break;
                }
                default: throw std::runtime_error(std::string("unsupported op ") + op::OpName(op::OpOf(i)));
            }
            ++pc;
        }
    }
};

}  // namespace minivm
