#include "core/github.h"

#include <algorithm>
#include <cstdio>
#include <initializer_list>

#include "core/audio.h"
#include "core/http.h"
#include "core/json.h"
#include "core/utf8.h"

namespace kare {

namespace {

bool name_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
           c == '.';
}

bool space_char(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

std::string lower_ascii(std::string s) {
    for (auto& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

bool starts_with(const std::string& s, const std::string& p) { return s.size() >= p.size() && s.compare(0, p.size(), p) == 0; }

bool ends_with(const std::string& s, const std::string& p) {
    return s.size() >= p.size() && s.compare(s.size() - p.size(), p.size(), p) == 0;
}

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && space_char(s[a])) a++;
    while (b > a && space_char(s[b - 1])) b--;
    return s.substr(a, b - a);
}

bool emoji_like(uint32_t cp) {
    return cp >= 0x1F000 || (cp >= 0x2600 && cp <= 0x27BF) || (cp >= 0x2B00 && cp <= 0x2BFF) || cp == 0xFE0F ||
           cp == 0x200D || (cp >= 0x2190 && cp <= 0x21FF && cp != 0x2192);
}

std::string cut_words(const std::string& text, size_t max_cps) {
    auto cps = utf8_cps(text);
    if (cps.size() <= max_cps) return text;
    size_t cut = max_cps;
    while (cut > max_cps / 2 && cps[cut] != ' ') cut--;
    if (cut <= max_cps / 2) cut = max_cps;
    std::vector<uint32_t> head(cps.begin(), cps.begin() + static_cast<std::ptrdiff_t>(cut));
    while (!head.empty() && (head.back() == ' ' || head.back() == ',' || head.back() == ';' || head.back() == ':'))
        head.pop_back();
    head.push_back(0x2026);
    return cps_utf8(head);
}

std::string drop_parens(const std::string& text) {
    std::string out;
    int depth = 0;
    for (char c : text) {
        if (c == '(') depth++;
        else if (c == ')' && depth > 0) depth--;
        else if (!depth) out.push_back(c);
    }
    std::string t = tidy(out);
    while (!t.empty() && (t.back() == ',' || t.back() == ';' || t.back() == ':')) t.pop_back();
    return t.empty() ? text : t;
}

std::string first_sentences(const std::string& text, size_t max_cps) {
    auto cps = utf8_cps(text);
    if (cps.size() <= max_cps) return text;
    size_t best = 0;
    for (size_t i = 0; i < max_cps && i < cps.size(); i++) {
        if ((cps[i] == '.' || cps[i] == '!' || cps[i] == '?') && (i + 1 == cps.size() || cps[i + 1] == ' ')) best = i + 1;
    }
    if (best >= max_cps / 3) return cps_utf8(std::vector<uint32_t>(cps.begin(), cps.begin() + static_cast<std::ptrdiff_t>(best)));
    return cut_words(text, max_cps);
}

bool heading_has(const std::string& folded, std::initializer_list<const char*> words) {
    for (const char* w : words)
        if (folded.find(w) != std::string::npos) return true;
    return false;
}

bool skip_heading(const std::string& folded) {
    return heading_has(folded, {"install", "kurulum", "usage", "kullanım", "kullanim", "license", "lisans", "build",
                                "derle", "contribut", "katkı", "katki", "requirement", "gereksinim", "table of content",
                                "içindekiler", "icindekiler", "credit", "acknowledg", "teşekkür", "support", "destek",
                                "faq", "changelog", "değişiklik", "sponsor", "author", "yazar", "contact",
                                "iletişim", "getting started", "başlarken", "quick start", "hızlı başlangıç",
                                "setup", "download", "indirme", "api reference", "running test", "testing",
                                "development"});
}

bool feature_heading(const std::string& folded) {
    return heading_has(folded, {"feature", "özellik", "ozellik", "highlight", "why", "neden", "what", "neler",
                                "capabilit", "key", "öne çıkan", "one cikan", "overview", "genel bakış", "yapabilir"});
}

struct Block {
    std::string heading;
    std::vector<std::string> paragraphs;
    std::vector<std::string> items;
};

std::vector<Block> parse_readme(const std::string& md) {
    std::vector<Block> blocks(1);
    bool code = false;
    bool comment = false;
    std::string para;
    auto end_para = [&] {
        std::string p = tidy(para);
        para.clear();
        if (!p.empty()) blocks.back().paragraphs.push_back(p);
    };
    size_t i = 0;
    while (i <= md.size()) {
        size_t nl = md.find('\n', i);
        if (nl == std::string::npos) nl = md.size();
        std::string raw = md.substr(i, nl - i);
        i = nl + 1;
        std::string line = trim(raw);
        if (starts_with(line, "```") || starts_with(line, "~~~")) {
            end_para();
            code = !code;
            continue;
        }
        if (code) continue;
        if (comment) {
            if (line.find("-->") != std::string::npos) comment = false;
            continue;
        }
        if (starts_with(line, "<!--")) {
            if (line.find("-->") == std::string::npos) comment = true;
            continue;
        }
        if (line.empty()) {
            end_para();
            continue;
        }
        if (line[0] == '#') {
            end_para();
            Block b;
            b.heading = clean_markdown(line);
            blocks.push_back(b);
            continue;
        }
        if (line.find_first_not_of("=-_*") == std::string::npos) {
            end_para();
            continue;
        }
        if (line[0] == '|' || line[0] == '>') {
            end_para();
            continue;
        }
        bool item = false;
        std::string body;
        if ((line[0] == '-' || line[0] == '*' || line[0] == '+') && line.size() > 2 && line[1] == ' ') {
            item = true;
            body = line.substr(2);
        } else {
            size_t d = 0;
            while (d < line.size() && line[d] >= '0' && line[d] <= '9') d++;
            if (d > 0 && d + 1 < line.size() && (line[d] == '.' || line[d] == ')') && line[d + 1] == ' ') {
                item = true;
                body = line.substr(d + 2);
            }
        }
        if (item) {
            end_para();
            if (starts_with(body, "[ ] ") || starts_with(body, "[x] ") || starts_with(body, "[X] ")) body = body.substr(4);
            std::string c = clean_markdown(body);
            if (utf8_cps(c).size() >= 3) blocks.back().items.push_back(c);
            continue;
        }
        if (raw.size() >= 4 && raw.compare(0, 4, "    ") == 0 && para.empty()) continue;
        std::string c = clean_markdown(line);
        if (c.empty()) {
            end_para();
            continue;
        }
        if (!para.empty()) para.push_back(' ');
        para += c;
    }
    end_para();
    return blocks;
}

bool looks_like_url(const std::string& s) {
    std::string l = lower_ascii(s);
    return starts_with(l, "http") || starts_with(l, "www.") || l.find("://") != std::string::npos;
}

std::string json_get(const std::string& url, const std::string& headers, int timeout, size_t max, int& status,
                     std::string& err) {
    HttpResult r = http_request("GET", url, "", headers, timeout, max);
    status = r.status;
    if (r.status == 0) err = r.error.empty() ? "no response" : r.error;
    return r.body;
}

int lang_rank(const std::string& lang) {
    static const char* night[] = {"C++", "C", "Rust", "Go", "Zig", "Assembly", "CUDA"};
    static const char* neon[] = {"JavaScript", "TypeScript", "HTML", "CSS", "Vue", "Svelte", "SCSS", "Astro"};
    static const char* sea[] = {"Python", "Jupyter Notebook", "R", "Julia"};
    static const char* ember[] = {"Java", "Kotlin", "Swift", "C#", "Dart", "Ruby", "PHP", "Scala"};
    for (const char* l : night)
        if (lang == l) return 1;
    for (const char* l : neon)
        if (lang == l) return 2;
    for (const char* l : sea)
        if (lang == l) return 3;
    for (const char* l : ember)
        if (lang == l) return 4;
    if (lang == "TeX" || lang == "Markdown") return 5;
    return 0;
}

std::string style_for(const RepoInfo& info) {
    std::string main = info.languages.empty() ? "" : info.languages.front().first;
    switch (lang_rank(main)) {
        case 1: return "night";
        case 2: return "neon";
        case 3: return "sea";
        case 4: return "ember";
        case 5: return "paper";
        default: return "night";
    }
}

std::string percent(double share, bool turkish) {
    int p = static_cast<int>(share * 100.0 + 0.5);
    if (p < 1) p = 1;
    return turkish ? "%" + std::to_string(p) : std::to_string(p) + "%";
}

void split_item(const std::string& item, Scene& s) {
    static const char* seps[] = {" - ", " \xE2\x80\x94 ", " \xE2\x80\x93 ", ": "};
    for (const char* sep : seps) {
        auto pos = item.find(sep);
        if (pos == std::string::npos || pos == 0) continue;
        std::string left = tidy(item.substr(0, pos));
        std::string right = tidy(item.substr(pos + std::string(sep).size()));
        if (utf8_cps(left).size() <= 40 && !right.empty()) {
            s.title = left;
            s.text = cut_words(right, 110);
            return;
        }
    }
    if (utf8_cps(item).size() <= 48) s.title = item;
    else s.text = cut_words(item, 120);
}

}  // namespace

bool find_github_repo(const std::string& text, std::string& owner, std::string& repo, size_t* begin, size_t* end) {
    std::string low = lower_ascii(text);
    size_t from = 0;
    while (true) {
        size_t at = low.find("github.com", from);
        if (at == std::string::npos) return false;
        from = at + 10;
        size_t p = at + 10;
        if (p >= low.size() || (low[p] != '/' && low[p] != ':')) continue;
        p++;
        size_t o0 = p;
        while (p < text.size() && name_char(text[p])) p++;
        if (p == o0 || p >= text.size() || text[p] != '/') continue;
        std::string o = text.substr(o0, p - o0);
        p++;
        size_t r0 = p;
        while (p < text.size() && name_char(text[p])) p++;
        if (p == r0) continue;
        std::string r = text.substr(r0, p - r0);
        if (ends_with(lower_ascii(r), ".git")) r.resize(r.size() - 4);
        while (!r.empty() && r.back() == '.') r.pop_back();
        std::string lo = lower_ascii(o);
        if (r.empty() || lo == "orgs" || lo == "topics" || lo == "settings" || lo == "marketplace" || lo == "features")
            continue;
        owner = o;
        repo = r;
        size_t b = at;
        while (b > 0 && !space_char(text[b - 1])) b--;
        size_t e = p;
        while (e < text.size() && !space_char(text[e])) e++;
        if (begin) *begin = b;
        if (end) *end = e;
        return true;
    }
}

std::string strip_github_url(const std::string& text) {
    std::string owner, repo;
    size_t b = 0, e = 0;
    if (!find_github_repo(text, owner, repo, &b, &e)) return tidy(text);
    return tidy(text.substr(0, b) + " " + text.substr(e));
}

std::string clean_markdown(const std::string& line) {
    std::string s = line;
    while (!s.empty() && (s[0] == '#' || s[0] == '>' || s[0] == ' ')) s.erase(0, 1);
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        char c = s[i];
        if (c == '!' && i + 1 < s.size() && s[i + 1] == '[') {
            size_t close = s.find(']', i + 2);
            if (close != std::string::npos && close + 1 < s.size() && s[close + 1] == '(') {
                size_t paren = s.find(')', close + 2);
                if (paren != std::string::npos) {
                    i = paren + 1;
                    continue;
                }
            }
        }
        if (c == '[') {
            int depth = 1;
            size_t j = i + 1;
            while (j < s.size() && depth) {
                if (s[j] == '[') depth++;
                else if (s[j] == ']') depth--;
                if (depth) j++;
            }
            if (j < s.size()) {
                std::string inner = clean_markdown(s.substr(i + 1, j - i - 1));
                size_t next = j + 1;
                if (next < s.size() && s[next] == '(') {
                    size_t paren = s.find(')', next + 1);
                    next = paren == std::string::npos ? s.size() : paren + 1;
                } else if (next < s.size() && s[next] == '[') {
                    size_t br = s.find(']', next + 1);
                    next = br == std::string::npos ? s.size() : br + 1;
                }
                out += inner;
                i = next;
                continue;
            }
        }
        if (c == '<') {
            size_t close = s.find('>', i + 1);
            if (close != std::string::npos) {
                out.push_back(' ');
                i = close + 1;
                continue;
            }
        }
        if (c == ':' && i + 2 < s.size()) {
            size_t j = i + 1;
            bool letters = false;
            while (j < s.size() && ((s[j] >= 'a' && s[j] <= 'z') || s[j] == '_' || s[j] == '+' || s[j] == '-' ||
                                    (s[j] >= '0' && s[j] <= '9'))) {
                if (s[j] >= 'a' && s[j] <= 'z') letters = true;
                j++;
            }
            if (letters && j < s.size() && s[j] == ':' && j > i + 1) {
                i = j + 1;
                continue;
            }
        }
        if (c == '*' || c == '`' || (c == '~' && i + 1 < s.size() && s[i + 1] == '~')) {
            i += (c == '~') ? 2 : 1;
            continue;
        }
        if (c == '_' && i + 1 < s.size() && s[i + 1] == '_') {
            i += 2;
            continue;
        }
        out.push_back(c);
        i++;
    }
    std::vector<uint32_t> kept;
    for (uint32_t cp : utf8_cps(out))
        if (!emoji_like(cp)) kept.push_back(cp);
    std::string t = tidy(cps_utf8(kept));
    while (!t.empty() && (t.back() == '|' || t.back() == '-' || t.back() == ':' || t.back() == ' ')) t.pop_back();
    while (!t.empty() && (t[0] == '|' || t[0] == '-' || t[0] == ':' || t[0] == ' ')) t.erase(0, 1);
    return t;
}

