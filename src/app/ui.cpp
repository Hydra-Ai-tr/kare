#include "app/ui.h"

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include "imgui_internal.h"
#include "misc/cpp/imgui_stdlib.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <algorithm>
#include <atomic>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "core/audio.h"
#include "core/brain.h"
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

#include <GLFW/glfw3.h>
#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <windowsx.h>
#include <mmsystem.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <shellapi.h>
#include <GL/gl.h>
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif
#ifndef DWMWA_CAPTION_COLOR
#define DWMWA_CAPTION_COLOR 35
#endif
#ifndef DWMWA_TEXT_COLOR
#define DWMWA_TEXT_COLOR 36
#endif
#else
#include <GL/gl.h>
#endif

namespace kare {

namespace {

float g_s = 1.f;
float P(float v) { return v * g_s; }

struct Fonts {
    ImFont* ui = nullptr;
    ImFont* bold = nullptr;
    ImFont* small = nullptr;
    ImFont* tiny = nullptr;
    ImFont* h1 = nullptr;
    ImFont* title = nullptr;
    ImFont* icon = nullptr;
};
Fonts F;
ImDrawList* g_root = nullptr;

constexpr ImU32 kBg = IM_COL32(5, 9, 13, 255);
constexpr ImU32 kPanel = IM_COL32(9, 15, 21, 255);
constexpr ImU32 kPanel2 = IM_COL32(13, 21, 29, 255);
constexpr ImU32 kRaised = IM_COL32(20, 31, 42, 255);
constexpr ImU32 kLine = IM_COL32(27, 41, 55, 255);
constexpr ImU32 kText = IM_COL32(228, 239, 246, 255);
constexpr ImU32 kSoft = IM_COL32(184, 200, 212, 255);
constexpr ImU32 kMuted = IM_COL32(124, 144, 161, 255);
constexpr ImU32 kDim = IM_COL32(78, 97, 113, 255);
constexpr ImU32 kAccent = IM_COL32(40, 226, 232, 255);
constexpr ImU32 kInk = IM_COL32(3, 20, 24, 255);
constexpr ImU32 kRed = IM_COL32(255, 84, 84, 255);
constexpr ImU32 kWarm = IM_COL32(255, 190, 92, 255);

ImU32 with_alpha(ImU32 c, int a) {
    a = std::clamp(a, 0, 255);
    return (c & ~IM_COL32_A_MASK) | (static_cast<ImU32>(a) << IM_COL32_A_SHIFT);
}
ImVec4 v4(ImU32 c) { return ImGui::ColorConvertU32ToFloat4(c); }

namespace ic {
constexpr uint32_t house = 0xe0f8, sparkles = 0xe417, film = 0xe0d4, clapper = 0xe29b, grid = 0xe105, folder = 0xe0dc,
                   box = 0xe065, settings = 0xe158, plus = 0xe141, play = 0xe140, pause = 0xe132, skip_back = 0xe163,
                   skip_fwd = 0xe164, left = 0xe072, right = 0xe073, down = 0xe071, maximize = 0xe116,
                   minimize = 0xe11e, camera = 0xe068, scissors = 0xe152, copy = 0xe0a2, trash = 0xe18e,
                   arrow_left = 0xe04c, arrow_right = 0xe04d, type = 0xe198, video = 0xe1a5, undo = 0xe2a1,
                   redo = 0xe2a0, upload = 0xe19e, pencil = 0xe1f9, github = 0xe0e9, cpu = 0xe0ad, memory = 0xe44a,
                   disk = 0xe0f0, monitor = 0xe121, list = 0xe10c, save = 0xe151, folder_open = 0xe247,
                   globe = 0xe0eb, key = 0xe4a8, star = 0xe17a, check = 0xe070, x = 0xe1b2, repeat = 0xe14a,
                   captions = 0xe3a8, wand = 0xe35b, layers = 0xe104, file_video = 0xe32c, brain = 0xe3ca,
                   external = 0xe0bd, languages = 0xe103, palette = 0xe1dd, zoom_in = 0xe1b6, zoom_out = 0xe1b7,
                   info = 0xe0fe, sliders = 0xe29a, file_text = 0xe0d0, shield = 0xe1ff, activity = 0xe038,
                   bot = 0xe1bb, zap = 0xe1b4, scan = 0xe257, timer = 0xe1e0, monitor_play = 0xe48a, music = 0xe126,
                   volume = 0xe1ab, mute = 0xe1ac, image = 0xe0f9, image_plus = 0xe1f7, image_off = 0xe1c0,
                   layout = 0xe207, blend = 0xe5a1, cursor = 0xe264, paste = 0xe3ec, clipboard = 0xe089,
                   eraser = 0xe28f, restart = 0xe14c, list_x = 0xe240, insert = 0xe597, wave = 0xe560,
                   file_audio = 0xe30e, link = 0xe108, move = 0xe1c6, square_plus = 0xe177;
}  // namespace ic

const char* I(uint32_t cp) {
    static std::unordered_map<uint32_t, std::string> cache;
    auto it = cache.find(cp);
    if (it == cache.end()) it = cache.emplace(cp, cps_utf8(std::vector<uint32_t>{cp})).first;
    return it->second.c_str();
}

std::string IL(uint32_t icon, const char* label) { return std::string(I(icon)) + "  " + label; }

const char* tr(const Config& cfg, const char* turkish, const char* english) {
    return cfg.lang == "en" ? english : turkish;
}

enum Page { kHome, kCreate, kEdit, kScenes, kAssets, kModels, kSettings };

struct Opt {
    const char* id;
    const char* tr;
    const char* en;
};

const Opt kStyles[] = {
    {"", "Otomatik", "Auto"},     {"ember", "Sinema", "Cinema"}, {"dusk", "Gün batımı", "Dusk"},
    {"night", "Gece", "Night"},   {"sea", "Deniz", "Sea"},       {"forest", "Orman", "Forest"},
    {"neon", "Neon", "Neon"},     {"paper", "Kağıt", "Paper"},   {"minimal", "Sade", "Minimal"},
};
const char* kAspectIds[] = {"", "16:9", "9:16", "1:1"};
const int kSecondsOpt[] = {0, 5, 8, 10, 15, 20, 30, 45, 60};
const int kFpsOpt[] = {12, 24, 30};
const Opt kLayoutOpts[] = {{"auto", "Otomatik", "Auto"},       {"hero", "Kapak", "Hero"},
                           {"statement", "Açıklama", "Statement"}, {"list", "Liste", "List"},
                           {"stat", "Sayı", "Stat"},           {"quote", "Alıntı", "Quote"},
                           {"split", "İki sütun", "Split"},    {"terminal", "Terminal", "Terminal"},
                           {"bars", "Çubuk grafik", "Bar chart"}, {"media", "Görsel", "Media"},
                           {"showcase", "Vitrin", "Showcase"}, {"story", "Belgesel", "Documentary"},
                           {"end", "Kapanış", "End card"}};
const Opt kCaptionOpts[] = {{"auto", "Otomatik", "Auto"}, {"words", "Kelime kelime", "Word by word"},
                            {"line", "Alt yazı", "Subtitle line"}, {"none", "Kapalı", "Off"}};
const Opt kTransitionOpts[] = {{"auto", "Otomatik", "Auto"}, {"fade", "Çapraz geçiş", "Crossfade"}, {"slide", "Kaydırma", "Slide"},
                               {"wipe", "Silme", "Wipe"},    {"zoom", "Yakınlaşma", "Zoom"},       {"flash", "Flaş", "Flash"},
                               {"glitch", "Glitch", "Glitch"}, {"cut", "Kesme", "Cut"}};
const Opt kAnimOpts[] = {{"auto", "Otomatik", "Auto"}, {"rise", "Yükselme", "Rise"},    {"fade", "Belirme", "Fade"},
                         {"type", "Daktilo", "Typewriter"}, {"reveal", "Perde", "Reveal"}, {"spring", "Yaylanma", "Spring"}};
const Opt kMusicOpts[] = {{"auto", "Otomatik", "Auto"}, {"ambient", "Ambiyans", "Ambient"}, {"pulse", "Nabız", "Pulse"},
                          {"drive", "Hız", "Drive"},    {"lofi", "Lo-fi", "Lo-fi"},         {"none", "Sessiz", "Silent"}};
const Opt kMotionOpts[] = {{"drift", "Kaydır", "Drift"}, {"zoom", "Yakınlaş", "Zoom"}, {"rise", "Yüksel", "Rise"},
                           {"pan", "Pan", "Pan"},        {"still", "Sabit", "Still"}};

template <size_t N>
int opt_index(const Opt (&opts)[N], const std::string& id) {
    for (size_t i = 0; i < N; i++)
        if (id == opts[i].id) return static_cast<int>(i);
    return 0;
}

template <size_t N>
std::string opt_label(const Config& cfg, const Opt (&opts)[N], const std::string& id) {
    const Opt& o = opts[opt_index(opts, id)];
    return cfg.lang == "en" ? o.en : o.tr;
}

struct Recent {
    std::string path;
    std::string name;
    std::string meta;
    bool exists = false;
};

struct App {
    Config cfg;
    int page = kCreate;
    std::string prompt;
    int style_i = 0;
    int aspect_i = 0;
    int res_i = 0;
    int fps_i = 1;
    int seconds_i = 0;
    bool advanced = true;
    bool plan_api = false;
    bool shorts = false;
    bool web = false;
    int voice_i = 0;  // auto, off, then installed voices
    std::vector<VoiceInfo> voices;
    std::string plan_note;

    Storyboard board;
    bool has_board = false;
    int selected = 0;
    std::string project_path;
    bool editing_title = false;
    bool focus_title = false;

    float play_t = 0;
    bool playing = false;
    double play_wall = 0;
    float play_from = 0;
    bool loop = false;
    bool focus = false;
    bool mute = false;
    bool shown_low = false;

    std::thread audio_thread;
    std::atomic<bool> audio_busy{false};
    int audio_job_serial = -1;
    bool track_ready = false;
    int pending_serial = -1;
    std::vector<int16_t> track_pending;
    std::vector<int16_t> track;
    int track_serial = -1;
    std::vector<float> peaks;
    std::vector<uint8_t> wav_play[2];
    int wav_slot = 0;
    bool audio_on = false;
    double audio_wall = 0;
    float audio_t0 = 0;

    int ctx_scene = -1;
    std::string ctx_path;
    bool ctx_project = false;

    std::string status;
    std::atomic<bool> exporting{false};
    std::atomic<float> progress{0};
    std::atomic<bool> cancel{false};
    std::atomic<bool> planning{false};
    std::atomic<bool> testing{false};
    std::mutex mu;
    PlanResult pending;
    bool plan_ready = false;
    std::string export_error;
    std::string export_target;
    bool export_done = false;
    std::string test_text;
    bool test_ok = false;
    bool test_done = false;
    std::string test_view;
    bool test_view_ok = false;

    unsigned tex = 0;
    int tex_w = 0;
    int tex_h = 0;
    int shown_frame = -1;
    int shown_serial = -1;
    std::vector<unsigned> thumbs;
    int thumb_serial = -1;
    int thumb_w = 16;
    int thumb_h = 9;

    int serial = 1;
    double changed_at = 0;
    std::vector<Storyboard> undo;
    std::vector<Storyboard> redo;
    Storyboard committed;
    int committed_serial = 1;

    std::thread worker;
    std::thread plan_thread;
    std::thread test_thread;
    GLFWwindow* window = nullptr;
    float fit_seconds = 10;
    float tl_zoom = 1.f;
    float frozen_pps = 0.f;
    std::string last_repo;
    std::string last_repo_note;
    std::string gpu_name;
    std::vector<Recent> recent;
    bool recent_dirty = true;
};

// ---------------------------------------------------------------- system meters

struct SysStats {
    std::atomic<float> cpu{-1.f};
    std::atomic<float> gpu{-1.f};
    std::atomic<float> ram_used{0.f};
    std::atomic<float> ram_total{0.f};
    std::atomic<float> disk_used{0.f};
    std::atomic<float> disk_total{0.f};
    std::atomic<bool> stop{false};
    std::thread thread;
};
SysStats g_sys;

// ---------------------------------------------------------------- window chrome

struct Chrome {
    bool on = false;
    int caption = 56;
    std::vector<ImVec4> hot;
#ifdef _WIN32
    HWND hwnd = nullptr;
    WNDPROC prev = nullptr;
#endif
};
Chrome g_chrome;

void hot_item() {
    ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    g_chrome.hot.push_back(ImVec4(a.x, a.y, b.x, b.y));
}

#ifdef _WIN32
int frame_pad() { return GetSystemMetrics(SM_CYFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER); }

LRESULT CALLBACK chrome_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_NCCALCSIZE && w) {
        auto* p = reinterpret_cast<NCCALCSIZE_PARAMS*>(l);
        LONG top = p->rgrc[0].top;
        LRESULT r = CallWindowProcW(g_chrome.prev, h, m, w, l);
        p->rgrc[0].top = top + (IsZoomed(h) ? frame_pad() : 0);
        return r;
    }
    if (m == WM_NCHITTEST) {
        LRESULT hit = CallWindowProcW(g_chrome.prev, h, m, w, l);
        if (hit != HTCLIENT) return hit;
        POINT pt{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
        ScreenToClient(h, &pt);
        RECT rc;
        GetClientRect(h, &rc);
        if (!IsZoomed(h)) {
            int border = frame_pad();
            if (pt.y < border) {
                if (pt.x < border * 2) return HTTOPLEFT;
                if (pt.x >= rc.right - border * 2) return HTTOPRIGHT;
                return HTTOP;
            }
        }
        if (pt.y < g_chrome.caption) {
            for (const auto& r : g_chrome.hot)
                if (pt.x >= r.x && pt.x < r.z && pt.y >= r.y && pt.y < r.w) return HTCLIENT;
            return HTCAPTION;
        }
        return HTCLIENT;
    }
    return CallWindowProcW(g_chrome.prev, h, m, w, l);
}

void install_chrome(GLFWwindow* window) {
    HWND hwnd = glfwGetWin32Window(window);
    if (!hwnd) return;
    g_chrome.hwnd = hwnd;
    g_chrome.prev = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(chrome_proc)));
    if (!g_chrome.prev) return;
    g_chrome.on = true;
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}
#endif

#ifdef _WIN32
unsigned long long ft64(const FILETIME& f) {
    return (static_cast<unsigned long long>(f.dwHighDateTime) << 32) | f.dwLowDateTime;
}

void sample_system(std::wstring disk_root) {
    FILETIME pi{}, pk{}, pu{};
    GetSystemTimes(&pi, &pk, &pu);
    PDH_HQUERY query = nullptr;
    PDH_HCOUNTER counter = nullptr;
    bool gpu_ok = PdhOpenQueryW(nullptr, 0, &query) == ERROR_SUCCESS &&
                  PdhAddEnglishCounterW(query, L"\\GPU Engine(*)\\Utilization Percentage", 0, &counter) == ERROR_SUCCESS;
    if (gpu_ok) PdhCollectQueryData(query);
    while (!g_sys.stop) {
        for (int i = 0; i < 10 && !g_sys.stop; i++) Sleep(100);
        if (g_sys.stop) break;
        FILETIME ci{}, ck{}, cu{};
        if (GetSystemTimes(&ci, &ck, &cu)) {
            unsigned long long idle = ft64(ci) - ft64(pi);
            unsigned long long total = (ft64(ck) - ft64(pk)) + (ft64(cu) - ft64(pu));
            if (total) g_sys.cpu = static_cast<float>(100.0 * static_cast<double>(total - idle) / static_cast<double>(total));
            pi = ci;
            pk = ck;
            pu = cu;
        }
        MEMORYSTATUSEX mem{};
        mem.dwLength = sizeof mem;
        if (GlobalMemoryStatusEx(&mem)) {
            const double gb = 1024.0 * 1024.0 * 1024.0;
            g_sys.ram_total = static_cast<float>(mem.ullTotalPhys / gb);
            g_sys.ram_used = static_cast<float>((mem.ullTotalPhys - mem.ullAvailPhys) / gb);
        }
        ULARGE_INTEGER free_bytes{}, total_bytes{}, total_free{};
        if (GetDiskFreeSpaceExW(disk_root.c_str(), &free_bytes, &total_bytes, &total_free)) {
            const double gb = 1024.0 * 1024.0 * 1024.0;
            g_sys.disk_total = static_cast<float>(total_bytes.QuadPart / gb);
            g_sys.disk_used = static_cast<float>((total_bytes.QuadPart - total_free.QuadPart) / gb);
        }
        if (gpu_ok && PdhCollectQueryData(query) == ERROR_SUCCESS) {
            DWORD size = 0, count = 0;
            if (PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &size, &count, nullptr) == static_cast<PDH_STATUS>(PDH_MORE_DATA)) {
                std::vector<unsigned char> buf(size);
                auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buf.data());
                if (PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &size, &count, items) == ERROR_SUCCESS) {
                    double sum = 0;
                    for (DWORD i = 0; i < count; i++)
                        if (items[i].szName && wcsstr(items[i].szName, L"engtype_3D") && items[i].FmtValue.CStatus == ERROR_SUCCESS)
                            sum += items[i].FmtValue.doubleValue;
                    g_sys.gpu = static_cast<float>(std::min(100.0, sum));
                }
            }
        }
    }
    if (query) PdhCloseQuery(query);
}
#endif

// ---------------------------------------------------------------- text and shapes

ImVec2 text_size(ImFont* f, const char* s) { return f->CalcTextSizeA(f->FontSize, FLT_MAX, 0.f, s); }

void draw_text(ImDrawList* dl, ImFont* f, ImVec2 p, ImU32 c, const char* s) { dl->AddText(f, f->FontSize, p, c, s); }

std::string fit(ImFont* f, const std::string& text, float max_w) {
    if (text.empty() || text_size(f, text.c_str()).x <= max_w) return text;
    auto cps = utf8_cps(text);
    for (size_t n = cps.size(); n > 0; --n) {
        std::string cut = cps_utf8(std::vector<uint32_t>(cps.begin(), cps.begin() + static_cast<std::ptrdiff_t>(n))) + "\xE2\x80\xA6";
        if (text_size(f, cut.c_str()).x <= max_w) return cut;
    }
    return {};
}

std::string clock_text(double seconds) {
    if (seconds < 0) seconds = 0;
    int s = static_cast<int>(seconds + 0.5);
    char buf[16];
    std::snprintf(buf, sizeof buf, "%d:%02d", s / 60, s % 60);
    return buf;
}

std::string timecode(double t, int fps) {
    if (t < 0) t = 0;
    if (fps <= 0) fps = 24;
    int frames = static_cast<int>(t * fps + 0.5);
    int s = frames / fps;
    char buf[32];
    std::snprintf(buf, sizeof buf, "%02d:%02d:%02d:%02d", s / 3600, (s / 60) % 60, s % 60, frames % fps);
    return buf;
}

std::string seconds_text(double s, bool turkish) {
    char buf[32];
    std::snprintf(buf, sizeof buf, turkish ? "%.1f sn" : "%.1f s", s);
    return buf;
}

void glow(ImDrawList* dl, ImVec2 a, ImVec2 b, float r, ImU32 c, float strength = 1.f) {
    for (int i = 1; i <= 4; i++) {
        float e = P(1.7f) * static_cast<float>(i);
        dl->AddRect(a - ImVec2(e, e), b + ImVec2(e, e), with_alpha(c, static_cast<int>(30.f * strength / static_cast<float>(i))), r + e, 0, P(2.f));
    }
}

void panel_bg(ImVec2 a, ImVec2 b) {
    g_root->AddRectFilled(a, b, kPanel, P(10));
    g_root->AddRect(a, b, kLine, P(10), 0, 1.f);
}

void spinner(ImDrawList* dl, ImVec2 c, float r, ImU32 col) {
    float t = static_cast<float>(ImGui::GetTime());
    dl->PathArcTo(c, r, t * 6.f, t * 6.f + 4.2f, 24);
    dl->PathStroke(col, 0, P(2.f));
}

void draw_logo(ImDrawList* dl, ImVec2 p, float s) {
    ImVec2 q(p.x + s, p.y + s);
    dl->AddRect(p, q, with_alpha(kAccent, 60), P(4), 0, P(4));
    dl->AddRect(p, q, kAccent, P(4), 0, P(1.8f));
    dl->AddLine(ImVec2(p.x + s * 0.24f, p.y + s * 0.76f), ImVec2(p.x + s * 0.76f, p.y + s * 0.24f), kAccent, P(1.8f));
    dl->AddLine(ImVec2(p.x + s * 0.24f, p.y + s * 0.24f), ImVec2(p.x + s * 0.62f, p.y + s * 0.24f), kAccent, P(1.8f));
    dl->AddLine(ImVec2(p.x + s * 0.38f, p.y + s * 0.76f), ImVec2(p.x + s * 0.76f, p.y + s * 0.76f), kAccent, P(1.8f));
}

float spaced_text(ImDrawList* dl, ImFont* f, ImVec2 p, ImU32 c, const char* s, float spacing) {
    float x = p.x;
    for (const char* ch = s; *ch; ch++) {
        char one[2] = {*ch, 0};
        draw_text(dl, f, ImVec2(x, p.y), c, one);
        x += text_size(f, one).x + spacing;
    }
    return x - p.x;
}

// ---------------------------------------------------------------- widgets

bool icon_button(const char* id, uint32_t icon, ImVec2 size, bool active = false, bool enabled = true,
                 const char* tip = nullptr) {
    ImGui::PushID(id);
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool pressed = ImGui::InvisibleButton("ib", size);
    bool hot = ImGui::IsItemHovered();
    if (tip && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("%s", tip);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (active) {
        dl->AddRectFilled(p, p + size, with_alpha(kAccent, 30), P(7));
        dl->AddRect(p, p + size, with_alpha(kAccent, 160), P(7), 0, 1.f);
    } else if (hot && enabled) {
        dl->AddRectFilled(p, p + size, kRaised, P(7));
    }
    const char* s = I(icon);
    ImVec2 ts = text_size(F.ui, s);
    ImU32 c = !enabled ? kDim : active ? kAccent : hot ? kText : kSoft;
    draw_text(dl, F.ui, p + (size - ts) * 0.5f, c, s);
    ImGui::PopID();
    return pressed && enabled;
}

enum ButtonKind { kGhost, kOutline, kPrimary, kGlow };

float button_width(uint32_t icon, const char* label, ButtonKind kind) {
    std::string text = icon ? IL(icon, label) : std::string(label);
    ImFont* f = (kind == kPrimary || kind == kGlow) ? F.bold : F.ui;
    return text_size(f, text.c_str()).x + P(30);
}

bool button(const char* id, uint32_t icon, const char* label, ImVec2 size, ButtonKind kind, bool enabled = true,
            bool busy = false) {
    ImGui::PushID(id);
    std::string text = icon && !busy ? IL(icon, label) : std::string(label);
    ImFont* f = (kind == kPrimary || kind == kGlow) ? F.bold : F.ui;
    ImVec2 ts = text_size(f, text.c_str());
    if (size.x == 0) size.x = ts.x + P(30);
    if (size.x < 0) size.x = ImGui::GetContentRegionAvail().x;
    if (size.y <= 0) size.y = P(36);
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool pressed = ImGui::InvisibleButton("b", size);
    bool hot = enabled && ImGui::IsItemHovered();
    bool held = enabled && ImGui::IsItemActive();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float r = P(8);
    ImU32 fg = kText;
    if (kind == kPrimary) {
        ImU32 bg = !enabled ? kRaised : held ? IM_COL32(26, 190, 198, 255) : hot ? IM_COL32(110, 240, 244, 255) : kAccent;
        if (enabled) glow(dl, p, p + size, r, kAccent, hot ? 1.2f : 0.8f);
        dl->AddRectFilled(p, p + size, bg, r);
        fg = enabled ? kInk : kDim;
    } else if (kind == kGlow) {
        if (enabled) glow(dl, p, p + size, r, kAccent, hot ? 1.6f : 1.f);
        dl->AddRectFilled(p, p + size, IM_COL32(7, 30, 36, 255), r);
        dl->AddRectFilled(p, p + size, with_alpha(kAccent, held ? 70 : hot ? 46 : 26), r);
        dl->AddRect(p, p + size, enabled ? kAccent : kLine, r, 0, P(1.4f));
        fg = enabled ? kText : kDim;
    } else if (kind == kOutline) {
        dl->AddRectFilled(p, p + size, hot ? kRaised : kPanel2, r);
        dl->AddRect(p, p + size, hot ? with_alpha(kAccent, 140) : kLine, r, 0, 1.f);
        fg = enabled ? kText : kDim;
    } else {
        if (hot) dl->AddRectFilled(p, p + size, kRaised, r);
        fg = enabled ? kSoft : kDim;
    }
    float x = p.x + (size.x - ts.x) * 0.5f;
    if (busy) {
        x += P(12);
        spinner(dl, ImVec2(x - P(16), p.y + size.y * 0.5f), P(7), fg);
    }
    if (icon && !busy && kind == kGlow) {
        const char* is = I(icon);
        draw_text(dl, f, ImVec2(x, p.y + (size.y - ts.y) * 0.5f), kAccent, is);
        std::string rest = std::string("  ") + label;
        draw_text(dl, f, ImVec2(x + text_size(f, is).x, p.y + (size.y - ts.y) * 0.5f), fg, rest.c_str());
    } else {
        draw_text(dl, f, ImVec2(x, p.y + (size.y - ts.y) * 0.5f), fg, text.c_str());
    }
    ImGui::PopID();
    return pressed && enabled && !busy;
}

bool chip(const char* id, const char* label, bool on, uint32_t icon = 0) {
    ImGui::PushID(id);
    std::string text = icon ? std::string(I(icon)) + " " + label : std::string(label);
    ImVec2 ts = text_size(F.small, text.c_str());
    ImVec2 size(ts.x + P(20), ts.y + P(12));
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool pressed = ImGui::InvisibleButton("c", size);
    bool hot = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (on) {
        dl->AddRectFilled(p, p + size, with_alpha(kAccent, 30), P(7));
        dl->AddRect(p, p + size, kAccent, P(7), 0, P(1.2f));
    } else {
        dl->AddRectFilled(p, p + size, hot ? kRaised : kPanel2, P(7));
        dl->AddRect(p, p + size, hot ? with_alpha(kAccent, 90) : kLine, P(7), 0, 1.f);
    }
    draw_text(dl, F.small, p + ImVec2(P(10), P(6)), on ? kText : hot ? kText : kSoft, text.c_str());
    ImGui::PopID();
    return pressed;
}

bool wrap_chip(const char* id, const char* label, bool on, float& used, float width, uint32_t icon = 0) {
    std::string text = icon ? std::string(I(icon)) + " " + label : std::string(label);
    float need = text_size(F.small, text.c_str()).x + P(20);
    if (used > 1.f && used + need > width) used = 0.f;
    else if (used > 1.f) ImGui::SameLine(0.f, P(6));
    bool pressed = chip(id, label, on, icon);
    used += need + P(6);
    return pressed;
}

bool nav_item(const char* id, uint32_t icon, const char* label, bool on, float width) {
    ImGui::PushID(id);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImVec2 size(width, P(38));
    bool pressed = ImGui::InvisibleButton("n", size);
    bool hot = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (on) {
        glow(dl, p, p + size, P(8), kAccent, 0.7f);
        dl->AddRectFilled(p, p + size, with_alpha(kAccent, 24), P(8));
        dl->AddRect(p, p + size, with_alpha(kAccent, 200), P(8), 0, P(1.2f));
    } else if (hot) {
        dl->AddRectFilled(p, p + size, kRaised, P(8));
    }
    float cy = p.y + size.y * 0.5f;
    const char* is = I(icon);
    draw_text(dl, F.ui, ImVec2(p.x + P(12), cy - text_size(F.ui, is).y * 0.5f), on ? kAccent : hot ? kText : kMuted, is);
    std::string shown = fit(F.ui, label, width - P(48));
    draw_text(dl, F.ui, ImVec2(p.x + P(40), cy - text_size(F.ui, shown.c_str()).y * 0.5f), on || hot ? kText : kSoft,
              shown.c_str());
    ImGui::PopID();
    return pressed;
}

bool top_tab(const char* id, uint32_t icon, const char* label, bool on, float height) {
    ImGui::PushID(id);
    std::string text = IL(icon, label);
    ImVec2 ts = text_size(F.ui, text.c_str());
    ImVec2 size(ts.x + P(30), height);
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool pressed = ImGui::InvisibleButton("t", size);
    bool hot = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (on) {
        glow(dl, p, p + size, P(8), kAccent, 0.6f);
        dl->AddRectFilled(p, p + size, with_alpha(kAccent, 22), P(8));
        dl->AddRect(p, p + size, with_alpha(kAccent, 170), P(8), 0, 1.f);
    } else if (hot) {
        dl->AddRectFilled(p, p + size, kPanel2, P(8));
    }
    const char* is = I(icon);
    float y = p.y + (size.y - ts.y) * 0.5f;
    draw_text(dl, F.ui, ImVec2(p.x + P(15), y), on ? kAccent : hot ? kText : kMuted, is);
    std::string rest = std::string("  ") + label;
    draw_text(dl, F.ui, ImVec2(p.x + P(15) + text_size(F.ui, is).x, y), on || hot ? kText : kSoft, rest.c_str());
    ImGui::PopID();
    return pressed;
}

bool panel_tab(const char* id, uint32_t icon, const char* label, bool on, float width) {
    ImGui::PushID(id);
    ImVec2 size(width, P(40));
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool pressed = ImGui::InvisibleButton("pt", size);
    bool hot = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (on) {
        glow(dl, p, p + size, P(8), kAccent, 0.9f);
        dl->AddRectFilled(p, p + size, with_alpha(kAccent, 22), P(8));
        dl->AddRect(p, p + size, kAccent, P(8), 0, P(1.2f));
    } else if (hot) {
        dl->AddRectFilled(p, p + size, kPanel2, P(8));
    }
    ImFont* f = on ? F.bold : F.ui;
    const char* is = I(icon);
    std::string rest = fit(f, std::string("  ") + label, width - P(20) - text_size(f, is).x);
    float tw = text_size(f, is).x + text_size(f, rest.c_str()).x;
    float x = p.x + (size.x - tw) * 0.5f;
    float y = p.y + (size.y - text_size(f, is).y) * 0.5f;
    draw_text(dl, f, ImVec2(x, y), on ? kAccent : kMuted, is);
    draw_text(dl, f, ImVec2(x + text_size(f, is).x, y), on ? kText : hot ? kText : kMuted, rest.c_str());
    ImGui::PopID();
    return pressed;
}

void push_field() {
    ImGui::PushStyleColor(ImGuiCol_FrameBg, v4(IM_COL32(6, 11, 16, 255)));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, v4(IM_COL32(9, 15, 21, 255)));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, v4(IM_COL32(9, 15, 21, 255)));
    ImGui::PushStyleColor(ImGuiCol_Border, v4(kLine));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, P(8));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(P(10), P(8)));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f);
}

