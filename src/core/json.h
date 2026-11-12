#pragma once

#include <string>
#include <utility>
#include <vector>

namespace kare {

struct Json {
    enum Type { Null, Bool, Num, Str, Arr, Obj } type = Null;
    bool b = false;
    double n = 0;
    std::string s;
    std::vector<Json> a;
    std::vector<std::pair<std::string, Json>> o;

    const Json* field(const std::string& key) const;
    std::string str(const std::string& key, const std::string& fallback = "") const;
    double num(const std::string& key, double fallback) const;
    bool boolean(const std::string& key, bool fallback) const;

    static Json strv(std::string v);
    static Json numv(double v);
    static Json boolv(bool v);
    static Json obj();
    static Json arr();
    void put(const std::string& key, Json v);

    std::string dump() const;
};

bool json_parse(const std::string& text, Json& out, std::string& err);
std::string json_escape(const std::string& s);

}  // namespace kare
