#include "core/director.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "core/audio.h"
#include "core/brain.h"
#include "core/github.h"
#include "core/render.h"
#include "core/tts.h"
#include "core/utf8.h"
#include "core/web.h"

namespace kare {

namespace {

bool is_digit(uint32_t cp) { return cp >= '0' && cp <= '9'; }
bool is_letter(uint32_t cp) {
    if (cp >= 'a' && cp <= 'z') return true;
    if (cp >= 'A' && cp <= 'Z') return true;
    return cp > 127 && cp != 0x201C && cp != 0x201D && cp != '"' && cp != '\'' && cp != 0x2022;
}

std::vector<uint32_t> needle(const char* s) {
    auto cps = utf8_cps(s);
    for (auto& cp : cps) cp = fold_cp(cp);
    return cps;
}

bool match_at(const std::vector<uint32_t>& folded, size_t i, const std::vector<uint32_t>& n) {
    if (i + n.size() > folded.size()) return false;
    for (size_t k = 0; k < n.size(); k++)
        if (folded[i + k] != n[k]) return false;
    return true;
}

bool contains(const std::vector<uint32_t>& folded, const char* s) {
    auto n = needle(s);
    if (n.empty() || folded.size() < n.size()) return false;
    for (size_t i = 0; i + n.size() <= folded.size(); i++)
        if (match_at(folded, i, n)) return true;
    return false;
}

void erase_range(std::vector<uint32_t>& cps, std::vector<uint32_t>& folded, size_t begin, size_t end) {
    if (end < begin) std::swap(begin, end);
    if (begin > cps.size()) begin = cps.size();
    if (end > cps.size()) end = cps.size();
    cps.erase(cps.begin() + static_cast<std::ptrdiff_t>(begin), cps.begin() + static_cast<std::ptrdiff_t>(end));
    folded.erase(folded.begin() + static_cast<std::ptrdiff_t>(begin), folded.begin() + static_cast<std::ptrdiff_t>(end));
}

struct Line {
    std::string text;
    bool item = false;
};

bool instruction_word(const std::string& folded_word) {
    static const char* words[] = {"dikey",     "dusey",    "düşey",     "yatay",     "horizontal", "vertical",
                                  "square",    "shorts",   "reels",     "hikaye",    "story",      "16:9",
                                  "9:16",      "1:1",      "sinematik", "cinematic", "minimal",    "sade",
                                  "format"};
    for (const char* w : words)
        if (folded_word == w) return true;
    return false;
}

std::string strip_instructions(const std::string& text) {
    std::string current;
    std::string out;
    auto flush = [&] {
        if (current.empty()) return;
        if (!instruction_word(fold_tr(current))) {
            if (!out.empty()) out.push_back(' ');
            out += current;
        }
        current.clear();
    };
    for (uint32_t cp : utf8_cps(text)) {
        if (cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r') {
            flush();
            continue;
        }
        current += cps_utf8(std::vector<uint32_t>{cp});
    }
    flush();
    return tidy(out);
}

std::vector<std::string> words_of(const std::string& text) {
    std::vector<std::string> words;
    std::string cur;
    for (uint32_t cp : utf8_cps(text)) {
        if (cp == ' ') {
            if (!cur.empty()) words.push_back(cur);
            cur.clear();
        } else {
            std::vector<uint32_t> one{cp};
            cur += cps_utf8(one);
        }
    }
    if (!cur.empty()) words.push_back(cur);
    return words;
}

std::string first_words(const std::string& text, int count) {
    auto w = words_of(text);
    std::string o;
    int n = std::min(count, static_cast<int>(w.size()));
    for (int i = 0; i < n; i++) {
        if (i) o.push_back(' ');
        o += w[static_cast<size_t>(i)];
    }
    return tidy(o);
}

std::vector<std::string> split_sentences(const std::string& text) {
    auto cps = utf8_cps(text);
    std::vector<std::string> out;
    std::vector<uint32_t> cur;
    auto flush = [&] {
        std::string s = tidy(cps_utf8(cur));
        cur.clear();
        if (!s.empty()) out.push_back(s);
    };
    for (size_t i = 0; i < cps.size(); i++) {
        uint32_t cp = cps[i];
        if (cp == '\n') {
            flush();
            continue;
        }
        cur.push_back(cp);
        bool end = cp == '!' || cp == '?' || cp == 0x2026 || cp == '.';
        if (!end) continue;
        // "15. yüzyıl" is an ordinal, "in 1912. The ship" ends a sentence.
        if (cp == '.' && i > 0 && is_digit(cps[i - 1]) && i + 2 < cps.size()) {
            uint32_t next = cps[i + 2];
            bool lower = (next >= 'a' && next <= 'z') || next == 0xE7 || next == 0x11F || next == 0x131 || next == 0xF6 ||
                         next == 0x15F || next == 0xFC || is_digit(next);
            if (lower) continue;
        }
        if (i + 1 == cps.size() || cps[i + 1] == ' ' || cps[i + 1] == '\n') flush();
    }
    flush();
    return out;
}

bool digit_between(const std::vector<uint32_t>& cps, size_t i) {
    return i > 0 && i + 1 < cps.size() && is_digit(cps[i - 1]) && is_digit(cps[i + 1]);
}

// "Head: a, b ve c" -> {"Head", "a", "b", "c"}; the trailing conjunction only splits real lists.
std::vector<std::string> split_clauses(const std::string& text) {
    auto cps = utf8_cps(text);
    std::vector<std::string> out;
    std::vector<uint32_t> cur;
    auto flush = [&] {
        std::string s = tidy(cps_utf8(cur));
        cur.clear();
        while (!s.empty() && (s.back() == '.' || s.back() == '!' || s.back() == '?')) s.pop_back();
        if (!s.empty()) out.push_back(s);
    };
    for (size_t i = 0; i < cps.size(); i++) {
        uint32_t cp = cps[i];
        bool sep = cp == ',' || cp == ';' || cp == ':' || cp == 0x2014;
        if ((cp == ',' || cp == ':') && digit_between(cps, i)) sep = false;
        if ((cp == '-' || cp == 0x2013) && i > 0 && i + 1 < cps.size() && cps[i - 1] == ' ' && cps[i + 1] == ' ') sep = true;
        if (sep) {
            flush();
            continue;
        }
        cur.push_back(cp);
    }
    flush();
    if (out.size() >= 2) {
        for (const char* conj : {" ve ", " and ", " & "}) {
            std::string last = out.back();
            auto p = last.find(conj);
            if (p == std::string::npos) continue;
            std::string a = tidy(last.substr(0, p)), b = tidy(last.substr(p + std::char_traits<char>::length(conj)));
            if (a.empty() || b.empty()) continue;
            out.back() = a;
            out.push_back(b);
            break;
        }
    }
    return out;
}

std::string cap_first(const std::string& s, bool turkish) {
    auto cps = utf8_cps(s);
    if (cps.empty()) return s;
    uint32_t& c = cps[0];
    if (c == 'i' && turkish) c = 0x130;
    else if (c >= 'a' && c <= 'z') c -= 32;
    else if (c == 0xE7 || c == 0xF6 || c == 0xFC) c -= 32;
    else if (c == 0x15F || c == 0x11F) c -= 1;
    else if (c == 0x131) c = 'I';
    return cps_utf8(cps);
}

bool has_colon_head(const std::string& sentence) {
    auto cps = utf8_cps(sentence);
    for (size_t i = 0; i < cps.size(); i++) {
        if (cps[i] == ',' && !digit_between(cps, i)) return false;
        if (cps[i] == ':' && !digit_between(cps, i)) return true;
    }
    return false;
}

std::vector<Line> split_lines(const std::string& text) {
    std::vector<Line> lines;
    std::string cur;
    auto push = [&] {
        std::string t = tidy(cur);
        cur.clear();
        if (t.empty()) return;
        Line line;
        auto cps = utf8_cps(t);
        size_t i = 0;
        while (i < cps.size() && cps[i] == ' ') i++;
        size_t start = i;
        if (i < cps.size() && (cps[i] == '-' || cps[i] == '*' || cps[i] == 0x2022)) {
            i++;
            while (i < cps.size() && cps[i] == ' ') i++;
            if (i > start + 1 && i < cps.size()) {
                line.item = true;
                line.text = tidy(cps_utf8(std::vector<uint32_t>(cps.begin() + static_cast<std::ptrdiff_t>(i), cps.end())));
                if (!line.text.empty()) lines.push_back(line);
                return;
            }
        }
        i = start;
        if (i < cps.size() && is_digit(cps[i])) {
            size_t j = i;
            while (j < cps.size() && is_digit(cps[j])) j++;
            if (j < cps.size() && (cps[j] == '.' || cps[j] == ')')) {
                j++;
                while (j < cps.size() && cps[j] == ' ') j++;
                if (j < cps.size()) {
                    line.item = true;
                    line.text = tidy(cps_utf8(std::vector<uint32_t>(cps.begin() + static_cast<std::ptrdiff_t>(j), cps.end())));
                    if (!line.text.empty()) lines.push_back(line);
                    return;
                }
            }
        }
        line.text = t;
        lines.push_back(line);
    };
    for (uint32_t cp : utf8_cps(text)) {
        if (cp == '\n') push();
        else {
            std::vector<uint32_t> one{cp};
            cur += cps_utf8(one);
        }
    }
    push();
    return lines;
}

void assign_time(std::vector<Scene>& scenes, double target) {
    if (scenes.empty()) return;
    if (target < 0.4) target = 0.4;
    std::vector<double> w(scenes.size(), 1.0);
    if (scenes.size() > 1) {
        w.front() = 0.85;
        w.back() = 0.9;
    }
    for (size_t i = 0; i < scenes.size(); i++) {
        if (scenes[i].text.size() > 80) w[i] += 0.25;
    }
    double sum = 0;
    for (double x : w) sum += x;
    for (size_t i = 0; i < scenes.size(); i++) scenes[i].seconds = target * w[i] / sum;
    const char* motions[] = {"zoom", "drift", "pan", "rise"};
    for (size_t i = 0; i < scenes.size(); i++) scenes[i].motion = motions[i % 4];
}

std::string detect_style(const std::vector<uint32_t>& folded) {
    if (contains(folded, "gün bat") || contains(folded, "gun bat") || contains(folded, "sunset") ||
        contains(folded, "dusk") || contains(folded, "golden hour") || contains(folded, "gün doğ") ||
        contains(folded, "gun dog") || contains(folded, "sunrise") || contains(folded, "şafak") || contains(folded, "dawn"))
        return "dusk";
    if (contains(folded, "neon") || contains(folded, "siber") || contains(folded, "cyber") || contains(folded, "teknoloji") ||
        contains(folded, "technology") || contains(folded, "yapay zeka") || contains(folded, "yazılım") ||
        contains(folded, "software") || contains(folded, "oyun") || contains(folded, "gaming"))
        return "neon";
    if (contains(folded, "uzay") || contains(folded, "galaksi") || contains(folded, "galaxy") || contains(folded, "space") ||
        contains(folded, "yıldız") || contains(folded, "stars"))
        return "night";
    if (contains(folded, "kağıt") || contains(folded, "kagit") || contains(folded, "paper")) return "paper";
    if (contains(folded, "minimal") || contains(folded, "sade")) return "minimal";
    if (contains(folded, "orman") || contains(folded, "forest") || contains(folded, "doğa") || contains(folded, "doga"))
        return "forest";
    if (contains(folded, "deniz") || contains(folded, "okyanus") || contains(folded, "ocean") || contains(folded, "sea") ||
        contains(folded, "sahil") || contains(folded, "beach"))
        return "sea";
    if (contains(folded, "gece") || contains(folded, "night") || contains(folded, "karanl")) return "night";
    return {};
}

std::string detect_aspect(const std::vector<uint32_t>& folded) {
    if (contains(folded, "9:16") || contains(folded, "dikey") || contains(folded, "düşey") || contains(folded, "dusey") ||
        contains(folded, "hikaye") || contains(folded, "shorts") || contains(folded, "reels") || contains(folded, "tiktok") ||
        contains(folded, "vertical"))
        return "9:16";
    if (contains(folded, "1:1") || contains(folded, "square")) return "1:1";
    return {};
}

double take_seconds(std::vector<uint32_t>& cps, std::vector<uint32_t>& folded) {
    double seconds = 0;
    for (size_t i = 0; i < folded.size();) {
        if (!is_digit(folded[i])) {
            i++;
            continue;
        }
        if (i > 0 && is_letter(folded[i - 1])) {
            i++;
            continue;
        }
        size_t start = i;
        double value = 0;
        while (i < folded.size() && is_digit(folded[i])) {
            value = value * 10.0 + static_cast<double>(folded[i] - '0');
            i++;
        }
        if (i < folded.size() && (folded[i] == '.' || folded[i] == ',')) {
            i++;
            double div = 10;
            while (i < folded.size() && is_digit(folded[i])) {
                value += static_cast<double>(folded[i] - '0') / div;
                div *= 10;
                i++;
            }
        }
        size_t unit = i;
        while (unit < folded.size() && folded[unit] == ' ') unit++;
        size_t unit_len = 0;
        if (match_at(folded, unit, needle("saniye"))) unit_len = needle("saniye").size();
        else if (match_at(folded, unit, needle("seconds"))) unit_len = needle("seconds").size();
        else if (match_at(folded, unit, needle("second"))) unit_len = needle("second").size();
        else if (match_at(folded, unit, needle("sec"))) unit_len = needle("sec").size();
        else if (match_at(folded, unit, needle("sn")) &&
                 (unit + 2 >= folded.size() || !is_letter(folded[unit + 2])))
            unit_len = 2;
        else if (match_at(folded, unit, needle("s")) && (unit + 1 >= folded.size() || !is_letter(folded[unit + 1])))
            unit_len = 1;
        if (!unit_len || value <= 0) {
            i = start + 1;
            continue;
        }
        seconds = value;
        erase_range(cps, folded, start, unit + unit_len);
        break;
    }
    return seconds;
}

std::vector<uint32_t> folded_of(const std::vector<uint32_t>& cps) {
    std::vector<uint32_t> folded;
    folded.reserve(cps.size());
    for (uint32_t cp : cps) folded.push_back(fold_cp(cp));
    return folded;
}

PlanResult plan_repo(const PlanInput& in, const std::string& owner, const std::string& name) {
    PlanResult result;
    PlanInput eff = in;
    std::string note = strip_github_url(in.prompt);
    auto cps = utf8_cps(note);
    auto folded = folded_of(cps);
    double hinted = take_seconds(cps, folded);
    if (eff.seconds <= 0 && hinted > 0) eff.seconds = std::min(120.0, std::max(2.0, hinted));
    if (eff.style.empty()) eff.style = detect_style(folded);
    if (eff.aspect.empty()) eff.aspect = detect_aspect(folded);

    RepoInfo info;
    std::string err;
    bool fetched = fetch_repo(owner, name, in.github_token, info, err);
    if (!fetched) {
        result.warning = err;
        info = RepoInfo{};
        info.owner = owner;
        info.name = name;
    }
    result.repo = info.owner + "/" + info.name;
    result.repo_note = repo_note(info, in.lang != "en");

    if (fetched && in.use_api) {
        PlanInput api = eff;
        api.prompt = "Create a short promo / explainer video storyboard for this GitHub project. "
                     "Use only the facts below; do not invent features. First scene names the project, "
                     "last scene shows github.com/" + info.owner + "/" + info.name + ".\n" +
                     std::string("On-screen language: ") + (in.lang == "en" ? "English" : "Turkish") + "\n" +
                     (note.empty() ? "" : "User request: " + note + "\n") + repo_brief(info);
        Storyboard board;
        std::string aerr;
        if (plan_with_api(api, board, aerr)) {
            if (!eff.aspect.empty()) board.aspect = eff.aspect;
            if (!eff.style.empty()) board.style = eff.style;
            board.mark = in.mark;
            board.prompt = in.prompt;
            apply_aspect_size(board);
            if (eff.seconds > 0) scale_duration(board, eff.seconds);
            normalize(board);
            snap_to_beat(board, eff.seconds);
            result.board = std::move(board);
            result.from_api = true;
            return result;
        }
        result.warning = aerr.empty() ? "API plan failed" : aerr;
    }
    result.board = plan_repo_local(info, eff);
    return result;
}

// ---------------------------------------------------------------- researched, narrated videos

bool turkish_letters(const std::string& s) {
    for (uint32_t cp : utf8_cps(s))
        if (cp == 0xE7 || cp == 0xC7 || cp == 0x11F || cp == 0x11E || cp == 0x131 || cp == 0x130 || cp == 0xF6 ||
            cp == 0xD6 || cp == 0x15F || cp == 0x15E || cp == 0xFC || cp == 0xDC)
            return true;
    return false;
}

bool prompt_is_turkish(const std::string& prompt) {
    if (turkish_letters(prompt)) return true;
    std::string f = " " + ascii_fold(prompt) + " ";
    for (const char* w : {" ve ", " bir ", " yap ", " anlatan ", " hakkinda ", " olayi", " nedir ", " kimdir ", " tarihi ",
                          " kisa ", " dikey ", " sesli ", " videosu "})
        if (f.find(w) != std::string::npos) return true;
    return false;
}

std::string narration_lang(const PlanInput& in, const std::vector<uint32_t>& folded) {
    if (in.voice == "tr" || in.voice == "en") return in.voice;
    if (in.voice != "auto" && in.voice != "none") {
        std::string l = voice_language(in.voice);
        if (!l.empty()) return l;
    }
    if (contains(folded, "ingilizce") || contains(folded, "english")) return "en";
    if (contains(folded, "türkçe") || contains(folded, "turkish")) return "tr";
    if (prompt_is_turkish(in.prompt)) return "tr";
    if (words_of(in.prompt).size() >= 3) return "en";
    return in.lang == "en" ? "en" : "tr";
}

bool asks_for_voice(const std::vector<uint32_t>& folded) {
    for (const char* k : {"sesli", "seslendir", "anlatım", "anlatıcı", "dublaj", "narrat", "voice", "spoken"})
        if (contains(folded, k)) return true;
    return false;
}

bool asks_for_shorts(const PlanInput& in, const std::vector<uint32_t>& folded) {
    if (in.shorts || in.aspect == "9:16") return true;
    if (!in.aspect.empty()) return false;
    std::string f = " " + ascii_fold(in.prompt) + " ";
    for (char& c : f)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))) c = ' ';
    if (f.find(" short ") != std::string::npos) return true;
    return detect_aspect(folded) == "9:16";
}