void pop_field() {
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(4);
}

bool combo(const char* id, int& cur, const std::vector<std::string>& items, float width) {
    if (items.empty()) return false;
    cur = std::clamp(cur, 0, static_cast<int>(items.size()) - 1);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, v4(kPanel2));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, v4(kRaised));
    ImGui::PushStyleColor(ImGuiCol_Button, v4(kPanel2));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, v4(kRaised));
    ImGui::PushStyleColor(ImGuiCol_Border, v4(kLine));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, v4(kPanel2));
    ImGui::PushStyleColor(ImGuiCol_Header, v4(with_alpha(kAccent, 40)));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, v4(with_alpha(kAccent, 60)));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, P(6));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(P(10), P(6)));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, P(8));
    ImGui::SetNextItemWidth(width);
    bool changed = false;
    ImDrawList* host = ImGui::GetWindowDrawList();
    ImVec2 at = ImGui::GetCursorScreenPos();
    float fh = ImGui::GetFrameHeight();
    bool open = ImGui::BeginCombo(id, items[static_cast<size_t>(cur)].c_str(), ImGuiComboFlags_NoArrowButton);
    const char* chev = I(ic::down);
    ImVec2 cs = text_size(F.small, chev);
    draw_text(host, F.small, ImVec2(at.x + width - P(10) - cs.x, at.y + (fh - cs.y) * 0.5f), kMuted, chev);
    if (open) {
        for (int i = 0; i < static_cast<int>(items.size()); i++) {
            bool sel = i == cur;
            if (ImGui::Selectable(items[static_cast<size_t>(i)].c_str(), sel)) {
                cur = i;
                changed = true;
            }
            if (sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor(8);
    return changed;
}

void row_label(const char* label, float total_w, float control_w) {
    float x0 = ImGui::GetCursorPosX();
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(P(10), P(6)));
    ImGui::AlignTextToFramePadding();
    ImGui::PopStyleVar();
    ImGui::PushStyleColor(ImGuiCol_Text, v4(kSoft));
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    ImGui::SameLine(x0 + total_w - control_w);
}

bool toggle(const char* id, const char* label, bool on, float width) {
    ImGui::PushID(id);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImVec2 size(width, P(30));
    bool pressed = ImGui::InvisibleButton("tg", size);
    bool hot = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float cy = p.y + size.y * 0.5f;
    draw_text(dl, F.ui, ImVec2(p.x, cy - text_size(F.ui, label).y * 0.5f), hot ? kText : kSoft, label);
    ImVec2 ta(p.x + width - P(38), cy - P(10)), tb(p.x + width, cy + P(10));
    dl->AddRectFilled(ta, tb, on ? with_alpha(kAccent, 210) : kRaised, P(10));
    if (!on) dl->AddRect(ta, tb, kLine, P(10));
    float kx = on ? tb.x - P(10) : ta.x + P(10);
    dl->AddCircleFilled(ImVec2(kx, cy), P(7), on ? kInk : kSoft, 20);
    ImGui::PopID();
    return pressed;
}

void section(const char* label) {
    ImGui::PushFont(F.bold);
    ImGui::PushStyleColor(ImGuiCol_Text, v4(kText));
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0.f, P(2)));
}

void caption(const char* text, float wrap = 0.f, ImU32 col = kMuted) {
    ImGui::PushFont(F.small);
    ImGui::PushStyleColor(ImGuiCol_Text, v4(col));
    if (wrap > 0.f) ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrap);
    ImGui::TextUnformatted(text);
    if (wrap > 0.f) ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

void divider(float width) {
    ImGui::Dummy(ImVec2(0.f, P(6)));
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddLine(p, ImVec2(p.x + width, p.y), kLine, 1.f);
    ImGui::Dummy(ImVec2(width, P(8)));
}

// ---------------------------------------------------------------- context menus

void push_menu_style() {
    ImGui::PushStyleColor(ImGuiCol_PopupBg, v4(IM_COL32(11, 18, 25, 250)));
    ImGui::PushStyleColor(ImGuiCol_Border, v4(IM_COL32(36, 54, 70, 255)));
    ImGui::PushStyleColor(ImGuiCol_Header, v4(with_alpha(kAccent, 34)));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, v4(with_alpha(kAccent, 44)));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, v4(with_alpha(kAccent, 70)));
    ImGui::PushStyleColor(ImGuiCol_Separator, v4(kLine));
    ImGui::PushStyleColor(ImGuiCol_TextDisabled, v4(kDim));
    ImGui::PushStyleColor(ImGuiCol_CheckMark, v4(kAccent));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(P(6), P(7)));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, P(10));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(P(12), P(9)));
}

void pop_menu_style() {
    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor(8);
}

bool menu_begin(const char* id) {
    push_menu_style();
    ImGui::SetNextWindowSizeConstraints(ImVec2(P(230), 0.f), ImVec2(FLT_MAX, FLT_MAX));
    if (ImGui::BeginPopup(id)) return true;
    pop_menu_style();
    return false;
}

void menu_end() {
    ImGui::EndPopup();
    pop_menu_style();
}

bool menu_item(uint32_t icon, const char* label, const char* shortcut = nullptr, bool checked = false, bool enabled = true,
               ImU32 color = 0) {
    std::string text = std::string(I(icon)) + "   " + label;
    if (color) ImGui::PushStyleColor(ImGuiCol_Text, v4(color));
    bool hit = ImGui::MenuItem(text.c_str(), shortcut, checked, enabled);
    if (color) ImGui::PopStyleColor();
    return hit;
}

bool submenu_begin(uint32_t icon, const char* label, bool enabled = true) {
    std::string text = std::string(I(icon)) + "   " + label;
    ImGui::SetNextWindowSizeConstraints(ImVec2(P(190), 0.f), ImVec2(FLT_MAX, FLT_MAX));
    return ImGui::BeginMenu(text.c_str(), enabled);
}

void menu_heading(const std::string& text) {
    ImGui::PushFont(F.tiny);
    ImGui::PushStyleColor(ImGuiCol_Text, v4(kMuted));
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + P(4));
    ImGui::TextUnformatted(fit(F.tiny, text, P(260)).c_str());
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::Separator();
}

template <size_t N>
bool opt_menu(const Config& cfg, uint32_t icon, const char* tr_label, const char* en_label, const Opt (&opts)[N], std::string& value,
              bool enabled = true, const std::string& auto_hint = {}) {
    bool changed = false;
    if (submenu_begin(icon, tr(cfg, tr_label, en_label), enabled)) {
        for (size_t i = 0; i < N; i++) {
            std::string lab = tr(cfg, opts[i].tr, opts[i].en);
            if (std::string(opts[i].id) == "auto" && !auto_hint.empty()) lab += "  \xC2\xB7  " + auto_hint;
            if (ImGui::MenuItem(lab.c_str(), nullptr, value == opts[i].id)) {
                value = opts[i].id;
                changed = true;
            }
        }
        ImGui::EndMenu();
    }
    return changed;
}

void open_menu_on_right_click(const char* id) {
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) ImGui::OpenPopup(id);
}

void pill(const char* text) {
    ImVec2 ts = text_size(F.small, text);
    ImVec2 size(ts.x + P(20), P(28));
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, p + size, kPanel2, P(6));
    dl->AddRect(p, p + size, kLine, P(6));
    draw_text(dl, F.small, ImVec2(p.x + P(10), p.y + (size.y - ts.y) * 0.5f), kSoft, text);
    ImGui::Dummy(size);
}

struct Card {
    ImDrawList* dl = nullptr;
    ImVec2 start;
    float width = 0;
};

Card card_begin(float width) {
    Card c;
    c.dl = ImGui::GetWindowDrawList();
    c.dl->ChannelsSplit(2);
    c.dl->ChannelsSetCurrent(1);
    c.start = ImGui::GetCursorScreenPos();
    c.width = width;
    ImGui::BeginGroup();
    ImGui::Dummy(ImVec2(width, P(4)));
    ImGui::Indent(P(18));
    return c;
}

void card_end(const Card& c, bool active = false) {
    ImGui::Dummy(ImVec2(0.f, P(10)));
    ImGui::Unindent(P(18));
    ImGui::EndGroup();
    ImVec2 end(c.start.x + c.width, ImGui::GetItemRectMax().y);
    c.dl->ChannelsSetCurrent(0);
    if (active) glow(c.dl, c.start, end, P(12), kAccent, 0.8f);
    c.dl->AddRectFilled(c.start, end, kPanel2, P(12));
    c.dl->AddRect(c.start, end, active ? with_alpha(kAccent, 190) : kLine, P(12), 0, 1.f);
    c.dl->ChannelsMerge();
}

void card_title(uint32_t icon, const char* title, const char* badge = nullptr) {
    ImGui::Dummy(ImVec2(0.f, P(6)));
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 box(P(34), P(34));
    dl->AddRectFilled(p, p + box, with_alpha(kAccent, 22), P(8));
    const char* is = I(icon);
    draw_text(dl, F.ui, p + (box - text_size(F.ui, is)) * 0.5f, kAccent, is);
    draw_text(dl, F.h1, ImVec2(p.x + P(46), p.y + (box.y - text_size(F.h1, title).y) * 0.5f), kText, title);
    if (badge) {
        float x = p.x + P(46) + text_size(F.h1, title).x + P(10);
        ImVec2 bs = text_size(F.tiny, badge);
        ImVec2 ba(x, p.y + (box.y - bs.y - P(8)) * 0.5f);
        dl->AddRectFilled(ba, ba + bs + ImVec2(P(14), P(8)), with_alpha(kAccent, 26), P(5));
        draw_text(dl, F.tiny, ba + ImVec2(P(7), P(4)), kAccent, badge);
    }
    ImGui::Dummy(box);
    ImGui::Dummy(ImVec2(0.f, P(4)));
}

// ---------------------------------------------------------------- state helpers

double scene_start(const Storyboard& b, int index) {
    double t = 0;
    for (int i = 0; i < index && i < static_cast<int>(b.scenes.size()); i++) t += b.scenes[static_cast<size_t>(i)].seconds;
    return t;
}

int scene_at(const Storyboard& b, double t) {
    double acc = 0;
    for (int i = 0; i < static_cast<int>(b.scenes.size()); i++) {
        acc += b.scenes[static_cast<size_t>(i)].seconds;
        if (t < acc) return i;
    }
    return std::max(0, static_cast<int>(b.scenes.size()) - 1);
}

int res_of(const Storyboard& b) { return std::max(b.width, b.height) >= 1900 ? 1 : 0; }

void apply_res(Storyboard& b, int res) {
    apply_aspect_size(b);
    if (res == 1) {
        b.width = b.width * 3 / 2;
        b.height = b.height * 3 / 2;
    }
}

int fps_index(int fps) { return fps == 12 ? 0 : fps == 30 ? 2 : 1; }

void touch(App& app) {
    app.serial++;
    app.changed_at = glfwGetTime();
}

void set_board(App& app, Storyboard board) {
    app.board = std::move(board);
    app.has_board = true;
    app.selected = 0;
    app.play_t = app.board.scenes.empty() ? 0.0 : std::min(1.0, app.board.scenes[0].seconds * 0.4);
    app.playing = false;
    app.editing_title = false;
    app.serial++;
    app.changed_at = 0;
    app.undo.clear();
    app.redo.clear();
    app.committed = app.board;
    app.committed_serial = app.serial;
}

void commit_history(App& app, bool force = false) {
    if (!app.has_board || app.serial == app.committed_serial) return;
    if (!force && ImGui::IsAnyItemActive()) return;
    app.undo.push_back(app.committed);
    if (app.undo.size() > 80) app.undo.erase(app.undo.begin());
    app.redo.clear();
    app.committed = app.board;
    app.committed_serial = app.serial;
}

void restore(App& app, std::vector<Storyboard>& from, std::vector<Storyboard>& to) {
    commit_history(app, true);
    if (from.empty()) return;
    to.push_back(app.board);
    app.board = from.back();
    from.pop_back();
    app.serial++;
    app.changed_at = glfwGetTime();
    app.committed = app.board;
    app.committed_serial = app.serial;
    app.selected = std::clamp(app.selected, 0, static_cast<int>(app.board.scenes.size()) - 1);
    app.playing = false;
}

void undo(App& app) { restore(app, app.undo, app.redo); }
void redo(App& app) { restore(app, app.redo, app.undo); }

std::string file_name_of(const std::string& path) {
    auto pos = path.find_last_of("\\/");
    return pos == std::string::npos ? path : path.substr(pos + 1);
}

std::string dir_of(const std::string& path) {
    auto pos = path.find_last_of("\\/");
    return pos == std::string::npos ? std::string() : path.substr(0, pos);
}

std::string safe_name(const std::string& title) {
    std::string out;
    for (char c : title) out.push_back(std::string("\\/:*?\"<>|").find(c) == std::string::npos ? c : '-');
    out = tidy(out);
    return out.empty() ? "kare" : out;
}

bool file_exists(const std::string& path) {
#ifdef _WIN32
    return GetFileAttributesW(widen(path).c_str()) != INVALID_FILE_ATTRIBUTES;
#else
    std::vector<unsigned char> tmp;
    return read_file(path, tmp);
#endif
}

void open_path(const std::string& path) {
#ifdef _WIN32
    ShellExecuteW(nullptr, L"open", widen(path).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#else
    (void)path;
#endif
}

void reveal(const std::string& path) {
#ifdef _WIN32
    std::wstring args = L"/select,\"" + widen(path) + L"\"";
    ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
#else
    (void)path;
#endif
}

std::string default_dir(const App& app) {
    if (!app.cfg.last_dir.empty()) return app.cfg.last_dir;
    return dir_of(default_export_path());
}

void remember_dir(App& app, const std::string& path) {
    std::string d = dir_of(path);
    if (!d.empty()) app.cfg.last_dir = d;
    save_config(app.cfg);
}

#ifdef _WIN32
std::string save_dialog(GLFWwindow* window, const wchar_t* filter, const wchar_t* ext, const std::wstring& initial) {
    wchar_t file[1024];
    wcsncpy(file, initial.c_str(), 1023);
    file[1023] = 0;
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof ofn;
    ofn.hwndOwner = glfwGetWin32Window(window);
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = file;
    ofn.nMaxFile = 1024;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    ofn.lpstrDefExt = ext;
    if (!GetSaveFileNameW(&ofn)) return {};
    return narrow(file);
}

std::string open_dialog(GLFWwindow* window, const wchar_t* filter) {
    wchar_t file[1024] = L"";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof ofn;
    ofn.hwndOwner = glfwGetWin32Window(window);
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = file;
    ofn.nMaxFile = 1024;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameW(&ofn)) return {};
    return narrow(file);
}
#endif

void upload(unsigned& tex, const Frame& frame) {
    if (!tex) glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, 0x812F);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, 0x812F);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, frame.w, frame.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, frame.rgba.data());
}

Frame half_frame(const Frame& f) {
    Frame o;
    o.w = std::max(1, f.w / 2);
    o.h = std::max(1, f.h / 2);
    o.rgba.resize(static_cast<size_t>(o.w) * o.h * 4);
    for (int y = 0; y < o.h; y++) {
        for (int x = 0; x < o.w; x++) {
            for (int c = 0; c < 4; c++) {
                auto at = [&](int xx, int yy) {
                    return static_cast<int>(f.rgba[(static_cast<size_t>(std::min(yy, f.h - 1)) * f.w + std::min(xx, f.w - 1)) * 4 + c]);
                };
                int sum = at(x * 2, y * 2) + at(x * 2 + 1, y * 2) + at(x * 2, y * 2 + 1) + at(x * 2 + 1, y * 2 + 1);
                o.rgba[(static_cast<size_t>(y) * o.w + x) * 4 + c] = static_cast<uint8_t>(sum / 4);
            }
        }
    }
    return o;
}

void refresh_preview(App& app) {
    if (!app.has_board || app.board.scenes.empty()) return;
    float total = static_cast<float>(total_seconds(app.board));
    if (app.playing) {
        double now = glfwGetTime();
        float t = app.play_from + static_cast<float>(now - app.play_wall);
        if (t >= total) {
            if (app.loop) {
                app.play_from = 0;
                app.play_wall = now;
                t = 0;
            } else {
                app.playing = false;
                t = std::max(0.f, total - 0.02f);
            }
        }
        app.play_t = t;
        app.selected = scene_at(app.board, t);
    }
    int frame_i = static_cast<int>(app.play_t * static_cast<float>(app.board.fps));
    bool low = app.playing && std::max(app.board.width, app.board.height) > 1000;
    if (frame_i == app.shown_frame && app.serial == app.shown_serial && low == app.shown_low) return;
    std::string ferr;
    if (!fonts_load(ferr)) return;
    Frame frame;
    double t = std::min<double>(app.play_t, std::max(0.0, total_seconds(app.board) - 1e-3));
    if (low) {
        Storyboard small = app.board;
        float k = 960.f / static_cast<float>(std::max(app.board.width, app.board.height));
        small.width = std::max(16, static_cast<int>(app.board.width * k) & ~1);
        small.height = std::max(16, static_cast<int>(app.board.height * k) & ~1);
        render_frame(small, t, frame);
    } else {
        render_frame(app.board, t, frame);
    }
    upload(app.tex, frame);
    app.tex_w = frame.w;
    app.tex_h = frame.h;
    app.shown_frame = frame_i;
    app.shown_serial = app.serial;
    app.shown_low = low;
}

void refresh_thumbs(App& app) {
    if (!app.has_board || app.thumb_serial == app.serial) return;
    if (!app.thumbs.empty() && glfwGetTime() - app.changed_at < 0.3) return;
    std::string ferr;
    if (!fonts_load(ferr)) return;
    Storyboard small = app.board;
    small.width = std::max(16, (app.board.width / 2) & ~1);
    small.height = std::max(16, (app.board.height / 2) & ~1);
    small.mark = false;
    size_t n = small.scenes.size();
    while (app.thumbs.size() > n) {
        glDeleteTextures(1, &app.thumbs.back());
        app.thumbs.pop_back();
    }
    app.thumbs.resize(n, 0);
    double acc = 0;
    for (size_t i = 0; i < n; i++) {
        double sec = small.scenes[i].seconds;
        Frame f;
        render_frame(small, acc + sec * 0.55, f);
        Frame h = half_frame(f);
        upload(app.thumbs[i], h);
        app.thumb_w = h.w;
        app.thumb_h = h.h;
        acc += sec;
    }
    app.thumb_serial = app.serial;
}

void toggle_play(App& app) {
    if (!app.has_board) return;
    if (app.playing) {
        app.playing = false;
        return;
    }
    float total = static_cast<float>(total_seconds(app.board));
    if (app.play_t >= total - 0.05f) app.play_t = 0;
    app.playing = true;
    app.play_from = app.play_t;
    app.play_wall = glfwGetTime();
}

void jump_scene(App& app, int index) {
    if (!app.has_board) return;
    index = std::clamp(index, 0, static_cast<int>(app.board.scenes.size()) - 1);
    app.selected = index;
    app.play_t = static_cast<float>(scene_start(app.board, index) + 0.02);
    app.playing = false;
}

void split_at_playhead(App& app) {
    if (!app.has_board) return;
    int i = scene_at(app.board, app.play_t);
    double start = scene_start(app.board, i);
    double sec = app.board.scenes[static_cast<size_t>(i)].seconds;
    double off = app.play_t - start;
    if (off < 0.4 || sec - off < 0.4 || static_cast<int>(app.board.scenes.size()) >= kMaxScenes) {
        app.status = tr(app.cfg, "Bölmek için oynatma çizgisini sahnenin ortasına getirin.",
                        "Move the playhead into the middle of a scene to split it.");
        return;
    }
    duplicate_scene(app.board, i);
    app.board.scenes[static_cast<size_t>(i)].seconds = off;
    app.board.scenes[static_cast<size_t>(i) + 1].seconds = sec - off;
    app.selected = i + 1;
    touch(app);
}

void start_plan(App& app);
void ask_export(App& app);
void open_project(App& app, std::string path = {});
void snapshot(App& app);

bool scene_full(const App& app) { return static_cast<int>(app.board.scenes.size()) >= kMaxScenes; }

void scene_duplicate(App& app, int i) {
    if (!app.has_board || scene_full(app)) return;
    duplicate_scene(app.board, i);
    app.selected = std::min(i + 1, static_cast<int>(app.board.scenes.size()) - 1);
    touch(app);
}

void scene_insert(App& app, int at) {
    if (!app.has_board || scene_full(app)) return;
    add_scene(app.board, at);
    app.selected = std::clamp(at, 0, static_cast<int>(app.board.scenes.size()) - 1);
    app.board.scenes[static_cast<size_t>(app.selected)].title = tr(app.cfg, "Yeni sahne", "New scene");
    touch(app);
}

void scene_delete(App& app, int i) {
    if (!app.has_board || app.board.scenes.size() <= 1) return;
    remove_scene(app.board, i);
    if (app.selected > i) app.selected--;
    app.selected = std::clamp(app.selected, 0, static_cast<int>(app.board.scenes.size()) - 1);
    touch(app);
}

void scene_move(App& app, int i, int to) {
    if (!app.has_board) return;
    if (move_scene(app.board, i, to)) {
        app.selected = to;
        touch(app);
    }
}

void play_from_scene(App& app, int i) {
    jump_scene(app, i);
    toggle_play(app);
}

