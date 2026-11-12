#include "core/web.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

#include "core/config.h"
#include "core/http.h"
#include "core/json.h"
#include "core/render.h"
#include "core/utf8.h"

namespace kare {

std::string url_encode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' ||
            c == '~') {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(hex[c >> 4]);
            out.push_back(hex[c & 15]);
        }
    }
    return out;
}

std::string strip_html(const std::string& html) {
    std::string out;
    bool tag = false;
    for (size_t i = 0; i < html.size(); i++) {
        char c = html[i];
        if (c == '<') {
            tag = true;
            continue;
        }
        if (c == '>') {
            tag = false;
            out.push_back(' ');
            continue;
        }
        if (tag) continue;
        if (c == '&') {
            static const std::pair<const char*, const char*> ents[] = {
                {"&amp;", "&"}, {"&lt;", "<"}, {"&gt;", ">"}, {"&quot;", "\""}, {"&#39;", "'"}, {"&nbsp;", " "}};
            bool hit = false;
            for (const auto& e : ents) {
                size_t n = std::char_traits<char>::length(e.first);
                if (html.compare(i, n, e.first) == 0) {
                    out += e.second;
                    i += n - 1;
                    hit = true;
                    break;
                }
            }
            if (hit) continue;
        }
        out.push_back(c == '\n' || c == '\t' ? ' ' : c);
    }
    return tidy(out);
}

std::string ascii_fold(const std::string& s) {
    std::vector<uint32_t> out;
    for (uint32_t cp : utf8_cps(s)) {
        cp = fold_cp(cp);
        switch (cp) {
            case 0xE7: cp = 'c'; break;
            case 0x11F: cp = 'g'; break;
            case 0x131: cp = 'i'; break;
            case 0xF6: case 0xF2: case 0xF3: case 0xF4: case 0xF5: case 0xF8: cp = 'o'; break;
            case 0x15F: cp = 's'; break;
            case 0xFC: case 0xF9: case 0xFA: case 0xFB: cp = 'u'; break;
            case 0xE0: case 0xE1: case 0xE2: case 0xE3: case 0xE4: case 0xE5: cp = 'a'; break;
            case 0xE8: case 0xE9: case 0xEA: case 0xEB: cp = 'e'; break;
            case 0xEC: case 0xED: case 0xEE: case 0xEF: cp = 'i'; break;
            case 0xF1: cp = 'n'; break;
            default: break;
        }
        out.push_back(cp);
    }
    return cps_utf8(out);
}