std::string pick_voice(const PlanInput& in, const std::string& lang, bool narrate_by_default, std::string& warning) {
    if (in.voice == "none") return "none";
    std::string want = in.voice == "auto" ? (narrate_by_default ? lang : "none") : in.voice;
    if (want == "none") return "none";
    if (voice_available(want)) return want;
    warning = (in.lang == "en" ? "No installed voice for: " : "Bu dil için yüklü ses yok: ") + want;
    return "none";
}

std::string pick_mood(const std::string& folded_text, bool shorts) {
    auto has = [&](std::initializer_list<const char*> keys) {
        for (const char* k : keys)
            if (folded_text.find(k) != std::string::npos) return true;
        return false;
    };
    if (has({"felaket", "facia", "trajedi", "olum", "oldu", "savas", "batt", "batis", "deprem", "katliam", "soykirim",
             "salgin", "kaza", "suikast", "disaster", "tragedy", "war ", "death", "died", "sank", "sinking", "earthquake",
             "massacre", "genocide", "pandemic", "crash", "killed", "assassin"}))
        return "ambient";
    if (has({"futbol", "spor", "yaris", "otomobil", "roket", "uzay", "teknoloji", "bilgisayar", "football", "sport",
             "racing", "rocket", "space", "technology", "computer", "olympic", "olimpiyat"}))
        return "drive";
    if (has({"sanat", "muzik", "sair", "yazar", "ressam", "sinema", "art ", "music", "poet", "writer", "painter",
             "novel", "roman "}))
        return "lofi";
    return shorts ? "pulse" : "ambient";
}

