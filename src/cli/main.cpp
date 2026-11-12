#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

#include "core/audio.h"
#include "core/config.h"
#include "core/director.h"
#include "core/encode.h"
#include "core/font.h"
#include "core/github.h"
#include "core/render.h"
#include "core/story.h"
#include "core/tts.h"
#include "core/utf8.h"
#include "core/web.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#endif

namespace {

int fails = 0;

void expect(bool ok, const char* name) {
    if (ok) std::cout << "ok   " << name << "\n";
    else {
        std::cout << "FAIL " << name << "\n";
        fails++;
    }
}

bool contains(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }

bool scene_has(const kare::Storyboard& b, const std::string& part) {
    if (contains(b.title, part)) return true;
    for (const auto& s : b.scenes)
        if (contains(s.title, part) || contains(s.text, part)) return true;
    return false;
}

std::string temp_path(const char* name) {
    const char* t = std::getenv("TEMP");
    if (!t) t = std::getenv("TMP");
    if (!t) t = ".";
    return std::string(t) + "\\" + name;
}

int selftest() {
    expect(kare::fold_tr("İSTANBUL") == "istanbul", "fold istanbul");
    expect(kare::fold_tr("Night") == "night", "fold night");
    expect(kare::fold_tr("I") == "i", "fold I");

    kare::PlanInput dusk;
    dusk.prompt = "Gün batımında sahil, 8 saniye";
    auto dusk_board = kare::plan_local(dusk);
    expect(dusk_board.style == "dusk", "dusk style");
    expect(dusk_board.aspect == "16:9", "dusk aspect");
    expect(std::abs(kare::total_seconds(dusk_board) - 8.0) < 0.25, "dusk duration");
    expect(scene_has(dusk_board, "sahil"), "dusk shows sahil");

    kare::PlanInput list;
    list.prompt = "Dikey tanıtım\n1. Yerel\n2. Hızlı\n3. Kurgu";
    auto list_board = kare::plan_local(list);
    expect(list_board.aspect == "9:16", "vertical aspect");
    expect(list_board.scenes.size() >= 3, "list scenes");
    expect(scene_has(list_board, "Yerel"), "list item");

    kare::PlanInput hello;
    hello.prompt = "Minimal square 1:1 title: Hello, 4 seconds";
    auto hello_board = kare::plan_local(hello);
    expect(hello_board.style == "minimal", "minimal style");
    expect(hello_board.aspect == "1:1", "square aspect");
    expect(hello_board.title == "Hello", "explicit title");
    expect(std::abs(kare::total_seconds(hello_board) - 4.0) < 0.25, "hello duration");

    kare::PlanInput night;
    night.prompt = "Night market, 5 seconds";
    auto night_board = kare::plan_local(night);
    expect(night_board.style == "night", "night style");
    expect(scene_has(night_board, "market") || scene_has(night_board, "Night"), "night text");

    kare::PlanInput clauses;
    clauses.prompt = "Kapadokya'da gün doğumu: sıcak hava balonları, peri bacaları ve sessiz bir sabah, 20 saniye";
    auto clause_board = kare::plan_local(clauses);
    expect(clause_board.title == "Kapadokya'da gün doğumu" && clause_board.style == "dusk", "clause head");
    expect(clause_board.scenes.size() == 5 && scene_has(clause_board, "Peri bacaları") &&
               scene_has(clause_board, "Sessiz bir sabah"),
           "clause scenes");

    std::string dumped = kare::story_to_json(dusk_board);
    kare::Storyboard round;
    std::string err;
    expect(kare::story_from_json(dumped, round, err), "json roundtrip");
    expect(round.style == "dusk" && round.scenes.size() == dusk_board.scenes.size(), "json fields");

    const char* sample =
        R"({"title":"Merhaba","style":"sea","aspect":"1:1","scenes":[{"title":"Bir","text":"Iki","seconds":2,"motion":"rise"}]})";
    kare::Storyboard parsed;
    expect(kare::story_from_json(sample, parsed, err), "sample json");
    expect(parsed.title == "Merhaba" && parsed.style == "sea" && parsed.scenes.size() == 1, "sample fields");
    expect(parsed.scenes[0].motion == "rise", "sample motion");

    std::string owner, repo;
    expect(kare::find_github_repo("bunu incele https://github.com/Hydra-Ai-tr/kare.git 20 saniye", owner, repo) &&
               owner == "Hydra-Ai-tr" && repo == "kare",
           "github url");
    expect(kare::find_github_repo("github.com/ocornut/imgui/tree/master", owner, repo) && repo == "imgui", "github tree url");
    expect(!kare::find_github_repo("https://github.com/orgs/x", owner, repo), "github org url");
    expect(kare::strip_github_url("https://github.com/a/b dikey 10 saniye") == "dikey 10 saniye", "github note");
    expect(kare::clean_markdown("**Fast** [docs](https://x.y) `cmd` :rocket:") == "Fast docs cmd", "markdown clean");
    const char* readme =
        "# Demo\n[![ci](https://b/x.svg)](https://c)\n\nDemo turns prompts into short local videos for everyone.\n\n"
        "## Install\n- run this\n- then that\n\n## Features\n- **Local** - works offline\n- Fast export\n- Tiny\n";
    expect(kare::readme_summary(readme).find("Demo turns prompts") == 0, "readme summary");
    auto feats = kare::readme_features(readme, 3);
    expect(feats.size() == 3 && feats[0] == "Local - works offline", "readme features");
    kare::RepoInfo info;
    info.owner = "a";
    info.name = "demo";
    info.description = "Local video studio";
    info.readme = readme;
    info.stars = 1520;
    info.license = "MIT";
    info.languages = {{"C++", 0.9}, {"C", 0.1}};
    info.tooling = {"CMake"};
    info.files = 12;
    kare::PlanInput repo_in;
    auto repo_board = kare::plan_repo_local(info, repo_in);
    expect(repo_board.title == "demo" && repo_board.style == "night", "repo board");
    expect(scene_has(repo_board, "github.com/a/demo") && scene_has(repo_board, "1.5k"), "repo end card");
    expect(scene_has(repo_board, "Local") && scene_has(repo_board, "C++ %90"), "repo features and tech");
    std::string repo_layouts;
    for (int i = 0; i < static_cast<int>(repo_board.scenes.size()); i++) repo_layouts += kare::resolve_layout(repo_board, i) + " ";
    expect(contains(repo_layouts, "hero") && contains(repo_layouts, "list") && contains(repo_layouts, "bars") &&
               contains(repo_layouts, "terminal") && contains(repo_layouts, "stat") && contains(repo_layouts, "end"),
           "repo edit layouts");
    if (fails) std::cout << "     " << repo_layouts << "\n";

    kare::Storyboard moved = list_board;
    std::string first = moved.scenes.front().title;
    expect(kare::move_scene(moved, 0, static_cast<int>(moved.scenes.size()) - 1), "move scene");
    expect(moved.scenes.back().title == first, "moved title");

    std::string font_err;
    expect(kare::fonts_load(font_err), "font load");
    if (!font_err.empty() && fails) std::cout << "     " << font_err << "\n";
    const kare::Glyph& g = kare::glyph_for(false, 0x15F, 48);
    expect(g.w > 0 && g.h > 0, "glyph s-cedilla");

    kare::Frame frame_a, frame_b;
    kare::render_frame(dusk_board, kare::total_seconds(dusk_board) * 0.4, frame_a);
    kare::render_frame(hello_board, kare::total_seconds(hello_board) * 0.4, frame_b);
    bool varied = false;
    if (!frame_a.rgba.empty()) {
        uint8_t r0 = frame_a.rgba[0];
        for (size_t i = 0; i < frame_a.rgba.size(); i += 4) {
            if (frame_a.rgba[i] != r0) {
                varied = true;
                break;
            }
        }
    }
    expect(varied, "frame is not flat");
    expect(kare::frame_hash(frame_a) != kare::frame_hash(frame_b), "frames differ");

    kare::Storyboard tiny;
    tiny.width = 320;
    tiny.height = 180;
    tiny.fps = 12;
    tiny.aspect = "16:9";
    tiny.style = "ember";
    tiny.title = "Test";
    tiny.mark = true;
    kare::Scene scene;
    scene.title = "Merhaba";
    scene.text = "Şğüİı";
    scene.seconds = 0.5;
    scene.motion = "zoom";
    tiny.scenes.push_back(scene);
    kare::normalize(tiny);
    expect(tiny.width == 320 && tiny.height == 180, "keep small size");

    std::string mp4 = temp_path("kare-selftest.mp4");
    std::string avi = temp_path("kare-selftest.avi");
    std::string enc = kare::encode_video(tiny, mp4, {});
    expect(enc.empty(), "encode mp4");
    if (!enc.empty()) std::cout << "     " << enc << "\n";
    std::string probe_err;
    bool mp4_audio = false;
    expect(kare::probe_video(mp4, probe_err, &mp4_audio), "read mp4 back");
    expect(mp4_audio, "mp4 has audio");
    if (!probe_err.empty() && !enc.empty()) std::cout << "     " << probe_err << "\n";
    else if (!probe_err.empty()) std::cout << "     " << probe_err << "\n";

    enc = kare::encode_video(tiny, avi, {});
    expect(enc.empty(), "encode avi");
    std::vector<unsigned char> avi_bytes;
    expect(kare::read_file(avi, avi_bytes) && avi_bytes.size() > 64, "avi readable");
    bool riff = avi_bytes.size() > 12 && avi_bytes[0] == 'R' && avi_bytes[1] == 'I' && avi_bytes[2] == 'F' && avi_bytes[3] == 'F';
    expect(riff, "avi riff");
    bool jpeg = false;
    for (size_t i = 0; i + 1 < avi_bytes.size(); i++) {
        if (avi_bytes[i] == 0xFF && avi_bytes[i + 1] == 0xD8) {
            jpeg = true;
            break;
        }
    }
    expect(jpeg, "avi jpeg");
    std::string avi_text(avi_bytes.begin(), avi_bytes.end());
    expect(contains(avi_text, "auds") && contains(avi_text, "01wb"), "avi audio stream");

    auto track = kare::render_soundtrack(dusk_board);
    double track_sec = static_cast<double>(track.size() / 2) / kare::kAudioRate;
    expect(std::abs(track_sec - kare::total_seconds(dusk_board)) < 0.05, "soundtrack length");
    double energy = 0;
    int clipped = 0;
    for (int16_t v : track) {
        energy += static_cast<double>(v) * v;
        if (v >= 32000 || v <= -32000) clipped++;
    }
    double rms = track.empty() ? 0 : std::sqrt(energy / static_cast<double>(track.size())) / 32768.0;
    expect(rms > 0.05 && rms < 0.6, "soundtrack level");
    expect(clipped < static_cast<int>(track.size() / 1000) + 1, "soundtrack not clipped");
    auto wav = kare::wav_bytes(track, 0);
    expect(wav.size() == 44 + track.size() * 2 && wav[0] == 'R' && wav[8] == 'W', "wav bytes");
    kare::Storyboard silent = dusk_board;
    silent.music = "none";
    expect(kare::render_soundtrack(silent).empty(), "music none is silent");

    kare::Storyboard snapped = dusk_board;
    kare::snap_to_beat(snapped, 8.0);
    double beat = kare::beat_seconds(snapped);
    bool on_grid = std::abs(kare::total_seconds(snapped) - 8.0) < 0.01;
    for (const auto& s : snapped.scenes) {
        double k = s.seconds / beat;
        on_grid = on_grid && std::abs(k - std::round(k)) < 1e-3;
    }
    expect(on_grid, "scenes snap to beat");

    kare::Storyboard lay;
    lay.title = "Kare";
    kare::Scene a;
    a.title = "Kare";
    a.text = "Yerel video stüdyosu";
    lay.scenes.push_back(a);
    kare::Scene st;
    st.title = "%98";
    st.text = "daha hızlı";
    lay.scenes.push_back(st);
    kare::Scene li;
    li.title = "Özellikler";
    li.text = "Yerel\nHızlı\nÜcretsiz";
    lay.scenes.push_back(li);
    kare::Scene end;
    end.title = "Şimdi dene";
    end.text = "github.com/Hydra-Ai-tr/kare";
    lay.scenes.push_back(end);
    kare::normalize(lay);
    expect(kare::resolve_layout(lay, 0) == "hero", "layout hero");
    expect(kare::resolve_layout(lay, 1) == "stat", "layout stat");
    expect(kare::resolve_layout(lay, 2) == "list", "layout list");
    expect(kare::resolve_layout(lay, 3) == "end", "layout end");
    kare::Frame mid;
    kare::render_frame(lay, lay.scenes[0].seconds - 0.1, mid);
    expect(!mid.rgba.empty(), "transition frame");

    expect(kare::research_query("Titanik olayını anlatan shorts videosu yap") == "Titanik", "research query tr");
    expect(kare::research_query("make a short video about the Apollo 11 mission") == "Apollo 11 mission", "research query en");
    expect(kare::wants_research("Titanik olayını anlatan video"), "wants research");
    expect(!kare::wants_research("gün batımında sahil, 8 saniye"), "no research for a mood prompt");

    kare::Storyboard doc;
    doc.width = 360;
    doc.height = 640;
    doc.aspect = "9:16";
    kare::Scene shot;
    shot.image = temp_path("kare_selftest_story.png");
    shot.narration = "Bu bir deneme anlatımıdır.";
    std::vector<uint8_t> px(64 * 48 * 4);
    for (size_t k = 0; k < px.size(); k++) px[k] = static_cast<uint8_t>((k * 37) & 0xFF);
    std::string perr;
    kare::write_png(shot.image, 64, 48, px.data(), perr);
    doc.scenes.push_back(shot);
    kare::normalize(doc);
    expect(kare::resolve_layout(doc, 0) == "story", "layout story");
    kare::Frame docf;
    kare::render_frame(doc, 1.0, docf);
    expect(docf.w == 360 && docf.h == 640, "story frame");

    auto voices = kare::list_voices();
    std::string voice;
    for (const auto& v : voices)
        if (v.lang == "tr") voice = "tr";
    if (voice.empty() && !voices.empty()) voice = voices.front().name;
    if (voice.empty()) {
        std::cout << "skip narration (no speech voice installed)\n";
    } else {
        auto clip = kare::narrate("Merhaba, bu Kare.", voice, 0);
        expect(clip && clip->seconds > 0.4 && !clip->pcm.empty(), "narrate");
        expect(clip && clip->words.size() >= 2, "narration word timings");
        doc.voice = voice;
        doc.music = "none";
        kare::fit_to_voice(doc);
        auto voiced = kare::render_soundtrack(doc);
        int loud = 0;
        for (int16_t v : voiced)
            if (v > 3000 || v < -3000) loud++;
        expect(!voiced.empty() && loud > 100, "narration in soundtrack");
        expect(std::abs(doc.scenes[0].seconds - (kare::kVoiceLead + clip->seconds)) < 3.0, "scene fits voice");
    }

    {
        kare::Storyboard ed;
        ed.width = 320;
        ed.height = 180;
        ed.mark = false;
        ed.grain = false;
        ed.music = "none";
        kare::Scene sc;
        sc.title = "Elle";
        sc.seconds = 3;
        kare::Layer box;
        box.kind = "box";
        box.x = 0.5;
        box.y = 0.5;
        box.w = 0.3;
        box.h = 0.3;
        box.fill = "#ff0000";
        box.in = "none";
        box.shadow = false;
        sc.layers.push_back(box);
        kare::Layer text;
        text.text = "*Merhaba*";
        text.x = 0.2;
        text.y = 0.3;
        text.start = 1.0;
        text.end = 2.0;
        sc.layers.push_back(text);
        ed.scenes.push_back(sc);
        kare::normalize(ed);
        kare::Frame lf;
        kare::render_frame(ed, 1.5, lf);
        const uint8_t* mid = &lf.rgba[(static_cast<size_t>(90) * 320 + 160) * 4];
        expect(mid[0] > 200 && mid[1] < 60 && mid[2] < 60, "box layer drawn");
        float cx = 0, cy = 0, w = 0, h = 0;
        expect(kare::layer_box(ed, 0, 0, cx, cy, w, h) && std::abs(cx - 160) < 1 && std::abs(w - 96) < 1 && std::abs(h - 54) < 1,
               "layer box geometry");
        expect(kare::layer_visible(ed, 0, 1, 1.5) && !kare::layer_visible(ed, 0, 1, 2.5), "layer timing");

        ed.scenes[0].look.filter = "bw";
        kare::render_frame(ed, 1.5, lf);
        const uint8_t* bg = &lf.rgba[(static_cast<size_t>(8) * 320 + 8) * 4];
        mid = &lf.rgba[(static_cast<size_t>(90) * 320 + 160) * 4];
        expect(std::abs(bg[0] - bg[1]) < 4 && std::abs(bg[1] - bg[2]) < 4 && mid[0] > 200 && mid[1] < 60,
               "bw look grades the scene, not the layers");

        ed.scenes[0].camera = "left";
        ed.scenes[0].layers[0].rotation = 30;
        ed.music_volume = 0.5;
        ed.caption_color = "#00FF00";
        ed.caption_upper = false;
        std::string dumped_ed = kare::story_to_json(ed);
        kare::Storyboard back;
        expect(kare::story_from_json(dumped_ed, back, err) && back.scenes.size() == 1 && back.scenes[0].layers.size() == 2 &&
                   back.scenes[0].layers[0].kind == "box" && back.scenes[0].layers[0].rotation == 30 &&
                   back.scenes[0].layers[1].text == "*Merhaba*" && back.scenes[0].layers[1].end == 2.0 &&
                   back.scenes[0].look.filter == "bw" && back.scenes[0].camera == "left" && back.music_volume == 0.5 &&
                   back.caption_color == "#00ff00" && !back.caption_upper,
               "edit json roundtrip");

        kare::Layer junk;
        junk.kind = "laser";
        junk.x = 9;
        junk.color = "mavi";
        junk.in = "boom";
        kare::normalize_layer(junk);
        expect(junk.kind == "text" && junk.x == 1.5 && junk.color == "#ffffff" && junk.in == "fade", "layer normalize");

        std::vector<int16_t> tone(static_cast<size_t>(kare::kAudioRate) * 2);
        for (size_t k = 0; k < tone.size() / 2; k++) {
            int16_t v = static_cast<int16_t>(std::sin(k * 2.0 * 3.14159265 * 440.0 / kare::kAudioRate) * 12000);
            tone[k * 2] = tone[k * 2 + 1] = v;
        }
        std::string wav = temp_path("kare_selftest_music.wav");
        auto bytes = kare::wav_bytes(tone);
        kare::write_bytes(wav, bytes.data(), bytes.size());
        ed.music_file = wav;
        ed.music_volume = 1.0;
        auto own = kare::render_soundtrack(ed);
        int loud = 0;
        for (size_t k = static_cast<size_t>(kare::kAudioRate) * 2; k < static_cast<size_t>(kare::kAudioRate) * 4 && k < own.size(); k++)
            if (own[k] > 6000 || own[k] < -6000) loud++;
        expect(own.size() == static_cast<size_t>(3 * kare::kAudioRate) * 2 && loud > 1000, "own music file loops");
#ifdef _WIN32
        _wremove(kare::widen(wav).c_str());
#endif
    }
#ifdef _WIN32
    _wremove(kare::widen(shot.image).c_str());
#endif

#ifdef _WIN32
    _wremove(kare::widen(mp4).c_str());
    _wremove(kare::widen(avi).c_str());
#endif
    std::cout << (fails ? "selftest failed\n" : "selftest passed\n");
    return fails ? 1 : 0;
}

void usage() {
    std::cout << "Kare " << KARE_VERSION << "\n\n"
              << "kare-cli plan \"prompt\" [-o file.kare] [--seconds N] [--aspect 16:9|9:16|1:1] [--style name] [--api] [--lang tr|en]\n"
              << "              [--shorts] [--web | --no-web] [--voice auto|none|tr|en|\"voice name\"]\n"
              << "kare-cli plan https://github.com/owner/repo   (inspects the repository)\n"
              << "kare-cli voices                                (installed narration voices)\n"
              << "kare-cli research \"topic\" [--lang tr|en]        (what the internet research finds)\n"
              << "kare-cli render \"prompt\" -o video.mp4 [same flags] [--project file.kare]\n"
              << "kare-cli frame \"prompt\" -o frame.png [--at seconds] [--project file.kare]\n"
              << "kare-cli audio \"prompt\" -o music.wav [--project file.kare]\n"
              << "kare-cli selftest\n";
}

struct Args {
    std::string prompt;
    std::string out;
    std::string project;
    std::string aspect;
    std::string style;
    std::string lang;
    std::string voice = "auto";
    double seconds = 0;
    double at = -1;
    bool api = false;
    bool shorts = false;
    int web = 0;
};

bool take(size_t& i, const std::vector<std::string>& argv, std::string& dest) {
    if (i + 1 >= argv.size()) return false;
    dest = argv[++i];
    return true;
}

Args parse_args(const std::vector<std::string>& argv) {
    Args a;
    for (size_t i = 2; i < argv.size(); i++) {
        std::string s = argv[i];
        if (s == "-o" || s == "--out") {
            if (!take(i, argv, a.out)) continue;
        } else if (s == "--seconds") {
            std::string v;
            if (take(i, argv, v)) a.seconds = std::atof(v.c_str());
        } else if (s == "--at") {
            std::string v;
            if (take(i, argv, v)) a.at = std::atof(v.c_str());
        } else if (s == "--aspect") {
            take(i, argv, a.aspect);
        } else if (s == "--style") {
            take(i, argv, a.style);
        } else if (s == "--project") {
            take(i, argv, a.project);
        } else if (s == "--api") {
            a.api = true;
        } else if (s == "--lang") {
            take(i, argv, a.lang);
        } else if (s == "--voice") {
            take(i, argv, a.voice);
        } else if (s == "--shorts") {
            a.shorts = true;
        } else if (s == "--web") {
            a.web = 1;
        } else if (s == "--no-web") {
            a.web = -1;
        } else {
            if (!a.prompt.empty()) a.prompt.push_back(' ');
            a.prompt += s;
        }
    }
    return a;
}

kare::PlanInput to_input(const Args& a) {
    kare::PlanInput in;
    in.prompt = a.prompt;
    in.seconds = a.seconds;
    in.aspect = a.aspect;
    in.style = a.style;
    kare::Config cfg = kare::load_config();
    in.lang = a.lang.empty() ? cfg.lang : a.lang;
    in.github_token = cfg.github_token;
    in.voice = a.voice;
    in.shorts = a.shorts;
    in.web = a.web;
    in.note = [](const std::string& m) { std::cerr << m << "\n"; };
    if (a.api) {
        in.use_api = true;
        in.api_base = cfg.api_base;
        in.api_key = cfg.api_key;
        in.api_model = cfg.api_model;
        in.mark = cfg.mark;
    }
    return in;
}

}  // namespace

