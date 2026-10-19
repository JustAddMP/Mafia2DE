#include "game/lua51_compile.h"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <utility>

namespace mc::game::lua51 {
namespace {

struct CompileError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

[[noreturn]] void Error(int line, const std::string& what) {
    throw CompileError("line " + std::to_string(line) + ": " + what);
}

// ---- lexer ----

enum Tok {
    T_EOF, T_NAME, T_STRING, T_NUMBER,
    // keywords
    T_AND, T_BREAK, T_DO, T_ELSE, T_ELSEIF, T_END, T_FALSE, T_FOR, T_FUNCTION, T_IF, T_IN, T_LOCAL, T_NIL, T_NOT,
    T_OR, T_REPEAT, T_RETURN, T_THEN, T_TRUE, T_UNTIL, T_WHILE,
    // symbols
    T_EQ, T_NE, T_LE, T_GE, T_CONCAT, T_DOTS, T_SYM  // T_SYM: a single character (in `ch`)
};

const char* const kKeywords[] = {"and", "break", "do", "else", "elseif", "end", "false", "for", "function", "if",
                                 "in", "local", "nil", "not", "or", "repeat", "return", "then", "true", "until",
                                 "while"};

struct Token {
    Tok t = T_EOF;
    char ch = 0;
    std::string s;
    double num = 0;
    int line = 1;
};

class Lexer {
public:
    explicit Lexer(const std::string& src) : s_(src) {}

    Token Next() {
        SkipSpace();
        Token k;
        k.line = line_;
        if (p_ >= s_.size()) return k;
        const char c = s_[p_];
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            const size_t b = p_;
            while (p_ < s_.size() && (std::isalnum(static_cast<unsigned char>(s_[p_])) || s_[p_] == '_')) ++p_;
            k.s = s_.substr(b, p_ - b);
            k.t = T_NAME;
            for (size_t i = 0; i < std::size(kKeywords); ++i)
                if (k.s == kKeywords[i]) k.t = static_cast<Tok>(T_AND + i);
            return k;
        }
        if (std::isdigit(static_cast<unsigned char>(c)) || (c == '.' && p_ + 1 < s_.size() && std::isdigit(static_cast<unsigned char>(s_[p_ + 1])))) {
            const size_t b = p_;
            while (p_ < s_.size() && (std::isalnum(static_cast<unsigned char>(s_[p_])) || s_[p_] == '.' ||
                                      ((s_[p_] == '+' || s_[p_] == '-') && (s_[p_ - 1] == 'e' || s_[p_ - 1] == 'E'))))
                ++p_;
            const std::string text = s_.substr(b, p_ - b);
            char* end = nullptr;
            k.num = std::strtod(text.c_str(), &end);
            if (!end || *end) Error(line_, "malformed number '" + text + "'");
            k.t = T_NUMBER;
            return k;
        }
        if (c == '"' || c == '\'') {
            ++p_;
            for (;;) {
                if (p_ >= s_.size() || s_[p_] == '\n') Error(line_, "unfinished string");
                char ch = s_[p_++];
                if (ch == c) break;
                if (ch == '\\') {
                    if (p_ >= s_.size()) Error(line_, "unfinished string");
                    const char e = s_[p_++];
                    switch (e) {
                        case 'n': ch = '\n'; break;
                        case 't': ch = '\t'; break;
                        case 'r': ch = '\r'; break;
                        case '\\': case '"': case '\'': ch = e; break;
                        default:
                            if (std::isdigit(static_cast<unsigned char>(e))) {
                                int v = e - '0';
                                for (int i = 0; i < 2 && p_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[p_])); ++i)
                                    v = v * 10 + (s_[p_++] - '0');
                                if (v > 255) Error(line_, "escape too large");
                                ch = static_cast<char>(v);
                            } else Error(line_, std::string("unsupported escape \\") + e);
                    }
                }
                k.s += ch;
            }
            k.t = T_STRING;
            return k;
        }
        auto two = [&](const char* sym) { return s_.compare(p_, 2, sym) == 0; };
        if (two("==")) return p_ += 2, k.t = T_EQ, k;
        if (two("~=")) return p_ += 2, k.t = T_NE, k;
        if (two("<=")) return p_ += 2, k.t = T_LE, k;
        if (two(">=")) return p_ += 2, k.t = T_GE, k;
        if (s_.compare(p_, 3, "...") == 0) return p_ += 3, k.t = T_DOTS, k;
        if (two("..")) return p_ += 2, k.t = T_CONCAT, k;
        if (std::strchr("(){}[],.:;=<>-+*/%^#", c)) {
            ++p_;
            k.t = T_SYM;
            k.ch = c;
            return k;
        }
        Error(line_, std::string("unexpected character '") + c + "'");
    }

