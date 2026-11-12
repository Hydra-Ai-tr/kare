#include "core/json.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <sstream>

namespace kare {

const Json* Json::field(const std::string& key) const {
    if (type != Obj) return nullptr;
    for (const auto& kv : o)
        if (kv.first == key) return &kv.second;
    return nullptr;
}

std::string Json::str(const std::string& key, const std::string& fallback) const {
    const Json* f = field(key);
    if (!f) return fallback;
    if (f->type == Str) return f->s;
    if (f->type == Num) {
        std::ostringstream os;
        if (std::floor(f->n) == f->n) os << static_cast<long long>(f->n);
        else os << f->n;
        return os.str();
    }
    return fallback;
}

double Json::num(const std::string& key, double fallback) const {
    const Json* f = field(key);
    if (!f) return fallback;
    if (f->type == Num) return f->n;
    if (f->type == Str) {
        try {
            return std::stod(f->s);
        } catch (...) {
            return fallback;
        }
    }
    return fallback;
}

bool Json::boolean(const std::string& key, bool fallback) const {
    const Json* f = field(key);
    if (!f || f->type != Bool) return fallback;
    return f->b;
}

Json Json::strv(std::string v) {
    Json j;
    j.type = Str;
    j.s = std::move(v);
    return j;
}
Json Json::numv(double v) {
    Json j;
    j.type = Num;
    j.n = v;
    return j;
}
Json Json::boolv(bool v) {
    Json j;
    j.type = Bool;
    j.b = v;
    return j;
}
Json Json::obj() {
    Json j;
    j.type = Obj;
    return j;
}
Json Json::arr() {
    Json j;
    j.type = Arr;
    return j;
}
void Json::put(const std::string& key, Json v) { o.emplace_back(key, std::move(v)); }

static void append_utf8(std::string& o, uint32_t cp) {
    if (cp < 0x80) o.push_back(static_cast<char>(cp));
    else if (cp < 0x800) {
        o.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        o.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        o.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        o.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        o.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        o.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        o.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        o.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        o.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

struct Parser {
    const std::string& in;
    size_t i = 0;
    std::string& err;
    Parser(const std::string& s, std::string& e) : in(s), err(e) {}

    void skip() {
        while (i < in.size()) {
            char c = in[i];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') i++;
            else break;
        }
    }
    bool fail(const char* m) {
        if (err.empty()) err = m;
        return false;
    }

    bool parse_string(std::string& out) {
        if (i >= in.size() || in[i] != '"') return fail("expected string");
        i++;
        out.clear();
        while (i < in.size()) {
            unsigned char c = static_cast<unsigned char>(in[i++]);
            if (c == '"') return true;
            if (c == '\\') {
                if (i >= in.size()) return fail("bad escape");
                char e = in[i++];
                switch (e) {
                    case '"':
                    case '\\':
                    case '/': out.push_back(e); break;
                    case 'b': out.push_back('\b'); break;
                    case 'f': out.push_back('\f'); break;
                    case 'n': out.push_back('\n'); break;
                    case 'r': out.push_back('\r'); break;
                    case 't': out.push_back('\t'); break;
                    case 'u': {
                        if (i + 4 > in.size()) return fail("bad unicode");
                        int cp = 0;
                        for (int k = 0; k < 4; k++) {
                            char h = in[i++];
                            cp <<= 4;
                            if (h >= '0' && h <= '9') cp += h - '0';
                            else if (h >= 'a' && h <= 'f') cp += h - 'a' + 10;
                            else if (h >= 'A' && h <= 'F') cp += h - 'A' + 10;
                            else return fail("bad unicode");
                        }
                        append_utf8(out, static_cast<uint32_t>(cp));
                        break;
                    }
                    default: return fail("bad escape");
                }
            } else {
                out.push_back(static_cast<char>(c));
            }
        }
        return fail("unterminated string");
    }

    bool parse_value(Json& out) {
        skip();
        if (i >= in.size()) return fail("unexpected end");
        char c = in[i];
        if (c == '"') {
            out.type = Json::Str;
            return parse_string(out.s);
        }
        if (c == '{') return parse_object(out);
        if (c == '[') return parse_array(out);
        if (c == 't' && in.compare(i, 4, "true") == 0) {
            out.type = Json::Bool;
            out.b = true;
            i += 4;
            return true;
        }
        if (c == 'f' && in.compare(i, 5, "false") == 0) {
            out.type = Json::Bool;
            out.b = false;
            i += 5;
            return true;
        }
        if (c == 'n' && in.compare(i, 4, "null") == 0) {
            out.type = Json::Null;
            i += 4;
            return true;
        }
        if (c == '-' || (c >= '0' && c <= '9')) {
            size_t start = i;
            if (in[i] == '-') i++;
            while (i < in.size() && in[i] >= '0' && in[i] <= '9') i++;
            if (i < in.size() && in[i] == '.') {
                i++;
                while (i < in.size() && in[i] >= '0' && in[i] <= '9') i++;
            }
            if (i < in.size() && (in[i] == 'e' || in[i] == 'E')) {
                i++;
                if (i < in.size() && (in[i] == '+' || in[i] == '-')) i++;
                while (i < in.size() && in[i] >= '0' && in[i] <= '9') i++;
            }
            try {
                out.type = Json::Num;
                out.n = std::stod(in.substr(start, i - start));
                return true;
            } catch (...) {
                return fail("bad number");
            }
        }
        return fail("unexpected json");
    }

    bool parse_object(Json& out) {
        out.type = Json::Obj;
        i++;
        skip();
        if (i < in.size() && in[i] == '}') {
            i++;
            return true;
        }
        while (i < in.size()) {
            skip();
            std::string key;
            if (!parse_string(key)) return false;
            skip();
            if (i >= in.size() || in[i] != ':') return fail("expected colon");
            i++;
            Json val;
            if (!parse_value(val)) return false;
            out.o.emplace_back(std::move(key), std::move(val));
            skip();
            if (i < in.size() && in[i] == ',') {
                i++;
                continue;
            }
            if (i < in.size() && in[i] == '}') {
                i++;
                return true;
            }
            return fail("expected closing brace");
        }
        return fail("unterminated object");
    }

    bool parse_array(Json& out) {
        out.type = Json::Arr;
        i++;
        skip();
        if (i < in.size() && in[i] == ']') {
            i++;
            return true;
        }
        while (i < in.size()) {
            Json val;
            if (!parse_value(val)) return false;
            out.a.push_back(std::move(val));
            skip();
            if (i < in.size() && in[i] == ',') {
                i++;
                continue;
            }
            if (i < in.size() && in[i] == ']') {
                i++;
                return true;
            }
            return fail("expected closing bracket");
        }
        return fail("unterminated array");
    }
};

bool json_parse(const std::string& text, Json& out, std::string& err) {
    Parser p(text, err);
    if (!p.parse_value(out)) return false;
    p.skip();
    return true;
}

static void dump_into(std::ostringstream& o, const Json& j) {
    switch (j.type) {
        case Json::Null: o << "null"; break;
        case Json::Bool: o << (j.b ? "true" : "false"); break;
        case Json::Num:
            if (std::isfinite(j.n) && std::floor(j.n) == j.n && std::abs(j.n) < 1e15)
                o << static_cast<long long>(j.n);
            else o << j.n;
            break;
        case Json::Str: o << '"' << json_escape(j.s) << '"'; break;
        case Json::Arr: {
            o << '[';
            for (size_t i = 0; i < j.a.size(); i++) {
                if (i) o << ',';
                dump_into(o, j.a[i]);
            }
            o << ']';
            break;
        }
        case Json::Obj: {
            o << '{';
            for (size_t i = 0; i < j.o.size(); i++) {
                if (i) o << ',';
                o << '"' << json_escape(j.o[i].first) << '"' << ':';
                dump_into(o, j.o[i].second);
            }
            o << '}';
            break;
        }
    }
}

std::string Json::dump() const {
    std::ostringstream o;
    dump_into(o, *this);
    return o.str();
}

std::string json_escape(const std::string& s) {
    std::string o;
    o.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            default:
                if (c < 0x20) {
                    char b[8];
                    std::snprintf(b, sizeof b, "\\u%04x", c);
                    o += b;
                } else o.push_back(static_cast<char>(c));
        }
    }
    return o;
}

}  // namespace kare
