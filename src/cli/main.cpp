#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

#include "core/config.h"
#include "core/director.h"
#include "core/encode.h"
#include "core/font.h"
#include "core/render.h"
#include "core/story.h"
#include "core/utf8.h"

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
    expect(kare::probe_video(mp4, probe_err), "read mp4 back");
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

#ifdef _WIN32
    _wremove(kare::widen(mp4).c_str());
    _wremove(kare::widen(avi).c_str());
#endif
    std::cout << (fails ? "selftest failed\n" : "selftest passed\n");
    return fails ? 1 : 0;
}

void usage() {
    std::cout << "Kare " << KARE_VERSION << "\n\n"
              << "kare-cli plan \"prompt\" [--seconds N] [--aspect 16:9|9:16|1:1] [--style name] [--api]\n"
              << "kare-cli render \"prompt\" -o video.mp4 [same flags] [--project file.kare]\n"
              << "kare-cli frame \"prompt\" -o frame.png\n"
              << "kare-cli selftest\n";
}

struct Args {
    std::string prompt;
    std::string out;
    std::string project;
    std::string aspect;
    std::string style;
    double seconds = 0;
    bool api = false;
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
        } else if (s == "--aspect") {
            take(i, argv, a.aspect);
        } else if (s == "--style") {
            take(i, argv, a.style);
        } else if (s == "--project") {
            take(i, argv, a.project);
        } else if (s == "--api") {
            a.api = true;
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
    if (a.api) {
        kare::Config cfg = kare::load_config();
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

    Args options = parse_args(args);
    if (cmd == "plan") {
        auto result = kare::plan(to_input(options));
        if (!result.warning.empty()) std::cerr << result.warning << "\n";
        std::cout << kare::story_to_json(result.board) << "\n";
        return 0;
    }
    if (cmd == "render" || cmd == "frame") {
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
        if (options.out.empty()) options.out = cmd == "frame" ? "kare.png" : "kare.mp4";
        if (cmd == "frame") {
            std::string err;
            double t = kare::total_seconds(board) * 0.4;
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