private:
    void SkipSpace() {
        for (;;) {
            while (p_ < s_.size() && std::isspace(static_cast<unsigned char>(s_[p_]))) {
                if (s_[p_] == '\n') ++line_;
                ++p_;
            }
            if (s_.compare(p_, 2, "--") != 0) return;
            p_ += 2;
            if (s_.compare(p_, 2, "[[") == 0) {
                const size_t e = s_.find("]]", p_ + 2);
                if (e == std::string::npos) Error(line_, "unfinished block comment");
                for (size_t i = p_; i < e; ++i)
                    if (s_[i] == '\n') ++line_;
                p_ = e + 2;
            } else {
                while (p_ < s_.size() && s_[p_] != '\n') ++p_;
            }
        }
    }

    const std::string& s_;
    size_t p_ = 0;
    int line_ = 1;
};

// ---- syntax tree ----

struct FuncDef;
struct Stmt;
using Block = std::vector<std::unique_ptr<Stmt>>;

struct Expr {
    enum Kind { Nil, True, False, Number, String, Name, Index, Call, Method, Function, Table, Compare } kind = Nil;
    int line = 0;
    double num = 0;
    std::string str;                    // String / Name / Index key / Method name / Compare op
    std::unique_ptr<Expr> obj, rhs;     // Index / Call / Method object; Compare: obj op rhs
    std::vector<std::unique_ptr<Expr>> args;  // Call / Method arguments, Table items
    std::unique_ptr<FuncDef> fn;
};
using ExprP = std::unique_ptr<Expr>;

struct FuncDef {
    std::vector<std::string> params;
    Block body;
    int line = 0, lastLine = 0;
};

struct Stmt {
    enum Kind { Local, Assign, CallStmt, If, Return } kind = CallStmt;
    int line = 0;
    std::string name;            // Local
    ExprP target, value;         // Assign target / value, Local value, CallStmt / Return expression (value)
    std::vector<std::pair<ExprP, Block>> branches;  // If
    bool hasElse = false;
    Block elseBlock;
};

// ---- parser ----

class Parser {
public:
    explicit Parser(const std::string& src) : lex_(src) { tok_ = lex_.Next(); }

    Block Chunk() {
        Block b = ParseBlock();
        if (tok_.t != T_EOF) Error(tok_.line, "'<eof>' expected");
        return b;
    }

private:
    void Advance() { tok_ = lex_.Next(); }
    bool IsSym(char c) const { return tok_.t == T_SYM && tok_.ch == c; }
    void ExpectSym(char c, const char* what) {
        if (!IsSym(c)) Error(tok_.line, std::string("'") + c + "' expected " + what);
        Advance();
    }
    void Expect(Tok t, const char* text) {
        if (tok_.t != t) Error(tok_.line, std::string("'") + text + "' expected");
        Advance();
    }
    std::string Name() {
        if (tok_.t != T_NAME) Error(tok_.line, "name expected");
        std::string n = tok_.s;
        Advance();
        return n;
    }
    bool BlockEnd() const {
        return tok_.t == T_EOF || tok_.t == T_END || tok_.t == T_ELSE || tok_.t == T_ELSEIF;
    }

    Block ParseBlock() {
        Block b;
        while (!BlockEnd()) {
            const bool isReturn = tok_.t == T_RETURN;
            b.push_back(Statement());
            if (IsSym(';')) Advance();
            if (isReturn) {
                if (!BlockEnd()) Error(tok_.line, "'return' must be the last statement of a block");
                break;
            }
        }
        return b;
    }