void pick_image(App& app, int i) {
#ifdef _WIN32
    if (!app.has_board || i < 0 || i >= static_cast<int>(app.board.scenes.size())) return;
    std::string path = open_dialog(app.window, L"Images (PNG, JPG, BMP)\0*.png;*.jpg;*.jpeg;*.bmp\0\0");
    if (path.empty()) return;
    int w = 0, h = 0;
    if (!image_readable(path, &w, &h)) {
        app.status = tr(app.cfg, "Bu görsel okunamadı (PNG, JPG ya da BMP seçin).", "Could not read this image (use PNG, JPG or BMP).");
        return;
    }
    app.board.scenes[static_cast<size_t>(i)].image = path;
    app.selected = i;
    touch(app);
    app.status = std::string(tr(app.cfg, "Görsel eklendi: ", "Image added: ")) + file_name_of(path);
#else
    (void)app;
    (void)i;
#endif
}

// ---------------------------------------------------------------- soundtrack preview

void stop_audio(App& app) {
    if (!app.audio_on) return;
#ifdef _WIN32
    PlaySoundW(nullptr, nullptr, 0);
#endif
    app.audio_on = false;
}

void sync_audio(App& app) {
    if (app.has_board && !app.audio_busy && app.audio_job_serial != app.serial && glfwGetTime() - app.changed_at > 0.35) {
        if (app.audio_thread.joinable()) app.audio_thread.join();
        Storyboard copy = app.board;
        int serial = app.serial;
        app.audio_job_serial = serial;
        app.audio_busy = true;
        app.audio_thread = std::thread([&app, copy, serial] {
            std::vector<int16_t> pcm = render_soundtrack(copy);
            {
                std::lock_guard<std::mutex> lock(app.mu);
                app.track_pending = std::move(pcm);
                app.pending_serial = serial;
                app.track_ready = true;
            }
            app.audio_busy = false;
        });
    }
    {
        std::lock_guard<std::mutex> lock(app.mu);
        if (app.track_ready) {
            app.track = std::move(app.track_pending);
            app.track_serial = app.pending_serial;
            app.track_ready = false;
            app.peaks = audio_peaks(app.track, 1600);
            app.shown_serial = -1;  // the voice is spoken now, so captions can use real word timings
        }
    }
    bool fresh = app.has_board && app.track_serial == app.serial && !app.track.empty();
    if (!app.playing || app.mute || !fresh) {
        stop_audio(app);
        return;
    }
    double now = glfwGetTime();
    float expected = app.audio_t0 + static_cast<float>(now - app.audio_wall);
    if (app.audio_on && std::fabs(expected - app.play_t) < 0.15f) return;
    stop_audio(app);
    size_t start = static_cast<size_t>(std::max(0.f, app.play_t) * kAudioRate);
    if (start * 2 + kAudioRate / 10 >= app.track.size()) return;
    app.wav_slot ^= 1;
    app.wav_play[app.wav_slot] = wav_bytes(app.track, start);
#ifdef _WIN32
    PlaySoundW(reinterpret_cast<LPCWSTR>(app.wav_play[app.wav_slot].data()), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
#endif
    app.audio_on = true;
    app.audio_t0 = app.play_t;
    app.audio_wall = now;
}

void save_soundtrack(App& app) {
#ifdef _WIN32
    if (!app.has_board) return;
    std::string initial = default_dir(app) + "\\" + safe_name(app.board.title) + ".wav";
    std::string path = save_dialog(app.window, L"WAV\0*.wav\0\0", L"wav", widen(initial));
    if (path.empty()) return;
    std::vector<int16_t> pcm = app.track_serial == app.serial ? app.track : render_soundtrack(app.board);
    if (pcm.empty()) {
        app.status = tr(app.cfg, "Bu videoda müzik kapalı.", "Music is off for this video.");
        return;
    }
    auto wav = wav_bytes(pcm, 0);
    if (write_bytes(path, wav.data(), wav.size()))
        app.status = std::string(tr(app.cfg, "Müzik kaydedildi: ", "Soundtrack saved: ")) + file_name_of(path);
    else
        app.status = tr(app.cfg, "Dosya yazılamadı.", "Could not write the file.");
#else
    (void)app;
#endif
}

// ---------------------------------------------------------------- menus

void scene_menu(App& app, int i) {
    int n = static_cast<int>(app.board.scenes.size());
    if (!app.has_board || i < 0 || i >= n) return;
    Scene& s = app.board.scenes[static_cast<size_t>(i)];
    std::string name = s.title.empty() ? s.text : s.title;
    menu_heading(std::string(tr(app.cfg, "SAHNE ", "SCENE ")) + std::to_string(i + 1) + "  \xC2\xB7  " + name);
    if (menu_item(ic::play, tr(app.cfg, "Buradan oynat", "Play from here"))) play_from_scene(app, i);
    double start = scene_start(app.board, i);
    bool can_split = app.play_t > start + 0.4 && app.play_t < start + s.seconds - 0.4 && !scene_full(app);
    if (menu_item(ic::scissors, tr(app.cfg, "Oynatma çizgisinden böl", "Split at playhead"), "S", false, can_split)) {
        split_at_playhead(app);
        return;
    }
    if (menu_item(ic::copy, tr(app.cfg, "Çoğalt", "Duplicate"), "Ctrl+D", false, !scene_full(app))) {
        scene_duplicate(app, i);
        return;
    }
    if (submenu_begin(ic::plus, tr(app.cfg, "Sahne ekle", "Insert scene"), !scene_full(app))) {
        if (menu_item(ic::arrow_left, tr(app.cfg, "Öncesine", "Before"))) scene_insert(app, i);
        if (menu_item(ic::arrow_right, tr(app.cfg, "Sonrasına", "After"))) scene_insert(app, i + 1);
        ImGui::EndMenu();
    }
    if (menu_item(ic::arrow_left, tr(app.cfg, "Sola taşı", "Move left"), nullptr, false, i > 0)) {
        scene_move(app, i, i - 1);
        return;
    }
    if (menu_item(ic::arrow_right, tr(app.cfg, "Sağa taşı", "Move right"), nullptr, false, i + 1 < n)) {
        scene_move(app, i, i + 1);
        return;
    }
    ImGui::Separator();
    std::string auto_layout = opt_label(app.cfg, kLayoutOpts, resolve_layout(app.board, i));
    if (opt_menu(app.cfg, ic::layout, "Yerleşim", "Layout", kLayoutOpts, s.layout, true, s.layout == "auto" ? auto_layout : ""))
        touch(app);
    std::string auto_tr = i > 0 ? opt_label(app.cfg, kTransitionOpts, resolve_transition(app.board, i)) : "";
    if (opt_menu(app.cfg, ic::blend, "Geçiş (girişte)", "Transition (in)", kTransitionOpts, s.transition, i > 0, auto_tr)) touch(app);
    std::string auto_anim = opt_label(app.cfg, kAnimOpts, resolve_anim(app.board, i));
    if (opt_menu(app.cfg, ic::type, "Yazı animasyonu", "Text animation", kAnimOpts, s.anim, true, auto_anim)) touch(app);
    if (opt_menu(app.cfg, ic::move, "Kamera hareketi", "Camera motion", kMotionOpts, s.motion)) touch(app);
    if (submenu_begin(ic::palette, tr(app.cfg, "Görünüm", "Look"))) {
        for (int k = 0; k < 9; k++) {
            const char* lab = k == 0 ? tr(app.cfg, "Videodan", "From video") : tr(app.cfg, kStyles[k].tr, kStyles[k].en);
            if (ImGui::MenuItem(lab, nullptr, s.style == kStyles[k].id)) {
                s.style = kStyles[k].id;
                touch(app);
            }
        }
        ImGui::EndMenu();
    }
    ImGui::Separator();
    if (menu_item(s.image.empty() ? ic::image_plus : ic::image,
                  s.image.empty() ? tr(app.cfg, "Görsel ekle...", "Add image...") : tr(app.cfg, "Görseli değiştir...", "Change image...")))
        pick_image(app, i);
    if (!s.image.empty() && menu_item(ic::image_off, tr(app.cfg, "Görseli kaldır", "Remove image"))) {
        s.image.clear();
        touch(app);
    }
    if (menu_item(ic::clipboard, tr(app.cfg, "Metni kopyala", "Copy text"))) {
        std::string all = s.title;
        if (!s.text.empty()) all += (all.empty() ? "" : "\n") + s.text;
        ImGui::SetClipboardText(all.c_str());
    }
    ImGui::Separator();
    if (menu_item(ic::trash, tr(app.cfg, "Sahneyi sil", "Delete scene"), "Del", false, n > 1, kRed)) scene_delete(app, i);
}

void music_menu_items(App& app) {
    std::string auto_hint = opt_label(app.cfg, kMusicOpts, resolve_music(app.board));
    if (opt_menu(app.cfg, ic::music, "Müzik", "Music", kMusicOpts, app.board.music, app.has_board,
                 app.board.music == "auto" ? auto_hint : ""))
        touch(app);
    if (menu_item(app.mute ? ic::mute : ic::volume, tr(app.cfg, "Önizlemede sessiz", "Mute preview"), "M", app.mute)) app.mute = !app.mute;
    if (menu_item(ic::timer, tr(app.cfg, "Sahneleri ritme oturt", "Snap scenes to beat"), nullptr, false,
                  app.has_board && resolve_music(app.board) != "none")) {
        snap_to_beat(app.board, total_seconds(app.board));
        touch(app);
    }
    if (menu_item(ic::file_audio, tr(app.cfg, "Müziği WAV olarak kaydet...", "Save soundtrack as WAV..."), nullptr, false, app.has_board))
        save_soundtrack(app);
}

void preview_menu(App& app) {
    if (menu_item(app.playing ? ic::pause : ic::play, app.playing ? tr(app.cfg, "Duraklat", "Pause") : tr(app.cfg, "Oynat", "Play"), "Space"))
        toggle_play(app);
    if (menu_item(ic::restart, tr(app.cfg, "Baştan oynat", "Play from start"))) {
        app.play_t = 0;
        app.playing = false;
        toggle_play(app);
    }
    if (menu_item(ic::repeat, tr(app.cfg, "Döngü", "Loop"), nullptr, app.loop)) app.loop = !app.loop;
    ImGui::Separator();
    music_menu_items(app);
    ImGui::Separator();
    if (menu_item(ic::camera, tr(app.cfg, "Kareyi PNG kaydet...", "Save frame as PNG..."))) snapshot(app);
    if (menu_item(app.focus ? ic::minimize : ic::maximize, tr(app.cfg, "Geniş önizleme", "Wide preview"), nullptr, app.focus))
        app.focus = !app.focus;
    if (app.selected >= 0 && submenu_begin(ic::film, tr(app.cfg, "Bu sahne", "This scene"))) {
        scene_menu(app, app.selected);
        ImGui::EndMenu();
    }
    ImGui::Separator();
    if (menu_item(ic::upload, tr(app.cfg, "Videoyu dışa aktar...", "Export video..."), "Ctrl+E", false, !app.exporting)) ask_export(app);
}

void prompt_menu(App& app, std::string& text) {
    auto release = [] { ImGui::ClearActiveID(); };
    if (menu_item(ic::paste, tr(app.cfg, "Yapıştır", "Paste"), "Ctrl+V")) {
        release();
        if (const char* clip = ImGui::GetClipboardText()) {
            if (!text.empty() && text.back() != '\n' && text.back() != ' ') text.push_back(' ');
            text += clip;
        }
    }
    if (menu_item(ic::copy, tr(app.cfg, "Tümünü kopyala", "Copy all"), nullptr, false, !text.empty())) ImGui::SetClipboardText(text.c_str());
    if (menu_item(ic::eraser, tr(app.cfg, "Temizle", "Clear"), nullptr, false, !text.empty())) {
        release();
        text.clear();
    }
    ImGui::Separator();
    if (submenu_begin(ic::sparkles, tr(app.cfg, "Örnekler", "Examples"))) {
        const char* examples[][2] = {
            {"Gün batımında sahil, 8 saniye. Başlık: Boğaz", "Sunset shore, 8 seconds. Title: Bosphorus"},
            {"Dikey tanıtım\n1. Yerel\n2. Hızlı\n3. Kurgu", "Vertical promo\n1. Local\n2. Fast\n3. Editing"},
            {"Neon şehir gecesi, sinematik, 12 saniye", "Neon city night, cinematic, 12 seconds"},
            {"https://github.com/ocornut/imgui", "https://github.com/ocornut/imgui"}};
        for (const auto& ex : examples) {
            std::string label = tidy(tr(app.cfg, ex[0], ex[1]));
            if (ImGui::MenuItem(fit(F.ui, label, P(320)).c_str())) {
                release();
                text = tr(app.cfg, ex[0], ex[1]);
            }
        }
        ImGui::EndMenu();
    }
    if (menu_item(ic::sparkles, tr(app.cfg, "Video oluştur", "Create video"), "Ctrl+Enter", false, !app.planning && !tidy(text).empty())) {
        release();
        app.prompt = text;
        start_plan(app);
    }
}

void path_menu(App& app, const std::string& path, bool project) {
    bool exists = file_exists(path);
    menu_heading(file_name_of(path));
    if (menu_item(project ? ic::folder_open : ic::play, tr(app.cfg, "Aç", "Open"), nullptr, false, exists)) {
        if (project) open_project(app, path);
        else open_path(path);
    }
    if (menu_item(ic::folder, tr(app.cfg, "Klasörde göster", "Show in folder"), nullptr, false, exists)) reveal(path);
    if (menu_item(ic::link, tr(app.cfg, "Yolu kopyala", "Copy path"))) ImGui::SetClipboardText(path.c_str());
    ImGui::Separator();
    if (menu_item(ic::list_x, tr(app.cfg, "Listeden kaldır", "Remove from list"))) {
        auto& list = project ? app.cfg.recent_projects : app.cfg.recent_exports;
        list.erase(std::remove(list.begin(), list.end(), path), list.end());
        save_config(app.cfg);
        app.recent_dirty = true;
    }
}

void start_plan(App& app) {
    if (app.planning || app.exporting) return;
    if (tidy(app.prompt).empty()) {
        app.status = tr(app.cfg, "Bir prompt ya da GitHub bağlantısı yazın.", "Write a prompt or paste a GitHub link.");
        return;
    }
    PlanInput in;
    in.prompt = app.prompt;
    in.style = kStyles[app.style_i].id;
    in.aspect = kAspectIds[app.aspect_i];
    in.seconds = kSecondsOpt[app.seconds_i];
    in.mark = app.cfg.mark;
    in.lang = app.cfg.lang;
    in.use_api = app.plan_api;
    in.api_base = app.cfg.api_base;
    in.api_key = app.cfg.api_key;
    in.api_model = app.cfg.api_model;
    in.github_token = app.cfg.github_token;
    in.shorts = app.shorts;
    in.web = app.web ? 1 : 0;
    if (app.voice_i == 1) in.voice = "none";
    else if (app.voice_i >= 2 && app.voice_i - 2 < static_cast<int>(app.voices.size())) in.voice = app.voices[static_cast<size_t>(app.voice_i - 2)].name;
    in.note = [&app](const std::string& m) {
        std::lock_guard<std::mutex> lock(app.mu);
        app.plan_note = m;
    };
    {
        std::lock_guard<std::mutex> lock(app.mu);
        app.plan_note.clear();
    }
    std::string owner, repo;
    if (find_github_repo(app.prompt, owner, repo))
        app.status = std::string(tr(app.cfg, "GitHub projesi inceleniyor: ", "Inspecting GitHub project: ")) + owner + "/" + repo;
    else if (app.web || wants_research(app.prompt))
        app.status = std::string(tr(app.cfg, "İnternette araştırılıyor: ", "Researching online: ")) + research_query(app.prompt);
    else
        app.status = tr(app.cfg, "Kurgu hazırlanıyor...", "Building the storyboard...");
    app.planning = true;
    if (app.plan_thread.joinable()) app.plan_thread.join();
    app.plan_thread = std::thread([&app, in] {
        PlanResult result = plan(in);
        {
            std::lock_guard<std::mutex> lock(app.mu);
            app.pending = std::move(result);
            app.plan_ready = true;
        }
        app.planning = false;
    });
}

void take_pending(App& app) {
    PlanResult result;
    {
        std::lock_guard<std::mutex> lock(app.mu);
        if (!app.plan_ready) return;
        result = std::move(app.pending);
        app.plan_ready = false;
    }
    Storyboard board = std::move(result.board);
    // Researched and narrated videos come out at 1080p/30 sized for their platform; others follow the form.
    if (result.source.empty() && !has_narration(board)) {
        board.fps = kFpsOpt[app.fps_i];
        apply_res(board, app.res_i);
    }
    normalize(board);
    set_board(app, std::move(board));
    app.project_path.clear();
    if (app.page == kHome || app.page == kScenes || app.page == kAssets) app.page = kCreate;
    app.last_repo = result.repo;
    app.last_repo_note = result.repo_note;
    std::string msg;
    if (!result.repo.empty()) msg = std::string(tr(app.cfg, "GitHub projesi incelendi: ", "GitHub project inspected: ")) + result.repo;
    else if (!result.source.empty())
        msg = std::string(tr(app.cfg, "Araştırma tamam: ", "Research done: ")) + app.board.title + "  ·  " +
              std::to_string(app.board.scenes.size()) + tr(app.cfg, " sahne", " scenes");
    else if (result.from_api) msg = tr(app.cfg, "API kurgusu hazır.", "API storyboard is ready.");
    else msg = tr(app.cfg, "Yerel kurgu hazır.", "Local storyboard is ready.");
    if (!result.warning.empty()) msg += "  ·  " + result.warning;
    app.status = msg;
}

void start_export(App& app, const std::string& path) {
    if (!app.has_board || app.exporting || app.planning || path.empty()) return;
    if (app.worker.joinable()) app.worker.join();
    Storyboard copy = app.board;
    app.cancel = false;
    app.progress = 0;
    app.exporting = true;
    app.export_target = path;
    app.status = tr(app.cfg, "Video dışa aktarılıyor...", "Exporting video...");
    app.worker = std::thread([&app, copy, path] {
        std::string err = encode_video(copy, path, [&app](float p) {
            app.progress = p;
            return !app.cancel.load();
        });
        {
            std::lock_guard<std::mutex> lock(app.mu);
            app.export_error = err;
            app.export_done = true;
        }
        app.exporting = false;
    });
}

void take_export(App& app) {
    std::string err;
    {
        std::lock_guard<std::mutex> lock(app.mu);
        if (!app.export_done) return;
        err = app.export_error;
        app.export_done = false;
    }
    if (err.empty()) {
        push_recent(app.cfg.recent_exports, app.export_target);
        save_config(app.cfg);
        app.status = std::string(tr(app.cfg, "Video kaydedildi: ", "Video saved: ")) + file_name_of(app.export_target);
    } else {
        app.status = err;
    }
}

void ask_export(App& app) {
    if (!app.has_board || app.exporting || app.planning) return;
#ifdef _WIN32
    std::string initial = default_dir(app) + "\\" + safe_name(app.board.title) + ".mp4";
    std::string path = save_dialog(app.window, L"MP4 (H.264)\0*.mp4\0AVI (MJPEG)\0*.avi\0\0", L"mp4", widen(initial));
    if (path.empty()) return;
    remember_dir(app, path);
    start_export(app, path);
#else
    start_export(app, default_export_path());
#endif
}

void save_project(App& app, bool ask) {
    if (!app.has_board) return;
    std::string path = app.project_path;
#ifdef _WIN32
    if (path.empty() || ask) {
        std::string initial = default_dir(app) + "\\" + safe_name(app.board.title) + ".kare";
        path = save_dialog(app.window, L"Kare\0*.kare\0\0", L"kare", widen(initial));
    }
#endif
    if (path.empty()) return;
    std::string err;
    app.board.prompt = app.prompt;
    if (!save_story(path, app.board, err)) {
        app.status = err;
        return;
    }
    app.project_path = path;
    push_recent(app.cfg.recent_projects, path);
    remember_dir(app, path);
    app.recent_dirty = true;
    app.status = std::string(tr(app.cfg, "Proje kaydedildi: ", "Project saved: ")) + file_name_of(path);
}

void open_project(App& app, std::string path) {
#ifdef _WIN32
    if (path.empty()) path = open_dialog(app.window, L"Kare\0*.kare\0\0");
#endif
    if (path.empty()) return;
    Storyboard loaded;
    std::string err;
    if (!load_story(path, loaded, err)) {
        app.status = err;
        return;
    }
    app.prompt = loaded.prompt;
    set_board(app, std::move(loaded));
    app.project_path = path;
    app.res_i = res_of(app.board);
    app.fps_i = fps_index(app.board.fps);
    push_recent(app.cfg.recent_projects, path);
    remember_dir(app, path);
    app.recent_dirty = true;
    if (app.page == kHome || app.page == kAssets) app.page = kEdit;
    app.status = std::string(tr(app.cfg, "Proje açıldı: ", "Project opened: ")) + file_name_of(path);
}

void new_project(App& app) {
    app.has_board = false;
    app.board = Storyboard{};
    app.project_path.clear();
    app.prompt.clear();
    app.playing = false;
    app.play_t = 0;
    app.undo.clear();
    app.redo.clear();
    app.last_repo.clear();
    app.page = kCreate;
}

void snapshot(App& app) {
    if (!app.has_board) return;
#ifdef _WIN32
    std::string initial = default_dir(app) + "\\" + safe_name(app.board.title) + ".png";
    std::string path = save_dialog(app.window, L"PNG\0*.png\0\0", L"png", widen(initial));
    if (path.empty()) return;
    std::string err;
    if (write_preview_png(path, app.board, app.play_t, err))
        app.status = std::string(tr(app.cfg, "Kare kaydedildi: ", "Frame saved: ")) + file_name_of(path);
    else
        app.status = err;
#endif
}

void start_test(App& app) {
    if (app.testing) return;
    if (app.test_thread.joinable()) app.test_thread.join();
    std::string base = app.cfg.api_base, key = app.cfg.api_key;
    app.testing = true;
    app.test_thread = std::thread([&app, base, key] {
        std::string msg;
        bool ok = test_api(base, key, msg);
        {
            std::lock_guard<std::mutex> lock(app.mu);
            app.test_text = msg;
            app.test_ok = ok;
            app.test_done = true;
        }
        app.testing = false;
    });
}

void take_test(App& app) {
    std::lock_guard<std::mutex> lock(app.mu);
    if (!app.test_done) return;
    app.test_done = false;
    app.test_view = app.test_text;
    app.test_view_ok = app.test_ok;
}

void refresh_recent(App& app) {
    if (!app.recent_dirty) return;
    app.recent_dirty = false;
    app.recent.clear();
    for (const auto& path : app.cfg.recent_projects) {
        Recent r;
        r.path = path;
        Storyboard b;
        std::string err;
        r.exists = load_story(path, b, err);
        r.name = r.exists ? b.title : file_name_of(path);
        if (r.exists)
            r.meta = std::to_string(b.scenes.size()) + tr(app.cfg, " sahne · ", " scenes · ") + std::to_string(b.width) +
                     "\xC3\x97" + std::to_string(b.height);
        else
            r.meta = tr(app.cfg, "bulunamadı", "missing");
        app.recent.push_back(r);
    }
}

// ---------------------------------------------------------------- chrome

float draw_window_buttons(App& app, ImVec2 o, float W, float TB) {
    g_chrome.caption = static_cast<int>(TB);
    if (!g_chrome.on) return 0.f;
    ImDrawList* dl = g_root;
    float bw = P(46);
    float x0 = o.x + W - bw * 3;
    bool maxed = glfwGetWindowAttrib(app.window, GLFW_MAXIMIZED) != 0;
    for (int k = 0; k < 3; k++) {
        ImVec2 a(x0 + bw * k, o.y), b(a.x + bw, o.y + TB - 1);
        ImGui::SetCursorScreenPos(a);
        ImGui::PushID(900 + k);
        bool pressed = ImGui::InvisibleButton("wb", b - a);
        hot_item();
        bool hot = ImGui::IsItemHovered();
        bool held = ImGui::IsItemActive();
        ImGui::PopID();
        if (hot) dl->AddRectFilled(a, b, k == 2 ? (held ? IM_COL32(200, 40, 40, 255) : IM_COL32(232, 17, 35, 255)) : (held ? kRaised : kPanel2));
        ImVec2 c((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
        c.x = std::floor(c.x) + 0.5f;
        c.y = std::floor(c.y) + 0.5f;
        ImU32 col = hot ? kText : kSoft;
        float s = P(5);
        if (k == 0) {
            dl->AddLine(ImVec2(c.x - s, c.y), ImVec2(c.x + s, c.y), col, 1.f);
        } else if (k == 1) {
            if (maxed) {
                float s2 = P(4);
                dl->AddRect(ImVec2(c.x - s, c.y - s2 + P(2)), ImVec2(c.x + s2 - P(1), c.y + s), col, P(1), 0, 1.f);
                dl->AddLine(ImVec2(c.x - s + P(2), c.y - s), ImVec2(c.x + s, c.y - s), col, 1.f);
                dl->AddLine(ImVec2(c.x + s, c.y - s), ImVec2(c.x + s, c.y + s2 - P(2)), col, 1.f);
            } else {
                dl->AddRect(ImVec2(c.x - s, c.y - s), ImVec2(c.x + s, c.y + s), col, P(1), 0, 1.f);
            }
        } else {
            dl->AddLine(ImVec2(c.x - s, c.y - s), ImVec2(c.x + s, c.y + s), col, 1.2f);
            dl->AddLine(ImVec2(c.x - s, c.y + s), ImVec2(c.x + s, c.y - s), col, 1.2f);
        }
        if (pressed) {
            if (k == 0) glfwIconifyWindow(app.window);
            else if (k == 1) maxed ? glfwRestoreWindow(app.window) : glfwMaximizeWindow(app.window);
            else glfwSetWindowShouldClose(app.window, GLFW_TRUE);
        }
    }
    dl->AddLine(ImVec2(x0 - P(8), o.y + TB * 0.3f), ImVec2(x0 - P(8), o.y + TB * 0.7f), kLine);
    return bw * 3 + P(16);
}

void draw_topbar(App& app, ImVec2 o, float W, float TB, float tabs_x) {
    ImDrawList* dl = g_root;
    g_chrome.hot.clear();
    dl->AddLine(ImVec2(o.x, o.y + TB - 1), ImVec2(o.x + W, o.y + TB - 1), kLine);
    float buttons_w = draw_window_buttons(app, o, W, TB);
    float ls = P(26);
    ImVec2 lp(o.x + P(18), o.y + (TB - ls) * 0.5f);
    draw_logo(dl, lp, ls);
    float tx = lp.x + ls + P(12);
    float kw = spaced_text(dl, F.h1, ImVec2(tx, o.y + (TB - F.h1->FontSize) * 0.5f), kText, "KARE", P(3));
    draw_text(dl, F.tiny, ImVec2(tx + kw + P(8), o.y + (TB - F.tiny->FontSize) * 0.5f + P(2)), kDim, "v" KARE_VERSION);

    struct Tab {
        int page;
        uint32_t icon;
        const char* tr;
        const char* en;
    };
    const Tab tabs[] = {{kHome, ic::folder, "Proje", "Project"},         {kCreate, ic::sparkles, "Oluştur", "Create"},
                        {kEdit, ic::film, "Kurgu", "Edit"},              {kScenes, ic::grid, "Sahneler", "Scenes"},
                        {kAssets, ic::box, "Varlıklar", "Assets"},       {kSettings, ic::settings, "Ayarlar", "Settings"}};
    float x = tabs_x;
    float th = P(38);
    for (const auto& t : tabs) {
        ImGui::SetCursorScreenPos(ImVec2(x, o.y + (TB - th) * 0.5f));
        if (top_tab(t.tr, t.icon, tr(app.cfg, t.tr, t.en), app.page == t.page, th)) app.page = t.page;
        hot_item();
        x = ImGui::GetItemRectMax().x + P(6);
    }

    struct Meter {
        uint32_t icon;
        const char* name;
        float value;
    };
    float ram = g_sys.ram_total > 0 ? g_sys.ram_used / g_sys.ram_total * 100.f : -1.f;
    const Meter meters[] = {{ic::cpu, "CPU", g_sys.cpu}, {ic::monitor, "GPU", g_sys.gpu}, {ic::memory, "RAM", ram}};
    float mx = o.x + W - P(18) - buttons_w;
    if (mx < x + P(200)) return;
    for (int i = 2; i >= 0; i--) {
        char val[16];
        if (meters[i].value < 0) std::snprintf(val, sizeof val, "--");
        else std::snprintf(val, sizeof val, "%d%%", static_cast<int>(meters[i].value + 0.5f));
        float w = std::max(text_size(F.tiny, meters[i].name).x, text_size(F.tiny, val).x);
        const char* is = I(meters[i].icon);
        float iw = text_size(F.ui, is).x;
        float block = iw + P(6) + w;
        float bx = mx - block;
        draw_text(dl, F.ui, ImVec2(bx, o.y + (TB - F.ui->FontSize) * 0.5f), with_alpha(kAccent, 220), is);
        draw_text(dl, F.tiny, ImVec2(bx + iw + P(6), o.y + TB * 0.5f - F.tiny->FontSize - P(1)), kMuted, meters[i].name);
        draw_text(dl, F.tiny, ImVec2(bx + iw + P(6), o.y + TB * 0.5f + P(1)), kText, val);
        mx = bx - P(18);
    }
}

void draw_statusbar(App& app, ImVec2 o, float W, float H, float SB) {
    ImDrawList* dl = g_root;
    float y0 = o.y + H - SB;
    dl->AddLine(ImVec2(o.x, y0), ImVec2(o.x + W, y0), kLine);
    float cy = y0 + SB * 0.5f;
    float x = o.x + P(18);
    float ls = P(16);
    draw_logo(dl, ImVec2(x, cy - ls * 0.5f), ls);
    x += ls + P(8);
    x += spaced_text(dl, F.bold, ImVec2(x, cy - F.bold->FontSize * 0.5f), kText, "KARE", P(2)) + P(16);
    auto sep = [&] {
        dl->AddLine(ImVec2(x, cy - P(8)), ImVec2(x, cy + P(8)), kLine);
        x += P(14);
    };
    auto item = [&](const std::string& s, ImU32 c) {
        draw_text(dl, F.small, ImVec2(x, cy - F.small->FontSize * 0.5f), c, s.c_str());
        x += text_size(F.small, s.c_str()).x + P(14);
    };
    bool busy = app.planning || app.exporting;
    dl->AddCircleFilled(ImVec2(x + P(4), cy), P(3.5f), busy ? kWarm : kAccent, 12);
    x += P(14);
    item(busy ? tr(app.cfg, "Çalışıyor", "Working") : tr(app.cfg, "Hazır", "Ready"), kSoft);
    sep();
    std::string model = app.plan_api ? std::string("API · ") + app.cfg.api_model : tr(app.cfg, "Kare Yerel", "Kare Local");
    item(std::string(tr(app.cfg, "Model: ", "Model: ")) + model, kMuted);
    sep();
    if (!app.gpu_name.empty()) {
        item(std::string(tr(app.cfg, "Ekran: ", "Display: ")) + app.gpu_name, kMuted);
        sep();
    }

    float right = o.x + W - P(18);
    if (app.exporting) {
        float bar_w = P(220);
        char pct[16];
        std::snprintf(pct, sizeof pct, "%%%d", static_cast<int>(app.progress.load() * 100.f));
        ImGui::SetCursorScreenPos(ImVec2(right - P(24), cy - P(12)));
        if (icon_button("cancel_export", ic::x, ImVec2(P(24), P(24)), false, true, tr(app.cfg, "İptal", "Cancel")))
            app.cancel = true;
        float pw = text_size(F.small, pct).x;
        draw_text(dl, F.small, ImVec2(right - P(32) - pw, cy - F.small->FontSize * 0.5f), kText, pct);
        float bx1 = right - P(42) - pw;
        float bx0 = bx1 - bar_w;
        dl->AddRectFilled(ImVec2(bx0, cy - P(3)), ImVec2(bx1, cy + P(3)), kRaised, P(3));
        dl->AddRectFilled(ImVec2(bx0, cy - P(3)), ImVec2(bx0 + bar_w * app.progress.load(), cy + P(3)), kAccent, P(3));
        const char* label = tr(app.cfg, "Video oluşturuluyor...", "Rendering video...");
        float lw = text_size(F.small, label).x;
        draw_text(dl, F.small, ImVec2(bx0 - P(12) - lw, cy - F.small->FontSize * 0.5f), kSoft, label);
        right = bx0 - P(24) - lw;
    } else if (app.planning) {
        std::string label;
        {
            std::lock_guard<std::mutex> lock(app.mu);
            label = app.plan_note;
        }
        if (label.empty()) label = tr(app.cfg, "Kurgu hazırlanıyor...", "Building storyboard...");
        float lw = text_size(F.small, label.c_str()).x;
        draw_text(dl, F.small, ImVec2(right - lw, cy - F.small->FontSize * 0.5f), kSoft, label.c_str());
        spinner(dl, ImVec2(right - lw - P(14), cy), P(6), kAccent);
        right -= lw + P(30);
    }
    if (!app.status.empty() && right - x > P(80)) {
        std::string s = fit(F.small, app.status, right - x);
        draw_text(dl, F.small, ImVec2(right - text_size(F.small, s.c_str()).x, cy - F.small->FontSize * 0.5f), kMuted, s.c_str());
    }
}

void draw_sidebar(App& app, ImVec2 a, ImVec2 b) {
    panel_bg(a, b);
    float pad = P(10);
    float w = b.x - a.x - pad * 2;
    ImGui::PushID("side");
    ImGui::SetCursorScreenPos(a + ImVec2(pad, pad));
    struct Nav {
        int page;
        uint32_t icon;
        const char* tr;
        const char* en;
    };
    const Nav navs[] = {{kHome, ic::house, "Ana Ekran", "Home"},
                        {kCreate, ic::sparkles, "Oluştur", "Create"},
                        {kEdit, ic::film, "Kurgu (Timeline)", "Edit (Timeline)"},
                        {kScenes, ic::grid, "Sahneler", "Scenes"},
                        {kAssets, ic::folder, "Varlıklar", "Assets"},
                        {kModels, ic::box, "Modeller", "Models"},
                        {kSettings, ic::settings, "Ayarlar", "Settings"}};
    float ny = a.y + pad;
    for (const auto& n : navs) {
        ImGui::SetCursorScreenPos(ImVec2(a.x + pad, ny));
        if (nav_item(n.tr, n.icon, tr(app.cfg, n.tr, n.en), app.page == n.page, w)) app.page = n.page;
        ny += P(40);
    }

    float sys_h = P(176);
    ImVec2 sa(a.x + pad, b.y - pad - sys_h), sb(b.x - pad, b.y - pad);

    ImVec2 hp(a.x + pad, ny + P(10));
    g_root->AddLine(ImVec2(hp.x, hp.y - P(4)), ImVec2(hp.x + w, hp.y - P(4)), kLine);
    draw_text(g_root, F.bold, hp + ImVec2(P(4), P(8)), kText, tr(app.cfg, "Projeler", "Projects"));
    ImGui::SetCursorScreenPos(ImVec2(hp.x + w - P(28), hp.y + P(4)));
    if (icon_button("newp", ic::plus, ImVec2(P(28), P(28)), false, true, tr(app.cfg, "Yeni proje", "New project")))
        new_project(app);
    ImGui::SetCursorScreenPos(ImVec2(hp.x, hp.y + P(38)));
    float list_h = sa.y - P(10) - (hp.y + P(38));
    if (list_h > P(40)) {
        ImGui::BeginChild("plist", ImVec2(w, list_h), 0, 0);
        auto item = [&](const char* id, const std::string& name, const std::string& meta, bool active) {
            ImGui::PushID(id);
            ImVec2 p = ImGui::GetCursorScreenPos();
            float iw = ImGui::GetContentRegionAvail().x;
            ImVec2 size(iw, P(46));
            int pressed = ImGui::InvisibleButton("pi", size) ? 1 : 0;
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) pressed = 2;
            bool hot = ImGui::IsItemHovered();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            if (active) {
                dl->AddRectFilled(p, p + size, with_alpha(kAccent, 20), P(8));
                dl->AddRect(p, p + size, with_alpha(kAccent, 150), P(8));
            } else if (hot) {
                dl->AddRectFilled(p, p + size, kRaised, P(8));
            }
            ImVec2 box(P(28), P(28));
            ImVec2 bp(p.x + P(8), p.y + (size.y - box.y) * 0.5f);
            dl->AddRectFilled(bp, bp + box, kPanel2, P(6));
            dl->AddRect(bp, bp + box, kLine, P(6));
            const char* is = I(ic::film);
            draw_text(dl, F.small, bp + (box - text_size(F.small, is)) * 0.5f, active ? kAccent : kMuted, is);
            float tx = bp.x + box.x + P(10);
            float tw = size.x - (tx - p.x) - P(26);
            draw_text(dl, F.ui, ImVec2(tx, p.y + P(6)), kText, fit(F.ui, name, tw).c_str());
            draw_text(dl, F.tiny, ImVec2(tx, p.y + P(26)), kMuted, fit(F.tiny, meta, tw).c_str());
            if (active) {
                float cx = p.x + size.x - P(14), cy = p.y + size.y * 0.5f;
                dl->AddTriangleFilled(ImVec2(cx - P(4), cy - P(6)), ImVec2(cx - P(4), cy + P(6)), ImVec2(cx + P(6), cy), kAccent);
            }
            ImGui::PopID();
            return pressed;
        };
        if (app.has_board) {
            std::string meta = std::to_string(app.board.scenes.size()) + tr(app.cfg, " sahne · ", " scenes · ") +
                               std::to_string(app.board.width) + "\xC3\x97" + std::to_string(app.board.height);
            std::string name = app.board.title.empty() ? tr(app.cfg, "İsimsiz Proje", "Untitled project") : app.board.title;
            int hit = item("current", name, meta, true);
            if (hit == 1) app.page = kEdit;
            if (hit == 2) ImGui::OpenPopup("current_menu");
            ImGui::Dummy(ImVec2(0.f, P(2)));
        }
        for (size_t i = 0; i < app.recent.size(); i++) {
            const Recent& r = app.recent[i];
            if (!app.project_path.empty() && fold_tr(r.path) == fold_tr(app.project_path)) continue;
            ImGui::PushID(static_cast<int>(i));
            int hit = item("recent", r.name, r.meta, false);
            ImGui::PopID();
            if (hit == 1 && r.exists) open_project(app, r.path);
            if (hit == 2) {
                app.ctx_path = r.path;
                ImGui::OpenPopup("recent_menu");
            }
            ImGui::Dummy(ImVec2(0.f, P(2)));
        }
        if (menu_begin("current_menu")) {
            menu_heading(app.board.title);
            if (menu_item(ic::film, tr(app.cfg, "Kurguda aç", "Open in editor"))) app.page = kEdit;
            if (menu_item(ic::pencil, tr(app.cfg, "Yeniden adlandır", "Rename"))) {
                app.page = kEdit;
                app.editing_title = true;
                app.focus_title = true;
            }
            if (menu_item(ic::save, tr(app.cfg, "Kaydet", "Save"), "Ctrl+S")) save_project(app, false);
            if (menu_item(ic::save, tr(app.cfg, "Farklı kaydet...", "Save as..."))) save_project(app, true);
            if (!app.project_path.empty() && menu_item(ic::folder, tr(app.cfg, "Klasörde göster", "Show in folder"))) reveal(app.project_path);
            ImGui::Separator();
            if (menu_item(ic::upload, tr(app.cfg, "Videoyu dışa aktar...", "Export video..."), "Ctrl+E", false, !app.exporting)) ask_export(app);
            if (menu_item(ic::file_audio, tr(app.cfg, "Müziği WAV olarak kaydet...", "Save soundtrack as WAV..."))) save_soundtrack(app);
            ImGui::Separator();
            if (menu_item(ic::x, tr(app.cfg, "Projeyi kapat", "Close project"))) new_project(app);
            menu_end();
        }
        if (menu_begin("recent_menu")) {
            path_menu(app, app.ctx_path, true);
            menu_end();
        }
        if (!app.has_board && app.recent.empty())
            caption(tr(app.cfg, "Henüz proje yok. Oluştur sekmesinden başlayın.", "No projects yet. Start from Create."),
                    ImGui::GetContentRegionAvail().x);
        ImGui::EndChild();
    }

    g_root->AddRectFilled(sa, sb, kPanel2, P(10));
    g_root->AddRect(sa, sb, kLine, P(10));
    draw_text(g_root, F.bold, sa + ImVec2(P(14), P(12)), kText, tr(app.cfg, "Sistem", "System"));
    struct Row {
        uint32_t icon;
        const char* name;
        float frac;
        std::string value;
    };
    char buf[64];
    std::vector<Row> rows;
    float cpu = g_sys.cpu, gpu = g_sys.gpu;
    std::snprintf(buf, sizeof buf, cpu < 0 ? "--" : "%d%%", static_cast<int>(cpu + 0.5f));
    rows.push_back({ic::cpu, "CPU", std::max(0.f, cpu) / 100.f, buf});
    std::snprintf(buf, sizeof buf, gpu < 0 ? "--" : "%d%%", static_cast<int>(gpu + 0.5f));
    rows.push_back({ic::monitor, "GPU", std::max(0.f, gpu) / 100.f, buf});
    std::snprintf(buf, sizeof buf, "%.1f / %.1f GB", g_sys.ram_used.load(), g_sys.ram_total.load());
    rows.push_back({ic::memory, "RAM", g_sys.ram_total > 0 ? g_sys.ram_used / g_sys.ram_total : 0.f, buf});
    std::snprintf(buf, sizeof buf, "%.0f / %.0f GB", g_sys.disk_used.load(), g_sys.disk_total.load());
    rows.push_back({ic::disk, "Disk", g_sys.disk_total > 0 ? g_sys.disk_used / g_sys.disk_total : 0.f, buf});
    float ry = sa.y + P(40);
    for (const auto& r : rows) {
        const char* is = I(r.icon);
        draw_text(g_root, F.small, ImVec2(sa.x + P(14), ry), kMuted, is);
        draw_text(g_root, F.small, ImVec2(sa.x + P(36), ry), kSoft, r.name);
        float vw = text_size(F.small, r.value.c_str()).x;
        draw_text(g_root, F.small, ImVec2(sb.x - P(14) - vw, ry), kText, r.value.c_str());
        float by = ry + P(19);
        g_root->AddRectFilled(ImVec2(sa.x + P(36), by), ImVec2(sb.x - P(14), by + P(3)), kRaised, P(2));
        g_root->AddRectFilled(ImVec2(sa.x + P(36), by), ImVec2(sa.x + P(36) + (sb.x - P(50) - sa.x) * std::clamp(r.frac, 0.f, 1.f), by + P(3)),
                              kAccent, P(2));
        ry += P(33);
    }
    ImGui::SetCursorScreenPos(sa);
    ImGui::Dummy(sb - sa);
    ImGui::PopID();
}

// ---------------------------------------------------------------- studio

void draw_preview(App& app, ImVec2 a, ImVec2 b) {
    ImDrawList* dl = g_root;
    dl->AddRectFilled(a, b, IM_COL32(3, 6, 9, 255), P(10));
    dl->AddRect(a, b, kLine, P(10));
    if (!app.has_board) {
        const char* is = I(ic::clapper);
        ImVec2 c((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
        ImVec2 isz = text_size(F.icon, is);
        dl->AddCircleFilled(c - ImVec2(0, P(40)), P(34), with_alpha(kAccent, 18), 40);
        dl->AddCircle(c - ImVec2(0, P(40)), P(34), with_alpha(kAccent, 90), 40, 1.f);
        draw_text(dl, F.icon, c - ImVec2(isz.x * 0.5f, P(40) + isz.y * 0.5f), kAccent, is);
        const char* t1 = tr(app.cfg, "Prompt yaz ya da GitHub bağlantısı yapıştır", "Write a prompt or paste a GitHub link");
        const char* t2 = tr(app.cfg, "Kare sahneleri kurar; burada izler, aşağıda kurgularsın.",
                            "Kare lays out the scenes; watch them here and edit below.");
        draw_text(dl, F.h1, ImVec2(c.x - text_size(F.h1, t1).x * 0.5f, c.y + P(10)), kText, t1);
        draw_text(dl, F.small, ImVec2(c.x - text_size(F.small, t2).x * 0.5f, c.y + P(40)), kMuted, t2);
        ImGui::SetCursorScreenPos(a);
        ImGui::Dummy(b - a);
        return;
    }
    refresh_preview(app);
    if (!app.tex || app.tex_w <= 0 || app.tex_h <= 0) return;
    float m = P(10);
    float aw = b.x - a.x - m * 2, ah = b.y - a.y - m * 2;
    float aspect = static_cast<float>(app.tex_w) / static_cast<float>(app.tex_h);
    float dw = aw, dh = aw / aspect;
    if (dh > ah) {
        dh = ah;
        dw = dh * aspect;
    }
    ImVec2 ip(a.x + (b.x - a.x - dw) * 0.5f, a.y + (b.y - a.y - dh) * 0.5f);
    ImVec2 iq(ip.x + dw, ip.y + dh);
    dl->AddImageRounded((ImTextureID)(intptr_t)app.tex, ip, iq, ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, P(6));

    ImGui::SetCursorScreenPos(ip);
    if (ImGui::InvisibleButton("preview_click", iq - ip)) toggle_play(app);
    open_menu_on_right_click("preview_menu");
    bool hot = ImGui::IsItemHovered();
    if (menu_begin("preview_menu")) {
        preview_menu(app);
        menu_end();
    }

    float inset = std::min(P(18), dw * 0.05f);
    float len = std::min(P(30), dw * 0.08f);
    ImU32 lc = with_alpha(IM_COL32_WHITE, 215);
    float th = P(2);
    auto corner = [&](ImVec2 c, float sx, float sy) {
        dl->AddLine(c, ImVec2(c.x + len * sx, c.y), lc, th);
        dl->AddLine(c, ImVec2(c.x, c.y + len * sy), lc, th);
    };
    corner(ImVec2(ip.x + inset, ip.y + inset), 1, 1);
    corner(ImVec2(iq.x - inset, ip.y + inset), -1, 1);
    corner(ImVec2(ip.x + inset, iq.y - inset), 1, -1);
    corner(ImVec2(iq.x - inset, iq.y - inset), -1, -1);
    auto shadow_text = [&](ImVec2 p, const char* s, ImU32 c) {
        draw_text(dl, F.small, p + ImVec2(1, 1), IM_COL32(0, 0, 0, 170), s);
        draw_text(dl, F.small, p, c, s);
    };
    float tx = ip.x + inset + P(12), ty = ip.y + inset + P(8);
    float pulse = app.playing ? 0.55f + 0.45f * std::sin(static_cast<float>(ImGui::GetTime()) * 5.f) : 1.f;
    dl->AddCircleFilled(ImVec2(tx + P(4), ty + F.small->FontSize * 0.5f), P(4),
                        app.playing ? with_alpha(kRed, static_cast<int>(255 * pulse)) : kMuted, 12);
    shadow_text(ImVec2(tx + P(14), ty), app.playing ? "REC" : tr(app.cfg, "DURDU", "PAUSED"), IM_COL32_WHITE);
    std::string tc = timecode(app.play_t, app.board.fps);
    shadow_text(ImVec2(iq.x - inset - P(12) - text_size(F.small, tc.c_str()).x, ty), tc.c_str(), IM_COL32_WHITE);
    char info[64];
    std::snprintf(info, sizeof info, "%d\xC3\x97%d   %dFPS", app.board.width, app.board.height, app.board.fps);
    float by = iq.y - inset - P(8) - F.small->FontSize;
    shadow_text(ImVec2(tx, by), info, IM_COL32_WHITE);
    std::string sc = std::string(tr(app.cfg, "Sahne ", "Scene ")) + std::to_string(app.selected + 1) + "/" +
                     std::to_string(app.board.scenes.size());
    shadow_text(ImVec2(iq.x - inset - P(12) - text_size(F.small, sc.c_str()).x, by), sc.c_str(), IM_COL32_WHITE);
    ImVec2 c((ip.x + iq.x) * 0.5f, (ip.y + iq.y) * 0.5f);
    dl->AddLine(ImVec2(c.x - P(9), c.y), ImVec2(c.x + P(9), c.y), with_alpha(IM_COL32_WHITE, 110), 1.f);
    dl->AddLine(ImVec2(c.x, c.y - P(9)), ImVec2(c.x, c.y + P(9)), with_alpha(IM_COL32_WHITE, 110), 1.f);
    if (hot && !app.playing) {
        dl->AddCircleFilled(c, P(30), IM_COL32(0, 0, 0, 150), 40);
        dl->AddCircle(c, P(30), with_alpha(kAccent, 200), 40, P(1.5f));
        dl->AddTriangleFilled(ImVec2(c.x - P(8), c.y - P(12)), ImVec2(c.x - P(8), c.y + P(12)), ImVec2(c.x + P(13), c.y), kAccent);
    }
    float total = static_cast<float>(total_seconds(app.board));
    float u = total > 0 ? std::clamp(app.play_t / total, 0.f, 1.f) : 0.f;
    dl->AddRectFilled(ImVec2(ip.x, iq.y - P(3)), ImVec2(iq.x, iq.y), IM_COL32(0, 0, 0, 120));
    dl->AddRectFilled(ImVec2(ip.x, iq.y - P(3)), ImVec2(ip.x + dw * u, iq.y), kAccent);
}

void draw_transport(App& app, ImVec2 a, ImVec2 b) {
    ImDrawList* dl = g_root;
    float cy = (a.y + b.y) * 0.5f;
    bool hb = app.has_board;
    int fps = hb ? app.board.fps : 24;
    double total = hb ? total_seconds(app.board) : 0;
    std::string cur = timecode(hb ? app.play_t : 0, fps);
    std::string tot = " / " + timecode(total, fps);
    draw_text(dl, F.bold, ImVec2(a.x + P(4), cy - F.bold->FontSize * 0.5f), kText, cur.c_str());
    draw_text(dl, F.ui, ImVec2(a.x + P(4) + text_size(F.bold, cur.c_str()).x, cy - F.ui->FontSize * 0.5f), kMuted, tot.c_str());

    float bs = P(32), big = P(44), gap = P(10);
    float cw = bs * 4 + big + gap * 4;
    float x = (a.x + b.x) * 0.5f - cw * 0.5f;
    auto place = [&](float size) {
        ImGui::SetCursorScreenPos(ImVec2(x, cy - size * 0.5f));
        x += size + gap;
    };
    place(bs);
    if (icon_button("t_start", ic::skip_back, ImVec2(bs, bs), false, hb, tr(app.cfg, "Başa dön", "Go to start"))) {
        app.play_t = 0;
        app.play_from = 0;
        app.play_wall = glfwGetTime();
    }
    place(bs);
    if (icon_button("t_prev", ic::left, ImVec2(bs, bs), false, hb, tr(app.cfg, "Önceki sahne", "Previous scene")))
        jump_scene(app, app.selected - 1);
    place(big);
    {
        ImVec2 p = ImGui::GetCursorScreenPos();
        bool pressed = ImGui::InvisibleButton("t_play", ImVec2(big, big));
        bool hot = ImGui::IsItemHovered();
        ImVec2 c = p + ImVec2(big, big) * 0.5f;
        ImU32 col = hb ? (hot ? IM_COL32(120, 244, 248, 255) : kAccent) : kDim;
        if (hb) dl->AddCircleFilled(c, big * 0.5f, with_alpha(kAccent, hot ? 36 : 18), 40);
        if (app.playing) {
            dl->AddRectFilled(ImVec2(c.x - P(8), c.y - P(10)), ImVec2(c.x - P(2), c.y + P(10)), col, P(1.5f));
            dl->AddRectFilled(ImVec2(c.x + P(2), c.y - P(10)), ImVec2(c.x + P(8), c.y + P(10)), col, P(1.5f));
        } else {
            dl->AddTriangleFilled(ImVec2(c.x - P(7), c.y - P(11)), ImVec2(c.x - P(7), c.y + P(11)), ImVec2(c.x + P(12), c.y), col);
        }
        if (pressed) toggle_play(app);
    }
    place(bs);
    if (icon_button("t_next", ic::right, ImVec2(bs, bs), false, hb, tr(app.cfg, "Sonraki sahne", "Next scene")))
        jump_scene(app, app.selected + 1);
    place(bs);
    if (icon_button("t_end", ic::skip_fwd, ImVec2(bs, bs), false, hb, tr(app.cfg, "Sona git", "Go to end"))) {
        app.playing = false;
        app.play_t = static_cast<float>(std::max(0.0, total - 0.02));
    }

    float rx = b.x - bs * 4 - P(8) * 3;
    ImGui::SetCursorScreenPos(ImVec2(rx, cy - bs * 0.5f));
    if (icon_button("t_mute", app.mute ? ic::mute : ic::volume, ImVec2(bs, bs), app.mute, hb,
                    app.mute ? tr(app.cfg, "Sesi aç (M)", "Unmute (M)") : tr(app.cfg, "Sessize al (M)", "Mute (M)")))
        app.mute = !app.mute;
    ImGui::SetCursorScreenPos(ImVec2(rx + bs + P(8), cy - bs * 0.5f));
    if (icon_button("t_loop", ic::repeat, ImVec2(bs, bs), app.loop, hb, tr(app.cfg, "Döngü", "Loop"))) app.loop = !app.loop;
    ImGui::SetCursorScreenPos(ImVec2(rx + (bs + P(8)) * 2, cy - bs * 0.5f));
    if (icon_button("t_focus", app.focus ? ic::minimize : ic::maximize, ImVec2(bs, bs), app.focus, true,
                    tr(app.cfg, "Geniş önizleme", "Wide preview")))
        app.focus = !app.focus;
    ImGui::SetCursorScreenPos(ImVec2(rx + (bs + P(8)) * 3, cy - bs * 0.5f));
    if (icon_button("t_shot", ic::camera, ImVec2(bs, bs), false, hb, tr(app.cfg, "Kareyi PNG kaydet", "Save frame as PNG")))
        snapshot(app);
}

void draw_center(App& app, ImVec2 a, ImVec2 b) {
    panel_bg(a, b);
    ImGui::PushID("center");
    float pad = P(12);
    float hh = P(34);
    ImVec2 hp = a + ImVec2(pad, pad);
    float hw = b.x - a.x - pad * 2;
    ImGui::SetCursorScreenPos(hp);
    if (icon_button("menu", ic::list, ImVec2(hh, hh), false, true, tr(app.cfg, "Sahneler", "Scenes"))) app.page = kScenes;
    ImGui::SameLine(0.f, P(8));
    if (app.editing_title && app.has_board) {
        push_field();
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(P(10), P(7)));
        if (app.focus_title) {
            ImGui::SetKeyboardFocusHere();
            app.focus_title = false;
        }
        ImGui::SetNextItemWidth(P(260));
        bool enter = ImGui::InputText("##ptitle", &app.board.title, ImGuiInputTextFlags_EnterReturnsTrue);
        if (ImGui::IsItemEdited()) touch(app);
        if (enter || ImGui::IsItemDeactivated()) app.editing_title = false;
        ImGui::PopStyleVar();
        pop_field();
    } else {
        std::string title = app.has_board && !app.board.title.empty() ? app.board.title
                                                                        : tr(app.cfg, "İsimsiz Proje", "Untitled project");
        title = fit(F.h1, title, std::max(P(80), hw * 0.32f));
        ImVec2 ts = text_size(F.h1, title.c_str());
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("title", ImVec2(ts.x, hh));
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0) && app.has_board) {
            app.editing_title = true;
            app.focus_title = true;
        }
        draw_text(g_root, F.h1, ImVec2(p.x, p.y + (hh - ts.y) * 0.5f), kText, title.c_str());
    }
    ImGui::SameLine(0.f, P(4));
    if (icon_button("rename", ic::pencil, ImVec2(P(28), hh), false, app.has_board, tr(app.cfg, "Yeniden adlandır", "Rename"))) {
        app.editing_title = true;
        app.focus_title = true;
    }
    if (app.has_board) {
        char res[32], fps[16];
        std::snprintf(res, sizeof res, "%d\xC3\x97%d", app.board.width, app.board.height);
        std::snprintf(fps, sizeof fps, "%d FPS", app.board.fps);
        ImGui::SameLine(0.f, P(14));
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (hh - P(28)) * 0.5f);
        pill(res);
        ImGui::SameLine(0.f, P(6));
        pill(fps);
    }
    const char* prev_label = app.playing ? tr(app.cfg, "Duraklat", "Pause") : tr(app.cfg, "Önizleme", "Preview");
    const char* export_label = tr(app.cfg, "Dışa Aktar", "Export");
    float w_prev = button_width(ic::play, prev_label, kOutline);
    float w_exp = button_width(ic::upload, export_label, kPrimary);
    float right_w = hh * 2 + P(4) + P(14) + w_prev + P(8) + w_exp;
    float rx = hp.x + hw - right_w;
    ImGui::SetCursorScreenPos(ImVec2(rx, hp.y));
    if (icon_button("undo", ic::undo, ImVec2(hh, hh), false, !app.undo.empty() || app.serial != app.committed_serial,
                    tr(app.cfg, "Geri al (Ctrl+Z)", "Undo (Ctrl+Z)")))
        undo(app);
    ImGui::SetCursorScreenPos(ImVec2(rx + hh + P(4), hp.y));
    if (icon_button("redo", ic::redo, ImVec2(hh, hh), false, !app.redo.empty(), tr(app.cfg, "Yinele (Ctrl+Y)", "Redo (Ctrl+Y)")))
        redo(app);
    ImGui::SetCursorScreenPos(ImVec2(rx + hh * 2 + P(18), hp.y));
    if (button("preview", app.playing ? ic::pause : ic::play, prev_label, ImVec2(w_prev, hh), kOutline, app.has_board))
        toggle_play(app);
    ImGui::SetCursorScreenPos(ImVec2(rx + hh * 2 + P(18) + w_prev + P(8), hp.y));
    if (button("export", ic::upload, export_label, ImVec2(w_exp, hh), kPrimary, app.has_board && !app.exporting))
        ask_export(app);

    float th = P(54);
    ImVec2 wa(a.x + pad, hp.y + hh + P(10));
    ImVec2 wb(b.x - pad, b.y - pad - th);
    draw_preview(app, wa, wb);
    draw_transport(app, ImVec2(a.x + pad, wb.y + P(4)), ImVec2(b.x - pad, b.y - pad + P(2)));
    ImGui::PopID();
}

