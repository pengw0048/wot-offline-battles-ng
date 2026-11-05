#include "room_json.hpp"
#include <charconv>
#include <iomanip>
#include <locale>
#include <sstream>

namespace wot::room {
namespace {
struct Reader {
    const std::string& input;
    std::size_t at = 0;
    [[noreturn]] void fail(const char* why) const { throw std::invalid_argument(std::string("JSON ") + why + " at byte " + std::to_string(at)); }
    char peek() const { return at < input.size() ? input[at] : '\0'; }
    void spaces() { while (peek() == ' ' || peek() == '\r' || peek() == '\n' || peek() == '\t') ++at; }
    bool take(char c) { if (peek() != c) return false; ++at; return true; }
    void expect(char c) { if (!take(c)) fail("unexpected token"); }
    void literal(const char* text) { for (; *text; ++text) expect(*text); }
    unsigned hex() {
        unsigned n = 0;
        for (unsigned i = 0; i < 4; ++i) {
            const char c = peek(); ++at;
            n <<= 4;
            if (c >= '0' && c <= '9') n += c - '0';
            else if (c >= 'a' && c <= 'f') n += c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') n += c - 'A' + 10;
            else fail("invalid unicode escape");
        }
        return n;
    }
    static void utf8(std::string& out, unsigned cp) {
        if (cp < 0x80) out += static_cast<char>(cp);
        else if (cp < 0x800) {
            out += static_cast<char>(0xc0 | (cp >> 6)); out += static_cast<char>(0x80 | (cp & 63));
        } else if (cp < 0x10000) {
            out += static_cast<char>(0xe0 | (cp >> 12)); out += static_cast<char>(0x80 | ((cp >> 6) & 63));
            out += static_cast<char>(0x80 | (cp & 63));
        } else {
            out += static_cast<char>(0xf0 | (cp >> 18)); out += static_cast<char>(0x80 | ((cp >> 12) & 63));
            out += static_cast<char>(0x80 | ((cp >> 6) & 63)); out += static_cast<char>(0x80 | (cp & 63));
        }
    }
    std::string string() {
        expect('"'); std::string out;
        while (at < input.size()) {
            unsigned char c = static_cast<unsigned char>(input[at++]);
            if (c == '"') return out;
            if (c < 32) fail("control character in string");
            if (c != '\\') { out += static_cast<char>(c); continue; }
            c = static_cast<unsigned char>(peek()); ++at;
            switch (c) {
            case '"': case '\\': case '/': out += static_cast<char>(c); break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                unsigned cp = hex();
                if (cp >= 0xd800 && cp <= 0xdbff) {
                    expect('\\'); expect('u'); const auto low = hex();
                    if (low < 0xdc00 || low > 0xdfff) fail("invalid unicode pair");
                    cp = 0x10000 + ((cp - 0xd800) << 10) + low - 0xdc00;
                } else if (cp >= 0xdc00 && cp <= 0xdfff) fail("unpaired unicode surrogate");
                utf8(out, cp); break;
            }
            default: fail("invalid string escape");
            }
        }
        fail("unterminated string");
    }
    Value numeric() {
        const auto begin = at;
        take('-');
        if (!take('0')) {
            if (peek() < '1' || peek() > '9') fail("invalid number");
            while (peek() >= '0' && peek() <= '9') ++at;
        }
        bool real = false;
        if (take('.')) {
            real = true;
            if (peek() < '0' || peek() > '9') fail("invalid fraction");
            while (peek() >= '0' && peek() <= '9') ++at;
        }
        if (peek() == 'e' || peek() == 'E') {
            ++at; real = true; if (peek() == '+' || peek() == '-') ++at;
            if (peek() < '0' || peek() > '9') fail("invalid exponent");
            while (peek() >= '0' && peek() <= '9') ++at;
        }
        const auto token = input.substr(begin, at - begin);
        if (!real) {
            if (token[0] == '-') {
                std::int64_t n; const auto result = std::from_chars(token.data(), token.data() + token.size(), n);
                if (result.ec != std::errc()) fail("integer out of range");
                return integer(n);
            }
            std::uint64_t n; const auto result = std::from_chars(token.data(), token.data() + token.size(), n);
            if (result.ec != std::errc()) fail("integer out of range");
            auto v = integer(static_cast<std::int64_t>(n));
            v.unsigned_integer = n > std::uint64_t(std::numeric_limits<std::int64_t>::max());
            return v;
        }
        std::istringstream stream(token); stream.imbue(std::locale::classic());
        double n = 0.; stream >> n;
        if (!stream || !std::isfinite(n)) fail("nonfinite number");
        return number(n);
    }
    Value value(unsigned depth = 0) {
        if (depth > 128) fail("nesting limit");
        spaces();
        switch (peek()) {
        case '"': return text(string());
        case 'n': literal("null"); return {};
        case 't': literal("true"); return boolean(true);
        case 'f': literal("false"); return boolean(false);
        case '[': {
            ++at; auto out = array(); spaces(); if (take(']')) return out;
            do { out.array.push_back(value(depth + 1)); spaces(); if (take(']')) return out; expect(','); } while (true);
        }
        case '{': {
            ++at; auto out = object(); spaces(); if (take('}')) return out;
            do {
                spaces(); auto key = string(); spaces(); expect(':');
                if (out.object.count(key)) fail("duplicate object key");
                out.object.emplace(std::move(key), value(depth + 1)); spaces();
                if (take('}')) return out;
                expect(',');
            } while (true);
        }
        default: return numeric();
        }
    }
};
void quote(std::string& out, const std::string& s) {
    static const char hex[] = "0123456789abcdef";
    out += '"';
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') { out += '\\'; out += static_cast<char>(c); }
        else if (c < 32) { out += "\\u00"; out += hex[c >> 4]; out += hex[c & 15]; }
        else out += static_cast<char>(c);
    }
    out += '"';
}
void emit(std::string& out, const Value& raw, unsigned depth) {
    if (depth > 128) throw std::invalid_argument("JSON nesting limit");
    const auto& v = raw.resolved();
    switch (v.kind) {
    case Value::Kind::Null: out += "null"; break;
    case Value::Kind::Boolean: out += v.boolean ? "true" : "false"; break;
    case Value::Kind::Integer:
        out += v.unsigned_integer ? std::to_string(std::uint64_t(v.integer)) : std::to_string(v.integer); break;
    case Value::Kind::Number: {
        if (!std::isfinite(v.number)) throw std::invalid_argument("JSON nonfinite number");
        std::ostringstream stream; stream.imbue(std::locale::classic());
        stream << std::setprecision(std::numeric_limits<double>::max_digits10) << v.number;
        const auto encoded=stream.str();out+=encoded;
        if(encoded.find_first_of(".eE")==std::string::npos)out+=".0";
        break;
    }
    case Value::Kind::Text: quote(out, v.text); break;
    case Value::Kind::Array: {
        out += '['; bool first = true;
        for (const auto& row : v.array) { if (!first) out += ','; first = false; emit(out, row, depth + 1); }
        out += ']'; break;
    }
    case Value::Kind::Object: {
        out += '{'; bool first = true;
        for (const auto& row : v.object) {
            if (!first) out += ',';
            first = false;
            quote(out, row.first); out += ':'; emit(out, row.second, depth + 1);
        }
        out += '}'; break;
    }
    case Value::Kind::Shared: throw std::invalid_argument("JSON shared value has no target");
    }
}
}
Value parseJson(const std::string& input) {
    Reader r{input}; auto out = r.value(); r.spaces();
    if (r.at != input.size()) r.fail("trailing input");
    return out;
}
std::string writeJson(const Value& value) { std::string out; emit(out, value, 0); return out; }
}