    std::unique_ptr<Stmt> Statement() {
        auto s = std::make_unique<Stmt>();
        s->line = tok_.line;
        switch (tok_.t) {
            case T_FUNCTION: {
                Advance();
                auto target = std::make_unique<Expr>();
                target->kind = Expr::Name;
                target->line = s->line;
                target->str = Name();
                if (IsSym('.') || IsSym(':')) Error(tok_.line, "only global functions 'function Name()' are supported");
                s->kind = Stmt::Assign;
                s->target = std::move(target);
                s->value = std::make_unique<Expr>();
                s->value->kind = Expr::Function;
                s->value->line = s->line;
                s->value->fn = Body(s->line);
                return s;
            }
            case T_LOCAL: {
                Advance();
                if (tok_.t == T_FUNCTION) Error(tok_.line, "'local function' is not supported");
                s->kind = Stmt::Local;
                s->name = Name();
                if (IsSym(',')) Error(tok_.line, "multiple assignment is not supported");
                if (IsSym('=')) {
                    Advance();
                    s->value = Expression();
                }
                return s;
            }
            case T_IF: {
                s->kind = Stmt::If;
                do {
                    Advance();  // 'if' / 'elseif'
                    ExprP cond = Condition();
                    Expect(T_THEN, "then");
                    s->branches.emplace_back(std::move(cond), ParseBlock());
                } while (tok_.t == T_ELSEIF);
                if (tok_.t == T_ELSE) {
                    Advance();
                    s->hasElse = true;
                    s->elseBlock = ParseBlock();
                }
                Expect(T_END, "end");
                return s;
            }
            case T_RETURN: {
                Advance();
                s->kind = Stmt::Return;
                if (!BlockEnd() && !IsSym(';')) {
                    s->value = Expression();
                    if (IsSym(',')) Error(tok_.line, "multiple return values are not supported");
                }
                return s;
            }
            case T_NAME: case T_SYM: {
                ExprP e = Suffixed();
                if (IsSym('=')) {
                    if (e->kind != Expr::Name && e->kind != Expr::Index) Error(s->line, "cannot assign to this expression");
                    Advance();
                    s->kind = Stmt::Assign;
                    s->target = std::move(e);
                    s->value = Expression();
                    return s;
                }
                if (IsSym(',')) Error(tok_.line, "multiple assignment is not supported");
                if (e->kind != Expr::Call && e->kind != Expr::Method) Error(s->line, "syntax error (a statement must be a call or an assignment)");
                s->kind = Stmt::CallStmt;
                s->value = std::move(e);
                return s;
            }
            default:
                Error(tok_.line, "unsupported statement '" + (tok_.s.empty() ? std::string(1, tok_.ch) : tok_.s) + "'");
        }
    }

    std::unique_ptr<FuncDef> Body(int line) {
        auto f = std::make_unique<FuncDef>();
        f->line = line;
        ExpectSym('(', "after the function name");
        if (!IsSym(')')) {
            for (;;) {
                if (tok_.t == T_DOTS) Error(tok_.line, "varargs are not supported");
                f->params.push_back(Name());
                if (!IsSym(',')) break;
                Advance();
            }
        }
        ExpectSym(')', "after the parameters");
        f->body = ParseBlock();
        f->lastLine = tok_.line;
        Expect(T_END, "end");
        return f;
    }

    ExprP Condition() {
        ExprP l = Simple();
        Tok t = tok_.t;
        std::string op;
        if (t == T_EQ) op = "==";
        else if (t == T_NE) op = "~=";
        else if (t == T_LE) op = "<=";
        else if (t == T_GE) op = ">=";
        else if (IsSym('<')) op = "<";
        else if (IsSym('>')) op = ">";
        if (op.empty()) {
            RejectOperator();
            return l;
        }
        auto c = std::make_unique<Expr>();
        c->kind = Expr::Compare;
        c->line = tok_.line;
        c->str = op;
        Advance();
        c->obj = std::move(l);
        c->rhs = Simple();
        RejectOperator();
        return c;
    }

    void RejectOperator() {
        if (tok_.t == T_AND || tok_.t == T_OR || tok_.t == T_CONCAT || tok_.t == T_EQ || tok_.t == T_NE ||
            tok_.t == T_LE || tok_.t == T_GE ||
            (tok_.t == T_SYM && std::strchr("<>+-*/%^", tok_.ch)))
            Error(tok_.line, "operators other than one comparison in a condition are not supported");
    }

    ExprP Expression() {
        ExprP e = Simple();
        RejectOperator();
        return e;
    }

