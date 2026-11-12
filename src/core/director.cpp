#include "core/director.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "core/audio.h"
#include "core/brain.h"
#include "core/github.h"
#include "core/utf8.h"

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
        if (cp == '.' && i > 0 && is_digit(cps[i - 1])) continue;
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
    std::string owner, repo;
    if (find_github_repo(in.prompt, owner, repo)) return plan_repo(in, owner, repo);
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