namespace {

bool word_char(uint32_t cp) { return (cp >= '0' && cp <= '9') || (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z') || cp > 127; }

std::vector<std::string> plain_words(const std::string& text) {
    std::vector<std::string> words;
    std::vector<uint32_t> cur;
    bool skip_suffix = false;
    auto flush = [&] {
        if (!cur.empty()) words.push_back(cps_utf8(cur));
        cur.clear();
        skip_suffix = false;
    };
    for (uint32_t cp : utf8_cps(text)) {
        if (cp == '\'' || cp == 0x2019) {
            skip_suffix = !cur.empty();
            continue;
        }
        if (!word_char(cp)) {
            flush();
            continue;
        }
        if (!skip_suffix) cur.push_back(cp);
    }
    flush();
    return words;
}

const char* const kFiller[] = {
    "video", "videosu", "videoyu", "videosunu", "videolar", "yap", "yapar", "yapin", "yapsin", "yapabilir", "yapmak", "olustur",
    "olusturun", "olustursun", "hazirla", "hazirlar", "hazirlasin", "anlatan", "anlat", "anlatsin", "anlatir", "anlatimli",
    "anlatim", "anlatimi", "hakkinda", "ile", "ilgili", "bir", "bu", "su", "olayini", "olayi", "olay", "olaylari",
    "olaylarini", "hikayesi", "hikayesini", "hikaye", "hikayeyi", "kisa", "uzun", "shorts", "short", "reels", "tiktok",
    "dikey", "yatay", "sesli", "seslendirmeli", "seslendirme", "turkce", "ingilizce", "internetten", "internet", "arastir",
    "arastirip", "arastirarak", "arastirsin", "gorsel", "gorselleri", "gorsellerle", "gorselli", "fotograf", "fotograflari",
    "fotograflarla", "resim", "resimleri", "muzik", "muzikli", "muzigi", "arka", "plan", "edit", "editli", "editle",
    "saniye", "saniyelik", "sn", "dakika", "dakikalik", "belgesel", "belgeseli", "icin", "gibi", "mi", "mu", "lutfen",
    "bana", "benim", "ver", "istiyorum", "olsun", "tanitan", "tanit", "tanitim", "youtube", "instagram", "kanal", "kanali",
    "nedir", "kimdir", "neydi", "kimdi", "konu", "konulu", "konusunu", "make", "create", "a", "an", "the", "about", "of",
    "on", "explaining", "explain", "explains", "narrated", "narration", "story", "documentary", "with", "images",
    "pictures", "photos", "music", "voice", "voiceover", "english", "turkish", "please", "vertical", "seconds", "second",
    "minute", "minutes", "for", "me", "what", "who", "was", "is", "tell", "telling", "video's", "clip", "reel"};

bool filler(const std::string& folded) {
    for (const char* f : kFiller)
        if (folded == f) return true;
    return false;
}

Json fetch_json(const std::string& url, std::string& err) {
    HttpResult r = http_request("GET", url, "", "Accept: application/json\r\n", 20000, 8u << 20);
    Json j;
    if (r.status != 200) {
        err = r.error.empty() ? "HTTP " + std::to_string(r.status) : r.error;
        return j;
    }
    std::string perr;
    if (!json_parse(r.body, j, perr)) {
        err = "bad JSON from " + url.substr(0, 40);
        j = Json();
    }
    return j;
}

std::string wiki_api(const std::string& lang) {
    return "https://" + lang + ".wikipedia.org/w/api.php?format=json&formatversion=2&";
}

std::vector<std::string> search_titles(const std::string& lang, const std::string& query, std::string& err) {
    std::vector<std::string> out;
    Json j = fetch_json(wiki_api(lang) + "action=query&list=search&srlimit=6&srsearch=" + url_encode(query), err);
    const Json* q = j.field("query");
    const Json* s = q ? q->field("search") : nullptr;
    if (s && s->type == Json::Arr)
        for (const auto& it : s->a) out.push_back(it.str("title"));
    return out;
}

struct Page {
    std::string title, extract, url;
    std::map<std::string, std::string> links;
    bool disambiguation = false;
};

bool fetch_page(const std::string& lang, const std::string& title, Page& page, std::string& err) {
    Json j = fetch_json(wiki_api(lang) +
                            "action=query&redirects=1&prop=extracts|langlinks|pageprops|info&inprop=url&explaintext=1"
                            "&exsectionformat=wiki&lllimit=500&ppprop=disambiguation&titles=" +
                            url_encode(title),
                        err);
    const Json* q = j.field("query");
    const Json* pages = q ? q->field("pages") : nullptr;
    if (!pages || pages->type != Json::Arr || pages->a.empty()) return false;
    const Json& pg = pages->a[0];
    if (pg.field("missing")) return false;
    page.title = pg.str("title");
    page.extract = pg.str("extract");
    page.url = pg.str("fullurl");
    if (const Json* pp = pg.field("pageprops")) page.disambiguation = pp->field("disambiguation") != nullptr;
    if (const Json* ll = pg.field("langlinks"); ll && ll->type == Json::Arr)
        for (const auto& l : ll->a) page.links[l.str("lang")] = l.str("title");
    return !page.title.empty();
}

void split_extract(const std::string& extract, std::string& intro, std::vector<WebSection>& sections) {
    std::string* cur = &intro;
    size_t i = 0;
    while (i <= extract.size()) {
        size_t nl = extract.find('\n', i);
        if (nl == std::string::npos) nl = extract.size();
        std::string line = extract.substr(i, nl - i);
        i = nl + 1;
        while (!line.empty() && (line.back() == ' ' || line.back() == '\r' || line.back() == '\t')) line.pop_back();
        while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) line.erase(line.begin());
        if (line.empty()) continue;
        if (line.size() > 4 && line[0] == '=' && line.back() == '=') {
            std::string h = line;
            while (!h.empty() && (h.front() == '=' || h.front() == ' ')) h.erase(h.begin());
            while (!h.empty() && (h.back() == '=' || h.back() == ' ')) h.pop_back();
            sections.push_back({h, {}});
            cur = &sections.back().text;
            continue;
        }
        if (!cur->empty()) cur->push_back('\n');
        *cur += line;
    }
}

std::vector<WebImage> media_list(const std::string& lang, const std::string& title, std::string& err) {
    std::string t = title;
    std::replace(t.begin(), t.end(), ' ', '_');
    Json j = fetch_json("https://" + lang + ".wikipedia.org/api/rest_v1/page/media-list/" + url_encode(t), err);
    std::vector<WebImage> out;
    const Json* items = j.field("items");
    if (!items || items->type != Json::Arr) return out;
    for (const auto& it : items->a) {
        if (it.str("type") != "image") continue;
        WebImage im;
        im.file = it.str("title");
        im.section = static_cast<int>(it.num("section_id", 0));
        if (const Json* c = it.field("caption")) im.caption = tidy(c->str("text"));
        if (!im.file.empty()) out.push_back(std::move(im));
    }
    return out;
}

std::vector<WebImage> commons_search(const std::string& query, std::string& err) {
    Json j = fetch_json("https://commons.wikimedia.org/w/api.php?format=json&formatversion=2&action=query&list=search"
                        "&srnamespace=6&srlimit=24&srsearch=" +
                            url_encode(query + " filetype:bitmap"),
                        err);
    std::vector<WebImage> out;
    const Json* q = j.field("query");
    const Json* s = q ? q->field("search") : nullptr;
    if (s && s->type == Json::Arr)
        for (const auto& it : s->a) {
            WebImage im;
            im.file = it.str("title");
            im.section = 1000;
            out.push_back(std::move(im));
        }
    return out;
}

std::string norm_file(std::string t) {
    std::replace(t.begin(), t.end(), '_', ' ');
    return fold_tr(t);
}

void fill_info(std::vector<WebImage>& images, std::string& err) {
    for (size_t at = 0; at < images.size(); at += 40) {
        std::string titles;
        size_t end = std::min(images.size(), at + 40);
        for (size_t i = at; i < end; i++) {
            if (i > at) titles += "%7C";
            titles += url_encode(images[i].file);
        }
        Json j = fetch_json("https://commons.wikimedia.org/w/api.php?format=json&formatversion=2&action=query"
                            "&prop=imageinfo&iiprop=url%7Csize%7Cmime%7Cextmetadata&iiurlwidth=1600"
                            "&iiextmetadatafilter=Artist%7CLicenseShortName%7CImageDescription&titles=" +
                                titles,
                            err);
        const Json* q = j.field("query");
        const Json* pages = q ? q->field("pages") : nullptr;
        if (!pages || pages->type != Json::Arr) continue;
        std::map<std::string, const Json*> by_title;
        for (const auto& p : pages->a) by_title[norm_file(p.str("title"))] = &p;
        for (size_t i = at; i < end; i++) {
            auto it = by_title.find(norm_file(images[i].file));
            if (it == by_title.end()) continue;
            const Json* info = it->second->field("imageinfo");
            if (!info || info->type != Json::Arr || info->a.empty()) continue;
            const Json& ii = info->a[0];
            std::string mime = ii.str("mime");
            if (mime != "image/jpeg" && mime != "image/png" && mime != "image/tiff") continue;
            images[i].w = static_cast<int>(ii.num("width", 0));
            images[i].h = static_cast<int>(ii.num("height", 0));
            images[i].source = ii.str("thumburl");
            if (images[i].source.empty()) images[i].source = ii.str("url");
            if (const Json* meta = ii.field("extmetadata")) {
                if (const Json* a = meta->field("Artist")) images[i].artist = strip_html(a->str("value"));
                if (const Json* l = meta->field("LicenseShortName")) images[i].license = strip_html(l->str("value"));
                if (images[i].caption.empty())
                    if (const Json* d = meta->field("ImageDescription")) images[i].caption = strip_html(d->str("value"));
            }
            auto cps = utf8_cps(images[i].caption);
            if (cps.size() > 200) images[i].caption = cps_utf8(std::vector<uint32_t>(cps.begin(), cps.begin() + 200));
        }
    }
}

bool usable(const WebImage& im) {
    if (im.source.empty() || im.w < 480 || im.h < 320) return false;
    double a = static_cast<double>(im.w) / im.h;
    if (a > 3.2 || a < 0.33) return false;
    std::string f = ascii_fold(im.file);
    static const char* bad[] = {"logo",    "icon",    "flag_of", "signature", "seal_of", "coat_of_arms", "emblem",
                                "symbol",  "padlock", "ambox",   "question_book", "locator", "location_map", "wikisource",
                                "wikiquote", "commons-"};
    for (const char* b : bad)
        if (f.find(b) != std::string::npos) return false;
    return true;
}

std::string slug(const std::string& file) {
    std::string base = ascii_fold(file.rfind("File:", 0) == 0 ? file.substr(5) : file);
    std::string out;
    for (char c : base) {
        bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
        if (ok) out.push_back(c);
        else if (!out.empty() && out.back() != '_') out.push_back('_');
        if (out.size() >= 48) break;
    }
    uint32_t h = 2166136261u;
    for (unsigned char c : file) h = (h ^ c) * 16777619u;
    char tail[16];
    std::snprintf(tail, sizeof tail, "_%08x", h);
    return out + tail;
}

bool download(WebImage& im) {
    std::string src = im.source;
    std::string ext = ".jpg";
    std::string lower = ascii_fold(src.substr(0, src.find('?')));
    if (lower.size() > 4 && lower.compare(lower.size() - 4, 4, ".png") == 0) ext = ".png";
    std::string path = media_dir() + "\\" + slug(im.file) + ext;
    std::vector<unsigned char> existing;
    if (!(read_file(path, existing) && existing.size() > 2000)) {
        HttpResult r = http_request("GET", src, "", "", 30000, 24u << 20);
        if (r.status != 200 || r.body.size() < 2000) return false;
        if (!write_bytes(path, r.body.data(), r.body.size())) return false;
    }
    if (!image_readable(path)) return false;
    im.path = path;
    return true;
}

}  // namespace

std::string media_dir() {
    std::string dir = data_dir() + "\\media";
    ensure_dir(data_dir());
    ensure_dir(dir);
    return dir;
}

std::string research_query(const std::string& prompt) {
    std::string out;
    auto words = plain_words(prompt);
    for (size_t i = 0; i < words.size(); i++) {
        std::string f = ascii_fold(words[i]);
        if (filler(f)) continue;
        bool digits = !f.empty() && std::all_of(f.begin(), f.end(), [](char c) { return c >= '0' && c <= '9'; });
        if (digits && i + 1 < words.size()) {
            std::string unit = ascii_fold(words[i + 1]);
            if (unit.rfind("saniye", 0) == 0 || unit.rfind("dakika", 0) == 0 || unit == "sn" || unit.rfind("second", 0) == 0 ||
                unit.rfind("minute", 0) == 0 || unit == "s" || unit == "sec")
                continue;
        }
        if (!out.empty()) out.push_back(' ');
        out += words[i];
    }
    return out.empty() ? tidy(prompt) : out;
}

bool wants_research(const std::string& prompt) {
    std::string f = " " + ascii_fold(prompt) + " ";
    static const char* keys[] = {"anlatan", "anlat", "hakkinda", "tarihi", "tarihce", "hikayesi", "hikayesini", "nedir",
                                 "kimdir", "neydi", "kimdi", "olayini", "olayi ", "belgesel", "internetten", "arastir",
                                 "biyografi", "explain", "about ", "history of", "story of", "documentary", "who was",
                                 "what is", "biography"};
    for (const char* k : keys)
        if (f.find(k) != std::string::npos) return true;
    return false;
}

Research research_topic(const std::string& query, const std::string& search_lang, const std::string& lang, int max_images,
                        const Note& note) {
    Research r;
    r.lang = lang;
    auto say = [&](const std::string& s) {
        if (note) note(s);
    };
    bool tr = lang == "tr";
    say(tr ? "Vikipedi'de aran\xC4\xB1yor: " + query : "Searching Wikipedia: " + query);
    std::string err;
    Page page;
    bool found = false;
    for (const std::string& sl : {search_lang, lang}) {
        auto titles = search_titles(sl, query, err);
        for (size_t i = 0; i < titles.size() && i < 4 && !found; i++) {
            Page p;
            if (!fetch_page(sl, titles[i], p, err) || p.disambiguation || p.extract.size() < 400) continue;
            if (sl != lang) {
                auto it = p.links.find(lang);
                if (it == p.links.end()) continue;
                Page target;
                if (!fetch_page(lang, it->second, target, err) || target.extract.size() < 400) continue;
                p = std::move(target);
            }
            page = std::move(p);
            found = true;
        }
        if (found || sl == lang) break;
    }
    if (!found) {
        r.error = err.empty() ? (tr ? "Konu Vikipedi'de bulunamad\xC4\xB1" : "Topic not found on Wikipedia") : err;
        return r;
    }
    r.title = page.title;
    r.url = page.url;
    split_extract(page.extract, r.intro, r.sections);

    say(tr ? "G\xC3\xB6rseller toplan\xC4\xB1yor" : "Collecting images");
    std::string img_lang = lang;
    std::string img_title = page.title;
    if (lang != "en") {
        auto it = page.links.find("en");
        if (it != page.links.end()) {
            img_lang = "en";
            img_title = it->second;
        }
    }
    std::vector<WebImage> images = media_list(img_lang, img_title, err);
    if (img_lang != lang) {
        auto own = media_list(lang, page.title, err);
        for (auto& im : own) {
            bool dup = std::any_of(images.begin(), images.end(), [&](const WebImage& x) { return norm_file(x.file) == norm_file(im.file); });
            if (!dup) {
                im.section += 500;
                images.push_back(std::move(im));
            }
        }
    }
    fill_info(images, err);
    images.erase(std::remove_if(images.begin(), images.end(), [](const WebImage& im) { return !usable(im); }), images.end());
    if (static_cast<int>(images.size()) < max_images / 2) {
        auto extra = commons_search(img_lang == "en" ? img_title : query, err);
        fill_info(extra, err);
        for (auto& im : extra) {
            if (!usable(im)) continue;
            bool dup = std::any_of(images.begin(), images.end(), [&](const WebImage& x) { return norm_file(x.file) == norm_file(im.file); });
            if (!dup) images.push_back(std::move(im));
        }
    }
    int total = std::min(max_images, static_cast<int>(images.size()));
    // Sample the whole article evenly so later chapters get pictures too; the rest stand in for failed downloads.
    std::vector<size_t> order;
    std::vector<bool> picked(images.size(), false);
    for (int i = 0; i < total; i++) {
        size_t k = total > 1 ? static_cast<size_t>(std::lround(static_cast<double>(i) * (images.size() - 1) / (total - 1))) : 0;
        if (!picked[k]) {
            picked[k] = true;
            order.push_back(k);
        }
    }
    for (size_t k = 0; k < images.size(); k++)
        if (!picked[k]) order.push_back(k);
    int done = 0;
    std::vector<size_t> got;
    for (size_t k : order) {
        if (done >= max_images) break;
        char msg[96];
        std::snprintf(msg, sizeof msg, tr ? "G\xC3\xB6rsel indiriliyor %d/%d" : "Downloading image %d/%d", done + 1, total);
        say(msg);
        if (download(images[k])) {
            got.push_back(k);
            done++;
        }
    }
    std::sort(got.begin(), got.end());
    for (size_t k : got) r.images.push_back(images[k]);
    if (r.images.empty()) r.error = tr ? "G\xC3\xB6rsel indirilemedi" : "No images could be downloaded";
    return r;
}

std::string research_brief(const Research& r, size_t max_chars) {
    std::string out = "Article: " + r.title + "\n\n" + r.intro + "\n";
    static const char* skip[] = {"kaynak", "dis baglanti", "ayrica bak", "notlar", "referans", "references", "see also",
                                 "external links", "further reading", "notes", "bibliography", "kaynakca", "dipnot"};
    for (const auto& s : r.sections) {
        if (out.size() >= max_chars) break;
        std::string h = ascii_fold(s.heading);
        bool bad = s.text.empty();
        for (const char* k : skip)
            if (h.find(k) != std::string::npos) bad = true;
        if (bad) continue;
        auto cps = utf8_cps(s.text);
        if (cps.size() > 600) cps.resize(600);
        out += "\n## " + s.heading + "\n" + cps_utf8(cps) + "\n";
    }
    auto all = utf8_cps(out);
    if (all.size() > max_chars) all.resize(max_chars);
    return cps_utf8(all);
}

std::string research_credits(const Research& r, bool turkish) {
    std::string out = turkish ? "Metin: Vikipedi, \"" + r.title + "\" (" + r.url + "), CC BY-SA 4.0\nG\xC3\xB6rseller: Wikimedia Commons\n"
                              : "Text: Wikipedia, \"" + r.title + "\" (" + r.url + "), CC BY-SA 4.0\nImages: Wikimedia Commons\n";
    for (const auto& im : r.images) {
        std::string name = im.file.rfind("File:", 0) == 0 ? im.file.substr(5) : im.file;
        out += "- " + name;
        if (!im.artist.empty()) out += " \xE2\x80\x94 " + im.artist;
        if (!im.license.empty()) out += ", " + im.license;
        out += "\n";
    }
    return out;
}

}  // namespace kare