void repo_badge(App& app, const std::string& name, float w) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    float h = P(52);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, p + ImVec2(w, h), with_alpha(kAccent, 16), P(8));
    dl->AddRect(p, p + ImVec2(w, h), with_alpha(kAccent, 130), P(8));
    const char* is = I(ic::github);
    draw_text(dl, F.ui, ImVec2(p.x + P(12), p.y + (h - F.ui->FontSize) * 0.5f), kAccent, is);
    draw_text(dl, F.bold, ImVec2(p.x + P(38), p.y + P(8)), kText, fit(F.bold, name, w - P(48)).c_str());
    std::string sub = tr(app.cfg, "Proje incelenip tanıtım videosu kurgulanacak", "The project will be inspected and turned into a video");
    draw_text(dl, F.tiny, ImVec2(p.x + P(38), p.y + P(29)), kMuted, fit(F.tiny, sub, w - P(48)).c_str());
    ImGui::Dummy(ImVec2(w, h));
}

void draw_ai_panel(App& app) {
    float w = ImGui::GetContentRegionAvail().x;
    section("Prompt");
    push_field();
    ImGui::InputTextMultiline("##prompt", &app.prompt, ImVec2(w, P(104)));
    bool active = ImGui::IsItemActive();
    ImVec2 pmin = ImGui::GetItemRectMin();
    open_menu_on_right_click("prompt_menu");
    pop_field();
    if (menu_begin("prompt_menu")) {
        prompt_menu(app, app.prompt);
        menu_end();
    }
    if (app.prompt.empty() && !active) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        draw_text(dl, F.small, pmin + ImVec2(P(10), P(9)), kDim,
                  tr(app.cfg, "Bir sahne anlat ya da GitHub bağlantısı yapıştır", "Describe a scene or paste a GitHub link"));
        draw_text(dl, F.small, pmin + ImVec2(P(10), P(9) + F.small->FontSize * 1.4f), kDim,
                  tr(app.cfg, "Örnek: Titanik olayını anlatan shorts videosu", "Example: a short explaining the Titanic disaster"));
    }
    std::string owner, repo;
    if (find_github_repo(app.prompt, owner, repo)) {
        ImGui::Dummy(ImVec2(0.f, P(2)));
        repo_badge(app, owner + "/" + repo, w);
    }
    ImGui::Dummy(ImVec2(0.f, P(6)));
    float used = 0.f;
    const uint32_t style_icons[] = {ic::wand, ic::film, ic::sparkles, ic::zap, ic::globe, ic::layers, ic::activity, ic::file_text, ic::scan};
    for (int i = 0; i < 9; i++) {
        ImGui::PushID(i);
        if (wrap_chip("s", tr(app.cfg, kStyles[i].tr, kStyles[i].en), app.style_i == i, used, w, style_icons[i])) app.style_i = i;
        ImGui::PopID();
    }
    ImGui::Dummy(ImVec2(0.f, P(6)));
    if (toggle("shorts", tr(app.cfg, "Shorts  ·  dikey 9:16, 1080p", "Shorts  ·  vertical 9:16, 1080p"), app.shorts, w))
        app.shorts = !app.shorts;
    if (toggle("web", tr(app.cfg, "İnternetten araştır  ·  Vikipedi + görseller", "Research online  ·  Wikipedia + images"), app.web, w))
        app.web = !app.web;
    {
        ImGui::PushFont(F.small);
        float cw = w * 0.56f;
        std::vector<std::string> voices = {tr(app.cfg, "Otomatik", "Auto"), tr(app.cfg, "Kapalı", "Off")};
        for (const auto& v : app.voices) {
            std::string lang = v.lang == "tr" ? tr(app.cfg, "Türkçe", "Turkish")
                               : v.lang == "en" ? tr(app.cfg, "İngilizce", "English")
                                                : (v.lang.empty() ? "?" : v.lang);
            std::string name = v.name;
            if (name.rfind("Microsoft ", 0) == 0) name = name.substr(10);
            voices.push_back(lang + "  \xC2\xB7  " + name);
        }
        row_label(tr(app.cfg, "Seslendirme", "Narration"), w, cw);
        combo("##voice", app.voice_i, voices, cw);
        ImGui::PopFont();
    }
    ImGui::Dummy(ImVec2(0.f, P(8)));

    {
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImVec2 size(w, P(30));
        if (ImGui::InvisibleButton("adv", size)) app.advanced = !app.advanced;
        bool hot = ImGui::IsItemHovered();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddLine(p, ImVec2(p.x + w, p.y), kLine);
        const char* is = I(app.advanced ? ic::down : ic::right);
        draw_text(dl, F.ui, ImVec2(p.x, p.y + P(7)), kMuted, is);
        draw_text(dl, F.bold, ImVec2(p.x + P(22), p.y + P(7)), hot ? kText : kSoft, tr(app.cfg, "Gelişmiş Ayarlar", "Advanced"));
    }
    if (app.advanced) {
        ImGui::PushFont(F.small);
        float cw = w * 0.56f;
        bool tr_lang = app.cfg.lang != "en";
        std::vector<std::string> aspects = {tr(app.cfg, "Otomatik", "Auto"), tr_lang ? "16:9 (Yatay)" : "16:9 (Landscape)",
                                            tr_lang ? "9:16 (Dikey)" : "9:16 (Portrait)", tr_lang ? "1:1 (Kare)" : "1:1 (Square)"};
        row_label(tr(app.cfg, "Görünüm (Oran)", "Aspect"), w, cw);
        combo("##aspect", app.aspect_i, aspects, cw);
        std::vector<std::string> res = {"HD (720p)", "Full HD (1080p)"};
        row_label(tr(app.cfg, "Çözünürlük", "Resolution"), w, cw);
        combo("##res", app.res_i, res, cw);
        std::vector<std::string> secs;
        for (int s : kSecondsOpt)
            secs.push_back(s == 0 ? std::string(tr(app.cfg, "Otomatik", "Auto"))
                                  : std::to_string(s) + (tr_lang ? " saniye" : " seconds"));
        row_label(tr(app.cfg, "Süre", "Duration"), w, cw);
        combo("##secs", app.seconds_i, secs, cw);
        std::vector<std::string> fps = {"12 FPS", "24 FPS", "30 FPS"};
        row_label(tr(app.cfg, "Kare Hızı", "Frame rate"), w, cw);
        combo("##fps", app.fps_i, fps, cw);
        std::vector<std::string> models = {tr(app.cfg, "Kare Yerel (çevrimdışı)", "Kare Local (offline)"),
                                           "API \xC2\xB7 " + app.cfg.api_model};
        int mi = app.plan_api ? 1 : 0;
        row_label("Model", w, cw);
        if (combo("##model", mi, models, cw)) {
            app.plan_api = mi == 1;
            app.cfg.use_api = app.plan_api;
            save_config(app.cfg);
        }
        ImGui::PopFont();
    }
    ImGui::Dummy(ImVec2(0.f, P(10)));
    bool busy = app.planning.load();
    if (button("create", ic::sparkles, busy ? tr(app.cfg, "Hazırlanıyor...", "Working...") : tr(app.cfg, "Video Oluştur", "Create Video"),
               ImVec2(w, P(46)), kGlow, !app.exporting, busy))
        start_plan(app);
    ImGui::Dummy(ImVec2(0.f, P(6)));
    caption(tr(app.cfg, "Ctrl+Enter ile de oluşturabilirsin. \"Titanik'i anlatan shorts\" gibi bir konu yazarsan Kare internetten araştırır, "
                        "görselleri indirir, seslendirir, alt yazı ve müzikle kurgular. GitHub bağlantısı yapıştırırsan depodan tanıtım videosu kurar.",
               "Ctrl+Enter also works. Ask for a topic such as \"a short about the Titanic\" and Kare researches it online, downloads "
               "pictures, narrates it and edits it with captions and music. Paste a GitHub link for a project promo."),
            w);
    if (!app.last_repo.empty() && app.has_board) {
        ImGui::Dummy(ImVec2(0.f, P(8)));
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        float h = P(58);
        dl->AddRectFilled(p, p + ImVec2(w, h), kPanel2, P(8));
        dl->AddRect(p, p + ImVec2(w, h), kLine, P(8));
        draw_text(dl, F.tiny, p + ImVec2(P(12), P(8)), kMuted, tr(app.cfg, "SON İNCELENEN PROJE", "LAST INSPECTED PROJECT"));
        draw_text(dl, F.bold, p + ImVec2(P(12), P(23)), kText, fit(F.bold, app.last_repo, w - P(60)).c_str());
        draw_text(dl, F.tiny, p + ImVec2(P(12), P(41)), kAccent, fit(F.tiny, app.last_repo_note, w - P(60)).c_str());
        ImGui::SetCursorScreenPos(ImVec2(p.x + w - P(40), p.y + (h - P(30)) * 0.5f));
        if (icon_button("open_repo", ic::external, ImVec2(P(30), P(30)), false, true, tr(app.cfg, "GitHub'da aç", "Open on GitHub")))
            open_path("https://github.com/" + app.last_repo);
        ImGui::SetCursorScreenPos(p);
        ImGui::Dummy(ImVec2(w, h));
    }
}

