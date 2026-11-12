#pragma once

#include <functional>
#include <string>
#include <vector>

namespace kare {

struct WebImage {
    std::string file;     // "File:Name.jpg" on Wikimedia Commons
    std::string source;   // thumbnail URL
    std::string path;     // downloaded copy
    std::string caption;  // English caption or description
    std::string artist;
    std::string license;
    int w = 0, h = 0;
    int section = 0;
};

struct WebSection {
    std::string heading;
    std::string text;
};

struct Research {
    std::string lang;
    std::string title;
    std::string url;
    std::string intro;
    std::vector<WebSection> sections;
    std::vector<WebImage> images;
    std::string error;
    bool ok() const { return !title.empty() && !intro.empty(); }
};

using Note = std::function<void(const std::string&)>;

std::string url_encode(const std::string& s);
std::string strip_html(const std::string& html);
std::string ascii_fold(const std::string& s);
// The topic of a request such as "Titanik olayını anlatan kısa video yap" -> "Titanik".
std::string research_query(const std::string& prompt);
bool wants_research(const std::string& prompt);
// Finds the article in `lang` (searching in `search_lang` first), then downloads up to max_images pictures.
Research research_topic(const std::string& query, const std::string& search_lang, const std::string& lang, int max_images,
                        const Note& note);
std::string research_brief(const Research& r, size_t max_chars);
std::string research_credits(const Research& r, bool turkish);
std::string media_dir();

}  // namespace kare