    ExprP Simple() {
        auto e = std::make_unique<Expr>();
        e->line = tok_.line;
        switch (tok_.t) {
            case T_NIL: Advance(); e->kind = Expr::Nil; return e;
            case T_TRUE: Advance(); e->kind = Expr::True; return e;
            case T_FALSE: Advance(); e->kind = Expr::False; return e;
            case T_NUMBER: e->kind = Expr::Number; e->num = tok_.num; Advance(); return e;
            case T_STRING: e->kind = Expr::String; e->str = tok_.s; Advance(); return e;
            case T_FUNCTION:
                Advance();
                e->kind = Expr::Function;
                e->fn = Body(e->line);
                return e;
            case T_NOT: Error(tok_.line, "'not' is not supported");
            case T_DOTS: Error(tok_.line, "varargs are not supported");
            default: break;
        }
        if (IsSym('{')) return Table();
        if (IsSym('-')) {
            Advance();
            if (tok_.t != T_NUMBER) Error(e->line, "unary minus is only supported on number literals");
            e->kind = Expr::Number;
            e->num = -tok_.num;
            Advance();
            return e;
        }
        return Suffixed();
    }

    ExprP Table() {
        auto e = std::make_unique<Expr>();
        e->kind = Expr::Table;
        e->line = tok_.line;
        ExpectSym('{', "");
        while (!IsSym('}')) {
            if (IsSym('[')) Error(tok_.line, "keyed table fields are not supported");
            ExprP item = Expression();
            if (IsSym('=')) Error(tok_.line, "keyed table fields are not supported");
            e->args.push_back(std::move(item));
            if (IsSym(',') || IsSym(';')) Advance();
            else break;
        }
        ExpectSym('}', "to close the table");
        if (e->args.size() > 50) Error(e->line, "tables of more than 50 items are not supported");
        return e;
    }

    ExprP Primary() {
        if (tok_.t == T_NAME) {
            auto e = std::make_unique<Expr>();
            e->kind = Expr::Name;
            e->line = tok_.line;
            e->str = Name();
            return e;
        }
        if (IsSym('(')) {
            Advance();
            ExprP e = Expression();
            ExpectSym(')', "to close the parenthesis");
            return e;
        }
        Error(tok_.line, "unexpected symbol");
    }

    ExprP Suffixed() {
        ExprP e = Primary();
        for (;;) {
            if (IsSym('.')) {
                Advance();
                auto x = std::make_unique<Expr>();
                x->kind = Expr::Index;
                x->line = tok_.line;
                x->str = Name();
                x->obj = std::move(e);
                e = std::move(x);
            } else if (IsSym('[')) {
                Error(tok_.line, "indexing with [] is not supported");
            } else if (IsSym(':')) {
                Advance();
                auto x = std::make_unique<Expr>();
                x->kind = Expr::Method;
                x->line = tok_.line;
                x->str = Name();
                x->obj = std::move(e);
                Args(*x);
                e = std::move(x);
            } else if (IsSym('(') || IsSym('{') || tok_.t == T_STRING) {
                auto x = std::make_unique<Expr>();
                x->kind = Expr::Call;
                x->line = tok_.line;
                x->obj = std::move(e);
                Args(*x);
                e = std::move(x);
            } else {
                return e;
            }
        }
    }

    void Args(Expr& call) {
        if (tok_.t == T_STRING) {
            auto s = std::make_unique<Expr>();
            s->kind = Expr::String;
            s->line = tok_.line;
            s->str = tok_.s;
            Advance();
            call.args.push_back(std::move(s));
            return;
        }
        if (IsSym('{')) {
            call.args.push_back(Table());
            return;
        }
        ExpectSym('(', "for the call arguments");
        if (!IsSym(')')) {
            for (;;) {
                call.args.push_back(Expression());
                if (!IsSym(',')) break;
                Advance();
            }
        }
        ExpectSym(')', "to close the call");
    }

    Lexer lex_;
    Token tok_;
};

// ---- code generation ----

int Int2Fb(unsigned x) {  // the "floating point byte" of NEWTABLE sizes (luaO_int2fb)
    int e = 0;
    while (x >= 16) {
        x = (x + 1) >> 1;
        ++e;
    }
    return x < 8 ? int(x) : ((e + 1) << 3) | (int(x) - 8);
}