std::string clean_sentence(const std::string& s) {
    auto cps = utf8_cps(s);
    std::vector<uint32_t> out;
    int depth = 0;
    for (size_t i = 0; i < cps.size(); i++) {
        uint32_t c = cps[i];
        if (c == '(' || c == '[') {
            depth++;
            continue;
        }
        if (c == ')' || c == ']') {
            if (depth) depth--;
            continue;
        }
        if (depth) continue;
        if (c == '"' || c == 0x201C || c == 0x201D || c == 0xAB || c == 0xBB) continue;
        if ((c == ' ') && !out.empty() && out.back() == ' ') continue;
        if ((c == ',' || c == '.' || c == ';' || c == ':') && !out.empty() && out.back() == ' ') out.pop_back();
        out.push_back(c);
        if ((c == ',' || c == ';') && i + 1 < cps.size() && cps[i + 1] != ' ' && !is_digit(cps[i + 1]))
            out.push_back(' ');
    }
    return tidy(cps_utf8(out));
}

std::string join_words(const std::vector<std::string>& w, size_t a, size_t b) {
    std::string out;
    for (size_t i = a; i < b && i < w.size(); i++) {
        if (!out.empty()) out.push_back(' ');
        out += w[i];
    }
    return out;
}

void split_long(const std::string& s, size_t max_words, bool turkish, std::vector<std::string>& out, int depth = 0) {
    auto w = words_of(s);
    if (w.size() <= max_words || depth > 2) {
        out.push_back(s);
        return;
    }
    int best = -1;
    bool before = false;
    double best_d = 1e9;
    double mid = w.size() * 0.5;
    for (size_t i = 5; i + 7 < w.size(); i++) {
        char last = w[i].empty() ? ' ' : w[i].back();
        if (last == ',' || last == ';') {
            auto next = utf8_cps(w[i + 1]);
            bool listy = !next.empty() && ((next[0] >= 'A' && next[0] <= 'Z') || next[0] == 0xC7 || next[0] == 0x130 ||
                                           next[0] == 0xD6 || next[0] == 0x15E || next[0] == 0xDC);
            double d = std::abs(static_cast<double>(i + 1) - mid) + (listy ? 4.0 : 0.0) + (last == ';' ? -1.0 : 0.0);
            if (d < best_d) {
                best_d = d;
                best = static_cast<int>(i);
                before = false;
            }
        }
    }
    if (best < 0) {
        for (size_t i = 6; i + 6 < w.size(); i++) {
            std::string f = ascii_fold(w[i]);
            if (f == "ve" || f == "ancak" || f == "fakat" || f == "ama" || f == "and" || f == "but" || f == "while" ||
                f == "which" || f == "where") {
                double d = std::abs(static_cast<double>(i) - mid);
                if (d < best_d) {
                    best_d = d;
                    best = static_cast<int>(i);
                    before = true;
                }
            }
        }
    }
    if (best < 0) {
        out.push_back(s);
        return;
    }
    // The first half keeps its comma so the voice ends it rising, as if the sentence goes on.
    size_t cut = before ? static_cast<size_t>(best) : static_cast<size_t>(best) + 1;
    std::string a = join_words(w, 0, cut);
    if (before) a += ",";
    std::string b = join_words(w, cut, w.size());
    (void)turkish;
    split_long(a, max_words, turkish, out, depth + 1);
    split_long(b, max_words, turkish, out, depth + 1);
}

