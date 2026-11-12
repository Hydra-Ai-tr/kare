#include "core/utf8.h"

#include <cstdio>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace kare {

std::vector<uint32_t> utf8_cps(const std::string& s) {
    std::vector<uint32_t> out;
    size_t i = 0;
    uint32_t cp = 0;
    while (utf8_next(s, i, cp)) out.push_back(cp);
    return out;
}

std::string cps_utf8(const std::vector<uint32_t>& cps) {
    std::string o;
    for (uint32_t cp : cps) {
        if (cp < 0x80) {
            o.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
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
    return o;
}

bool utf8_next(const std::string& s, size_t& i, uint32_t& cp) {
    if (i >= s.size()) return false;
    auto c0 = static_cast<unsigned char>(s[i]);
    if (c0 < 0x80) {
        cp = c0;
        i += 1;
        return true;
    }
    if ((c0 & 0xE0) == 0xC0 && i + 1 < s.size()) {
        auto c1 = static_cast<unsigned char>(s[i + 1]);
        cp = ((c0 & 0x1F) << 6) | (c1 & 0x3F);
        i += 2;
        return true;
    }
    if ((c0 & 0xF0) == 0xE0 && i + 2 < s.size()) {
        auto c1 = static_cast<unsigned char>(s[i + 1]);
        auto c2 = static_cast<unsigned char>(s[i + 2]);
        cp = ((c0 & 0x0F) << 12) | ((c1 & 0x3F) << 6) | (c2 & 0x3F);
        i += 3;
        return true;
    }
    if ((c0 & 0xF8) == 0xF0 && i + 3 < s.size()) {
        auto c1 = static_cast<unsigned char>(s[i + 1]);
        auto c2 = static_cast<unsigned char>(s[i + 2]);
        auto c3 = static_cast<unsigned char>(s[i + 3]);
        cp = ((c0 & 0x07) << 18) | ((c1 & 0x3F) << 12) | ((c2 & 0x3F) << 6) | (c3 & 0x3F);
        i += 4;
        return true;
    }
    cp = c0;
    i += 1;
    return true;
}

uint32_t fold_cp(uint32_t cp) {
    if (cp == 0x130 || cp == 'I' || cp == 0x131 || cp == 'i') return 'i';
    if (cp >= 'A' && cp <= 'Z') return cp - 'A' + 'a';
    switch (cp) {
        case 0x11E: return 0x11F;  // Ğ ğ
        case 0x15E: return 0x15F;  // Ş ş
        case 0xD6: return 0xF6;    // Ö ö
        case 0xC7: return 0xE7;    // Ç ç
        case 0xDC: return 0xFC;    // Ü ü
        default: return cp;
    }
}

std::string fold_tr(const std::string& s) {
    auto cps = utf8_cps(s);
    for (auto& cp : cps) cp = fold_cp(cp);
    return cps_utf8(cps);
}

static bool is_space_cp(uint32_t cp) { return cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r'; }

static bool is_edge_punct(uint32_t cp) {
    return cp == '.' || cp == ',' || cp == ';' || cp == ':' || cp == '!' || cp == '?' || cp == '-' || cp == 0x2026;
}

std::string tidy(std::string s) {
    auto cps = utf8_cps(s);
    std::vector<uint32_t> out;
    bool space = false;
    for (uint32_t cp : cps) {
        if (is_space_cp(cp)) {
            space = !out.empty();
            continue;
        }
        if (space) {
            out.push_back(' ');
            space = false;
        }
        out.push_back(cp);
    }
    while (!out.empty() && (is_edge_punct(out.front()) || out.front() == ' ')) out.erase(out.begin());
    while (!out.empty() && out.back() != '!' && out.back() != '?' && out.back() != 0x2026 &&
           (is_edge_punct(out.back()) || out.back() == ' '))
        out.pop_back();
    return cps_utf8(out);
}

std::string tidy_lines(const std::string& s) {
    if (s.find('\n') == std::string::npos) return tidy(s);
    std::string out;
    std::vector<uint32_t> line;
    auto flush = [&] {
        std::vector<uint32_t> clean;
        bool space = false;
        for (uint32_t cp : line) {
            if (is_space_cp(cp)) {
                space = !clean.empty();
                continue;
            }
            if (space) clean.push_back(' ');
            space = false;
            clean.push_back(cp);
        }
        line.clear();
        if (clean.empty()) return;
        if (!out.empty()) out.push_back('\n');
        out += cps_utf8(clean);
    };
    for (uint32_t cp : utf8_cps(s)) {
        if (cp == '\n') flush();
        else if (cp != '\r') line.push_back(cp);
    }
    flush();
    return out;
}

std::wstring widen(const std::string& utf8) {
#ifdef _WIN32
    if (utf8.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (n <= 0) return L"";
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), w.data(), n);
    return w;
#else
    std::wstring w;
    for (unsigned char c : utf8) w.push_back(static_cast<wchar_t>(c));
    return w;
#endif
}

std::string narrow(const std::wstring& w) {
#ifdef _WIN32
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
#else
    std::string s;
    for (wchar_t c : w) s.push_back(static_cast<char>(c & 0xFF));
    return s;
#endif
}

static FILE* open_file(const std::string& path, const wchar_t* wmode, const char* mode) {
#ifdef _WIN32
    (void)mode;
    return _wfopen(widen(path).c_str(), wmode);
#else
    (void)wmode;
    return std::fopen(path.c_str(), mode);
#endif
}

bool read_file(const std::string& path, std::vector<unsigned char>& out) {
    FILE* f = open_file(path, L"rb", "rb");
    if (!f) return false;
    if (std::fseek(f, 0, SEEK_END) != 0) {
        std::fclose(f);
        return false;
    }
    long n = std::ftell(f);
    if (n < 0) {
        std::fclose(f);
        return false;
    }
    std::rewind(f);
    out.resize(static_cast<size_t>(n));
    size_t got = n ? std::fread(out.data(), 1, out.size(), f) : 0;
    std::fclose(f);
    return got == out.size();
}

bool read_text(const std::string& path, std::string& out) {
    std::vector<unsigned char> b;
    if (!read_file(path, b)) return false;
    out.assign(reinterpret_cast<const char*>(b.data()), b.size());
    if (out.size() >= 3 && static_cast<unsigned char>(out[0]) == 0xEF && static_cast<unsigned char>(out[1]) == 0xBB &&
        static_cast<unsigned char>(out[2]) == 0xBF) {
        out.erase(0, 3);
    }
    return true;
}

bool write_bytes(const std::string& path, const void* data, size_t n) {
    FILE* f = open_file(path, L"wb", "wb");
    if (!f) return false;
    size_t got = n ? std::fwrite(data, 1, n, f) : 0;
    std::fclose(f);
    return got == n;
}

bool write_text(const std::string& path, const std::string& text) {
    return write_bytes(path, text.data(), text.size());
}

std::string exe_dir() {
#ifdef _WIN32
    wchar_t buf[MAX_PATH * 4];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH * 4);
    if (!n) return ".";
    std::wstring w(buf, n);
    auto pos = w.find_last_of(L"\\/");
    if (pos == std::wstring::npos) return ".";
    return narrow(w.substr(0, pos));
#else
    return ".";
#endif
}

void ensure_dir(const std::string& dir) {
#ifdef _WIN32
    CreateDirectoryW(widen(dir).c_str(), nullptr);
#else
    (void)dir;
#endif
}

}  // namespace kare