void draw_scene_settings(App& app) {
    float w = ImGui::GetContentRegionAvail().x;
    if (!app.has_board) {
        ImGui::Dummy(ImVec2(0.f, P(10)));
        caption(tr(app.cfg, "Önce bir video oluşturun ya da bir proje açın. Sahne seçince ayarları burada görünür.",
                   "Create a video or open a project first. Selected scene settings appear here."),
                w);
        return;
    }
    int n = static_cast<int>(app.board.scenes.size());
    app.selected = std::clamp(app.selected, 0, n - 1);
    Scene& s = app.board.scenes[static_cast<size_t>(app.selected)];
    {
        ImVec2 p = ImGui::GetCursorScreenPos();
        std::string head = std::string(tr(app.cfg, "Sahne ", "Scene ")) + std::to_string(app.selected + 1) + " / " + std::to_string(n);
        draw_text(ImGui::GetWindowDrawList(), F.h1, p + ImVec2(0, P(4)), kText, head.c_str());
        ImGui::SetCursorScreenPos(ImVec2(p.x + w - P(64), p.y));
        if (icon_button("sprev", ic::left, ImVec2(P(30), P(30)), false, app.selected > 0)) jump_scene(app, app.selected - 1);
        ImGui::SameLine(0.f, P(4));
        if (icon_button("snext", ic::right, ImVec2(P(30), P(30)), false, app.selected + 1 < n)) jump_scene(app, app.selected + 1);
        ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + P(38)));
    }
    caption(tr(app.cfg, "Başlık", "Title"));
    push_field();
    ImGui::SetNextItemWidth(w);
    if (ImGui::InputText("##st", &s.title)) touch(app);
    pop_field();
    caption(tr(app.cfg, "Metin  (liste için her satıra bir madde, vurgu için *kelime*)",
               "Text  (one item per line for lists, *word* to highlight)"));
    push_field();
    if (ImGui::InputTextMultiline("##sx", &s.text, ImVec2(w, P(72)))) touch(app);
    pop_field();
    caption(tr(app.cfg, "Üst etiket", "Kicker"));
    push_field();
    ImGui::SetNextItemWidth(w);
    if (ImGui::InputTextWithHint("##sk", tr(app.cfg, "örn. Özellikler", "e.g. Features"), &s.kicker)) touch(app);
    pop_field();
    caption(tr(app.cfg, "Seslendirme metni  (anlatıcının bu sahnede söyleyeceği)", "Narration  (what the narrator says in this scene)"));
    push_field();
    if (ImGui::InputTextMultiline("##sn", &s.narration, ImVec2(w, P(64)))) touch(app);
    bool narration_done = ImGui::IsItemDeactivatedAfterEdit();
    pop_field();
    if (narration_done && app.board.voice != "none" && !s.narration.empty()) {
        fit_to_voice(app.board, app.selected);
        touch(app);
    }

    ImGui::Dummy(ImVec2(0.f, P(2)));
    ImGui::PushFont(F.small);
    {
        float cw = w * 0.56f;
        auto opt_combo = [&](const char* id, const char* tr_label, const char* en_label, const Opt* opts, size_t count, std::string& value,
                             const std::string& resolved) {
            std::vector<std::string> items;
            int cur = 0;
            for (size_t k = 0; k < count; k++) {
                std::string lab = tr(app.cfg, opts[k].tr, opts[k].en);
                if (std::string(opts[k].id) == "auto" && !resolved.empty()) lab += " \xC2\xB7 " + resolved;
                items.push_back(lab);
                if (value == opts[k].id) cur = static_cast<int>(k);
            }
            row_label(tr(app.cfg, tr_label, en_label), w, cw);
            if (combo(id, cur, items, cw)) {
                value = opts[cur].id;
                touch(app);
            }
        };
        int i = app.selected;
        opt_combo("##slayout", "Yerleşim", "Layout", kLayoutOpts, std::size(kLayoutOpts), s.layout,
                  opt_label(app.cfg, kLayoutOpts, resolve_layout(app.board, i)));
        if (i > 0)
            opt_combo("##strans", "Geçiş", "Transition", kTransitionOpts, std::size(kTransitionOpts), s.transition,
                      opt_label(app.cfg, kTransitionOpts, resolve_transition(app.board, i)));
        opt_combo("##sanim", "Yazı animasyonu", "Text animation", kAnimOpts, std::size(kAnimOpts), s.anim,
                  opt_label(app.cfg, kAnimOpts, resolve_anim(app.board, i)));
    }
    ImGui::PopFont();

    caption(tr(app.cfg, "Görsel", "Image"));
    {
        ImVec2 p = ImGui::GetCursorScreenPos();
        float h = P(40);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, p + ImVec2(w, h), IM_COL32(6, 11, 16, 255), P(8));
        dl->AddRect(p, p + ImVec2(w, h), kLine, P(8));
        bool has = !s.image.empty();
        bool ok = has && image_readable(s.image);
        const char* is = I(has ? ic::image : ic::image_plus);
        draw_text(dl, F.ui, ImVec2(p.x + P(12), p.y + (h - F.ui->FontSize) * 0.5f), has ? (ok ? kAccent : kRed) : kMuted, is);
        std::string label = has ? file_name_of(s.image)
                                : std::string(tr(app.cfg, "Ekran görüntüsü ya da fotoğraf ekle", "Add a screenshot or photo"));
        float bw = has ? P(64) : 0.f;
        draw_text(dl, F.small, ImVec2(p.x + P(38), p.y + (h - F.small->FontSize) * 0.5f), has ? kText : kMuted,
                  fit(F.small, label, w - P(48) - bw).c_str());
        if (has) {
            ImGui::SetCursorScreenPos(ImVec2(p.x + w - P(62), p.y + P(5)));
            if (icon_button("imgc", ic::folder_open, ImVec2(P(28), P(30)), false, true, tr(app.cfg, "Değiştir", "Change")))
                pick_image(app, app.selected);
            ImGui::SameLine(0.f, P(2));
            if (icon_button("imgx", ic::x, ImVec2(P(28), P(30)), false, true, tr(app.cfg, "Kaldır", "Remove"))) {
                s.image.clear();
                touch(app);
            }
            ImGui::SetCursorScreenPos(p);
            ImGui::Dummy(ImVec2(w, h));
        } else {
            ImGui::SetCursorScreenPos(p);
            if (ImGui::InvisibleButton("imgadd", ImVec2(w, h))) pick_image(app, app.selected);
            if (ImGui::IsItemHovered()) dl->AddRect(p, p + ImVec2(w, h), with_alpha(kAccent, 150), P(8));
        }
    }
    caption(tr(app.cfg, "Süre", "Duration"));
    float sec = static_cast<float>(s.seconds);
    push_field();
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, v4(kAccent));
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, v4(kAccent));
    ImGui::SetNextItemWidth(w);
    if (ImGui::SliderFloat("##ss", &sec, 0.4f, static_cast<float>(kMaxSceneSeconds), app.cfg.lang == "en" ? "%.1f s" : "%.1f sn")) {
        s.seconds = sec;
        touch(app);
    }
    ImGui::PopStyleColor(2);
    pop_field();
    caption(tr(app.cfg, "Kamera hareketi", "Camera motion"));
    {
        float mused = 0.f;
        for (int i = 0; i < static_cast<int>(std::size(kMotionOpts)); i++) {
            ImGui::PushID(300 + i);
            if (wrap_chip("m", tr(app.cfg, kMotionOpts[i].tr, kMotionOpts[i].en), s.motion == kMotionOpts[i].id, mused, w)) {
                s.motion = kMotionOpts[i].id;
                touch(app);
            }
            ImGui::PopID();
        }
    }
    ImGui::Dummy(ImVec2(0.f, P(2)));
    caption(tr(app.cfg, "Görünüm", "Look"));
    float used = 0.f;
    for (int i = 0; i < 9; i++) {
        bool on = i == 0 ? s.style.empty() : s.style == kStyles[i].id;
        const char* label = i == 0 ? tr(app.cfg, "Videodan", "From video") : tr(app.cfg, kStyles[i].tr, kStyles[i].en);
        ImGui::PushID(400 + i);
        if (wrap_chip("ls", label, on, used, w)) {
            s.style = kStyles[i].id;
            touch(app);
        }
        ImGui::PopID();
    }

    divider(w);
    section(tr(app.cfg, "Video", "Video"));
    ImGui::PushFont(F.small);
    float cw = w * 0.52f;
    std::vector<std::string> aspects = {"16:9", "9:16", "1:1"};
    int ai = app.board.aspect == "9:16" ? 1 : app.board.aspect == "1:1" ? 2 : 0;
    row_label(tr(app.cfg, "Oran", "Aspect"), w, cw);
    if (combo("##baspect", ai, aspects, cw)) {
        app.board.aspect = aspects[static_cast<size_t>(ai)];
        apply_res(app.board, res_of(app.board));
        touch(app);
    }
    std::vector<std::string> res = {"HD (720p)", "Full HD (1080p)"};
    int ri = res_of(app.board);
    row_label(tr(app.cfg, "Çözünürlük", "Resolution"), w, cw);
    if (combo("##bres", ri, res, cw)) {
        apply_res(app.board, ri);
        touch(app);
    }
    std::vector<std::string> fps = {"12 FPS", "24 FPS", "30 FPS"};
    int fi = fps_index(app.board.fps);
    row_label(tr(app.cfg, "Kare hızı", "Frame rate"), w, cw);
    if (combo("##bfps", fi, fps, cw)) {
        app.board.fps = kFpsOpt[fi];
        touch(app);
    }
    row_label(tr(app.cfg, "Toplam süre", "Total length"), w, cw);
    push_field();
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(P(10), P(6)));
    ImGui::SetNextItemWidth(cw * 0.45f);
    ImGui::InputFloat("##fit", &app.fit_seconds, 0.f, 0.f, "%.0f");
    ImGui::PopStyleVar();
    pop_field();
    ImGui::SameLine(0.f, P(6));
    if (button("fitb", 0, tr(app.cfg, "Dağıt", "Fit"), ImVec2(cw * 0.55f - P(6), P(30)), kOutline)) {
        scale_duration(app.board, std::clamp(app.fit_seconds, 1.f, static_cast<float>(kMaxTotalSeconds)));
        normalize(app.board);
        touch(app);
    }
    ImGui::PopFont();
    if (toggle("mark", tr(app.cfg, "Köşede KARE işareti", "KARE mark in corner"), app.board.mark, w)) {
        app.board.mark = !app.board.mark;
        touch(app);
    }
    if (toggle("grain", tr(app.cfg, "Film greni", "Film grain"), app.board.grain, w)) {
        app.board.grain = !app.board.grain;
        touch(app);
    }

    divider(w);
    section(tr(app.cfg, "Müzik", "Music"));
    ImGui::PushFont(F.small);
    {
        float mw = w * 0.52f;
        std::vector<std::string> items;
        std::string resolved = opt_label(app.cfg, kMusicOpts, resolve_music(app.board));
        for (const auto& o : kMusicOpts) {
            std::string lab = tr(app.cfg, o.tr, o.en);
            if (std::string(o.id) == "auto") lab += " \xC2\xB7 " + resolved;
            items.push_back(lab);
        }
        int mi = opt_index(kMusicOpts, app.board.music);
        row_label(tr(app.cfg, "Parça", "Track"), w, mw);
        if (combo("##music", mi, items, mw)) {
            app.board.music = kMusicOpts[mi].id;
            touch(app);
        }
    }
    ImGui::PopFont();
    {
        bool silent = resolve_music(app.board) == "none";
        char info[96];
        if (silent && has_narration(app.board))
            std::snprintf(info, sizeof info, "%s", tr(app.cfg, "Müzik yok, yalnızca seslendirme.", "No music, narration only."));
        else if (silent)
            std::snprintf(info, sizeof info, "%s", tr(app.cfg, "Bu video sessiz dışa aktarılır.", "This video exports without sound."));
        else
            std::snprintf(info, sizeof info, tr(app.cfg, "%.0f BPM  ·  geçişlerde whoosh, kapanışta riser", "%.0f BPM  ·  whooshes on cuts, riser before the end"),
                          60.0 / beat_seconds(app.board));
        caption(info, w);
        ImGui::Dummy(ImVec2(0.f, P(2)));
        float bw2 = (w - P(8)) * 0.5f;
        if (button("snap", ic::timer, tr(app.cfg, "Ritme oturt", "Snap to beat"), ImVec2(bw2, P(34)), kOutline, !silent)) {
            snap_to_beat(app.board, total_seconds(app.board));
            touch(app);
        }
        ImGui::SameLine(0.f, P(8));
        if (button("wav", ic::file_audio, tr(app.cfg, "WAV kaydet", "Save WAV"), ImVec2(bw2, P(34)), kOutline,
                   !silent || has_narration(app.board)))
            save_soundtrack(app);
    }

    divider(w);
    section(tr(app.cfg, "Seslendirme", "Narration"));
    ImGui::PushFont(F.small);
    {
        float vw = w * 0.56f;
        std::vector<std::string> items = {tr(app.cfg, "Kapalı", "Off")};
        std::vector<std::string> ids = {"none"};
        int vi = 0;
        for (const auto& v : app.voices) {
            std::string name = v.name;
            if (name.rfind("Microsoft ", 0) == 0) name = name.substr(10);
            items.push_back((v.lang.empty() ? std::string("?") : v.lang) + "  \xC2\xB7  " + name);
            ids.push_back(v.name);
        }
        for (size_t k = 1; k < ids.size(); k++) {
            const VoiceInfo& v = app.voices[k - 1];
            if (app.board.voice == v.name || (app.board.voice == v.lang && vi == 0)) vi = static_cast<int>(k);
        }
        row_label(tr(app.cfg, "Ses", "Voice"), w, vw);
        if (combo("##bvoice", vi, items, vw)) {
            app.board.voice = ids[static_cast<size_t>(vi)];
            if (app.board.voice != "none") fit_to_voice(app.board);
            touch(app);
        }
        bool on = app.board.voice != "none";
        int rate = app.board.voice_rate;
        row_label(tr(app.cfg, "Hız", "Speed"), w, vw);
        push_field();
        ImGui::SetNextItemWidth(vw);
        ImGui::SliderInt("##vrate", &rate, -5, 6, rate > 0 ? "+%d" : "%d");
        bool rate_done = ImGui::IsItemDeactivatedAfterEdit();
        pop_field();
        if (rate != app.board.voice_rate) {
            app.board.voice_rate = rate;
            touch(app);
        }
        if (rate_done && on) {
            fit_to_voice(app.board);
            touch(app);
        }
        int ci = opt_index(kCaptionOpts, app.board.captions);
        std::vector<std::string> caps;
        for (const auto& o : kCaptionOpts) caps.push_back(tr(app.cfg, o.tr, o.en));
        row_label(tr(app.cfg, "Alt yazı", "Captions"), w, vw);
        if (combo("##bcaps", ci, caps, vw)) {
            app.board.captions = kCaptionOpts[ci].id;
            touch(app);
        }
        ImGui::Dummy(ImVec2(0.f, P(2)));
        if (button("vfit", ic::timer, tr(app.cfg, "Süreleri sese uydur", "Fit scenes to the voice"), ImVec2(w, P(34)), kOutline,
                   on && has_narration(app.board))) {
            fit_to_voice(app.board);
            touch(app);
        }
    }
    ImGui::PopFont();
    if (app.voices.empty())
        caption(tr(app.cfg, "Bu bilgisayarda yüklü konuşma sesi bulunamadı (Ayarlar > Zaman ve dil > Konuşma).",
                   "No speech voices are installed (Settings > Time & language > Speech)."),
                w);
    if (!app.board.credits.empty()) {
        ImGui::Dummy(ImVec2(0.f, P(4)));
        caption(tr(app.cfg, "Kaynaklar ve lisanslar  (dışa aktarınca .credits.txt olarak yanına yazılır)",
                   "Sources and licenses  (written next to the export as .credits.txt)"));
        push_field();
        ImGui::InputTextMultiline("##credits", &app.board.credits, ImVec2(w, P(90)), ImGuiInputTextFlags_ReadOnly);
        pop_field();
    }

    divider(w);
    section(tr(app.cfg, "Proje", "Project"));
    float bw = (w - P(8)) * 0.5f;
    if (button("psave", ic::save, tr(app.cfg, "Kaydet", "Save"), ImVec2(bw, P(36)), kOutline)) save_project(app, false);
    ImGui::SameLine(0.f, P(8));
    if (button("popen", ic::folder_open, tr(app.cfg, "Aç", "Open"), ImVec2(bw, P(36)), kOutline)) open_project(app);
    if (!app.project_path.empty()) caption(fit(F.small, app.project_path, w).c_str());
}