class FuncState {
public:
    FuncState(FuncState* parent, int line) : parent_(parent) { line_ = line; }

    Proto Finish(const FuncDef* def, bool isMain) {
        Emit(ABC(RETURN, 0, 1, 0));
        if (f_.maxStack < 2) f_.maxStack = 2;
        if (isMain) {
            f_.isVararg = 2;  // VARARG_ISVARARG, as for every main chunk
        } else {
            f_.lineDefined = def->line;
            f_.lastLineDefined = def->lastLine;
            f_.numParams = static_cast<uint8_t>(def->params.size());
        }
        return std::move(f_);
    }

    void Params(const std::vector<std::string>& params) {
        for (const auto& p : params) {
            locals_.emplace_back(p, freereg_);
            Reserve(1);
        }
    }

    void Statements(const Block& b) {
        const size_t nlocals = locals_.size();
        const int free = freereg_;
        for (const auto& s : b) Statement(*s);
        locals_.resize(nlocals);
        freereg_ = free;
    }

private:
    // -- emitting --
    int Emit(uint32_t ins) {
        f_.code.push_back(ins);
        f_.lineInfo.push_back(line_);
        return int(f_.code.size()) - 1;
    }
    void Reserve(int n) {
        freereg_ += n;
        if (freereg_ > 250) Error(line_, "function needs too many registers");
        if (freereg_ > f_.maxStack) f_.maxStack = static_cast<uint8_t>(freereg_);
    }
    int Konst(const Const& c) {
        for (size_t i = 0; i < f_.k.size(); ++i)
            if (f_.k[i] == c) return int(i);
        f_.k.push_back(c);
        if (f_.k.size() > size_t(kMaxArgBx)) Error(line_, "too many constants");
        return int(f_.k.size()) - 1;
    }
    int KString(const std::string& s) {
        Const c;
        c.type = Const::String;
        c.s = s;
        return Konst(c);
    }
    int KNumber(double v) {
        Const c;
        c.type = Const::Number;
        c.n = static_cast<float>(v);
        return Konst(c);
    }
    int KBool(bool v) {
        Const c;
        c.type = Const::Bool;
        c.b = v;
        return Konst(c);
    }
    void PatchJump(int pc, int target) { f_.code[size_t(pc)] = AsBx(JMP, 0, target - (pc + 1)); }
    int Here() const { return int(f_.code.size()); }

    // -- names --
    int LocalReg(const std::string& name) const {
        for (size_t i = locals_.size(); i-- > 0;)
            if (locals_[i].first == name) return locals_[i].second;
        return -1;
    }
    bool OuterLocal(const std::string& name) const {
        for (const FuncState* p = parent_; p; p = p->parent_)
            if (p->LocalReg(name) >= 0) return true;
        return false;
    }
    int Resolve(const Expr& e) const {  // a local's register, or -1 for a global
        const int r = LocalReg(e.str);
        if (r < 0 && OuterLocal(e.str))
            Error(e.line, "'" + e.str + "' is a local of an enclosing function (upvalues are not supported)");
        return r;
    }

    // -- expressions --
    static bool IsCall(const Expr& e) { return e.kind == Expr::Call || e.kind == Expr::Method; }

    // Puts the value of `e` into register `r` (already reserved). Temporaries above freereg are released again.
    void ToReg(const Expr& e, int r) {
        line_ = e.line;
        const int free = freereg_;
        switch (e.kind) {
            case Expr::Nil: Emit(ABC(LOADNIL, r, r, 0)); break;
            case Expr::True: Emit(ABC(LOADBOOL, r, 1, 0)); break;
            case Expr::False: Emit(ABC(LOADBOOL, r, 0, 0)); break;
            case Expr::Number: Emit(ABx(LOADK, r, KNumber(e.num))); break;
            case Expr::String: Emit(ABx(LOADK, r, KString(e.str))); break;
            case Expr::Name: {
                const int l = Resolve(e);
                if (l >= 0) {
                    if (l != r) Emit(ABC(MOVE, r, l, 0));
                } else Emit(ABx(GETGLOBAL, r, KString(e.str)));
                break;
            }
            case Expr::Index: {
                int o = -1;
                if (e.obj->kind == Expr::Name) o = Resolve(*e.obj);
                if (o < 0) {
                    ToReg(*e.obj, r);
                    o = r;
                }
                line_ = e.line;
                Emit(ABC(GETTABLE, r, o, RKString(e.str)));
                break;
            }
            case Expr::Call: case Expr::Method: {
                if (r == freereg_ - 1) {
                    freereg_ = r;
                    CallAt(e, 1, false);
                } else {
                    const int base = freereg_;
                    CallAt(e, 1, false);
                    Emit(ABC(MOVE, r, base, 0));
                }
                break;
            }
            case Expr::Function: Emit(ABx(CLOSURE, r, Function(*e.fn))); break;
            case Expr::Table: {
                if (r == freereg_ - 1) {
                    TableAt(e, r);
                } else {
                    const int t = freereg_;
                    Reserve(1);
                    TableAt(e, t);
                    Emit(ABC(MOVE, r, t, 0));
                }
                break;
            }
            case Expr::Compare: Error(e.line, "a comparison can only be used as an if / elseif condition");
        }
        freereg_ = free;
    }

