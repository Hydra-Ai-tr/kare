#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace kare {

struct Cp {
    uint32_t cp = 0;
};

std::vector<uint32_t> utf8_cps(const std::string& s);
std::string cps_utf8(const std::vector<uint32_t>& cps);
bool utf8_next(const std::string& s, size_t& i, uint32_t& cp);

uint32_t fold_cp(uint32_t cp);
std::string fold_tr(const std::string& s);

std::string tidy(std::string s);
std::string tidy_lines(const std::string& s);
std::wstring widen(const std::string& utf8);
std::string narrow(const std::wstring& w);

bool read_file(const std::string& path, std::vector<unsigned char>& out);
bool read_text(const std::string& path, std::string& out);
bool write_bytes(const std::string& path, const void* data, size_t n);
bool write_text(const std::string& path, const std::string& text);

std::string exe_dir();
void ensure_dir(const std::string& dir);

}  // namespace kare