// 1 = tells the story (events, people, consequences), 2 = neutral, 5 = technical detail.
int heading_rank(const std::string& heading) {
    std::string f = ascii_fold(heading);
    for (const char* k : {"batis", "batma", "kaza", "carpis", "carpma", "kurtar", "enkaz", "kesif", "sefer", "yolculuk",
                          "olay", "savas", "olum", "sonra", "miras", "etki", "tarih", "arka plan", "yasam", "hayat",
                          "kariyer", "basari", "felaket", "kurtul", "buzdag", "sinking", "collision", "rescue", "wreck", "discover", "voyage",
                          "aftermath", "legacy", "history", "death", "battle", "war", "event", "incident", "background",
                          "early life", "career", "biography", "life", "impact", "origin", "disaster", "investigation",
                          "sorusturma"})
        if (f.find(k) != std::string::npos) return 1;
    for (const char* k : {"ozellik", "teknik", "boyut", "karsilastir", "olcu", "donanim", "kabin", "ic mekan", "mimari",
                          "populer kultur", "medya", "film", "kurgu", "specification", "dimension", "comparison", "feature",
                          "design", "interior", "equipment", "layout", "fleet", "filo", "popular culture", "in fiction",
                          "media", "depiction", "gallery", "statistics", "istatistik"})
        if (f.find(k) != std::string::npos) return 5;
    return 2;
}

bool skip_heading(const std::string& heading) {
    std::string f = ascii_fold(heading);
    for (const char* k : {"kaynak", "dis baglanti", "ayrica bak", "notlar", "dipnot", "galeri", "bibliyografya",
                          "ileri okuma", "reference", "external link", "see also", "notes", "further reading",
                          "bibliography", "gallery", "sources", "citations", "footnotes"})
        if (f.find(k) != std::string::npos) return true;
    return false;
}