void draw_right(App& app, ImVec2 a, ImVec2 b) {
    panel_bg(a, b);
    ImGui::PushID("right");
    float pad = P(10);
    float tw = (b.x - a.x - pad * 2 - P(6)) * 0.5f;
    ImGui::SetCursorScreenPos(a + ImVec2(pad, pad));
    if (panel_tab("t0", ic::sparkles, tr(app.cfg, "Yapay Zekâ ile Oluştur", "Create with AI"), app.page != kEdit, tw))
        app.page = kCreate;
    ImGui::SameLine(0.f, P(6));
    if (panel_tab("t1", ic::sliders, tr(app.cfg, "Sahne Ayarları", "Scene Settings"), app.page == kEdit, tw)) app.page = kEdit;
    ImVec2 ba(a.x + P(2), a.y + pad + P(40) + P(8));
    ImGui::SetCursorScreenPos(ba);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(P(12), P(6)));
    ImGui::BeginChild("rbody", ImVec2(b.x - ba.x - P(2), b.y - ba.y - P(6)), ImGuiChildFlags_AlwaysUseWindowPadding, 0);
    if (app.page == kEdit) draw_scene_settings(app);
    else draw_ai_panel(app);
    ImGui::Dummy(ImVec2(0.f, P(6)));
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopID();
}

void draw_timeline(App& app, ImVec2 a, ImVec2 b) {
    panel_bg(a, b);
    ImGui::PushID("timeline");
    bool hb = app.has_board;
    float pad = P(10);
    float tb = P(30);
    ImGui::SetCursorScreenPos(a + ImVec2(pad, pad));
    struct Tool {
        const char* id;
        uint32_t icon;
        const char* tr;
        const char* en;
    };
    const Tool tools[] = {{"split", ic::scissors, "Oynatma çizgisinden böl", "Split at playhead"},
                          {"dup", ic::copy, "Sahneyi çoğalt", "Duplicate scene"},
                          {"del", ic::trash, "Sahneyi sil", "Delete scene"},
                          {"add", ic::plus, "Sahne ekle", "Add scene"},
                          {"mvl", ic::arrow_left, "Sola taşı", "Move left"},
                          {"mvr", ic::arrow_right, "Sağa taşı", "Move right"}};
    for (int i = 0; i < 6; i++) {
        if (i) ImGui::SameLine(0.f, i == 3 ? P(14) : P(4));
        if (i == 3) {
            ImVec2 p = ImGui::GetCursorScreenPos();
            g_root->AddLine(ImVec2(p.x - P(8), p.y + P(6)), ImVec2(p.x - P(8), p.y + tb - P(6)), kLine);
        }
        bool enabled = hb && !(i == 2 && app.board.scenes.size() <= 1) && !(i == 4 && app.selected <= 0) &&
                       !(i == 5 && app.selected + 1 >= static_cast<int>(app.board.scenes.size())) &&
                       !((i == 0 || i == 1 || i == 3) && scene_full(app));
        if (icon_button(tools[i].id, tools[i].icon, ImVec2(tb, tb), false, enabled, tr(app.cfg, tools[i].tr, tools[i].en))) {
            if (i == 0) split_at_playhead(app);
            else if (i == 1) scene_duplicate(app, app.selected);
            else if (i == 2) scene_delete(app, app.selected);
            else if (i == 3) scene_insert(app, app.selected + 1);
            else if (i == 4) scene_move(app, app.selected, app.selected - 1);
            else if (i == 5) scene_move(app, app.selected, app.selected + 1);
        }
    }
    {
        float zx = b.x - pad - tb * 3 - P(8) - P(54);
        ImGui::SetCursorScreenPos(ImVec2(zx, a.y + pad));
        if (icon_button("zout", ic::zoom_out, ImVec2(tb, tb), false, hb && app.tl_zoom > 1.01f, tr(app.cfg, "Uzaklaştır", "Zoom out")))
            app.tl_zoom = std::max(1.f, app.tl_zoom / 1.4f);
        char z[16];
        std::snprintf(z, sizeof z, "%d%%", static_cast<int>(app.tl_zoom * 100.f + 0.5f));
        float zw = text_size(F.small, z).x;
        draw_text(g_root, F.small, ImVec2(zx + tb + P(4) + (P(54) - zw) * 0.5f, a.y + pad + (tb - F.small->FontSize) * 0.5f), kMuted, z);
        ImGui::SetCursorScreenPos(ImVec2(zx + tb + P(4) + P(54), a.y + pad));
        if (icon_button("zin", ic::zoom_in, ImVec2(tb, tb), false, hb && app.tl_zoom < 8.f, tr(app.cfg, "Yakınlaştır", "Zoom in")))
            app.tl_zoom = std::min(8.f, app.tl_zoom * 1.4f);
        ImGui::SetCursorScreenPos(ImVec2(zx + tb * 2 + P(8) + P(54), a.y + pad));
        if (icon_button("zfit", ic::scan, ImVec2(tb, tb), false, hb, tr(app.cfg, "Sığdır", "Fit"))) app.tl_zoom = 1.f;
    }

    float top = a.y + pad + tb + P(8);
    float hc = P(132);
    float ruler_h = P(24), gap = P(6);
    float lanes_h = b.y - pad - top - ruler_h - gap * 3 - P(14);
    float video_h = std::max(P(40), lanes_h * 0.52f);
    float sub_h = std::max(P(24), lanes_h * 0.2f);
    float music_h = std::max(P(26), lanes_h - video_h - sub_h);
    float y_video = top + ruler_h + gap;
    float y_sub = y_video + video_h + gap;
    float y_music = y_sub + sub_h + gap;
    struct Head {
        uint32_t icon;
        const char* tr;
        const char* en;
        float y;
        float h;
    };
    const Head heads[] = {{ic::video, "Video 1", "Video 1", y_video, video_h},
                          {ic::captions, "Altyazı", "Captions", y_sub, sub_h},
                          {ic::music, "Müzik", "Music", y_music, music_h}};
    for (int k = 0; k < 3; k++) {
        const Head& h = heads[k];
        ImVec2 ha(a.x + pad, h.y), hb2(a.x + pad + hc - P(8), h.y + h.h);
        g_root->AddRectFilled(ha, hb2, kPanel2, P(6));
        g_root->AddRect(ha, hb2, kLine, P(6));
        const char* is = I(h.icon);
        float cy = h.y + h.h * 0.5f;
        draw_text(g_root, F.ui, ImVec2(ha.x + P(10), cy - F.ui->FontSize * 0.5f), kMuted, is);
        draw_text(g_root, F.small, ImVec2(ha.x + P(34), cy - F.small->FontSize * 0.5f), kSoft, tr(app.cfg, h.tr, h.en));
        if (k == 2 && hb) {
            ImGui::SetCursorScreenPos(ImVec2(hb2.x - P(28), cy - P(12)));
            if (icon_button("lane_mute", app.mute ? ic::mute : ic::volume, ImVec2(P(24), P(24)), app.mute, true,
                            app.mute ? tr(app.cfg, "Sesi aç", "Unmute") : tr(app.cfg, "Sessize al", "Mute")))
                app.mute = !app.mute;
        }
    }
    if (hb) {
        std::string tot = clock_text(total_seconds(app.board));
        draw_text(g_root, F.tiny, ImVec2(a.x + pad + P(10), top + (ruler_h - F.tiny->FontSize) * 0.5f), kDim,
                  (std::string(tr(app.cfg, "Toplam ", "Total ")) + tot).c_str());
    }

    ImVec2 la(a.x + pad + hc, top), lb(b.x - pad, b.y - pad);
    ImGui::SetCursorScreenPos(la);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, P(8));
    ImGui::BeginChild("lanes", lb - la, 0, ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float view_w = ImGui::GetWindowWidth();
    double total = hb ? std::max(0.5, total_seconds(app.board)) : 10.0;
    float fit_pps = (view_w - P(20)) / static_cast<float>(total);
    float pps = app.frozen_pps > 0 ? app.frozen_pps : fit_pps * app.tl_zoom;
    float content_w = std::max(view_w, static_cast<float>(total) * pps + P(20));
    ImVec2 o = ImGui::GetCursorScreenPos();
    float vy = o.y + ruler_h + gap, sy = vy + video_h + gap, my = sy + sub_h + gap;

    if (ImGui::IsWindowHovered()) {
        ImGuiIO& io = ImGui::GetIO();
        if (io.KeyCtrl && io.MouseWheel != 0.f && hb) {
            app.tl_zoom = std::clamp(app.tl_zoom * (io.MouseWheel > 0 ? 1.2f : 1.f / 1.2f), 1.f, 8.f);
        } else if (io.MouseWheel != 0.f || io.MouseWheelH != 0.f) {
            ImGui::SetScrollX(ImGui::GetScrollX() - (io.MouseWheel + io.MouseWheelH) * P(60));
        }
    }

    ImGui::SetCursorScreenPos(o);
    ImGui::InvisibleButton("ruler", ImVec2(content_w, ruler_h));
    if (ImGui::IsItemActive() && hb) {
        app.play_t = std::clamp((ImGui::GetIO().MousePos.x - o.x) / pps, 0.f, static_cast<float>(total) - 0.01f);
        app.playing = false;
        app.selected = scene_at(app.board, app.play_t);
    }
    dl->AddRectFilled(o, ImVec2(o.x + content_w, o.y + ruler_h), kPanel2, P(4));
    const float steps[] = {0.5f, 1.f, 2.f, 5.f, 10.f, 15.f, 30.f, 60.f};
    float step = 60.f;
    for (float s : steps) {
        if (s * pps >= P(64)) {
            step = s;
            break;
        }
    }
    float minor = step / 5.f;
    for (float t = 0.f; t <= static_cast<float>(total) + 0.001f; t += minor) {
        float x = o.x + t * pps;
        bool major = std::fmod(t + 0.001f, step) < 0.01f;
        dl->AddLine(ImVec2(x, o.y + ruler_h - (major ? P(9) : P(4))), ImVec2(x, o.y + ruler_h), major ? kMuted : kDim, 1.f);
        if (major) {
            char lab[16];
            int tenths = static_cast<int>(t * 10.f + 0.5f), s = tenths / 10;
            if (tenths % 10) std::snprintf(lab, sizeof lab, "%d:%02d.%d", s / 60, s % 60, tenths % 10);
            else std::snprintf(lab, sizeof lab, "%d:%02d", s / 60, s % 60);
            draw_text(dl, F.tiny, ImVec2(x + P(3), o.y + P(3)), kMuted, lab);
        }
    }
    dl->AddRectFilled(ImVec2(o.x, vy), ImVec2(o.x + content_w, vy + video_h), IM_COL32(7, 12, 17, 255), P(6));
    dl->AddRectFilled(ImVec2(o.x, sy), ImVec2(o.x + content_w, sy + sub_h), IM_COL32(7, 12, 17, 255), P(6));
    dl->AddRectFilled(ImVec2(o.x, my), ImVec2(o.x + content_w, my + music_h), IM_COL32(7, 12, 17, 255), P(6));

    if (!hb) {
        const char* msg = tr(app.cfg, "Sahneler burada görünecek", "Scenes will appear here");
        ImVec2 ts = text_size(F.small, msg);
        draw_text(dl, F.small, ImVec2(o.x + (view_w - ts.x) * 0.5f, vy + (video_h - ts.y) * 0.5f), kDim, msg);
    } else {
        refresh_thumbs(app);
        double acc = 0;
        bool any_edge = false;
        int n = static_cast<int>(app.board.scenes.size());
        int open_scene_menu = -1, open_trans_menu = -1, hot_trans = -1;
        {
            double at = 0;
            for (int i = 0; i < n; i++) {
                if (i > 0) {
                    float r = P(9);
                    ImVec2 m(o.x + static_cast<float>(at) * pps, vy + video_h * 0.5f);
                    ImGui::PushID(1000 + i);
                    ImGui::SetCursorScreenPos(ImVec2(m.x - r, m.y - r));
                    if (ImGui::InvisibleButton("trn", ImVec2(r * 2, r * 2))) open_trans_menu = i;
                    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) open_trans_menu = i;
                    if (ImGui::IsItemHovered()) {
                        hot_trans = i;
                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
                            std::string tip = std::string(tr(app.cfg, "Geçiş: ", "Transition: ")) +
                                              opt_label(app.cfg, kTransitionOpts, resolve_transition(app.board, i));
                            ImGui::SetTooltip("%s", tip.c_str());
                        }
                    }
                    ImGui::PopID();
                }
                at += app.board.scenes[static_cast<size_t>(i)].seconds;
            }
        }
        for (int i = 0; i < n; i++) {
            Scene& s = app.board.scenes[static_cast<size_t>(i)];
            float x0 = o.x + static_cast<float>(acc) * pps;
            float x1 = o.x + static_cast<float>(acc + s.seconds) * pps;
            ImVec2 ca(x0 + P(1), vy), cb(x1 - P(1), vy + video_h);
            float edge = std::min(P(8), (cb.x - ca.x) * 0.3f);
            float body = std::max(1.f, cb.x - ca.x - edge);
            ImGui::PushID(i);
            ImGui::SetCursorScreenPos(ca);
            if (ImGui::InvisibleButton("clip", ImVec2(body, video_h))) {
                app.selected = i;
                app.play_t = static_cast<float>(acc + 0.02);
                app.playing = false;
            }
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                app.selected = i;
                open_scene_menu = i;
            }
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) app.page = kEdit;
            bool hot = ImGui::IsItemHovered();
            ImGui::SetCursorScreenPos(ImVec2(ca.x + body, ca.y));
            ImGui::InvisibleButton("edge", ImVec2(edge, video_h));
            if (ImGui::IsItemHovered() || ImGui::IsItemActive()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            if (ImGui::IsItemActivated()) app.frozen_pps = pps;
            if (ImGui::IsItemActive()) {
                any_edge = true;
                float dx = ImGui::GetIO().MouseDelta.x;
                if (dx != 0.f) {
                    s.seconds = std::clamp(s.seconds + dx / pps, 0.4, 20.0);
                    app.selected = i;
                    touch(app);
                }
            }
            bool sel = app.selected == i;
            dl->PushClipRect(ca, cb, true);
            dl->AddRectFilled(ca, cb, kRaised);
            if (i < static_cast<int>(app.thumbs.size()) && app.thumbs[static_cast<size_t>(i)]) {
                float ta = static_cast<float>(app.thumb_w) / static_cast<float>(std::max(1, app.thumb_h));
                float ca_r = (cb.x - ca.x) / std::max(1.f, cb.y - ca.y);
                ImVec2 uv0(0.f, 0.f), uv1(1.f, 1.f);
                if (ca_r > ta) {
                    float v = ta / ca_r;
                    uv0.y = (1.f - v) * 0.5f;
                    uv1.y = (1.f + v) * 0.5f;
                } else {
                    float u = ca_r / ta;
                    uv0.x = (1.f - u) * 0.5f;
                    uv1.x = (1.f + u) * 0.5f;
                }
                dl->AddImage((ImTextureID)(intptr_t)app.thumbs[static_cast<size_t>(i)], ca, cb, uv0, uv1);
            }
            dl->AddRectFilledMultiColor(ImVec2(ca.x, cb.y - P(24)), cb, IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0),
                                        IM_COL32(0, 0, 0, 200), IM_COL32(0, 0, 0, 200));
            std::string name = s.title.empty() ? s.text : s.title;
            std::string lab = std::to_string(i + 1) + "  " + name;
            draw_text(dl, F.tiny, ImVec2(ca.x + P(6), cb.y - P(18)), IM_COL32_WHITE, fit(F.tiny, lab, cb.x - ca.x - P(12)).c_str());
            dl->PopClipRect();
            if (sel) {
                glow(dl, ca, cb, P(5), kAccent, 1.f);
                dl->AddRect(ca, cb, kAccent, P(5), 0, P(2));
            } else {
                dl->AddRect(ca, cb, hot ? with_alpha(IM_COL32_WHITE, 120) : IM_COL32(0, 0, 0, 160), P(5), 0, 1.f);
            }
            dl->AddRectFilled(ImVec2(cb.x - P(3), ca.y + video_h * 0.35f), ImVec2(cb.x - P(1), ca.y + video_h * 0.65f),
                              with_alpha(IM_COL32_WHITE, sel || hot ? 200 : 80), P(1));
            if (i > 0) {
                ImVec2 m(x0, vy + video_h * 0.5f);
                bool th = hot_trans == i;
                float r = th ? P(8) : P(6);
                bool custom = s.transition != "auto";
                if (th) glow(dl, ImVec2(m.x - r, m.y - r), ImVec2(m.x + r, m.y + r), r, kAccent, 0.8f);
                dl->AddQuadFilled(ImVec2(m.x, m.y - r), ImVec2(m.x + r, m.y), ImVec2(m.x, m.y + r), ImVec2(m.x - r, m.y),
                                  custom ? kAccent : kPanel2);
                dl->AddQuad(ImVec2(m.x, m.y - r), ImVec2(m.x + r, m.y), ImVec2(m.x, m.y + r), ImVec2(m.x - r, m.y),
                            th ? kAccent : with_alpha(kAccent, 160), 1.f);
            }

            ImVec2 sa(x0 + P(2), sy + P(3)), sb2(x1 - P(2), sy + sub_h - P(3));
            ImGui::SetCursorScreenPos(sa);
            if (ImGui::InvisibleButton("sub", ImVec2(std::max(1.f, sb2.x - sa.x), std::max(1.f, sb2.y - sa.y)))) {
                app.selected = i;
                app.play_t = static_cast<float>(acc + 0.02);
                app.playing = false;
                app.page = kEdit;
            }
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                app.selected = i;
                open_scene_menu = i;
            }
            bool shot = ImGui::IsItemHovered();
            dl->AddRectFilled(sa, sb2, with_alpha(kAccent, sel ? 40 : shot ? 26 : 14), P(5));
            dl->AddRect(sa, sb2, with_alpha(kAccent, sel ? 220 : 70), P(5));
            std::string cap = !s.narration.empty() && app.board.voice != "none"
                                  ? std::string(I(ic::volume)) + "  " + s.narration
                                  : std::string(I(ic::type)) + "  " + (s.title.empty() ? s.text : s.title);
            dl->PushClipRect(sa, sb2, true);
            draw_text(dl, F.tiny, ImVec2(sa.x + P(8), (sa.y + sb2.y - F.tiny->FontSize) * 0.5f), sel ? kText : kSoft,
                      fit(F.tiny, cap, sb2.x - sa.x - P(14)).c_str());
            dl->PopClipRect();
            ImGui::PopID();
            acc += s.seconds;
        }
        if (!any_edge) app.frozen_pps = 0.f;

        {
            float lane_w = static_cast<float>(total) * pps;
            ImVec2 ma(o.x + P(1), my), mb(o.x + lane_w - P(1), my + music_h);
            ImGui::SetCursorScreenPos(ma);
            ImGui::InvisibleButton("music", ImVec2(std::max(1.f, mb.x - ma.x), music_h));
            bool mhot = ImGui::IsItemHovered();
            if (ImGui::IsItemActive()) {
                app.play_t = std::clamp((ImGui::GetIO().MousePos.x - o.x) / pps, 0.f, static_cast<float>(total) - 0.01f);
                app.playing = false;
                app.selected = scene_at(app.board, app.play_t);
            }
            open_menu_on_right_click("music_menu");
            std::string mood = resolve_music(app.board);
            bool silent = mood == "none";
            bool fresh = app.track_serial == app.serial && !app.peaks.empty();
            ImU32 lane_col = silent ? kDim : IM_COL32(150, 120, 255, 255);
            dl->AddRectFilled(ma, mb, with_alpha(lane_col, silent ? 10 : mhot ? 30 : 20), P(5));
            dl->AddRect(ma, mb, with_alpha(lane_col, silent ? 40 : 90), P(5));
            if (!silent) {
                double beat = beat_seconds(app.board);
                for (int k = 0; beat > 0.05 && k * beat * 4 < total; k++) {
                    float bx = o.x + static_cast<float>(k * beat * 4) * pps;
                    dl->AddLine(ImVec2(bx, ma.y + P(2)), ImVec2(bx, mb.y - P(2)), with_alpha(lane_col, 36), 1.f);
                }
            }
            if (fresh && !silent) {
                float cy = (ma.y + mb.y) * 0.5f;
                float amp = (music_h - P(8)) * 0.5f;
                float clip_l = o.x + ImGui::GetScrollX() - P(4), clip_r = clip_l + view_w + P(8);
                float px_play = o.x + app.play_t * pps;
                for (float x = std::max(ma.x, clip_l); x < std::min(mb.x, clip_r); x += P(2)) {
                    float u = (x - o.x) / std::max(1.f, lane_w);
                    size_t idx = std::min(app.peaks.size() - 1, static_cast<size_t>(std::max(0.f, u) * app.peaks.size()));
                    float v = std::max(0.04f, app.peaks[idx]);
                    ImU32 c = x < px_play ? with_alpha(lane_col, 230) : with_alpha(lane_col, 130);
                    dl->AddLine(ImVec2(x, cy - v * amp), ImVec2(x, cy + v * amp), c, P(1.2f));
                }
            }
            std::string label = std::string(I(ic::music)) + "  " + opt_label(app.cfg, kMusicOpts, mood);
            if (!silent) {
                char bpm[24];
                std::snprintf(bpm, sizeof bpm, "  \xC2\xB7  %.0f BPM", 60.0 / beat_seconds(app.board));
                label += bpm;
                if (!fresh) label += tr(app.cfg, "  \xC2\xB7  hazırlanıyor...", "  \xC2\xB7  preparing...");
                if (app.mute) label += tr(app.cfg, "  \xC2\xB7  sessiz", "  \xC2\xB7  muted");
            }
            float lx = std::max(ma.x, o.x + ImGui::GetScrollX()) + P(8);
            ImVec2 ls = text_size(F.tiny, label.c_str());
            dl->AddRectFilled(ImVec2(lx - P(5), ma.y + P(4)), ImVec2(lx + ls.x + P(5), ma.y + P(4) + ls.y + P(4)), IM_COL32(7, 12, 17, 210), P(4));
            draw_text(dl, F.tiny, ImVec2(lx, ma.y + P(6)), silent ? kDim : kText, label.c_str());
        }

        if (open_scene_menu >= 0) {
            app.ctx_scene = open_scene_menu;
            ImGui::OpenPopup("clip_menu");
        }
        if (open_trans_menu >= 0) {
            app.ctx_scene = open_trans_menu;
            app.selected = open_trans_menu;
            ImGui::OpenPopup("trans_menu");
        }
        if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && !ImGui::IsAnyItemHovered())
            ImGui::OpenPopup("lane_menu");
        if (menu_begin("clip_menu")) {
            scene_menu(app, app.ctx_scene);
            menu_end();
        }
        if (menu_begin("trans_menu")) {
            int ci = app.ctx_scene;
            if (ci > 0 && ci < static_cast<int>(app.board.scenes.size())) {
                Scene& cs = app.board.scenes[static_cast<size_t>(ci)];
                menu_heading(std::string(tr(app.cfg, "GEÇİŞ  ", "TRANSITION  ")) + std::to_string(ci) + " \xE2\x86\x92 " + std::to_string(ci + 1));
                std::string resolved = opt_label(app.cfg, kTransitionOpts, resolve_transition(app.board, ci));
                for (const auto& o2 : kTransitionOpts) {
                    std::string lab = tr(app.cfg, o2.tr, o2.en);
                    if (std::string(o2.id) == "auto") lab += "  \xC2\xB7  " + resolved;
                    if (menu_item(std::string(o2.id) == "auto" ? ic::wand : ic::blend, lab.c_str(), nullptr, cs.transition == o2.id)) {
                        cs.transition = o2.id;
                        touch(app);
                    }
                }
            }
            menu_end();
        }
        if (menu_begin("music_menu")) {
            music_menu_items(app);
            menu_end();
        }
        if (menu_begin("lane_menu")) {
            if (menu_item(app.playing ? ic::pause : ic::play, app.playing ? tr(app.cfg, "Duraklat", "Pause") : tr(app.cfg, "Oynat", "Play"), "Space"))
                toggle_play(app);
            if (menu_item(ic::plus, tr(app.cfg, "Sona sahne ekle", "Add scene at end"), nullptr, false, !scene_full(app)))
                scene_insert(app, static_cast<int>(app.board.scenes.size()));
            if (menu_item(ic::scissors, tr(app.cfg, "Oynatma çizgisinden böl", "Split at playhead"), "S", false, !scene_full(app)))
                split_at_playhead(app);
            ImGui::Separator();
            if (menu_item(ic::zoom_in, tr(app.cfg, "Yakınlaştır", "Zoom in"), "Ctrl+Wheel", false, app.tl_zoom < 8.f))
                app.tl_zoom = std::min(8.f, app.tl_zoom * 1.4f);
            if (menu_item(ic::zoom_out, tr(app.cfg, "Uzaklaştır", "Zoom out"), nullptr, false, app.tl_zoom > 1.01f))
                app.tl_zoom = std::max(1.f, app.tl_zoom / 1.4f);
            if (menu_item(ic::scan, tr(app.cfg, "Sığdır", "Fit"))) app.tl_zoom = 1.f;
            ImGui::Separator();
            music_menu_items(app);
            menu_end();
        }

        float px = o.x + app.play_t * pps;
        dl->AddLine(ImVec2(px, o.y + P(2)), ImVec2(px, my + music_h), kAccent, P(1.5f));
        dl->AddRectFilled(ImVec2(px - P(5), o.y), ImVec2(px + P(5), o.y + P(8)), kAccent, P(2));
        dl->AddTriangleFilled(ImVec2(px - P(5), o.y + P(8)), ImVec2(px + P(5), o.y + P(8)), ImVec2(px, o.y + P(13)), kAccent);
        if (app.playing) {
            float sx = ImGui::GetScrollX();
            float local = app.play_t * pps;
            if (local < sx || local > sx + view_w - P(30)) ImGui::SetScrollX(std::max(0.f, local - view_w * 0.2f));
        }
    }
    ImGui::SetCursorScreenPos(o);
    ImGui::Dummy(ImVec2(content_w, my + music_h - o.y));
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopID();
}