std::string readme_summary(const std::string& markdown) {
    for (const auto& b : parse_readme(markdown)) {
        if (!b.heading.empty() && skip_heading(fold_tr(b.heading))) continue;
        for (const auto& p : b.paragraphs) {
            if (utf8_cps(p).size() < 30 || looks_like_url(p) || p[0] == '"' || starts_with(p, "\xE2\x80\x9C")) continue;
            return first_sentences(p, 170);
        }
    }
    return {};
}

std::vector<std::string> readme_features(const std::string& markdown, size_t limit) {
    auto blocks = parse_readme(markdown);
    const Block* pick = nullptr;
    for (const auto& b : blocks) {
        if (b.items.size() >= 2 && feature_heading(fold_tr(b.heading)) && !skip_heading(fold_tr(b.heading))) {
            pick = &b;
            break;
        }
    }
    if (!pick) {
        for (const auto& b : blocks) {
            if (b.items.size() >= 2 && !skip_heading(fold_tr(b.heading))) {
                pick = &b;
                break;
            }
        }
    }
    std::vector<std::string> out;
    if (!pick) return out;
    for (const auto& item : pick->items) {
        if (out.size() >= limit) break;
        if (looks_like_url(item) || utf8_cps(item).size() < 4) continue;
        out.push_back(cut_words(item, 140));
    }
    return out;
}

