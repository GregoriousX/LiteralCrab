// SPDX-License-Identifier: GPL-3.0-only
// Read-only access to the game's data chunks (DNA, actor lists, data maps,
// behavior trees). Every shipped DI1 data chunk is a Lua 5.1 binary using only
// NEWTABLE/LOADK/LOADBOOL/SETTABLE/SETLIST/SETGLOBAL/RETURN, or plain Lua
// table text. Both are evaluated exactly; anything that is not pure data
// (calls, expressions, upvalues, loops) is rejected instead of approximated.
#pragma once
#include <initializer_list>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace crabe::di1::data {
struct Table;
struct Value {
    enum class Type { Nil, Boolean, Number, String, Table } type = Type::Nil;
    bool boolean{};
    double number{};
    std::string string;
    std::shared_ptr<Table> table;
    bool isTable() const noexcept { return type == Type::Table && table; }
    bool isString() const noexcept { return type == Type::String; }
};
struct Table {
    std::map<double, Value> indices;     // Number keys.
    std::map<std::string, Value> fields; // String keys (case-sensitive, like Lua).
    std::map<bool, Value> flags;         // Boolean keys, rare but valid data.
    const Value* field(std::string_view key) const;
    const Table* table(std::string_view key) const;
    // Empty when absent or not a string.
    std::string text(std::string_view key) const;
};
using Globals = Table;

// Throws std::runtime_error naming the unsupported construct.
Globals evaluateBinary(std::string_view chunk);
Globals parseText(std::string_view source);
// Binary or text, whichever the bytes are.
Globals evaluate(std::string_view chunk);
// Deterministic Lua literal for a parsed value; nonfinite numbers are rejected.
std::string literal(const Value& value);
// Nested lookup, for example path(dna, {"AgentGadgets", "EntityInstance"}).
const Value* path(const Table& root, std::initializer_list<std::string_view> keys);
}