std::string year_in(const std::string& s) {
    auto cps = utf8_cps(s);
    for (size_t i = 0; i + 4 <= cps.size(); i++) {
        if (i > 0 && is_digit(cps[i - 1])) continue;
        if (!(is_digit(cps[i]) && is_digit(cps[i + 1]) && is_digit(cps[i + 2]) && is_digit(cps[i + 3]))) continue;
        if (i + 4 < cps.size() && is_digit(cps[i + 4])) continue;
        int y = static_cast<int>((cps[i] - '0') * 1000 + (cps[i + 1] - '0') * 100 + (cps[i + 2] - '0') * 10 + (cps[i + 3] - '0'));
        if (y >= 1000 && y <= 2100) return cps_utf8(std::vector<uint32_t>(cps.begin() + i, cps.begin() + i + 4));
    }
    return {};
}

struct Beat {
    std::string text, title, kicker;
    int image = -1;
};

std::vector<std::string> fold_words(const std::string& s, size_t min_len) {
    std::vector<std::string> out;
    std::string cur;
    for (char ch : ascii_fold(s) + " ") {
        if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')) {
            cur.push_back(ch);
        } else {
            if (cur.size() >= min_len) out.push_back(cur);
            cur.clear();
        }
    }
    return out;
}

bool word_match(const std::string& a, const std::string& b) {
    if (a == b) return true;
    if (a.size() >= 5 && b.size() >= 5 && a.compare(0, 5, b, 0, 5) == 0) return true;
    return false;
}

// Picks a picture for every beat: shared names and words first, then the next unused picture in article order.
void assign_images(std::vector<Beat>& beats, const std::vector<WebImage>& images) {
    if (images.empty()) return;
    static const std::pair<const char*, const char*> dict[] = {
        {"buzdag", "iceberg"},  {"gemi", "ship"},         {"bati", "sinking"},       {"batt", "sinking"},
        {"kaptan", "captain"},  {"yolcu", "passenger"},   {"filika", "lifeboat"},    {"enkaz", "wreck"},
        {"muretteba", "crew"},  {"liman", "port"},        {"insa", "construction"},  {"tersane", "shipyard"},
        {"kurtar", "rescue"},   {"hayatta", "survivors"}, {"anit", "memorial"},      {"savas", "war"},
        {"asker", "soldiers"},  {"kral", "king"},         {"kralice", "queen"},      {"sehir", "city"},
        {"kopru", "bridge"},    {"ucak", "aircraft"},     {"tren", "train"},         {"deniz", "sea"},
        {"okyanus", "ocean"},   {"harita", "map"},        {"bina", "building"},      {"kale", "castle"},
        {"cami", "mosque"},     {"kilise", "church"},     {"saray", "palace"},       {"yangin", "fire"},
        {"deprem", "earthquake"}, {"muze", "museum"},     {"heykel", "statue"},      {"portre", "portrait"},
        {"gazete", "newspaper"}, {"telsiz", "wireless"},  {"pervane", "propeller"},  {"baca", "funnel"},
        {"guverte", "deck"},    {"merdiven", "staircase"}, {"mezar", "grave"},       {"ordu", "army"},
        {"imparator", "emperor"}, {"padisah", "sultan"},  {"roket", "rocket"},       {"uzay", "space"},
        {"ay ", "moon"},        {"gezegen", "planet"},    {"yildiz", "star"},        {"dag", "mountain"},
        {"nehir", "river"},     {"ada", "island"},        {"film", "film"},          {"mahkeme", "court"},
        {"sorusturma", "inquiry"}, {"kesif", "expedition"}, {"dalgic", "diver"},     {"denizalti", "submarine"}};
    std::vector<std::vector<std::string>> iw(images.size());
    for (size_t j = 0; j < images.size(); j++) {
        std::string file = images[j].file;
        if (file.rfind("File:", 0) == 0) file = file.substr(5);
        iw[j] = fold_words(file + " " + images[j].caption, 3);
    }
    std::vector<int> uses(images.size(), 0);
    size_t cursor = 0;
    for (size_t i = 0; i < beats.size(); i++) {
        Beat& b = beats[i];
        if (b.image >= 0 && b.image < static_cast<int>(images.size())) {
            uses[static_cast<size_t>(b.image)]++;
            continue;
        }
        std::vector<std::pair<std::string, int>> keys;
        auto words = words_of(b.text);
        for (size_t k = 0; k < words.size(); k++) {
            auto cps = utf8_cps(words[k]);
            bool proper = k > 0 && !cps.empty() && ((cps[0] >= 'A' && cps[0] <= 'Z') || cps[0] == 0xC7 || cps[0] == 0x130 ||
                                                     cps[0] == 0xD6 || cps[0] == 0x15E || cps[0] == 0xDC);
            for (const auto& fw : fold_words(words[k], 3)) {
                keys.push_back({fw, proper ? 3 : 1});
                for (const auto& d : dict)
                    if (fw.rfind(d.first, 0) == 0) keys.push_back({d.second, 2});
            }
        }
        int best = -1, best_score = 0;
        for (size_t j = 0; j < images.size(); j++) {
            if (uses[j]) continue;
            int score = 0;
            for (const auto& [key, weight] : keys) {
                if (key.size() < 4 && weight < 3) continue;
                for (const auto& w : iw[j])
                    if (word_match(key, w)) {
                        score += weight;
                        break;
                    }
            }
            if (score > best_score) {
                best_score = score;
                best = static_cast<int>(j);
            }
        }
        if (i == 0 && !uses[0]) best = 0;
        else if (best_score < 3) best = -1;
        if (best < 0) {
            for (size_t step = 0; step < images.size(); step++) {
                size_t j = (cursor + step) % images.size();
                if (!uses[j]) {
                    best = static_cast<int>(j);
                    break;
                }
            }
        }
        if (best < 0) best = static_cast<int>(cursor % images.size());
        b.image = best;
        uses[static_cast<size_t>(best)]++;
        cursor = static_cast<size_t>(best) + 1;
    }
}

