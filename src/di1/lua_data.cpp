// SPDX-License-Identifier: GPL-3.0-only
#include "di1/lua_data.hpp"
#include "mods/mod_index.hpp"
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

namespace crabe::di1::data {
namespace {
[[noreturn]] void reject(const std::string& reason) { throw std::runtime_error("data chunk: " + reason); }

void assign(Table& table, const Value& key, Value value) {
    switch (key.type) {
    case Value::Type::String: table.fields[key.string] = std::move(value); return;
    case Value::Type::Number:
        if (!std::isfinite(key.number)) reject("nonfinite table key");
        table.indices[key.number] = std::move(value); return;
    case Value::Type::Boolean: table.flags[key.boolean] = std::move(value); return;
    default: reject("unsupported table key");
    }
}

class Reader {
public:
    explicit Reader(std::string_view bytes) : data(bytes) {}
    unsigned char byte() { need(1); return static_cast<unsigned char>(data[at++]); }
    std::uint32_t word() {
        need(4); std::uint32_t value{}; std::memcpy(&value, data.data() + at, 4); at += 4; return value;
    }
    double number() { need(8); double value{}; std::memcpy(&value, data.data() + at, 8); at += 8; return value; }
    std::string string() {
        const auto size = word();
        if (!size) return {};
        need(size);
        if (data[at + size - 1] != '\0') reject("unterminated string constant");
        std::string value(data.substr(at, size - 1)); at += size; return value;
    }
    void skip(std::size_t count) { need(count); at += count; }
    std::size_t remaining() const noexcept { return data.size() - at; }
private:
    void need(std::size_t count) const { if (count > data.size() - at) reject("truncated binary"); }
    std::string_view data;
    std::size_t at = 12;
};

// Lua 5.1 opcodes used by data chunks.
enum : unsigned { MOVE = 0, LOADK = 1, LOADBOOL = 2, LOADNIL = 3, GETGLOBAL = 5, SETGLOBAL = 7,
    SETTABLE = 9, NEWTABLE = 10, RETURN = 30, SETLIST = 34 };

class Parser {
public:
    explicit Parser(std::string_view text) : source(text) {}
    Globals chunk() {
        Globals globals;
        for (space(); at < source.size(); space()) {
            if (word("return")) { skipReturn(); break; }
            const auto name = identifier();
            if (name.empty()) reject("expected a global assignment");
            space(); expect('=');
            Value key; key.type = Value::Type::String; key.string = name;
            assign(globals, key, value());
            space(); if (peek() == ';') ++at;
        }
        return globals;
    }
private:
    char peek() const noexcept { return at < source.size() ? source[at] : '\0'; }
    void expect(char ch) { if (peek() != ch) reject(std::string("expected '") + ch + "'"); ++at; }
    void space() {
        for (;;) {
            while (at < source.size() && std::strchr(" \t\r\n\f\v", source[at]) && source[at]) ++at;
            if (source.compare(at, 2, "--") != 0) return;
            at += 2;
            if (const auto level = longBracket(); level >= 0) { longString(level); continue; }
            while (at < source.size() && source[at] != '\n') ++at;
        }
    }
    bool word(std::string_view keyword) {
        if (source.compare(at, keyword.size(), keyword) != 0) return false;
        const auto next = at + keyword.size();
        if (next < source.size() && (std::isalnum(static_cast<unsigned char>(source[next])) || source[next] == '_')) return false;
        at = next; return true;
    }
    std::string identifier() {
        const auto start = at;
        if (at < source.size() && (std::isalpha(static_cast<unsigned char>(source[at])) || source[at] == '_'))
            while (at < source.size() && (std::isalnum(static_cast<unsigned char>(source[at])) || source[at] == '_')) ++at;
        return std::string(source.substr(start, at - start));
    }
    // A trailing "return <name>" or "return" ends the data; nothing may follow.
    void skipReturn() {
        space(); identifier(); space(); if (peek() == ';') ++at; space();
        if (at != source.size()) reject("statements after return");
    }
    int longBracket() {
        if (peek() != '[') return -1;
        std::size_t p = at + 1; int level = 0;
        while (p < source.size() && source[p] == '=') { ++p; ++level; }
        if (p >= source.size() || source[p] != '[') return -1;
        at = p + 1; return level;
    }
    std::string longString(int level) {
        const std::string close = "]" + std::string(static_cast<std::size_t>(level), '=') + "]";
        const auto end = source.find(close, at);
        if (end == std::string_view::npos) reject("unterminated long string");
        auto start = at;
        if (start < end && source[start] == '\r') ++start;
        if (start < end && source[start] == '\n') ++start;
        std::string result(source.substr(start, end - start)); at = end + close.size(); return result;
    }
    std::string quoted() {
        const char quote = source[at++];
        std::string result;
        for (;;) {
            if (at >= source.size() || source[at] == '\n') reject("unterminated string");
            const char ch = source[at++];
            if (ch == quote) return result;
            if (ch != '\\') { result += ch; continue; }
            if (at >= source.size()) reject("unterminated escape");
            const char escape = source[at++];
            switch (escape) {
            case 'n': result += '\n'; break; case 't': result += '\t'; break; case 'r': result += '\r'; break;
            case 'a': result += '\a'; break; case 'b': result += '\b'; break; case 'f': result += '\f'; break;
            case 'v': result += '\v'; break; case '\\': case '"': case '\'': result += escape; break;
            case '\n': result += '\n'; break;
            default:
                // Lua 5.1 keeps the character after an unknown escape.
                if (!std::isdigit(static_cast<unsigned char>(escape))) { result += escape; break; }
                int code = escape - '0';
                for (int i = 0; i < 2 && at < source.size() && std::isdigit(static_cast<unsigned char>(source[at])); ++i)
                    code = code * 10 + (source[at++] - '0');
                if (code > 255) reject("invalid decimal escape");
                result += static_cast<char>(code);
            }
        }
    }
    double numeral(bool negative) {
        const auto start = at;
        if (source.compare(at, 2, "0x") == 0 || source.compare(at, 2, "0X") == 0) {
            at += 2;
            while (at < source.size() && std::isxdigit(static_cast<unsigned char>(source[at]))) ++at;
        } else {
            while (at < source.size() && (std::isdigit(static_cast<unsigned char>(source[at])) || source[at] == '.')) ++at;
            if (at < source.size() && (source[at] == 'e' || source[at] == 'E')) {
                ++at; if (peek() == '+' || peek() == '-') ++at;
                while (at < source.size() && std::isdigit(static_cast<unsigned char>(source[at]))) ++at;
            }
        }
        const std::string text(source.substr(start, at - start));
        char* end{};
        const double parsed = text.starts_with("0x") || text.starts_with("0X") ?
            static_cast<double>(std::strtoull(text.c_str(), &end, 16)) : std::strtod(text.c_str(), &end);
        if (text.empty() || end != text.c_str() + text.size() || !std::isfinite(parsed)) reject("invalid number");
        if (at < source.size() && (std::isalpha(static_cast<unsigned char>(source[at])) || source[at] == '_')) reject("malformed number");
        return negative ? -parsed : parsed;
    }
    Value value() {
        space();
        Value result;
        if (++depth > 64) reject("nesting exceeds 64 tables");
        const char ch = peek();
        if (ch == '{') result = constructor();
        else if (ch == '"' || ch == '\'') { result.type = Value::Type::String; result.string = quoted(); }
        else if (ch == '[') {
            const auto level = longBracket();
            if (level < 0) reject("unexpected '['");
            result.type = Value::Type::String; result.string = longString(level);
        }
        else if (ch == '-') { ++at; space(); result.type = Value::Type::Number; result.number = numeral(true); }
        else if (std::isdigit(static_cast<unsigned char>(ch)) || (ch == '.' && at + 1 < source.size() &&
            std::isdigit(static_cast<unsigned char>(source[at + 1])))) { result.type = Value::Type::Number; result.number = numeral(false); }
        else if (word("true")) { result.type = Value::Type::Boolean; result.boolean = true; }
        else if (word("false")) { result.type = Value::Type::Boolean; }
        else if (word("nil")) {}
        else reject("only literals and table constructors are data");
        --depth;
        space();
        if (peek() && std::strchr("+*/%^.:(", peek())) reject("expressions are not data");
        return result;
    }
    Value constructor() {
        expect('{');
        Value result; result.type = Value::Type::Table; result.table = std::make_shared<Table>();
        double next = 1;
        for (;;) {
            space();
            if (peek() == '}') { ++at; return result; }
            Value key;
            const auto bracket = at;
            const bool computed = peek() == '[' && longBracket() < 0;
            at = bracket;
            if (computed) {
                ++at; key = value(); expect(']'); space(); expect('=');
            } else {
                const auto mark = at;
                const auto name = identifier();
                space();
                if (!name.empty() && peek() == '=' && source.compare(at, 2, "==") != 0 &&
                    name != "true" && name != "false" && name != "nil") {
                    ++at; key.type = Value::Type::String; key.string = name;
                } else {
                    at = mark; key.type = Value::Type::Number; key.number = next++;
                }
            }
            auto item = value();
            // Lua drops nil entries but still advances positional indices.
            if (item.type != Value::Type::Nil) assign(*result.table, key, std::move(item));
            space();
            if (peek() == ',' || peek() == ';') { ++at; continue; }
            if (peek() != '}') reject("expected ',' or '}' in table");
        }
    }
    std::string_view source;
    std::size_t at{};
    int depth{};
};
}

const Value* Table::field(std::string_view key) const {
    const auto found = fields.find(std::string(key));
    return found == fields.end() ? nullptr : &found->second;
}
const Table* Table::table(std::string_view key) const {
    const auto* value = field(key);
    return value && value->isTable() ? value->table.get() : nullptr;
}
std::string Table::text(std::string_view key) const {
    const auto* value = field(key);
    return value && value->isString() ? value->string : std::string{};
}

Globals evaluateBinary(std::string_view chunk) {
    if (chunk.size() < 12 || chunk.substr(0, 4) != std::string_view("\x1bLua", 4) ||
        static_cast<unsigned char>(chunk[4]) != 0x51 || chunk[5] != 0 || chunk[6] != 1 || chunk[7] != 4 ||
        chunk[8] != 4 || chunk[9] != 4 || chunk[10] != 8 || chunk[11] != 0) reject("not a PC Lua 5.1 binary");
    Reader reader(chunk);
    reader.string(); reader.word(); reader.word();
    const unsigned upvalues = reader.byte(); reader.byte(); reader.byte();
    const unsigned stack = reader.byte();
    if (upvalues) reject("upvalues are not data");
    const auto codeCount = reader.word();
    if (codeCount > reader.remaining() / 4) reject("invalid instruction count");
    std::vector<std::uint32_t> code(codeCount);
    for (auto& instruction : code) instruction = reader.word();
    const auto constantCount = reader.word();
    if (constantCount > reader.remaining()) reject("invalid constant count");
    std::vector<Value> constants(constantCount);
    for (auto& constant : constants) {
        switch (reader.byte()) {
        case 0: break;
        case 1: constant.type = Value::Type::Boolean; constant.boolean = reader.byte() != 0; break;
        case 3: constant.type = Value::Type::Number; constant.number = reader.number(); break;
        case 4: constant.type = Value::Type::String; constant.string = reader.string(); break;
        default: reject("unknown constant type");
        }
    }
    if (reader.word()) reject("nested functions are not data");
    // Debug sections: line info, locals and upvalue names.
    const auto lines = reader.word();
    if (lines > reader.remaining() / 4) reject("invalid line information");
    reader.skip(lines * 4ull);
    const auto locals = reader.word();
    for (std::uint32_t i = 0; i < locals; ++i) { reader.string(); reader.word(); reader.word(); }
    const auto names = reader.word();
    for (std::uint32_t i = 0; i < names; ++i) reader.string();
    if (reader.remaining()) reject("trailing bytes after the main function");

    Globals globals;
    std::vector<Value> registers(stack ? stack : 1);
    const auto reg = [&](unsigned index) -> Value& {
        if (index >= registers.size()) reject("register out of range");
        return registers[index];
    };
    const auto rk = [&](unsigned operand) -> const Value& {
        if (operand & 0x100) {
            if ((operand & 0xFF) >= constants.size()) reject("constant out of range");
            return constants[operand & 0xFF];
        }
        return reg(operand);
    };
    const auto constant = [&](unsigned index) -> const Value& {
        if (index >= constants.size()) reject("constant out of range");
        return constants[index];
    };
    for (std::size_t pc = 0; pc < code.size(); ++pc) {
        const auto i = code[pc];
        const unsigned op = i & 0x3F, a = (i >> 6) & 0xFF, c = (i >> 14) & 0x1FF, b = (i >> 23) & 0x1FF, bx = i >> 14;
        switch (op) {
        case MOVE: reg(a) = reg(b); break;
        case LOADK: reg(a) = constant(bx); break;
        case LOADBOOL: reg(a) = Value{}; reg(a).type = Value::Type::Boolean; reg(a).boolean = b != 0; if (c) ++pc; break;
        case LOADNIL: for (unsigned r = a; r <= b; ++r) reg(r) = Value{}; break;
        case GETGLOBAL: {
            const auto& name = constant(bx);
            if (!name.isString()) reject("invalid global name");
            // Only globals this chunk assigned; anything else would come from
            // the game's environment, which a startup evaluation cannot see.
            const auto* found = globals.field(name.string);
            if (!found) reject("reads undefined global " + name.string);
            reg(a) = *found;
            break;
        }
        case SETGLOBAL: {
            const auto& name = constant(bx);
            if (!name.isString()) reject("invalid global name");
            assign(globals, name, reg(a));
            break;
        }
        case NEWTABLE: reg(a) = Value{}; reg(a).type = Value::Type::Table; reg(a).table = std::make_shared<Table>(); break;
        case SETTABLE: {
            auto& target = reg(a);
            if (!target.isTable()) reject("indexing a non-table");
            const auto& value = rk(c);
            if (value.type == Value::Type::Nil) break;
            assign(*target.table, rk(b), value);
            break;
        }
        case SETLIST: {
            auto& target = reg(a);
            if (!target.isTable() || !b) reject("unsupported SETLIST");
            std::uint32_t block = c;
            if (!block) { if (++pc >= code.size()) reject("truncated SETLIST"); block = code[pc]; }
            for (unsigned item = 1; item <= b; ++item) {
                const auto& value = reg(a + item);
                if (value.type == Value::Type::Nil) continue;
                target.table->indices[static_cast<double>((block - 1ull) * 50 + item)] = value;
            }
            break;
        }
        case RETURN: return globals;
        default: reject("opcode " + std::to_string(op) + " is not pure data");
        }
    }
    reject("missing RETURN");
}

Globals parseText(std::string_view source) {
    if (source.find('\0') != std::string_view::npos) reject("text contains NUL bytes");
    return Parser(source).chunk();
}

Globals evaluate(std::string_view chunk) {
    return chunk.substr(0, 4) == std::string_view("\x1bLua", 4) ? evaluateBinary(chunk) : parseText(chunk);
}

std::string literal(const Value& value) {
    switch (value.type) {
    case Value::Type::Nil: return "nil";
    case Value::Type::Boolean: return value.boolean ? "true" : "false";
    case Value::Type::Number: {
        if (!std::isfinite(value.number)) reject("nonfinite number");
        char text[40]; std::snprintf(text, sizeof text, "%.17g", value.number); return text;
    }
    case Value::Type::String: return mods::luaQuote(value.string);
    case Value::Type::Table: {
        if (!value.table) return "{}";
        std::string result = "{";
        for (const auto& [key, item] : value.table->indices) {
            Value number; number.type = Value::Type::Number; number.number = key;
            result += "[" + literal(number) + "]=" + literal(item) + ",";
        }
        for (const auto& [key, item] : value.table->fields) result += "[" + mods::luaQuote(key) + "]=" + literal(item) + ",";
        for (const auto& [key, item] : value.table->flags) result += std::string("[") + (key ? "true" : "false") + "]=" + literal(item) + ",";
        return result + "}";
    }
    }
    return "nil";
}

const Value* path(const Table& root, std::initializer_list<std::string_view> keys) {
    const Table* current = &root;
    const Value* found = nullptr;
    for (const auto key : keys) {
        if (!current) return nullptr;
        found = current->field(key);
        if (!found) return nullptr;
        current = found->isTable() ? found->table.get() : nullptr;
    }
    return found;
}
}