    // NEWTABLE at t (= freereg - 1), the items in t+1.., SETLIST
    void TableAt(const Expr& e, int t) {
        const int n = int(e.args.size());
        line_ = e.line;
        Emit(ABC(NEWTABLE, t, Int2Fb(unsigned(n)), 0));
        for (const auto& item : e.args) {
            const int r = freereg_;
            Reserve(1);
            ToReg(*item, r);
        }
        line_ = e.line;
        if (n) Emit(ABC(SETLIST, t, n, 1));
        freereg_ = t + 1;
    }

    // A register holding `e`: a local's own register, else a new one at freereg (the caller releases it).
    int AnyReg(const Expr& e) {
        if (e.kind == Expr::Name) {
            const int l = Resolve(e);
            if (l >= 0) return l;
        }
        const int r = freereg_;
        Reserve(1);
        ToReg(e, r);
        return r;
    }

    int RKString(const std::string& s) {
        const int k = KString(s);
        if (k > kMaxIndexRK) Error(line_, "too many constants for an RK operand");
        return k | kBitRK;
    }

    // RK operand: constants directly, everything else in a register (the caller releases it).
    int RKExpr(const Expr& e) {
        int k = -1;
        switch (e.kind) {
            case Expr::Nil: { Const c; k = Konst(c); break; }
            case Expr::True: k = KBool(true); break;
            case Expr::False: k = KBool(false); break;
            case Expr::Number: k = KNumber(e.num); break;
            case Expr::String: k = KString(e.str); break;
            default: break;
        }
        if (k >= 0 && k <= kMaxIndexRK) return k | kBitRK;
        return AnyReg(e);
    }

    // A call with its function at base = freereg: CALL base, nargs+1, nresults+1 (or TAILCALL ... 0).
    // Afterwards freereg = base + nresults (base for a tail call).
    void CallAt(const Expr& e, int nresults, bool tail) {
        const int base = freereg_;
        Reserve(1);
        int extra = 0;
        if (e.kind == Expr::Method) {
            int o = e.obj->kind == Expr::Name ? Resolve(*e.obj) : -1;  // a local object is used in place
            if (o < 0) {
                ToReg(*e.obj, base);
                o = base;
            }
            line_ = e.line;
            Reserve(1);  // base+1: the object (self)
            Emit(ABC(SELF, base, o, RKString(e.str)));
            extra = 1;
        } else {
            ToReg(*e.obj, base);
        }
        for (const auto& a : e.args) {
            const int r = freereg_;
            Reserve(1);
            ToReg(*a, r);
        }
        line_ = e.line;
        const int nargs = int(e.args.size()) + extra;
        if (tail) {
            Emit(ABC(TAILCALL, base, nargs + 1, 0));
            freereg_ = base;
        } else {
            Emit(ABC(CALL, base, nargs + 1, nresults + 1));
            freereg_ = base + nresults;
            if (freereg_ > f_.maxStack) f_.maxStack = static_cast<uint8_t>(freereg_);
        }
    }

    int Function(const FuncDef& def) {
        FuncState child(this, def.line);
        child.Params(def.params);
        child.Statements(def.body);
        child.line_ = def.lastLine;
        f_.p.push_back(child.Finish(&def, false));
        return int(f_.p.size()) - 1;
    }