void draw_studio(App& app, ImVec2 a, ImVec2 b) {
    float G = P(10);
    float tl = std::clamp((b.y - a.y) * 0.32f, P(200), P(280));
    ImVec2 ta(a.x, b.y - tl);
    float rp = app.focus ? 0.f : std::clamp((b.x - a.x) * 0.28f, P(320), P(390));
    ImVec2 cb(b.x - (rp > 0 ? rp + G : 0.f), ta.y - G);
    draw_center(app, a, cb);
    if (rp > 0) draw_right(app, ImVec2(cb.x + G, a.y), ImVec2(b.x, ta.y - G));
    draw_timeline(app, ta, b);
}

// ---------------------------------------------------------------- pages

void page_frame_begin(const char* id, ImVec2 a, ImVec2 b) {
    panel_bg(a, b);
    ImGui::SetCursorScreenPos(a + ImVec2(P(2), P(2)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(P(28), P(22)));
    ImGui::BeginChild(id, b - a - ImVec2(P(4), P(4)), ImGuiChildFlags_AlwaysUseWindowPadding, 0);
}

void page_frame_end() {
    ImGui::Dummy(ImVec2(0.f, P(10)));
    ImGui::EndChild();
    ImGui::PopStyleVar();
}

void page_header(const char* title, const char* sub) {
    ImGui::PushFont(F.title);
    ImGui::PushStyleColor(ImGuiCol_Text, v4(kText));
    ImGui::TextUnformatted(title);
    ImGui::PopStyleColor();
    ImGui::PopFont();
    if (sub) caption(sub, ImGui::GetContentRegionAvail().x);
    ImGui::Dummy(ImVec2(0.f, P(14)));
}

void draw_home(App& app, ImVec2 a, ImVec2 b) {
    page_frame_begin("home", a, b);
    float w = ImGui::GetContentRegionAvail().x;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    {
        ImVec2 p = ImGui::GetCursorScreenPos();
        float h = P(250);
        dl->AddRectFilled(p, p + ImVec2(w, h), IM_COL32(8, 18, 25, 255), P(14));
        dl->PushClipRect(p, p + ImVec2(w, h), true);
        for (int i = 0; i < 6; i++)
            dl->AddCircleFilled(ImVec2(p.x + w * 0.82f, p.y + h * 0.4f), P(220) - P(30) * i, with_alpha(kAccent, 5 + i * 2), 64);
        dl->AddCircleFilled(ImVec2(p.x + w * 0.95f, p.y + h * 1.1f), P(160), with_alpha(IM_COL32(120, 90, 255, 255), 14), 64);
        dl->PopClipRect();
        dl->AddRect(p, p + ImVec2(w, h), with_alpha(kAccent, 70), P(14));
        float x = p.x + P(30);
        draw_text(dl, F.tiny, ImVec2(x, p.y + P(28)), kAccent, tr(app.cfg, "YEREL VİDEO STÜDYOSU", "LOCAL VIDEO STUDIO"));
        draw_text(dl, F.title, ImVec2(x, p.y + P(48)), kText, tr(app.cfg, "Fikrini videoya dönüştür", "Turn an idea into a video"));
        draw_text(dl, F.ui, ImVec2(x, p.y + P(92)), kSoft,
                  tr(app.cfg, "Prompt yaz ya da bir GitHub projesinin bağlantısını yapıştır. Kare sahneleri kurar;",
                     "Write a prompt or paste a GitHub project link. Kare lays out the scenes;"));
        draw_text(dl, F.ui, ImVec2(x, p.y + P(114)), kSoft,
                  tr(app.cfg, "sen düzenler, MP4 olarak dışa aktarırsın. Her şey bu bilgisayarda çalışır.",
                     "you edit and export to MP4. Everything runs on this computer."));
        float iw = std::min(P(620), w - P(200));
        ImGui::SetCursorScreenPos(ImVec2(x, p.y + P(150)));
        push_field();
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(P(14), P(12)));
        ImGui::SetNextItemWidth(iw);
        bool enter = ImGui::InputTextWithHint("##home_prompt",
                                              tr(app.cfg, "Gün batımında sahil, 8 saniye  ·  ya da  https://github.com/kullanici/proje",
                                                 "Sunset shore, 8 seconds  ·  or  https://github.com/user/project"),
                                              &app.prompt, ImGuiInputTextFlags_EnterReturnsTrue);
        float ih = ImGui::GetItemRectSize().y;
        open_menu_on_right_click("home_prompt_menu");
        ImGui::PopStyleVar();
        pop_field();
        if (menu_begin("home_prompt_menu")) {
            prompt_menu(app, app.prompt);
            menu_end();
        }
        ImGui::SetCursorScreenPos(ImVec2(x + iw + P(10), p.y + P(150)));
        if (button("home_go", ic::sparkles, tr(app.cfg, "Oluştur", "Create"), ImVec2(P(140), ih), kPrimary, !app.planning) || enter) {
            app.page = kCreate;
            start_plan(app);
        }
        ImGui::SetCursorScreenPos(ImVec2(x, p.y + P(150) + ih + P(14)));
        const char* examples[] = {"Gün batımında sahil, 8 saniye. Başlık: Boğaz", "Dikey tanıtım\n1. Yerel\n2. Hızlı\n3. Kurgu",
                                  "https://github.com/ocornut/imgui"};
        const char* labels[] = {"Boğaz", tr(app.cfg, "Dikey tanıtım", "Vertical promo"), "github.com/ocornut/imgui"};
        const uint32_t icons[] = {ic::sparkles, ic::film, ic::github};
        for (int i = 0; i < 3; i++) {
            if (i) ImGui::SameLine(0.f, P(8));
            ImGui::PushID(i);
            if (chip("ex", labels[i], false, icons[i])) app.prompt = examples[i];
            ImGui::PopID();
        }
        ImGui::SetCursorScreenPos(p);
        ImGui::Dummy(ImVec2(w, h));
    }
    ImGui::Dummy(ImVec2(0.f, P(16)));

    float cw = (w - P(32)) / 3.f;
    struct Quick {
        uint32_t icon;
        const char* tr_t;
        const char* en_t;
        const char* tr_s;
        const char* en_s;
    };
    const Quick quick[] = {
        {ic::sparkles, "Prompttan video", "Video from a prompt", "Bir cümle yaz, sahneler otomatik kurulsun.",
         "Write a sentence and get scenes."},
        {ic::github, "GitHub projesinden video", "Video from a GitHub project",
         "Bağlantıyı yapıştır; README, diller ve yapı incelensin.", "Paste a link; README, languages and layout are read."},
        {ic::folder_open, "Proje aç", "Open a project", ".kare dosyasını açıp kurguya devam et.", "Open a .kare file and keep editing."},
    };
    for (int i = 0; i < 3; i++) {
        if (i) ImGui::SameLine(0.f, P(16));
        ImGui::PushID(50 + i);
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImVec2 size(cw, P(150));
        bool pressed = ImGui::InvisibleButton("q", size);
        bool hot = ImGui::IsItemHovered();
        if (hot) glow(dl, p, p + size, P(12), kAccent, 0.7f);
        dl->AddRectFilled(p, p + size, hot ? kRaised : kPanel2, P(12));
        dl->AddRect(p, p + size, hot ? with_alpha(kAccent, 170) : kLine, P(12));
        ImVec2 box(P(40), P(40));
        ImVec2 bp = p + ImVec2(P(18), P(18));
        dl->AddRectFilled(bp, bp + box, with_alpha(kAccent, 24), P(10));
        const char* is = I(quick[i].icon);
        draw_text(dl, F.ui, bp + (box - text_size(F.ui, is)) * 0.5f, kAccent, is);
        draw_text(dl, F.h1, p + ImVec2(P(18), P(72)), kText, fit(F.h1, tr(app.cfg, quick[i].tr_t, quick[i].en_t), cw - P(36)).c_str());
        draw_text(dl, F.small, p + ImVec2(P(18), P(102)), kMuted, fit(F.small, tr(app.cfg, quick[i].tr_s, quick[i].en_s), cw - P(36)).c_str());
        if (pressed) {
            std::string owner, repo;
            if (i == 1 && !find_github_repo(app.prompt, owner, repo)) app.prompt = "https://github.com/";
            if (i == 2) open_project(app);
            else app.page = kCreate;
        }
        ImGui::PopID();
    }
    ImGui::Dummy(ImVec2(0.f, P(18)));

    float half = (w - P(16)) * 0.5f;
    ImGui::BeginGroup();
    {
        Card c = card_begin(half);
        card_title(ic::film, tr(app.cfg, "Son projeler", "Recent projects"));
        if (app.recent.empty()) caption(tr(app.cfg, "Kaydettiğin projeler burada listelenir.", "Saved projects show up here."));
        for (size_t i = 0; i < app.recent.size() && i < 6; i++) {
            const Recent& r = app.recent[i];
            ImGui::PushID(static_cast<int>(i));
            ImVec2 p = ImGui::GetCursorScreenPos();
            float rw = half - P(36);
            if (ImGui::InvisibleButton("rp", ImVec2(rw, P(40))) && r.exists) open_project(app, r.path);
            bool hot = ImGui::IsItemHovered();
            ImDrawList* cdl = ImGui::GetWindowDrawList();
            if (hot) cdl->AddRectFilled(p, p + ImVec2(rw, P(40)), kRaised, P(6));
            draw_text(cdl, F.ui, p + ImVec2(P(8), P(4)), r.exists ? kText : kDim, fit(F.ui, r.name, rw - P(16)).c_str());
            draw_text(cdl, F.tiny, p + ImVec2(P(8), P(23)), kMuted, fit(F.tiny, r.meta + "  \xC2\xB7  " + r.path, rw - P(16)).c_str());
            ImGui::PopID();
        }
        card_end(c);
    }
    ImGui::EndGroup();
    ImGui::SameLine(0.f, P(16));
    ImGui::BeginGroup();
    {
        Card c = card_begin(half);
        card_title(ic::file_video, tr(app.cfg, "Son videolar", "Recent videos"));
        if (app.cfg.recent_exports.empty()) caption(tr(app.cfg, "Dışa aktardığın videolar burada görünür.", "Exported videos show up here."));
        for (size_t i = 0; i < app.cfg.recent_exports.size() && i < 6; i++) {
            const std::string& path = app.cfg.recent_exports[i];
            ImGui::PushID(static_cast<int>(i));
            ImVec2 p = ImGui::GetCursorScreenPos();
            float rw = half - P(36);
            bool exists = file_exists(path);
            if (ImGui::InvisibleButton("rv", ImVec2(rw, P(40))) && exists) open_path(path);
            bool hot = ImGui::IsItemHovered();
            ImDrawList* cdl = ImGui::GetWindowDrawList();
            if (hot) cdl->AddRectFilled(p, p + ImVec2(rw, P(40)), kRaised, P(6));
            draw_text(cdl, F.ui, p + ImVec2(P(8), P(4)), exists ? kText : kDim, fit(F.ui, file_name_of(path), rw - P(16)).c_str());
            draw_text(cdl, F.tiny, p + ImVec2(P(8), P(23)), kMuted, fit(F.tiny, dir_of(path), rw - P(16)).c_str());
            ImGui::PopID();
        }
        card_end(c);
    }
    ImGui::EndGroup();
    page_frame_end();
}

void draw_scenes_page(App& app, ImVec2 a, ImVec2 b) {
    page_frame_begin("scenes", a, b);
    page_header(tr(app.cfg, "Sahneler", "Scenes"),
                tr(app.cfg, "Bir sahneye tıklayınca kurguda açılır. Sıralamayı ve süreleri zaman çizelgesinden de değiştirebilirsin.",
                   "Click a scene to open it in the editor. You can also reorder and resize scenes on the timeline."));
    if (!app.has_board) {
        caption(tr(app.cfg, "Henüz sahne yok.", "No scenes yet."));
        ImGui::Dummy(ImVec2(0.f, P(8)));
        if (button("go_create", ic::sparkles, tr(app.cfg, "Video oluştur", "Create a video"), ImVec2(0, P(40)), kPrimary))
            app.page = kCreate;
        page_frame_end();
        return;
    }
    refresh_thumbs(app);
    float avail = ImGui::GetContentRegionAvail().x;
    float gap = P(16);
    int cols = std::max(1, static_cast<int>((avail + gap) / (P(250) + gap)));
    float cw = (avail - gap * static_cast<float>(cols - 1)) / static_cast<float>(cols);
    float th = cw * 9.f / 16.f;
    int n = static_cast<int>(app.board.scenes.size());
    ImDrawList* dl = ImGui::GetWindowDrawList();
    int open_menu = -1;
    for (int i = 0; i <= n; i++) {
        if (i == n && scene_full(app)) break;
        if (i % cols) ImGui::SameLine(0.f, gap);
        else if (i) ImGui::Dummy(ImVec2(0.f, P(4)));
        ImGui::PushID(i);
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImVec2 size(cw, th + P(70));
        bool pressed = ImGui::InvisibleButton("card", size);
        bool hot = ImGui::IsItemHovered();
        if (i == n) {
            dl->AddRect(p, p + size, hot ? with_alpha(kAccent, 180) : kLine, P(12), 0, 1.f);
            const char* is = I(ic::plus);
            const char* lab = tr(app.cfg, "Sahne ekle", "Add scene");
            ImVec2 c = p + size * 0.5f;
            draw_text(dl, F.icon, c - ImVec2(text_size(F.icon, is).x * 0.5f, P(26)), hot ? kAccent : kMuted, is);
            draw_text(dl, F.ui, ImVec2(c.x - text_size(F.ui, lab).x * 0.5f, c.y + P(6)), hot ? kText : kMuted, lab);
            if (pressed) scene_insert(app, n);
            ImGui::PopID();
            break;
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) open_menu = i;
        const Scene& s = app.board.scenes[static_cast<size_t>(i)];
        bool sel = app.selected == i;
        if (sel || hot) glow(dl, p, p + size, P(12), kAccent, sel ? 1.f : 0.5f);
        dl->AddRectFilled(p, p + size, kPanel2, P(12));
        dl->AddRect(p, p + size, sel ? kAccent : hot ? with_alpha(kAccent, 130) : kLine, P(12), 0, sel ? P(1.5f) : 1.f);
        ImVec2 ta = p + ImVec2(P(8), P(8)), tb2(p.x + cw - P(8), p.y + th);
        dl->AddRectFilled(ta, tb2, IM_COL32(3, 6, 9, 255), P(8));
        if (i < static_cast<int>(app.thumbs.size()) && app.thumbs[static_cast<size_t>(i)]) {
            float aw = tb2.x - ta.x, ah = tb2.y - ta.y;
            float asp = static_cast<float>(app.thumb_w) / static_cast<float>(std::max(1, app.thumb_h));
            float dw = aw, dh = aw / asp;
            if (dh > ah) {
                dh = ah;
                dw = dh * asp;
            }
            ImVec2 ip(ta.x + (aw - dw) * 0.5f, ta.y + (ah - dh) * 0.5f);
            dl->AddImageRounded((ImTextureID)(intptr_t)app.thumbs[static_cast<size_t>(i)], ip, ip + ImVec2(dw, dh), ImVec2(0, 0),
                                ImVec2(1, 1), IM_COL32_WHITE, P(6));
        }
        std::string num = std::to_string(i + 1);
        ImVec2 nb = ta + ImVec2(P(8), P(8));
        dl->AddRectFilled(nb, nb + ImVec2(P(26), P(22)), sel ? kAccent : IM_COL32(0, 0, 0, 170), P(5));
        draw_text(dl, F.small, nb + ImVec2((P(26) - text_size(F.small, num.c_str()).x) * 0.5f, P(4)), sel ? kInk : IM_COL32_WHITE, num.c_str());
        std::string name = s.title.empty() ? s.text : s.title;
        draw_text(dl, F.bold, ImVec2(p.x + P(12), p.y + th + P(14)), kText, fit(F.bold, name, cw - P(24)).c_str());
        std::string meta = seconds_text(s.seconds, app.cfg.lang != "en") + "  \xC2\xB7  " +
                           opt_label(app.cfg, kLayoutOpts, resolve_layout(app.board, i)) + "  \xC2\xB7  " +
                           opt_label(app.cfg, kMotionOpts, s.motion);
        draw_text(dl, F.small, ImVec2(p.x + P(12), p.y + th + P(40)), kMuted, fit(F.small, meta, cw - P(24)).c_str());
        if (pressed) {
            jump_scene(app, i);
            app.page = kEdit;
        }
        ImGui::PopID();
    }
    if (open_menu >= 0) {
        app.ctx_scene = open_menu;
        app.selected = open_menu;
        ImGui::OpenPopup("card_menu");
    }
    if (menu_begin("card_menu")) {
        scene_menu(app, app.ctx_scene);
        menu_end();
    }
    page_frame_end();
}

void file_row(App& app, const char* id, uint32_t icon, const std::string& path, float w) {
    ImGui::PushID(id);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    bool exists = file_exists(path);
    float h = P(48);
    dl->AddRectFilled(p, p + ImVec2(w, h), kPanel, P(8));
    dl->AddRect(p, p + ImVec2(w, h), kLine, P(8));
    const char* is = I(icon);
    draw_text(dl, F.ui, ImVec2(p.x + P(14), p.y + (h - F.ui->FontSize) * 0.5f), exists ? kAccent : kDim, is);
    float tw = w - P(140);
    draw_text(dl, F.ui, p + ImVec2(P(44), P(7)), exists ? kText : kDim, fit(F.ui, file_name_of(path), tw).c_str());
    std::string sub = exists ? dir_of(path) : std::string(tr(app.cfg, "Dosya bulunamadı", "File not found"));
    draw_text(dl, F.tiny, p + ImVec2(P(44), P(27)), kMuted, fit(F.tiny, sub, tw).c_str());
    ImGui::SetCursorScreenPos(ImVec2(p.x + w - P(76), p.y + (h - P(30)) * 0.5f));
    if (icon_button("open", ic::external, ImVec2(P(30), P(30)), false, exists, tr(app.cfg, "Aç", "Open"))) {
        if (icon == ic::film) open_project(app, path);
        else open_path(path);
    }
    ImGui::SameLine(0.f, P(6));
    if (icon_button("reveal", ic::folder_open, ImVec2(P(30), P(30)), false, exists, tr(app.cfg, "Klasörde göster", "Show in folder")))
        reveal(path);
    ImGui::SetCursorScreenPos(p);
    ImGui::Dummy(ImVec2(w, h));
    open_menu_on_right_click("row_menu");
    if (menu_begin("row_menu")) {
        path_menu(app, path, icon == ic::film);
        menu_end();
    }
    ImGui::Dummy(ImVec2(0.f, P(2)));
    ImGui::PopID();
}

void draw_assets(App& app, ImVec2 a, ImVec2 b) {
    page_frame_begin("assets", a, b);
    page_header(tr(app.cfg, "Varlıklar", "Assets"),
                tr(app.cfg, "Dışa aktarılan videolar, proje dosyaları ve uygulamanın kullandığı yazı tipleri.",
                   "Exported videos, project files and the fonts the app uses."));
    float w = std::min(ImGui::GetContentRegionAvail().x, P(980));
    section(tr(app.cfg, "Videolar", "Videos"));
    if (app.cfg.recent_exports.empty()) caption(tr(app.cfg, "Henüz dışa aktarılmış video yok.", "No exported videos yet."));
    for (size_t i = 0; i < app.cfg.recent_exports.size(); i++)
        file_row(app, ("v" + std::to_string(i)).c_str(), ic::file_video, app.cfg.recent_exports[i], w);
    ImGui::Dummy(ImVec2(0.f, P(14)));
    section(tr(app.cfg, "Projeler", "Projects"));
    if (app.cfg.recent_projects.empty()) caption(tr(app.cfg, "Henüz kaydedilmiş proje yok.", "No saved projects yet."));
    for (size_t i = 0; i < app.cfg.recent_projects.size(); i++)
        file_row(app, ("p" + std::to_string(i)).c_str(), ic::film, app.cfg.recent_projects[i], w);
    ImGui::Dummy(ImVec2(0.f, P(14)));
    section(tr(app.cfg, "Yazı tipleri ve simgeler", "Fonts and icons"));
    struct Asset {
        const char* name;
        const char* license;
    };
    const Asset assets[] = {{"Roboto Regular / Bold", "Apache 2.0"}, {"Lucide Icons", "ISC"}};
    for (const auto& as : assets) {
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, p + ImVec2(w, P(40)), kPanel, P(8));
        dl->AddRect(p, p + ImVec2(w, P(40)), kLine, P(8));
        draw_text(dl, F.ui, p + ImVec2(P(14), P(11)), kAccent, I(ic::type));
        draw_text(dl, F.ui, p + ImVec2(P(44), P(11)), kText, as.name);
        draw_text(dl, F.small, ImVec2(p.x + w - P(14) - text_size(F.small, as.license).x, p.y + P(12)), kMuted, as.license);
        ImGui::Dummy(ImVec2(w, P(40)));
        ImGui::Dummy(ImVec2(0.f, P(2)));
    }
    page_frame_end();
}