std::vector<Beat> script_local(const Research& r, const std::string& lang, double target, bool shorts) {
    const bool turkish = lang == "tr";
    const double wps = turkish ? 2.0 : 2.45;
    const int budget = static_cast<int>(target * wps);
    struct Candidate {
        std::string text, kicker;
        int rank;
    };
    std::vector<Candidate> pool;
    auto add = [&](const std::string& text, const std::string& kicker, int rank) {
        std::string c = clean_sentence(text);
        auto n = words_of(c).size();
        if (n < 5 || n > 48) return;
        std::string f = ascii_fold(c);
        if (f.find("may refer to") != std::string::npos || f.find("su anlamlara") != std::string::npos) return;
        pool.push_back({c, kicker, rank});
    };
    auto intro = split_sentences(r.intro);
    for (size_t i = 0; i < intro.size(); i++) add(intro[i], "", i == 0 ? 0 : (i < 2 ? 1 : 3));
    for (const auto& s : r.sections) {
        if (skip_heading(s.heading)) continue;
        int rank = heading_rank(s.heading);
        auto sentences = split_sentences(s.text);
        std::string kick = utf8_cps(s.heading).size() <= 26 ? s.heading : "";
        for (size_t k = 0; k < sentences.size() && k < 2; k++) add(sentences[k], k == 0 ? kick : "", k == 0 ? rank + 1 : rank + 3);
    }
    // Take the best ranked sentences that fit the time budget, then tell them in article order.
    std::vector<size_t> order(pool.size());
    for (size_t i = 0; i < pool.size(); i++) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return pool[a].rank < pool[b].rank; });
    std::vector<bool> keep(pool.size(), false);
    int words = 0;
    for (size_t idx : order) {
        int n = static_cast<int>(words_of(pool[idx].text).size());
        if (words > 0 && words + n > budget * 1.08) continue;
        keep[idx] = true;
        words += n;
        if (words >= budget) break;
    }
    std::vector<Beat> beats;
    for (size_t i = 0; i < pool.size(); i++) {
        if (!keep[i]) continue;
        std::vector<std::string> parts;
        split_long(pool[i].text, shorts ? 20 : 28, turkish, parts);
        for (size_t k = 0; k < parts.size(); k++) {
            Beat b;
            b.text = parts[k];
            if (k == 0) b.kicker = pool[i].kicker;
            beats.push_back(std::move(b));
        }
    }
    if (beats.size() > static_cast<size_t>(kMaxScenes - 1)) beats.resize(static_cast<size_t>(kMaxScenes - 1));
    for (auto& b : beats)
        if (b.kicker.empty()) b.kicker = year_in(b.text);
    if (!beats.empty()) {
        beats[0].title = r.title;
        beats[0].kicker = year_in(beats[0].text);
    }
    std::string last_kicker;
    for (auto& b : beats) {
        if (b.kicker == last_kicker) b.kicker.clear();
        else last_kicker = b.kicker;
    }
    return beats;
}

PlanResult plan_research(const PlanInput& in, bool shorts) {
    PlanResult result;
    auto cps = utf8_cps(in.prompt);
    auto folded = folded_of(cps);
    const auto folded_original = folded;
    double hinted = take_seconds(cps, folded);
    std::string lang = narration_lang(in, folded_original);
    const bool turkish = lang == "tr";
    double target = in.seconds > 0 ? in.seconds : hinted > 0 ? hinted : (shorts ? 50.0 : 75.0);
    target = std::clamp(target, 15.0, kMaxTotalSeconds - 10.0);
    std::string query = research_query(cps_utf8(cps));
    if (query.empty()) {
        result.warning = turkish ? "Araştırılacak konu bulunamadı" : "No topic to research";
        return result;
    }
    auto note = [&](const std::string& m) {
        if (in.note) in.note(m);
    };
    Research r = research_topic(query, prompt_is_turkish(in.prompt) ? "tr" : "en", lang, shorts ? 14 : 18, note);
    if (!r.ok()) {
        result.warning = r.error.empty() ? (turkish ? "Konu bulunamadı: " : "Topic not found: ") + query : r.error;
        return result;
    }
    std::vector<WebImage> images;
    for (const auto& im : r.images)
        if (!im.path.empty()) images.push_back(im);

    std::vector<Beat> beats;
    if (in.use_api) {
        note(turkish ? "Yapay zekâ senaryoyu yazıyor…" : "AI is writing the script…");
        std::vector<std::string> notes;
        for (const auto& im : images) {
            std::string file = im.file.rfind("File:", 0) == 0 ? im.file.substr(5) : im.file;
            auto dot = file.rfind('.');
            if (dot != std::string::npos) file = file.substr(0, dot);
            std::string line = file + (im.caption.empty() ? "" : " — " + im.caption);
            auto lc = utf8_cps(line);
            if (lc.size() > 160) lc.resize(160);
            notes.push_back(cps_utf8(lc));
        }
        std::vector<ScriptBeat> script;
        std::string err;
        if (script_with_api(in, research_brief(r, 9000), notes, lang, target, shorts, script, err)) {
            for (const auto& s : script) {
                Beat b;
                b.text = clean_sentence(s.narration);
                b.title = s.title;
                b.kicker = s.kicker;
                b.image = s.image >= 0 && s.image < static_cast<int>(images.size()) ? s.image : -1;
                if (!b.text.empty()) beats.push_back(std::move(b));
            }
            if (beats.size() > static_cast<size_t>(kMaxScenes - 1)) beats.resize(static_cast<size_t>(kMaxScenes - 1));
            result.from_api = true;
        } else {
            result.warning = err;
        }
    }
    if (beats.empty()) beats = script_local(r, lang, target, shorts);
    if (beats.empty()) {
        result.warning = turkish ? "Makaleden anlatım çıkarılamadı" : "Could not build a script from the article";
        return result;
    }
    assign_images(beats, images);

    Storyboard board;
    board.prompt = in.prompt;
    board.mark = in.mark;
    board.title = r.title;
    board.style = !in.style.empty() ? in.style : detect_style(folded_original);
    if (board.style.empty()) board.style = "night";
    if (shorts) {
        board.aspect = "9:16";
        board.width = 1080;
        board.height = 1920;
    } else if (in.aspect == "1:1") {
        board.aspect = "1:1";
        board.width = 1080;
        board.height = 1080;
    } else {
        board.aspect = "16:9";
        board.width = 1920;
        board.height = 1080;
    }
    board.fps = 30;
    std::string all = ascii_fold(r.intro) + " ";
    for (size_t i = 0; i < r.sections.size() && i < 4; i++) all += ascii_fold(r.sections[i].heading) + " ";
    board.music = pick_mood(all, shorts);
    std::string warn;
    board.voice = pick_voice(in, lang, true, warn);
    if (!warn.empty()) result.warning = warn;
    board.voice_rate = shorts ? 2 : 1;
    board.captions = "auto";
    board.credits = research_credits(r, turkish);

    const char* motions[] = {"zoom", "pan", "drift", "zoom", "rise"};
    for (size_t i = 0; i < beats.size(); i++) {
        Scene s;
        s.narration = beats[i].text;
        s.title = beats[i].title;
        s.kicker = beats[i].kicker;
        if (beats[i].image >= 0) s.image = images[static_cast<size_t>(beats[i].image)].path;
        s.layout = s.image.empty() ? "statement" : "story";
        if (s.image.empty()) s.text = s.narration;
        s.motion = motions[i % 5];
        s.seconds = 0.8 + static_cast<double>(words_of(s.narration).size()) / (turkish ? 2.0 : 2.45);
        board.scenes.push_back(std::move(s));
    }
    Scene end;
    end.title = r.title;
    end.kicker = turkish ? "Kaynak" : "Source";
    end.text = turkish ? "Vikipedi  \xC2\xB7  Wikimedia Commons" : "Wikipedia  \xC2\xB7  Wikimedia Commons";
    end.layout = "end";
    end.transition = "fade";
    end.seconds = 3.2;
    board.scenes.push_back(end);

    if (board.voice != "none") {
        note(turkish ? "Seslendirme hazırlanıyor…" : "Recording the narration…");
        fit_to_voice(board);
        // Drop the latest beats (never the hook) while the voice runs far past the requested length.
        while (board.scenes.size() > 4 && total_seconds(board) > target * 1.2 && in.seconds > 0) {
            board.scenes.erase(board.scenes.end() - 2);
        }
    }
    normalize(board);
    result.board = std::move(board);
    result.source = r.url;
    return result;
}

