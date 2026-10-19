#pragma once
// A small compiler from a subset of Lua 5.1 to the game's chunk format (game/lua51.h: Lua 5.1 bytecode with 4-byte
// float numbers). No stock compiler writes that format, so our own scripts (game/freeride.h) are compiled by this one.
// It emits the same instruction patterns as the reference Lua 5.1 compiler for the constructs it supports, and every
// function it builds passes lua51::Check (the loader's code rules).
//
// Supported:
//   statements   function Name(params) ... end      (a global function)
//                local name [= expr]
//                name = expr / expr.field = expr
//                calls: f(args), a.b:method(args)
//                if cond then ... {elseif cond then ...} [else ...] end
//                return [expr]                        (a returned call is a tail call, as in Lua)
//   expressions  nil, true, false, numbers (also negative literals), strings ("..." / '...' with \n \t \\ \" \'
//                \ddd), names (locals, parameters, globals), a.b, calls and method calls, function(params) ... end,
//                list tables { e1, e2, ... } (up to 50 items)
//   conditions   (only in if / elseif) a == b, a ~= b, a < b, a <= b, a > b, a >= b, or any expression (truthiness)
//   comments     -- line, --[[ block ]]
// Not supported (a compile error names the line): loops, and / or / not, arithmetic, string concatenation, keyed
// table fields, varargs, multiple assignment / returns, and functions that use locals of an enclosing function
// (upvalues).
#include "game/lua51.h"

#include <string>

namespace mc::game::lua51 {

// Compiles `source` into a main function (no parameters, vararg, as the Lua compiler makes it). Line info is kept;
// the main function gets no source name (callers may set one). False with "line N: ..." on a compile error.
bool Compile(const std::string& source, Proto& main, std::string* error);

}  // namespace mc::game::lua51