std::string compact_count(int n) {
    char buf[32];
    if (n < 1000) return std::to_string(n);
    if (n < 10000) {
        std::snprintf(buf, sizeof buf, "%.1fk", n / 1000.0);
        return buf;
    }
    if (n < 1000000) return std::to_string(n / 1000) + "k";
    std::snprintf(buf, sizeof buf, "%.1fM", n / 1000000.0);
    return buf;
}

std::string repo_note(const RepoInfo& info, bool turkish) {
    std::vector<std::string> parts;
    if (!info.languages.empty()) parts.push_back(info.languages.front().first);
    if (info.stars > 0) parts.push_back(compact_count(info.stars) + (turkish ? " yıldız" : " stars"));
    if (info.forks > 0) parts.push_back(compact_count(info.forks) + " fork");
    if (!info.license.empty()) parts.push_back(info.license);
    std::string out;
    for (const auto& p : parts) {
        if (!out.empty()) out += " · ";
        out += p;
    }
    return out;
}

bool fetch_repo(const std::string& owner, const std::string& repo, const std::string& token, RepoInfo& out,
                std::string& err) {
    const std::string api = "https://api.github.com/repos/" + owner + "/" + repo;
    std::string headers = "Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28\r\n";
    if (!token.empty()) headers += "Authorization: Bearer " + token + "\r\n";
    int status = 0;
    std::string body = json_get(api, headers, 15000, 1024 * 1024, status, err);
    if (status == 0) return false;
    if (status == 404) {
        err = "GitHub: " + owner + "/" + repo + " not found or private";
        return false;
    }
    if (status == 403 || status == 429) {
        err = "GitHub rate limit reached; add a token in Settings";
        return false;
    }
    Json root;
    std::string perr;
    if (status >= 400 || !json_parse(body, root, perr) || root.type != Json::Obj) {
        err = "GitHub returned HTTP " + std::to_string(status);
        return false;
    }
    RepoInfo info;
    info.name = root.str("name", repo);
    const Json* own = root.field("owner");
    info.owner = own ? own->str("login", owner) : owner;
    info.description = clean_markdown(root.str("description"));
    info.homepage = root.str("homepage");
    info.branch = root.str("default_branch", "HEAD");
    info.stars = static_cast<int>(root.num("stargazers_count", 0));
    info.forks = static_cast<int>(root.num("forks_count", 0));
    info.issues = static_cast<int>(root.num("open_issues_count", 0));
    const Json* lic = root.field("license");
    if (lic && lic->type == Json::Obj) {
        info.license = lic->str("spdx_id");
        if (info.license.empty() || info.license == "NOASSERTION") info.license = lic->str("name");
        if (info.license == "Other") info.license.clear();
    }
    const Json* topics = root.field("topics");
    if (topics && topics->type == Json::Arr)
        for (const auto& t : topics->a)
            if (t.type == Json::Str) info.topics.push_back(t.s);

    std::string lerr;
    body = json_get(api + "/languages", headers, 15000, 256 * 1024, status, lerr);
    Json langs;
    if (status == 200 && json_parse(body, langs, perr) && langs.type == Json::Obj) {
        double total = 0;
        for (const auto& kv : langs.o) total += kv.second.n;
        if (total > 0)
            for (const auto& kv : langs.o) info.languages.push_back({kv.first, kv.second.n / total});
        std::sort(info.languages.begin(), info.languages.end(),
                  [](const auto& a, const auto& b) { return a.second > b.second; });
    }
    if (info.languages.empty()) {
        std::string l = root.str("language");
        if (!l.empty()) info.languages.push_back({l, 1.0});
    }

    std::string raw_headers = "Accept: application/vnd.github.raw+json\r\nX-GitHub-Api-Version: 2022-11-28\r\n";
    if (!token.empty()) raw_headers += "Authorization: Bearer " + token + "\r\n";
    body = json_get(api + "/readme", raw_headers, 15000, 512 * 1024, status, lerr);
    if (status == 200) info.readme = body.size() > 60000 ? body.substr(0, 60000) : body;

    body = json_get(api + "/git/trees/" + info.branch + "?recursive=1", headers, 20000, 12 * 1024 * 1024, status, lerr);
    Json tree;
    if (status == 200 && json_parse(body, tree, perr) && tree.type == Json::Obj) {
        const Json* items = tree.field("tree");
        auto add_tool = [&](const char* t) {
            if (std::find(info.tooling.begin(), info.tooling.end(), t) == info.tooling.end()) info.tooling.push_back(t);
        };
        if (items && items->type == Json::Arr) {
            for (const auto& it : items->a) {
                if (it.str("type") != "blob") continue;
                info.files++;
                std::string path = lower_ascii(it.str("path"));
                size_t slash = path.find_last_of('/');
                std::string file = slash == std::string::npos ? path : path.substr(slash + 1);
                bool root_file = slash == std::string::npos;
                if (file == "cmakelists.txt") add_tool("CMake");
                else if (file == "package.json" && root_file) add_tool("npm");
                else if (file == "cargo.toml") add_tool("Cargo");
                else if (file == "go.mod") add_tool("Go modules");
                else if (root_file && (file == "pyproject.toml" || file == "requirements.txt" || file == "setup.py"))
                    add_tool("pip");
                else if (file == "dockerfile" || starts_with(file, "docker-compose")) add_tool("Docker");
                else if (root_file && file == "makefile") add_tool("Make");
                else if (file == "build.gradle" || file == "build.gradle.kts") add_tool("Gradle");
                else if (file == "pom.xml") add_tool("Maven");
                if (starts_with(path, ".github/workflows/")) add_tool("GitHub Actions");
                if (starts_with(path, "test/") || starts_with(path, "tests/") || path.find("/test/") != std::string::npos ||
                    path.find("/tests/") != std::string::npos || path.find("__tests__") != std::string::npos ||
                    file.find("_test.") != std::string::npos || file.find(".test.") != std::string::npos ||
                    file.find(".spec.") != std::string::npos || starts_with(file, "test_"))
                    info.has_tests = true;
                if (starts_with(path, "docs/") || starts_with(path, "doc/")) info.has_docs = true;
            }
        }
    }
    out = std::move(info);
    err.clear();
    return true;
}