// Gives every text scene a spoken line made from what it shows.
void narrate_from_text(Storyboard& board) {
    for (int i = 0; i < static_cast<int>(board.scenes.size()); i++) {
        Scene& s = board.scenes[static_cast<size_t>(i)];
        if (!s.narration.empty()) continue;
        std::string layout = resolve_layout(board, i);
        if (layout == "terminal") continue;
        std::string title = speakable(s.title);
        std::string text;
        for (const auto& line : split_lines(s.text)) {
            std::string t = speakable(line.text);
            if (t.empty() || t.find("http") != std::string::npos || t.find("github.com") != std::string::npos) continue;
            if (!text.empty()) text += ", ";
            text += t;
        }
        if (layout == "end" && title.find('.') != std::string::npos) title.clear();
        std::string line = title;
        if (!text.empty()) {
            if (!line.empty()) {
                char last = line.back();
                line += (last == '.' || last == '!' || last == '?' || last == ':') ? " " : ". ";
            }
            line += text;
        }
        s.narration = tidy(line);
    }
}

void finish_voice(PlanResult& result, const PlanInput& in, const std::vector<uint32_t>& folded, bool shorts) {
    Storyboard& board = result.board;
    if (shorts) {
        board.aspect = "9:16";
        board.width = 1080;
        board.height = 1920;
        board.fps = 30;
    }
    std::string lang = narration_lang(in, folded);
    std::string warn;
    std::string voice = pick_voice(in, lang, asks_for_voice(folded), warn);
    if (!warn.empty() && result.warning.empty()) result.warning = warn;
    if (voice == "none") return;
    board.voice = voice;
    board.voice_rate = shorts ? 2 : 1;
    narrate_from_text(board);
    fit_to_voice(board);
    normalize(board);
}

}  // namespace