    // Emits the test of `cond` and a JMP taken when it is false; returns the JMP's pc (patched later).
    int JumpIfFalse(const Expr& cond) {
        const int free = freereg_;
        if (cond.kind == Expr::Compare) {
            const int l = RKExpr(*cond.obj);
            const int r = RKExpr(*cond.rhs);
            line_ = cond.line;
            const std::string& op = cond.str;
            if (op == "==") Emit(ABC(EQ, 0, l, r));
            else if (op == "~=") Emit(ABC(EQ, 1, l, r));
            else if (op == "<") Emit(ABC(LT, 0, l, r));
            else if (op == "<=") Emit(ABC(LE, 0, l, r));
            else if (op == ">") Emit(ABC(LT, 0, r, l));
            else Emit(ABC(LE, 0, r, l));  // ">="
        } else {
            const int r = AnyReg(cond);
            line_ = cond.line;
            Emit(ABC(TEST, r, 0, 0));
        }
        freereg_ = free;
        return Emit(AsBx(JMP, 0, 0));
    }

    // -- statements --
    void Statement(const Stmt& s) {
        line_ = s.line;
        const int free = freereg_;
        switch (s.kind) {
            case Stmt::Local: {
                const int r = freereg_;
                Reserve(1);
                if (s.value) ToReg(*s.value, r);
                else Emit(ABC(LOADNIL, r, r, 0));
                locals_.emplace_back(s.name, r);
                return;  // the local keeps its register
            }
            case Stmt::Assign: {
                const Expr& t = *s.target;
                if (t.kind == Expr::Name) {
                    const int l = Resolve(t);
                    if (l >= 0) {
                        ToReg(*s.value, l);
                    } else {
                        const int name = KString(t.str);  // the name's constant first, as the Lua compiler does
                        const int r = AnyReg(*s.value);
                        line_ = s.line;
                        Emit(ABx(SETGLOBAL, r, name));
                    }
                } else {  // obj.key = value
                    int o = -1;
                    if (t.obj->kind == Expr::Name) o = Resolve(*t.obj);
                    if (o < 0) o = AnyReg(*t.obj);
                    const int key = RKString(t.str);
                    const int v = RKExpr(*s.value);
                    line_ = s.line;
                    Emit(ABC(SETTABLE, o, key, v));
                }
                break;
            }
            case Stmt::CallStmt: CallAt(*s.value, 0, false); break;
            case Stmt::If: {
                std::vector<int> toEnd;
                for (size_t i = 0; i < s.branches.size(); ++i) {
                    const int skip = JumpIfFalse(*s.branches[i].first);
                    Statements(s.branches[i].second);
                    if (i + 1 < s.branches.size() || s.hasElse) {
                        line_ = s.line;
                        toEnd.push_back(Emit(AsBx(JMP, 0, 0)));
                    }
                    PatchJump(skip, Here());
                }
                if (s.hasElse) Statements(s.elseBlock);
                for (int j : toEnd) PatchJump(j, Here());
                break;
            }
            case Stmt::Return: {
                if (!s.value) {
                    Emit(ABC(RETURN, 0, 1, 0));
                } else if (IsCall(*s.value)) {
                    const int base = freereg_;
                    CallAt(*s.value, 0, true);
                    line_ = s.line;
                    Emit(ABC(RETURN, base, 0, 0));
                } else {
                    const int r = AnyReg(*s.value);
                    line_ = s.line;
                    Emit(ABC(RETURN, r, 2, 0));
                }
                break;
            }
        }
        freereg_ = free;
    }

    Proto f_;
    FuncState* parent_;
    std::vector<std::pair<std::string, int>> locals_;
    int freereg_ = 0;
    int line_ = 0;
};

}  // namespace

bool Compile(const std::string& source, Proto& main, std::string* error) {
    try {
        Parser parser(source);
        const Block chunk = parser.Chunk();
        FuncState fs(nullptr, 0);
        fs.Statements(chunk);
        main = fs.Finish(nullptr, true);
        const std::string problem = Check(main);
        if (!problem.empty()) {
            if (error) *error = "internal: the compiled code fails the loader's check: " + problem;
            return false;
        }
        return true;
    } catch (const CompileError& e) {
        if (error) *error = e.what();
        return false;
    }
}

}  // namespace mc::game::lua51