std::string repo_brief(const RepoInfo& info) {
    std::string s = "Repository: github.com/" + info.owner + "/" + info.name + "\n";
    if (!info.description.empty()) s += "Description: " + info.description + "\n";
    s += "Stars: " + std::to_string(info.stars) + ", forks: " + std::to_string(info.forks);
    if (!info.license.empty()) s += ", license: " + info.license;
    s += "\n";
    if (!info.languages.empty()) {
        s += "Languages:";
        for (size_t i = 0; i < info.languages.size() && i < 5; i++)
            s += " " + info.languages[i].first + " " + percent(info.languages[i].second, false);
        s += "\n";
    }
    if (!info.tooling.empty()) {
        s += "Tooling:";
        for (const auto& t : info.tooling) s += " " + t;
        s += "\n";
    }
    if (info.files) s += "Files: " + std::to_string(info.files) + (info.has_tests ? ", has tests" : "") + "\n";
    if (!info.topics.empty()) {
        s += "Topics:";
        for (const auto& t : info.topics) s += " " + t;
        s += "\n";
    }
    if (!info.readme.empty()) {
        std::string excerpt;
        for (const auto& b : parse_readme(info.readme)) {
            if (!b.heading.empty()) excerpt += "\n## " + b.heading + "\n";
            for (const auto& p : b.paragraphs) excerpt += p + "\n";
            for (const auto& it : b.items) excerpt += "- " + it + "\n";
            if (excerpt.size() > 6000) break;
        }
        s += "README (cleaned excerpt):\n" + excerpt.substr(0, 6000) + "\n";
    }
    return s;
}