Storyboard plan_local(const PlanInput& in) {
    const bool turkish = in.lang != "en";
    std::vector<uint32_t> cps = utf8_cps(in.prompt);
    std::vector<uint32_t> folded = folded_of(cps);
    const std::vector<uint32_t> folded_original = folded;

    double seconds = take_seconds(cps, folded);

    std::string title;
    const std::vector<std::vector<uint32_t>> title_keys = {needle("başlık:"), needle("baslik:"), needle("title:")};
    for (size_t i = 0; i < folded.size(); i++) {
        const std::vector<uint32_t>* key = nullptr;
        for (const auto& k : title_keys) {
            if (match_at(folded, i, k)) {
                key = &k;
                break;
            }
        }
        if (!key) continue;
        size_t v = i + key->size();
        while (v < folded.size() && folded[v] == ' ') v++;
        size_t vend = v;
        if (v < folded.size() && (folded[v] == '"' || folded[v] == '\'' || folded[v] == 0x201C)) {
            uint32_t q = folded[v];
            uint32_t close = q == 0x201C ? 0x201D : q;
            v++;
            vend = v;
            while (vend < folded.size() && folded[vend] != close) vend++;
            title = tidy(cps_utf8(std::vector<uint32_t>(cps.begin() + static_cast<std::ptrdiff_t>(v),
                                                         cps.begin() + static_cast<std::ptrdiff_t>(vend))));
            if (vend < folded.size()) vend++;
        } else {
            while (vend < folded.size() && folded[vend] != ',' && folded[vend] != '.' && folded[vend] != '\n' &&
                   folded[vend] != '!' && folded[vend] != '?')
                vend++;
            title = tidy(cps_utf8(std::vector<uint32_t>(cps.begin() + static_cast<std::ptrdiff_t>(v),
                                                         cps.begin() + static_cast<std::ptrdiff_t>(vend))));
        }
        erase_range(cps, folded, i, vend);
        break;
    }

    if (in.seconds > 0) seconds = in.seconds;
    if (seconds <= 0) seconds = 8;
    if (seconds < 2) seconds = 2;
    if (seconds > 120) seconds = 120;

    Storyboard board;
    board.prompt = in.prompt;
    board.mark = in.mark;
    board.style = !in.style.empty() ? in.style : detect_style(folded_original);
    if (board.style.empty()) board.style = "ember";
    board.aspect = !in.aspect.empty() ? in.aspect : detect_aspect(folded_original);
    if (board.aspect.empty()) board.aspect = "16:9";
    board.title = title;

    auto lines = split_lines(cps_utf8(cps));
    std::vector<Line> items;
    std::string heading;
    for (const auto& line : lines) {
        std::string text = strip_instructions(line.text);
        if (text.empty()) continue;
        if (line.item) {
            Line item;
            item.item = true;
            item.text = text;
            items.push_back(item);
        } else {
            if (!heading.empty()) heading.push_back(' ');
            heading += text;
        }
    }
    std::string spoken = heading;
    if (spoken.empty() && items.empty() && title.empty()) spoken = tidy(cps_utf8(cps));

    if (!items.empty()) {
        if (board.title.empty()) board.title = heading.empty() ? items.front().text : heading;
        if (!board.title.empty()) {
            Scene open;
            open.title = board.title;
            if (!heading.empty() && heading != board.title) open.text = heading;
            board.scenes.push_back(open);
        }
        int shown = 0;
        int count = std::min<int>(static_cast<int>(items.size()), 8);
        for (const auto& item : items) {
            if (shown >= count) break;
            Scene s;
            s.title = item.text;
            char k[24];
            std::snprintf(k, sizeof(k), "%02d / %02d", shown + 1, count);
            s.kicker = k;
            board.scenes.push_back(s);
            shown++;
        }
        if (count >= 3 && seconds >= 12) {
            Scene recap;
            recap.title = heading.empty() ? board.title : heading;
            for (int i = 0; i < count; i++) {
                if (i) recap.text.push_back('\n');
                recap.text += items[static_cast<size_t>(i)].text;
            }
            recap.layout = "list";
            board.scenes.push_back(recap);
        }
    } else {
        auto sentences = split_sentences(spoken);
        if (sentences.empty() && !title.empty()) {
            Scene s;
            s.title = title;
            board.scenes.push_back(s);
        } else if (sentences.size() <= 1) {
            std::string sentence = sentences.empty() ? spoken : sentences.front();
            auto clauses = split_clauses(sentence);
            bool short_clauses = clauses.size() >= 2;
            for (const auto& c : clauses)
                if (utf8_cps(c).size() > 60) short_clauses = false;
            if (short_clauses && (has_colon_head(sentence) || clauses.size() >= 3 || !title.empty())) {
                std::string head = title.empty() ? cap_first(clauses.front(), turkish) : title;
                size_t first_item = title.empty() ? 1 : 0;
                size_t count = std::min<size_t>(clauses.size() - first_item, 7);
                Scene open;
                open.title = head;
                board.scenes.push_back(open);
                for (size_t k = 0; k < count; k++) {
                    Scene s;
                    s.title = cap_first(clauses[first_item + k], turkish);
                    char kicker[24];
                    std::snprintf(kicker, sizeof(kicker), "%02d / %02d", static_cast<int>(k + 1), static_cast<int>(count));
                    s.kicker = kicker;
                    board.scenes.push_back(s);
                }
                if (count >= 2 && seconds >= 14) {
                    Scene close;
                    close.title = head;
                    for (size_t k = 0; k < count; k++) {
                        if (k) close.text += "  \xC2\xB7  ";
                        close.text += cap_first(clauses[first_item + k], turkish);
                    }
                    close.layout = "statement";
                    close.transition = "flash";
                    board.scenes.push_back(close);
                }
                if (board.title.empty()) board.title = head;
            } else if (!title.empty()) {
                Scene s;
                s.title = title;
                s.text = sentence == title ? "" : sentence;
                board.scenes.push_back(s);
            } else if (utf8_cps(sentence).size() <= 48 || words_of(sentence).size() < 8) {
                Scene s;
                s.title = cap_first(sentence, turkish);
                board.scenes.push_back(s);
            } else {
                auto w = words_of(sentence);
                size_t half = w.size() / 2;
                Scene a, b;
                for (size_t k = 0; k < w.size(); k++) {
                    std::string& dst = k < half ? a.title : b.title;
                    if (!dst.empty()) dst.push_back(' ');
                    dst += w[k];
                }
                a.title = cap_first(a.title, turkish);
                b.title = cap_first(b.title, turkish);
                b.layout = "statement";
                board.scenes.push_back(a);
                board.scenes.push_back(b);
            }
        } else {
            size_t limit = std::min<size_t>(sentences.size(), 8);
            for (size_t i = 0; i < limit; i++) {
                Scene s;
                const std::string& sentence = sentences[i];
                auto clauses = split_clauses(sentence);
                if (i == 0 && !title.empty()) {
                    s.title = title;
                    s.text = sentence;
                } else if (utf8_cps(sentence).size() <= 48) {
                    s.title = sentence;
                } else if (clauses.size() >= 2 && utf8_cps(clauses.front()).size() <= 48) {
                    s.title = cap_first(clauses.front(), turkish);
                    for (size_t k = 1; k < clauses.size(); k++) {
                        if (k > 1) s.text += ", ";
                        s.text += clauses[k];
                    }
                    s.text = cap_first(s.text, turkish);
                } else {
                    s.text = sentence;
                }
                board.scenes.push_back(s);
            }
        }
    }

    if (board.title.empty()) {
        if (!board.scenes.empty() && !board.scenes.front().title.empty()) board.title = board.scenes.front().title;
        else board.title = first_words(spoken, 6);
    }
    if (board.scenes.empty() ||
        (board.scenes.size() == 1 && board.scenes[0].title.empty() && board.scenes[0].text.empty())) {
        board.scenes.clear();
        Scene s;
        s.title = board.title.empty() ? "Kare" : board.title;
        board.scenes.push_back(s);
        if (board.title.empty()) board.title = "Kare";
    }
    assign_time(board.scenes, seconds);
    apply_aspect_size(board);
    normalize(board);
    snap_to_beat(board, seconds);
    return board;
}

PlanResult plan(const PlanInput& in) {
    const auto folded = folded_of(utf8_cps(in.prompt));
    const bool shorts = asks_for_shorts(in, folded);
    std::string owner, repo;
    if (find_github_repo(in.prompt, owner, repo)) {
        PlanResult result = plan_repo(in, owner, repo);
        finish_voice(result, in, folded, shorts);
        return result;
    }
    std::string research_warning;
    if (in.web > 0 || (in.web == 0 && wants_research(in.prompt))) {
        PlanResult result = plan_research(in, shorts);
        if (!result.board.scenes.empty()) return result;
        research_warning = result.warning;
    }
    PlanResult result = plan_plain(in);
    finish_voice(result, in, folded, shorts);
    if (!research_warning.empty()) result.warning = research_warning;
    return result;
}

PlanResult plan_plain(const PlanInput& in) {
    PlanResult result;
    if (in.use_api) {
        std::string err;
        Storyboard board;
        if (plan_with_api(in, board, err)) {
            if (!in.aspect.empty()) board.aspect = in.aspect;
            if (!in.style.empty()) board.style = in.style;
            board.mark = in.mark;
            board.prompt = in.prompt;
            apply_aspect_size(board);
            if (in.seconds > 0) scale_duration(board, in.seconds);
            normalize(board);
            snap_to_beat(board, in.seconds);
            result.board = std::move(board);
            result.from_api = true;
            return result;
        }
        result.warning = err.empty() ? "API plan failed" : err;
    }
    result.board = plan_local(in);
    return result;
}

}  // namespace kare