std::vector<std::string> program_args(int argc, char** argv) {
#ifdef _WIN32
    (void)argc;
    (void)argv;
    int n = 0;
    LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &n);
    std::vector<std::string> args;
    if (!wargv) return args;
    for (int i = 0; i < n; i++) args.push_back(kare::narrow(wargv[i]));
    LocalFree(wargv);
    return args;
#else
    std::vector<std::string> args;
    for (int i = 0; i < argc; i++) args.push_back(argv[i] ? argv[i] : "");
    return args;
#endif
}

int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
    std::vector<std::string> args = program_args(argc, argv);
    if (args.size() < 2) {
        usage();
        return 1;
    }
    std::string cmd = args[1];
    if (cmd == "help" || cmd == "--help" || cmd == "-h") {
        usage();
        return 0;
    }
    if (cmd == "selftest") return selftest();
    if (cmd == "voices") {
        for (const auto& v : kare::list_voices()) std::cout << (v.lang.empty() ? "??" : v.lang) << "  " << v.name << "\n";
        return 0;
    }

    Args options = parse_args(args);
    if (cmd == "research") {
        std::string lang = options.lang.empty() ? "tr" : options.lang;
        auto r = kare::research_topic(kare::research_query(options.prompt), lang, lang, 16,
                                      [](const std::string& m) { std::cerr << m << "\n"; });
        if (!r.ok()) {
            std::cerr << r.error << "\n";
            return 1;
        }
        std::cout << r.title << "  " << r.url << "\n\n" << r.intro << "\n";
        for (const auto& s : r.sections) std::cout << "\n== " << s.heading << " (" << s.text.size() << " bytes)\n";
        std::cout << "\n";
        for (const auto& im : r.images) std::cout << im.w << "x" << im.h << "  " << im.file << "  " << im.caption << "\n";
        return 0;
    }
    if (cmd == "plan") {
        auto result = kare::plan(to_input(options));
        if (!result.warning.empty()) std::cerr << result.warning << "\n";
        if (options.out.empty()) {
            std::cout << kare::story_to_json(result.board) << "\n";
            return 0;
        }
        std::string err;
        if (!kare::save_story(options.out, result.board, err)) {
            std::cerr << err << "\n";
            return 1;
        }
        std::cout << options.out << "\n";
        return 0;
    }
    if (cmd == "render" || cmd == "frame" || cmd == "audio") {
        kare::Storyboard board;
        if (!options.project.empty()) {
            std::string err;
            if (!kare::load_story(options.project, board, err)) {
                std::cerr << err << "\n";
                return 1;
            }
        } else if (options.prompt.empty()) {
            usage();
            return 1;
        } else {
            auto result = kare::plan(to_input(options));
            if (!result.warning.empty()) std::cerr << result.warning << "\n";
            board = result.board;
        }
        if (options.out.empty()) options.out = cmd == "frame" ? "kare.png" : cmd == "audio" ? "kare.wav" : "kare.mp4";
        if (cmd == "audio") {
            auto track = kare::render_soundtrack(board);
            if (track.empty()) {
                std::cerr << "no music for this board\n";
                return 1;
            }
            auto wav = kare::wav_bytes(track, 0);
            if (!kare::write_bytes(options.out, wav.data(), wav.size())) {
                std::cerr << "could not write " << options.out << "\n";
                return 1;
            }
            std::cout << options.out << "\n";
            return 0;
        }
        if (cmd == "frame") {
            std::string err;
            double t = options.at >= 0 ? options.at : kare::total_seconds(board) * 0.4;
            if (!kare::write_preview_png(options.out, board, t, err)) {
                std::cerr << err << "\n";
                return 1;
            }
            std::cout << options.out << "\n";
            return 0;
        }
        int last = -1;
        std::string err = kare::encode_video(board, options.out, [&](float p) {
            int pct = static_cast<int>(p * 100);
            if (pct / 5 != last) {
                last = pct / 5;
                std::cerr << "\r" << pct << "%";
            }
            return true;
        });
        std::cerr << "\n";
        if (!err.empty()) {
            std::cerr << err << "\n";
            return 1;
        }
        std::cout << options.out << "\n";
        return 0;
    }
    usage();
    return 1;
}