void draw_models(App& app, ImVec2 a, ImVec2 b) {
    page_frame_begin("models", a, b);
    page_header(tr(app.cfg, "Modeller", "Models"),
                tr(app.cfg, "Kurguyu kimin yazacağını seç. Görüntüler her durumda bu bilgisayarda çizilir.",
                   "Choose who writes the storyboard. Frames are always drawn on this computer."));
    float w = std::min(ImGui::GetContentRegionAvail().x, P(1100));
    float half = (w - P(16)) * 0.5f;
    float inner = half - P(36);

    ImGui::BeginGroup();
    {
        Card c = card_begin(half);
        card_title(ic::bot, tr(app.cfg, "Kare Yerel Yönetmen", "Kare Local Director"), tr(app.cfg, "ÇEVRİMDIŞI", "OFFLINE"));
        caption(tr(app.cfg, "Türkçe ve İngilizce promptları okur: süre, oran, başlık, numaralı listeler ve görünüm kelimeleri. GitHub bağlantılarında README, diller ve dosya yapısından sahne çıkarır.",
                   "Reads Turkish and English prompts: duration, aspect, title, numbered lists and look words. For GitHub links it builds scenes from the README, languages and file layout."),
                inner);
        ImGui::Dummy(ImVec2(0.f, P(8)));
        if (button("use_local", app.plan_api ? 0 : ic::check, app.plan_api ? tr(app.cfg, "Bunu kullan", "Use this") : tr(app.cfg, "Kullanılıyor", "In use"),
                   ImVec2(P(150), P(36)), app.plan_api ? kOutline : kPrimary)) {
            app.plan_api = false;
            app.cfg.use_api = false;
            save_config(app.cfg);
        }
        card_end(c, !app.plan_api);
    }
    ImGui::Dummy(ImVec2(0.f, P(16)));
    {
        Card c = card_begin(half);
        card_title(ic::monitor_play, "Kare Render", "CPU");
        caption(tr(app.cfg, "Kareleri yazılımla çizer: renk paletleri, ışık küreleri, başlık ve metin, hareket ve geçişler. Çıktı H.264 MP4 ya da MJPEG AVI.",
                   "Draws frames in software: palettes, light orbs, title and text, motion and fades. Output is H.264 MP4 or MJPEG AVI."),
                inner);
        card_end(c);
    }
    ImGui::EndGroup();
    ImGui::SameLine(0.f, P(16));
    ImGui::BeginGroup();
    {
        Card c = card_begin(half);
        card_title(ic::brain, tr(app.cfg, "API Yönetmeni", "API Director"), tr(app.cfg, "OPENAI UYUMLU", "OPENAI COMPATIBLE"));
        caption(tr(app.cfg, "Açıkken yalnızca prompt (ya da GitHub özeti) gönderilir; kareler yine yerelde çizilir.",
                   "When on, only the prompt (or GitHub summary) is sent; frames are still drawn locally."),
                inner);
        ImGui::Dummy(ImVec2(0.f, P(6)));
        struct Preset {
            const char* name;
            const char* url;
            const char* model;
        };
        const Preset presets[] = {{"Ollama", "http://127.0.0.1:11434/v1", "qwen2.5:7b"},
                                  {"LM Studio", "http://127.0.0.1:1234/v1", "local"},
                                  {"Kor Studio", "http://127.0.0.1:8080/v1", "local"},
                                  {"OpenAI", "https://api.openai.com/v1", "gpt-4o-mini"},
                                  {"Groq", "https://api.groq.com/openai/v1", "llama-3.3-70b-versatile"}};
        float used = 0.f;
        for (int i = 0; i < 5; i++) {
            ImGui::PushID(500 + i);
            if (wrap_chip("p", presets[i].name, app.cfg.api_base == presets[i].url, used, inner)) {
                app.cfg.api_base = presets[i].url;
                app.cfg.api_model = presets[i].model;
                save_config(app.cfg);
            }
            ImGui::PopID();
        }
        ImGui::Dummy(ImVec2(0.f, P(4)));
        caption(tr(app.cfg, "Adres", "Address"));
        push_field();
        ImGui::SetNextItemWidth(inner);
        if (ImGui::InputText("##base", &app.cfg.api_base)) save_config(app.cfg);
        pop_field();
        caption("Model");
        push_field();
        ImGui::SetNextItemWidth(inner);
        if (ImGui::InputText("##model", &app.cfg.api_model)) save_config(app.cfg);
        pop_field();
        caption(tr(app.cfg, "Anahtar (isteğe bağlı)", "Key (optional)"));
        push_field();
        ImGui::SetNextItemWidth(inner);
        if (ImGui::InputText("##key", &app.cfg.api_key, ImGuiInputTextFlags_Password)) save_config(app.cfg);
        pop_field();
        ImGui::Dummy(ImVec2(0.f, P(6)));
        float bw = (inner - P(8)) * 0.5f;
        if (button("test", ic::activity, tr(app.cfg, "Bağlantıyı dene", "Test connection"), ImVec2(bw, P(36)), kOutline, true, app.testing.load()))
            start_test(app);
        ImGui::SameLine(0.f, P(8));
        if (button("use_api", app.plan_api ? ic::check : 0, app.plan_api ? tr(app.cfg, "Kullanılıyor", "In use") : tr(app.cfg, "Bunu kullan", "Use this"),
                   ImVec2(bw, P(36)), app.plan_api ? kPrimary : kOutline)) {
            app.plan_api = true;
            app.cfg.use_api = true;
            save_config(app.cfg);
        }
        if (!app.test_view.empty()) {
            ImGui::Dummy(ImVec2(0.f, P(4)));
            std::string t = std::string(app.test_view_ok ? tr(app.cfg, "Bağlandı: ", "Connected: ") : tr(app.cfg, "Bağlanamadı: ", "Failed: ")) + app.test_view;
            caption(t.c_str(), inner, app.test_view_ok ? kAccent : kRed);
        }
        card_end(c, app.plan_api);
    }
    ImGui::EndGroup();
    page_frame_end();
}

void draw_settings(App& app, ImVec2 a, ImVec2 b) {
    page_frame_begin("settings", a, b);
    page_header(tr(app.cfg, "Ayarlar", "Settings"), nullptr);
    float w = std::min(ImGui::GetContentRegionAvail().x, P(760));
    float inner = w - P(36);
    {
        Card c = card_begin(w);
        card_title(ic::languages, tr(app.cfg, "Dil ve görünüm", "Language and look"));
        if (chip("tr", "Türkçe", app.cfg.lang != "en")) {
            app.cfg.lang = "tr";
            save_config(app.cfg);
        }
        ImGui::SameLine(0.f, P(8));
        if (chip("en", "English", app.cfg.lang == "en")) {
            app.cfg.lang = "en";
            save_config(app.cfg);
        }
        ImGui::Dummy(ImVec2(0.f, P(6)));
        if (toggle("mark", tr(app.cfg, "Yeni videolarda köşede KARE işareti", "KARE mark on new videos"), app.cfg.mark, inner)) {
            app.cfg.mark = !app.cfg.mark;
            save_config(app.cfg);
        }
        card_end(c);
    }
    ImGui::Dummy(ImVec2(0.f, P(14)));
    {
        Card c = card_begin(w);
        card_title(ic::github, "GitHub");
        caption(tr(app.cfg, "Herkese açık depolar anahtarsız incelenir (saatte 60 istek). Kişisel erişim anahtarı eklersen sınır 5000'e çıkar; anahtar yalnızca api.github.com'a gönderilir.",
                   "Public repositories work without a token (60 requests per hour). A personal access token raises the limit to 5000 and is only sent to api.github.com."),
                inner);
        ImGui::Dummy(ImVec2(0.f, P(4)));
        push_field();
        ImGui::SetNextItemWidth(inner);
        if (ImGui::InputTextWithHint("##ghtoken", tr(app.cfg, "Token (isteğe bağlı)", "Token (optional)"), &app.cfg.github_token,
                                     ImGuiInputTextFlags_Password))
            save_config(app.cfg);
        pop_field();
        card_end(c);
    }
    ImGui::Dummy(ImVec2(0.f, P(14)));
    {
        Card c = card_begin(w);
        card_title(ic::folder, tr(app.cfg, "Dosyalar", "Files"));
        caption(tr(app.cfg, "Kaydet ve dışa aktar pencereleri bu klasörden açılır.", "Save and export dialogs start in this folder."), inner);
        ImGui::Dummy(ImVec2(0.f, P(4)));
        push_field();
        ImGui::SetNextItemWidth(inner);
        if (ImGui::InputText("##lastdir", &app.cfg.last_dir)) save_config(app.cfg);
        pop_field();
        ImGui::Dummy(ImVec2(0.f, P(4)));
        if (button("cfgdir", ic::folder_open, tr(app.cfg, "Ayar dosyasını göster", "Show config file"), ImVec2(0, P(34)), kOutline))
            reveal(config_path());
        card_end(c);
    }
    ImGui::Dummy(ImVec2(0.f, P(14)));
    {
        Card c = card_begin(w);
        card_title(ic::info, tr(app.cfg, "Hakkında", "About"), "v" KARE_VERSION);
        caption(tr(app.cfg, "Kare açık kaynaklı (MIT) yerel bir video stüdyosudur. C++, Dear ImGui ve Windows Media Foundation ile yazıldı.",
                   "Kare is an open-source (MIT) local video studio, written in C++ with Dear ImGui and Windows Media Foundation."),
                inner);
        ImGui::Dummy(ImVec2(0.f, P(4)));
        if (button("repo", ic::github, "github.com/Hydra-Ai-tr/kare", ImVec2(0, P(34)), kOutline))
            open_path("https://github.com/Hydra-Ai-tr/kare");
        card_end(c);
    }
    page_frame_end();
}

void shortcuts(App& app) {
    ImGuiIO& io = ImGui::GetIO();
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Enter) && !app.planning) {
        if (app.page != kCreate && app.page != kEdit) app.page = kCreate;
        start_plan(app);
    }
    if (io.WantTextInput) return;
    if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) toggle_play(app);
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Z)) undo(app);
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Y) || ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z))
        redo(app);
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S)) save_project(app, false);
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_O)) open_project(app);
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_E)) ask_export(app);
    if (app.has_board && ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) jump_scene(app, app.selected - 1);
    if (app.has_board && ImGui::IsKeyPressed(ImGuiKey_RightArrow)) jump_scene(app, app.selected + 1);
    if (ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)) return;
    if (ImGui::IsKeyPressed(ImGuiKey_M, false)) app.mute = !app.mute;
    if (!app.has_board) return;
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_D)) scene_duplicate(app, app.selected);
    if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) scene_delete(app, app.selected);
    if (ImGui::IsKeyPressed(ImGuiKey_S, false) && !io.KeyCtrl) split_at_playhead(app);
}

void draw_app(App& app) {
    take_pending(app);
    take_export(app);
    take_test(app);
    refresh_recent(app);
    shortcuts(app);
    sync_audio(app);

    const ImGuiViewport* view = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(view->WorkPos);
    ImGui::SetNextWindowSize(view->WorkSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
    ImGui::Begin("Kare", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    g_root = ImGui::GetWindowDrawList();
    ImVec2 o = view->WorkPos;
    float W = view->WorkSize.x, H = view->WorkSize.y;
    g_root->AddRectFilled(o, o + ImVec2(W, H), kBg);

    const float TB = P(56), SB = P(36), G = P(10);
    float SW = std::clamp(W * 0.15f, P(206), P(240));
    ImVec2 sa(o.x + G, o.y + TB), sb(o.x + G + SW, o.y + H - SB - G);
    ImVec2 ma(sb.x + G, o.y + TB), mb(o.x + W - G, sb.y);

    draw_topbar(app, o, W, TB, ma.x);
    draw_sidebar(app, sa, sb);
    switch (app.page) {
        case kHome: draw_home(app, ma, mb); break;
        case kScenes: draw_scenes_page(app, ma, mb); break;
        case kAssets: draw_assets(app, ma, mb); break;
        case kModels: draw_models(app, ma, mb); break;
        case kSettings: draw_settings(app, ma, mb); break;
        default: draw_studio(app, ma, mb); break;
    }
    draw_statusbar(app, o, W, H, SB);
    ImGui::End();
    ImGui::PopStyleVar();
    commit_history(app);
}

void apply_theme() {
    ImGui::StyleColorsDark();
    ImGuiStyle& st = ImGui::GetStyle();
    st.WindowRounding = 10.f;
    st.ChildRounding = 10.f;
    st.FrameRounding = 8.f;
    st.GrabRounding = 8.f;
    st.PopupRounding = 8.f;
    st.ScrollbarRounding = 8.f;
    st.WindowBorderSize = 0.f;
    st.ChildBorderSize = 0.f;
    st.PopupBorderSize = 1.f;
    st.FrameBorderSize = 0.f;
    st.WindowPadding = ImVec2(12.f, 12.f);
    st.FramePadding = ImVec2(10.f, 7.f);
    st.ItemSpacing = ImVec2(8.f, 7.f);
    st.ScrollbarSize = 9.f;
    st.GrabMinSize = 12.f;
    ImVec4* c = st.Colors;
    c[ImGuiCol_Text] = v4(kText);
    c[ImGuiCol_TextDisabled] = v4(kMuted);
    c[ImGuiCol_WindowBg] = v4(kBg);
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = v4(kPanel2);
    c[ImGuiCol_Border] = v4(kLine);
    c[ImGuiCol_FrameBg] = v4(kPanel2);
    c[ImGuiCol_FrameBgHovered] = v4(kRaised);
    c[ImGuiCol_FrameBgActive] = v4(kRaised);
    c[ImGuiCol_SliderGrab] = v4(kAccent);
    c[ImGuiCol_SliderGrabActive] = v4(kAccent);
    c[ImGuiCol_CheckMark] = v4(kAccent);
    c[ImGuiCol_Button] = v4(kPanel2);
    c[ImGuiCol_ButtonHovered] = v4(kRaised);
    c[ImGuiCol_ButtonActive] = v4(with_alpha(kAccent, 120));
    c[ImGuiCol_Header] = v4(with_alpha(kAccent, 40));
    c[ImGuiCol_HeaderHovered] = v4(with_alpha(kAccent, 60));
    c[ImGuiCol_HeaderActive] = v4(with_alpha(kAccent, 80));
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = v4(kRaised);
    c[ImGuiCol_ScrollbarGrabHovered] = v4(with_alpha(kAccent, 140));
    c[ImGuiCol_ScrollbarGrabActive] = v4(kAccent);
    c[ImGuiCol_TextSelectedBg] = v4(with_alpha(kAccent, 70));
    c[ImGuiCol_Separator] = v4(kLine);
}

// Offscreen render target for --shot; the GL 1.1 headers lack framebuffer objects.
struct ShotTarget {
    using GenFn = void(APIENTRY*)(GLsizei, GLuint*);
    using DelFn = void(APIENTRY*)(GLsizei, const GLuint*);
    using BindFn = void(APIENTRY*)(GLenum, GLuint);
    using StorageFn = void(APIENTRY*)(GLenum, GLenum, GLsizei, GLsizei);
    using AttachFn = void(APIENTRY*)(GLenum, GLenum, GLenum, GLuint);
    using StatusFn = GLenum(APIENTRY*)(GLenum);
    static constexpr GLenum kFramebuffer = 0x8D40, kRenderbuffer = 0x8D41, kColor0 = 0x8CE0, kComplete = 0x8CD5;

    GenFn gen_fb = nullptr, gen_rb = nullptr;
    DelFn del_fb = nullptr, del_rb = nullptr;
    BindFn bind_fb = nullptr, bind_rb = nullptr;
    StorageFn storage = nullptr;
    AttachFn attach = nullptr;
    StatusFn status = nullptr;
    GLuint fb = 0, rb = 0;
    int w = 0, h = 0;

    template <class T>
    static bool load(T& fn, const char* name) {
        fn = reinterpret_cast<T>(glfwGetProcAddress(name));
        return fn != nullptr;
    }

    bool create(int width, int height) {
        if (!load(gen_fb, "glGenFramebuffers") || !load(gen_rb, "glGenRenderbuffers") || !load(del_fb, "glDeleteFramebuffers") ||
            !load(del_rb, "glDeleteRenderbuffers") || !load(bind_fb, "glBindFramebuffer") || !load(bind_rb, "glBindRenderbuffer") ||
            !load(storage, "glRenderbufferStorage") || !load(attach, "glFramebufferRenderbuffer") ||
            !load(status, "glCheckFramebufferStatus"))
            return false;
        w = width;
        h = height;
        gen_fb(1, &fb);
        gen_rb(1, &rb);
        bind_rb(kRenderbuffer, rb);
        storage(kRenderbuffer, GL_RGBA8, w, h);
        bind_fb(kFramebuffer, fb);
        attach(kFramebuffer, kColor0, kRenderbuffer, rb);
        return status(kFramebuffer) == kComplete;
    }

    bool save(const std::string& path, std::string& err) const {
        std::vector<uint8_t> px(static_cast<size_t>(w) * h * 4), flipped(px.size());
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
        size_t row = static_cast<size_t>(w) * 4;
        for (int y = 0; y < h; y++) std::copy_n(&px[static_cast<size_t>(h - 1 - y) * row], row, &flipped[static_cast<size_t>(y) * row]);
        for (size_t i = 3; i < flipped.size(); i += 4) flipped[i] = 255;
        return write_png(path, w, h, flipped.data(), err);
    }

    void destroy() {
        if (!fb) return;
        bind_fb(kFramebuffer, 0);
        del_fb(1, &fb);
        del_rb(1, &rb);
        fb = rb = 0;
    }
};

}  // namespace

void run_app(const Launch& launch) {
    const bool shot = !launch.shot.empty();
    if (!glfwInit()) return;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(shot ? GLFW_VISIBLE : GLFW_MAXIMIZED, shot ? GLFW_FALSE : GLFW_TRUE);
    GLFWwindow* window = glfwCreateWindow(shot ? launch.width : 1520, shot ? launch.height : 900, "Kare", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return;
    }
    if (!shot) glfwSetWindowSizeLimits(window, 1100, 680, GLFW_DONT_CARE, GLFW_DONT_CARE);
    glfwMakeContextCurrent(window);
    glfwSwapInterval(shot ? 0 : 1);
    ShotTarget target;
    if (shot && !target.create(launch.width, launch.height)) {
        glfwDestroyWindow(window);
        glfwTerminate();
        return;
    }
#ifdef _WIN32
    if (shot) {
        g_chrome.on = true;
    } else if (HWND hwnd = glfwGetWin32Window(window)) {
        BOOL dark = TRUE;
        DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof dark);
        COLORREF caption_color = RGB(5, 9, 13);
        COLORREF text_color = RGB(228, 239, 246);
        COLORREF border = RGB(27, 41, 55);
        DwmSetWindowAttribute(hwnd, DWMWA_CAPTION_COLOR, &caption_color, sizeof caption_color);
        DwmSetWindowAttribute(hwnd, DWMWA_TEXT_COLOR, &text_color, sizeof text_color);
        DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR, &border, sizeof border);
        install_chrome(window);
    }
#endif

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    float scale = 1.f;
    glfwGetWindowContentScale(window, &scale, nullptr);
    if (shot) scale = launch.scale;
    if (scale < 1.f) scale = 1.f;
    g_s = scale;

    static std::vector<unsigned char> regular_bytes, bold_bytes, icon_bytes;
    read_file(exe_dir() + "\\fonts\\Roboto-Regular.ttf", regular_bytes);
    read_file(exe_dir() + "\\fonts\\Roboto-Bold.ttf", bold_bytes);
    read_file(exe_dir() + "\\fonts\\lucide.ttf", icon_bytes);
    static const ImWchar ranges[] = {0x0020, 0x00FF, 0x011E, 0x011F, 0x0130, 0x0131, 0x015E, 0x015F, 0x2013, 0x2026, 0};
    static const ImWchar icon_ranges[] = {0xe000, 0xe5ff, 0};
    auto add_font = [&](std::vector<unsigned char>& data, float size, bool icons) -> ImFont* {
        ImFontConfig cfg;
        cfg.FontDataOwnedByAtlas = false;
        cfg.OversampleH = 3;
        cfg.OversampleV = 2;
        ImFont* f = nullptr;
        if (!data.empty()) f = io.Fonts->AddFontFromMemoryTTF(data.data(), static_cast<int>(data.size()), size, &cfg, ranges);
        if (!f) {
            ImFontConfig def;
            def.SizePixels = size;
            f = io.Fonts->AddFontDefault(&def);
        }
        if (icons && !icon_bytes.empty()) {
            ImFontConfig ic_cfg;
            ic_cfg.FontDataOwnedByAtlas = false;
            ic_cfg.MergeMode = true;
            ic_cfg.PixelSnapH = true;
            ic_cfg.GlyphMinAdvanceX = size;
            ic_cfg.GlyphOffset = ImVec2(0.f, size * 0.10f);
            io.Fonts->AddFontFromMemoryTTF(icon_bytes.data(), static_cast<int>(icon_bytes.size()), size * 0.98f, &ic_cfg, icon_ranges);
        }
        return f;
    };
    F.ui = add_font(regular_bytes, 15.f * scale, true);
    F.bold = add_font(bold_bytes, 15.f * scale, true);
    F.small = add_font(regular_bytes, 13.f * scale, true);
    F.tiny = add_font(regular_bytes, 11.5f * scale, true);
    F.h1 = add_font(bold_bytes, 18.f * scale, true);
    F.title = add_font(bold_bytes, 28.f * scale, false);
    F.icon = F.ui;
    if (!icon_bytes.empty()) {
        ImFontConfig cfg;
        cfg.FontDataOwnedByAtlas = false;
        F.icon = io.Fonts->AddFontFromMemoryTTF(icon_bytes.data(), static_cast<int>(icon_bytes.size()), 30.f * scale, &cfg, icon_ranges);
        if (!F.icon) F.icon = F.ui;
    }
    io.FontDefault = F.ui;
    apply_theme();
    ImGui::GetStyle().ScaleAllSizes(scale);
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    App app;
    app.window = window;
    app.cfg = load_config();
    app.plan_api = app.cfg.use_api;
    app.voices = list_voices();
    if (const GLubyte* r = glGetString(GL_RENDERER)) {
        std::string name = reinterpret_cast<const char*>(r);
        auto slash = name.find('/');
        if (slash != std::string::npos) name.resize(slash);
        app.gpu_name = tidy(name);
    }
    if (shot && !launch.lang.empty()) app.cfg.lang = launch.lang;
    std::string ferr;
    if (!fonts_load(ferr)) app.status = ferr;
    if (!launch.open_path.empty()) open_project(app, launch.open_path);
    if (shot) {
        static const char* page_names[] = {"home", "create", "edit", "scenes", "assets", "models", "settings"};
        for (int i = 0; i < static_cast<int>(std::size(page_names)); i++)
            if (launch.page == page_names[i]) app.page = static_cast<Page>(i);
        if (app.has_board && launch.play_at >= 0) {
            app.play_t = static_cast<float>(std::min(launch.play_at, total_seconds(app.board) - 0.05));
            app.selected = scene_at(app.board, app.play_t);
        }
    }

#ifdef _WIN32
    std::wstring root = widen(default_export_path());
    root = root.size() >= 3 ? root.substr(0, 3) : L"C:\\";
    g_sys.thread = std::thread(sample_system, root);
#endif

    const int settle_frames = 24, input_gap = 6;
    const int shot_frame = settle_frames + static_cast<int>(launch.inputs.size()) * input_gap + 30;
    const double shot_start = glfwGetTime();
    for (int frame = 0; !glfwWindowShouldClose(window); frame++) {
        glfwPollEvents();
        if (!shot && glfwGetWindowAttrib(window, GLFW_ICONIFIED)) {
            glfwWaitEventsTimeout(0.1);
            continue;
        }
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        if (shot) {
            io.DisplaySize = ImVec2(static_cast<float>(launch.width), static_cast<float>(launch.height));
            io.DisplayFramebufferScale = ImVec2(1.f, 1.f);
            int k = frame - settle_frames;
            if (k >= 0 && k % input_gap == 0 && k / input_gap < static_cast<int>(launch.inputs.size())) {
                const ShotInput& in = launch.inputs[static_cast<size_t>(k / input_gap)];
                io.AddMousePosEvent(in.x, in.y);
                if (in.button >= 0) io.AddMouseButtonEvent(in.button, true);
            } else if (k > 0 && k % input_gap == 1 && (k - 1) / input_gap < static_cast<int>(launch.inputs.size())) {
                const ShotInput& in = launch.inputs[static_cast<size_t>((k - 1) / input_gap)];
                if (in.button >= 0) io.AddMouseButtonEvent(in.button, false);
            }
        }
        ImGui::NewFrame();
        draw_app(app);
        ImGui::Render();
        int w = 0, h = 0;
        if (shot) {
            w = launch.width;
            h = launch.height;
        } else {
            glfwGetFramebufferSize(window, &w, &h);
        }
        glViewport(0, 0, w, h);
        glClearColor(5.f / 255.f, 9.f / 255.f, 13.f / 255.f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        if (!shot) {
            glfwSwapBuffers(window);
            continue;
        }
        if (frame >= shot_frame && glfwGetTime() - shot_start > 3.0 && !app.audio_busy) {
            std::string err;
            if (!target.save(launch.shot, err)) std::fprintf(stderr, "%s\n", err.c_str());
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    target.destroy();

    app.cancel = true;
    stop_audio(app);
    g_sys.stop = true;
    if (g_sys.thread.joinable()) g_sys.thread.join();
    if (app.audio_thread.joinable()) app.audio_thread.join();
    if (app.worker.joinable()) app.worker.join();
    if (app.plan_thread.joinable()) app.plan_thread.join();
    if (app.test_thread.joinable()) app.test_thread.join();
    if (app.tex) glDeleteTextures(1, &app.tex);
    for (unsigned t : app.thumbs)
        if (t) glDeleteTextures(1, &t);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
}

}  // namespace kare
