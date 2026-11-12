#include "app/ui.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/config.h"
#include "core/director.h"
#include "core/encode.h"
#include "core/font.h"
#include "core/render.h"
#include "core/story.h"
#include "core/utf8.h"

#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

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
#include <commdlg.h>
#include <GL/gl.h>
#endif

namespace kare {

namespace {

const char* tr(const Config& cfg, const char* turkish, const char* english) {
    return cfg.lang == "en" ? english : turkish;
}

struct StyleOpt {
    const char* id;
    const char* tr;
    const char* en;
};

const StyleOpt kStyles[] = {
    {"", "Otomatik", "Auto"},       {"ember", "Sinema", "Cinema"}, {"dusk", "Gün batımı", "Dusk"},
    {"night", "Gece", "Night"},     {"sea", "Deniz", "Sea"},       {"forest", "Orman", "Forest"},
    {"neon", "Neon", "Neon"},       {"paper", "Kağıt", "Paper"},   {"minimal", "Sade", "Minimal"},
};

const char* kAspects[][3] = {
    {"", "Otomatik", "Auto"},
    {"16:9", "Yatay 16:9", "Landscape 16:9"},
    {"9:16", "Dikey 9:16", "Portrait 9:16"},
    {"1:1", "Kare 1:1", "Square 1:1"},
};

const char* kMotions[] = {"drift", "zoom", "rise"};

struct App {
    Config cfg;
    int page = 0;
    std::string prompt;
    int style_i = 0;
    int aspect_i = 0;
    float seconds = 0;
    bool plan_api = false;
    Storyboard board;
    bool has_board = false;
    int selected = 0;
    float play_t = 0;
    bool playing = false;
    double play_wall = 0;
    float play_from = 0;
    std::string status;
    std::string export_path;
    std::atomic<bool> exporting{false};
    std::atomic<float> progress{0};
    std::atomic<bool> cancel{false};
    std::atomic<bool> planning{false};
    std::mutex mu;
    PlanResult pending;
    bool plan_ready = false;
    std::string export_error;
    bool export_done = false;
    unsigned tex = 0;
    int tex_w = 0;
    int tex_h = 0;
    int shown_frame = -1;
    int shown_serial = -1;
    int serial = 1;
    std::thread worker;
    std::thread plan_thread;
    GLFWwindow* window = nullptr;
    float fit_seconds = 8;
};

std::string shorten(const std::string& s, size_t n) {
    if (s.size() <= n) return s;
    return s.substr(0, n) + "...";
}

void touch(App& app) {
    app.serial++;
    app.playing = false;
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

void remember(App& app, const std::string& path) {
    auto w = widen(path);
    auto pos = w.find_last_of(L"\\/");
    if (pos != std::wstring::npos) app.cfg.last_dir = narrow(w.substr(0, pos));
    save_config(app.cfg);
}

void upload_frame(App& app, const Frame& frame) {
    if (!app.tex) glGenTextures(1, &app.tex);
    glBindTexture(GL_TEXTURE_2D, app.tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (app.tex_w != frame.w || app.tex_h != frame.h) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, frame.w, frame.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, frame.rgba.data());
        app.tex_w = frame.w;
        app.tex_h = frame.h;
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, frame.w, frame.h, GL_RGBA, GL_UNSIGNED_BYTE, frame.rgba.data());
    }
}

void start_plan(App& app) {
    if (app.planning || app.exporting) return;
    if (app.prompt.empty()) {
        app.status = tr(app.cfg, "Bir prompt yazın.", "Write a prompt.");
        return;
    }
    PlanInput in;
    in.prompt = app.prompt;
    in.style = kStyles[app.style_i].id;
    in.aspect = kAspects[app.aspect_i][0];
    in.seconds = app.seconds;
    in.mark = app.cfg.mark;
    in.use_api = app.plan_api;
    in.api_base = app.cfg.api_base;
    in.api_key = app.cfg.api_key;
    in.api_model = app.cfg.api_model;
    app.planning = true;
    app.status = tr(app.cfg, "Planlanıyor...", "Planning...");
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

void start_export(App& app) {
    if (!app.has_board || app.exporting || app.planning) return;
    if (app.export_path.empty()) {
        app.status = tr(app.cfg, "Bir dosya yolu seçin.", "Choose a file path.");
        return;
    }
    if (app.worker.joinable()) app.worker.join();
    Storyboard copy = app.board;
    std::string path = app.export_path;
    app.cancel = false;
    app.progress = 0;
    app.exporting = true;
    app.status = tr(app.cfg, "Dışa aktarılıyor...", "Exporting...");
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

void take_pending(App& app) {
    PlanResult result;
    {
        std::lock_guard<std::mutex> lock(app.mu);
        if (!app.plan_ready) return;
        result = std::move(app.pending);
        app.plan_ready = false;
    }
    app.board = std::move(result.board);
    app.has_board = true;
    app.selected = 0;
    app.play_t = 0;
    app.playing = false;
    touch(app);
    app.page = 1;
    if (!result.warning.empty()) app.status = result.warning;
    else if (result.from_api) app.status = tr(app.cfg, "API kurgusu hazır.", "API storyboard is ready.");
    else app.status = tr(app.cfg, "Yerel kurgu hazır.", "Local storyboard is ready.");
}

void take_export(App& app) {
    std::string err;
    {
        std::lock_guard<std::mutex> lock(app.mu);
        if (!app.export_done) return;
        err = app.export_error;
        app.export_done = false;
    }
    app.status = err.empty() ? tr(app.cfg, "Video kaydedildi.", "Video saved.") : err;
}

void page_create(App& app) {
    ImGui::TextUnformatted(tr(app.cfg, "Prompt", "Prompt"));
    ImGui::InputTextMultiline("##prompt", &app.prompt, ImVec2(-1, 160));
    ImGui::TextDisabled("%s", tr(app.cfg, "Ctrl+Enter ile planla", "Ctrl+Enter to plan"));
    if (ImGui::BeginCombo(tr(app.cfg, "Görünüm", "Look"), tr(app.cfg, kStyles[app.style_i].tr, kStyles[app.style_i].en))) {
        for (int i = 0; i < 9; i++) {
            if (ImGui::Selectable(tr(app.cfg, kStyles[i].tr, kStyles[i].en), app.style_i == i)) app.style_i = i;
        }
        ImGui::EndCombo();
    }
    if (ImGui::BeginCombo(tr(app.cfg, "Çerçeve", "Frame"), tr(app.cfg, kAspects[app.aspect_i][1], kAspects[app.aspect_i][2]))) {
        for (int i = 0; i < 4; i++) {
            if (ImGui::Selectable(tr(app.cfg, kAspects[i][1], kAspects[i][2]), app.aspect_i == i)) app.aspect_i = i;
        }
        ImGui::EndCombo();
    }
    ImGui::SliderFloat(tr(app.cfg, "Süre (0 = otomatik)", "Duration (0 = auto)"), &app.seconds, 0.f, 30.f, "%.1f s");
    ImGui::Checkbox(tr(app.cfg, "Planı API ile yaz", "Write the plan with the API"), &app.plan_api);
    if (app.planning) ImGui::BeginDisabled();
    if (ImGui::Button(tr(app.cfg, "Planla", "Plan"), ImVec2(160, 36)) ||
        ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Enter))
        start_plan(app);
    if (app.planning) ImGui::EndDisabled();
}

void page_edit(App& app) {
    if (!app.has_board) {
        ImGui::TextWrapped("%s", tr(app.cfg, "Önce bir prompt planlayın.", "Plan a prompt first."));
        return;
    }
    float total = static_cast<float>(total_seconds(app.board));
    if (app.playing) {
        float t = app.play_from + static_cast<float>(glfwGetTime() - app.play_wall);
        if (t >= total) {
            app.playing = false;
            app.play_t = total > 0.05f ? total - 0.05f : 0;
        } else app.play_t = t;
    }
    ImGui::BeginChild("scenes", ImVec2(240, -1), true);
    for (int i = 0; i < static_cast<int>(app.board.scenes.size()); i++) {
        const Scene& s = app.board.scenes[static_cast<size_t>(i)];
        std::string label = s.title.empty() ? s.text : s.title;
        if (label.empty()) label = std::to_string(i + 1);
        label = std::to_string(i + 1) + ". " + shorten(label, 32);
        if (ImGui::Selectable(label.c_str(), app.selected == i)) app.selected = i;
    }
    if (ImGui::Button(tr(app.cfg, "Ekle", "Add"))) {
        add_scene(app.board, app.selected + 1);
        app.selected = std::min(app.selected + 1, static_cast<int>(app.board.scenes.size()) - 1);
        app.board.scenes[static_cast<size_t>(app.selected)].title = tr(app.cfg, "Yeni sahne", "New scene");
        touch(app);
    }
    ImGui::SameLine();
    if (ImGui::Button(tr(app.cfg, "Çoğalt", "Duplicate"))) {
        duplicate_scene(app.board, app.selected);
        touch(app);
    }
    if (ImGui::Button(tr(app.cfg, "Sil", "Delete"))) {
        remove_scene(app.board, app.selected);
        if (app.selected >= static_cast<int>(app.board.scenes.size())) app.selected = static_cast<int>(app.board.scenes.size()) - 1;
        touch(app);
    }
    ImGui::SameLine();
    if (ImGui::Button(tr(app.cfg, "Yukarı", "Up"))) {
        if (move_scene(app.board, app.selected, app.selected - 1)) app.selected--;
        touch(app);
    }
    ImGui::SameLine();
    if (ImGui::Button(tr(app.cfg, "Aşağı", "Down"))) {
        if (move_scene(app.board, app.selected, app.selected + 1)) app.selected++;
        touch(app);
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("detail", ImVec2(0, -1), false);
    if (app.selected >= 0 && app.selected < static_cast<int>(app.board.scenes.size())) {
        Scene& s = app.board.scenes[static_cast<size_t>(app.selected)];
        ImGui::TextUnformatted(tr(app.cfg, "Başlık", "Title"));
        if (ImGui::InputText("##title", &s.title)) touch(app);
        ImGui::TextUnformatted(tr(app.cfg, "Metin", "Text"));
        if (ImGui::InputTextMultiline("##text", &s.text, ImVec2(-1, 80))) touch(app);
        float sec = static_cast<float>(s.seconds);
        if (ImGui::SliderFloat(tr(app.cfg, "Saniye", "Seconds"), &sec, 0.4f, 15.f, "%.1f")) {
            s.seconds = sec;
            touch(app);
        }
        int motion = 0;
        for (int i = 0; i < 3; i++)
            if (s.motion == kMotions[i]) motion = i;
        if (ImGui::Combo(tr(app.cfg, "Hareket", "Motion"), &motion, "drift\0zoom\0rise\0")) {
            s.motion = kMotions[motion];
            touch(app);
        }
        const char* style_label = tr(app.cfg, "Devral", "Inherit");
        int style_i = 0;
        for (int i = 1; i < 9; i++) {
            if (s.style == kStyles[i].id) {
                style_i = i;
                style_label = tr(app.cfg, kStyles[i].tr, kStyles[i].en);
            }
        }
        if (ImGui::BeginCombo(tr(app.cfg, "Sahne görünümü", "Scene look"), style_i == 0 ? tr(app.cfg, "Devral", "Inherit") : style_label)) {
            for (int i = 0; i < 9; i++) {
                const char* label = i == 0 ? tr(app.cfg, "Devral", "Inherit") : tr(app.cfg, kStyles[i].tr, kStyles[i].en);
                if (ImGui::Selectable(label, style_i == i)) {
                    s.style = kStyles[i].id;
                    touch(app);
                }
            }
            ImGui::EndCombo();
        }
    }
    ImGui::Separator();
    if (app.playing) {
        if (ImGui::Button(tr(app.cfg, "Durdur", "Stop"))) app.playing = false;
    } else if (ImGui::Button(tr(app.cfg, "Oynat", "Play"))) {
        app.playing = true;
        app.play_from = app.play_t;
        app.play_wall = glfwGetTime();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    if (ImGui::SliderFloat("##scrub", &app.play_t, 0.f, std::max(0.1f, total), "%.1f s")) app.playing = false;

    ImGui::SetNextItemWidth(120);
    ImGui::InputFloat(tr(app.cfg, "Sığdır", "Fit"), &app.fit_seconds, 0, 0, "%.1f");
    ImGui::SameLine();
    if (ImGui::Button(tr(app.cfg, "Süreyi dağıt", "Scale duration"))) {
        scale_duration(app.board, app.fit_seconds);
        normalize(app.board);
        touch(app);
    }
#ifdef _WIN32
    ImGui::SameLine();
    if (ImGui::Button(tr(app.cfg, "Projeyi kaydet", "Save project"))) {
        std::string path = save_dialog(app.window, L"Kare\0*.kare\0\0", L"kare", L"proje.kare");
        if (!path.empty()) {
            std::string err;
            app.board.prompt = app.prompt;
            if (save_story(path, app.board, err)) {
                remember(app, path);
                app.status = tr(app.cfg, "Proje kaydedildi.", "Project saved.");
            } else app.status = err;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(tr(app.cfg, "Proje aç", "Open project"))) {
        std::string path = open_dialog(app.window, L"Kare\0*.kare\0\0");
        if (!path.empty()) {
            Storyboard loaded;
            std::string err;
            if (load_story(path, loaded, err)) {
                app.board = std::move(loaded);
                app.prompt = app.board.prompt;
                app.has_board = true;
                app.selected = 0;
                app.play_t = 0;
                remember(app, path);
                touch(app);
                app.status = tr(app.cfg, "Proje açıldı.", "Project opened.");
            } else app.status = err;
        }
    }
#endif

    if (app.has_board) {
        int frame_i = static_cast<int>(app.play_t * app.board.fps);
        if (frame_i != app.shown_frame || app.serial != app.shown_serial) {
            std::string ferr;
            if (fonts_load(ferr)) {
                Frame frame;
                double t = app.play_t;
                double tot = total_seconds(app.board);
                if (t > tot - 1e-3) t = std::max(0.0, tot - 1e-3);
                render_frame(app.board, t, frame);
                upload_frame(app, frame);
                app.shown_frame = frame_i;
                app.shown_serial = app.serial;
            }
        }
        if (app.tex && app.tex_w > 0) {
            float avail = ImGui::GetContentRegionAvail().x;
            float aspect = static_cast<float>(app.tex_w) / static_cast<float>(app.tex_h);
            float dw = avail;
            float dh = dw / aspect;
            float room = ImGui::GetContentRegionAvail().y;
            if (dh > room && room > 40) {
                dh = room;
                dw = dh * aspect;
            }
            ImGui::Image((ImTextureID)(intptr_t)app.tex, ImVec2(dw, dh));
        }
    }
    ImGui::EndChild();
}

void page_settings(App& app) {
    bool en = app.cfg.lang == "en";
    if (ImGui::Checkbox(tr(app.cfg, "English", "English"), &en)) {
        app.cfg.lang = en ? "en" : "tr";
        save_config(app.cfg);
    }
    if (ImGui::Checkbox(tr(app.cfg, "Köşede KARE yazısı", "KARE mark in the corner"), &app.cfg.mark)) {
        app.board.mark = app.cfg.mark;
        save_config(app.cfg);
        touch(app);
    }
    ImGui::Separator();
    ImGui::TextWrapped("%s", tr(app.cfg, "API yalnızca kurgunun metnini yazar. Kareler bu bilgisayarda çizilir. Anahtar yalnızca bu bilgisayarda durur.",
                                "The API only writes the storyboard text. Frames are drawn on this computer. The key stays on this computer."));
    struct Preset {
        const char* name;
        const char* url;
        const char* model;
    };
    const Preset presets[] = {
        {"Ollama", "http://127.0.0.1:11434/v1", "qwen2.5:7b"},
        {"LM Studio", "http://127.0.0.1:1234/v1", "local"},
        {"Kor Studio", "http://127.0.0.1:8080/v1", "local"},
        {"OpenAI", "https://api.openai.com/v1", "gpt-4o-mini"},
        {"Groq", "https://api.groq.com/openai/v1", "llama-3.3-70b-versatile"},
    };
    if (ImGui::BeginCombo(tr(app.cfg, "Hazır adres", "Preset"), tr(app.cfg, "Seç", "Choose"))) {
        for (const auto& p : presets) {
            if (ImGui::Selectable(p.name)) {
                app.cfg.api_base = p.url;
                app.cfg.api_model = p.model;
                save_config(app.cfg);
            }
        }
        ImGui::EndCombo();
    }
    ImGui::InputText(tr(app.cfg, "Adres", "Address"), &app.cfg.api_base);
    ImGui::InputText(tr(app.cfg, "Model", "Model"), &app.cfg.api_model);
    ImGui::InputText(tr(app.cfg, "API anahtarı", "API key"), &app.cfg.api_key, ImGuiInputTextFlags_Password);
    if (ImGui::Button(tr(app.cfg, "Ayarları kaydet", "Save settings"))) {
        save_config(app.cfg);
        app.status = tr(app.cfg, "Ayarlar kaydedildi.", "Settings saved.");
    }
}

void draw_app(App& app) {
    take_pending(app);
    take_export(app);
    const ImGuiViewport* view = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(view->WorkPos);
    ImGui::SetNextWindowSize(view->WorkSize);
    ImGui::Begin("Kare", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
                     ImGuiWindowFlags_NoSavedSettings);
    ImGui::BeginChild("side", ImVec2(190, -36), true);
    ImGui::SetWindowFontScale(1.4f);
    ImGui::TextUnformatted("Kare");
    ImGui::SetWindowFontScale(1.f);
    ImGui::TextDisabled("%s", tr(app.cfg, "yerel video", "local video"));
    ImGui::Separator();
    const char* pages[] = {tr(app.cfg, "Oluştur", "Create"), tr(app.cfg, "Kurgu", "Edit"), tr(app.cfg, "Ayarlar", "Settings")};
    for (int i = 0; i < 3; i++) {
        if (ImGui::Selectable(pages[i], app.page == i, 0, ImVec2(0, 32))) app.page = i;
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("main", ImVec2(0, -36), false);
    if (app.page == 0) page_create(app);
    else if (app.page == 1) page_edit(app);
    else page_settings(app);
    ImGui::EndChild();

    ImGui::Separator();
    if (app.exporting) {
        ImGui::ProgressBar(app.progress.load(), ImVec2(220, 0));
        ImGui::SameLine();
        if (ImGui::Button(tr(app.cfg, "İptal", "Cancel"))) app.cancel = true;
    } else {
        ImGui::SetNextItemWidth(360);
        ImGui::InputText("##out", &app.export_path);
        ImGui::SameLine();
#ifdef _WIN32
        if (ImGui::Button(tr(app.cfg, "Gözat", "Browse"))) {
            std::wstring initial = widen(app.export_path);
            std::string path = save_dialog(app.window, L"MP4\0*.mp4\0AVI\0*.avi\0\0", L"mp4", initial);
            if (!path.empty()) {
                app.export_path = path;
                remember(app, path);
            }
        }
        ImGui::SameLine();
#endif
        if (!app.has_board) ImGui::BeginDisabled();
        if (ImGui::Button(tr(app.cfg, "Dışa aktar", "Export"))) start_export(app);
        if (!app.has_board) ImGui::EndDisabled();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s", app.status.c_str());
    ImGui::End();
}

void apply_theme() {
    ImGui::StyleColorsDark();
    ImGuiStyle& st = ImGui::GetStyle();
    st.WindowRounding = 8;
    st.FrameRounding = 6;
    st.GrabRounding = 6;
    st.ChildRounding = 6;
    st.WindowPadding = ImVec2(14, 14);
    st.FramePadding = ImVec2(10, 6);
    ImVec4* c = st.Colors;
    c[ImGuiCol_WindowBg] = ImVec4(0.09f, 0.08f, 0.07f, 1);
    c[ImGuiCol_ChildBg] = ImVec4(0.12f, 0.105f, 0.09f, 1);
    c[ImGuiCol_FrameBg] = ImVec4(0.16f, 0.14f, 0.12f, 1);
    c[ImGuiCol_Button] = ImVec4(0.72f, 0.46f, 0.16f, 1);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.84f, 0.56f, 0.22f, 1);
    c[ImGuiCol_ButtonActive] = ImVec4(0.58f, 0.34f, 0.10f, 1);
    c[ImGuiCol_Header] = ImVec4(0.72f, 0.46f, 0.16f, 0.55f);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.84f, 0.56f, 0.22f, 0.75f);
    c[ImGuiCol_SliderGrab] = ImVec4(0.91f, 0.64f, 0.28f, 1);
    c[ImGuiCol_CheckMark] = ImVec4(0.95f, 0.74f, 0.38f, 1);
    c[ImGuiCol_TitleBg] = ImVec4(0.09f, 0.08f, 0.07f, 1);
    c[ImGuiCol_TitleBgActive] = ImVec4(0.14f, 0.11f, 0.08f, 1);
    c[ImGuiCol_Separator] = ImVec4(0.35f, 0.26f, 0.16f, 0.6f);
}

}  // namespace

void run_app() {
    if (!glfwInit()) return;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow* window = glfwCreateWindow(1240, 800, "Kare", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    float scale = 1.f;
    glfwGetWindowContentScale(window, &scale, nullptr);
    if (scale < 1.f) scale = 1.f;
    std::string font_err;
    fonts_load(font_err);
    static std::vector<unsigned char> font_bytes;
    read_file(exe_dir() + "\\fonts\\Roboto-Regular.ttf", font_bytes);
    static const ImWchar ranges[] = {0x0020, 0x00FF, 0x011E, 0x011F, 0x0130, 0x0131, 0x015E, 0x015F, 0};
    ImFontConfig fc;
    fc.OversampleH = 2;
    fc.OversampleV = 2;
    fc.FontDataOwnedByAtlas = false;
    if (font_bytes.empty() || !io.Fonts->AddFontFromMemoryTTF(font_bytes.data(), static_cast<int>(font_bytes.size()),
                                                              18.f * scale, &fc, ranges))
        io.Fonts->AddFontDefault();
    apply_theme();
    ImGui::GetStyle().ScaleAllSizes(scale);
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    App app;
    app.window = window;
    app.cfg = load_config();
    app.plan_api = app.cfg.use_api;
    app.export_path = default_export_path();
    app.board.mark = app.cfg.mark;
    std::string ferr;
    if (!fonts_load(ferr)) app.status = ferr;

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        draw_app(app);
        ImGui::Render();
        int w = 0, h = 0;
        glfwGetFramebufferSize(window, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(0.09f, 0.08f, 0.07f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    app.cancel = true;
    if (app.worker.joinable()) app.worker.join();
    if (app.plan_thread.joinable()) app.plan_thread.join();
    if (app.tex) glDeleteTextures(1, &app.tex);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
}

}  // namespace kare