Storyboard plan_repo_local(const RepoInfo& info, const PlanInput& in) {
    const bool tr = in.lang != "en";
    Storyboard board;
    board.prompt = in.prompt;
    board.mark = in.mark;
    board.title = info.name.empty() ? "GitHub" : info.name;
    board.style = !in.style.empty() ? in.style : style_for(info);
    board.aspect = !in.aspect.empty() ? in.aspect : "16:9";

    std::string summary = readme_summary(info.readme);
    std::string lead = !info.description.empty() ? info.description : summary;

    Scene open;
    open.title = board.title;
    open.text = cut_words(lead, 120);
    open.kicker = "GitHub · " + info.owner;
    open.layout = "hero";
    open.motion = "zoom";
    board.scenes.push_back(open);

    bool summary_scene = false;
    if (!summary.empty() && fold_tr(summary).find(fold_tr(info.description.substr(0, 24))) == std::string::npos) {
        Scene s;
        s.title = tr ? "Ne işe yarar?" : "What it does";
        s.text = summary;
        s.kicker = tr ? "Özet" : "Overview";
        s.motion = "drift";
        board.scenes.push_back(s);
        summary_scene = true;
    } else if (info.description.empty() && !summary.empty()) {
        summary_scene = true;
    }

    auto features = readme_features(info.readme, summary_scene ? 4 : 5);
    if (features.size() >= 3) {
        Scene s;
        s.title = tr ? "Öne çıkanlar" : "Highlights";
        s.kicker = tr ? "Özellikler" : "Features";
        for (size_t i = 0; i < features.size(); i++) {
            Scene part;
            split_item(features[i], part);
            std::string line;
            if (!part.title.empty() && !part.text.empty()) {
                size_t room = 50 > utf8_cps(part.title).size() + 12 ? 50 - utf8_cps(part.title).size() : 12;
                line = "*" + part.title + "* " + cut_words(drop_parens(part.text), room);
            } else {
                line = !part.title.empty() ? part.title : cut_words(drop_parens(part.text), 50);
            }
            if (i) s.text.push_back('\n');
            s.text += line;
        }
        s.layout = "list";
        s.motion = "pan";
        board.scenes.push_back(s);
    } else {
        for (size_t i = 0; i < features.size(); i++) {
            Scene s;
            split_item(features[i], s);
            s.kicker = tr ? "Özellik" : "Feature";
            s.motion = i % 2 ? "drift" : "rise";
            board.scenes.push_back(s);
        }
    }

    std::vector<std::string> langs;
    for (size_t i = 0; i < info.languages.size() && i < 4; i++) {
        if (info.languages[i].second < 0.03 && i > 0) break;
        langs.push_back(tr ? info.languages[i].first + " " + percent(info.languages[i].second, true)
                           : info.languages[i].first + " " + percent(info.languages[i].second, false));
    }
    std::vector<std::string> tools = info.tooling;
    if (info.has_tests) tools.push_back(tr ? "testler" : "tests");
    if (langs.size() >= 2) {
        Scene s;
        s.title = tr ? "Diller" : "Languages";
        s.kicker = tr ? "Teknoloji" : "Tech";
        for (size_t i = 0; i < langs.size(); i++) {
            if (i) s.text.push_back('\n');
            s.text += langs[i];
        }
        s.layout = "bars";
        s.motion = "drift";
        board.scenes.push_back(s);
    } else if (!langs.empty() || !tools.empty()) {
        std::vector<std::string> tech = langs;
        for (const auto& t : tools)
            if (tech.size() < 6) tech.push_back(t);
        Scene s;
        s.title = tr ? "Nasıl yapıldı" : "Built with";
        s.kicker = tr ? "Teknoloji" : "Tech";
        for (size_t i = 0; i < tech.size(); i++) {
            if (i) s.text += " · ";
            s.text += tech[i];
        }
        if (info.files > 0) s.text += tr ? " · " + std::to_string(info.files) + " dosya" : " · " + std::to_string(info.files) + " files";
        s.motion = "drift";
        board.scenes.push_back(s);
    }

    std::vector<std::string> cmds;
    auto has_tool = [&](const char* t) { return std::find(info.tooling.begin(), info.tooling.end(), t) != info.tooling.end(); };
    if (has_tool("CMake")) cmds.push_back("cmake -B build && cmake --build build");
    else if (has_tool("Cargo")) cmds.push_back("cargo build --release");
    else if (has_tool("Go modules")) cmds.push_back("go build ./...");
    else if (has_tool("npm")) cmds.push_back("npm install");
    else if (has_tool("Gradle")) cmds.push_back("./gradlew build");
    else if (has_tool("Maven")) cmds.push_back("mvn package");
    else if (has_tool("Make")) cmds.push_back("make");
    else if (has_tool("Docker")) cmds.push_back("docker compose up");
    if (!cmds.empty() && info.files > 0) {
        Scene s;
        s.title = tr ? "Başlarken" : "Getting started";
        s.kicker = tr ? "Kurulum" : "Setup";
        s.text = "git clone https://github.com/" + info.owner + "/" + info.name + "\ncd " + info.name;
        for (const auto& c : cmds) s.text += "\n" + c;
        s.layout = "terminal";
        s.motion = "still";
        board.scenes.push_back(s);
    }

    if (info.stars >= 50) {
        Scene s;
        s.title = compact_count(info.stars);
        s.text = tr ? "GitHub yıldızı" : "GitHub stars";
        s.kicker = tr ? "Topluluk" : "Community";
        s.layout = "stat";
        s.motion = "zoom";
        board.scenes.push_back(s);
    }

    Scene end;
    end.title = "github.com/" + info.owner + "/" + info.name;
    std::vector<std::string> stats;
    if (info.stars > 0 && info.stars < 50) stats.push_back(compact_count(info.stars) + (tr ? " yıldız" : " stars"));
    if (info.forks > 0) stats.push_back(compact_count(info.forks) + (tr ? " fork" : " forks"));
    if (!info.license.empty()) stats.push_back(info.license + (tr ? " lisansı" : " license"));
    for (size_t i = 0; i < stats.size(); i++) {
        if (i) end.text += " · ";
        end.text += stats[i];
    }
    if (end.text.empty()) end.text = tr ? "Açık kaynak" : "Open source";
    end.kicker = tr ? "Şimdi dene" : "Try it now";
    end.layout = "end";
    end.transition = "flash";
    end.motion = "rise";
    board.scenes.push_back(end);

    double target = in.seconds > 0 ? in.seconds : std::min(36.0, std::max(12.0, board.scenes.size() * 3.4));
    double weight_sum = 0;
    std::vector<double> w;
    for (const auto& s : board.scenes) {
        double x = 1.0 + std::min<double>(1.2, (utf8_cps(s.title).size() + utf8_cps(s.text).size()) / 90.0);
        w.push_back(x);
        weight_sum += x;
    }
    for (size_t i = 0; i < board.scenes.size(); i++) board.scenes[i].seconds = target * w[i] / weight_sum;
    apply_aspect_size(board);
    normalize(board);
    snap_to_beat(board, target);
    return board;
}

}  // namespace kare
