#include "app/ui.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

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

kare::ShotInput parse_point(const std::string& s, int button) {
    kare::ShotInput in;
    in.button = button;
    std::sscanf(s.c_str(), "%f,%f", &in.x, &in.y);
    return in;
}

}  // namespace

int main() {
    kare::Launch launch;
    std::vector<std::string> args;
#ifdef _WIN32
    SetProcessDPIAware();
    int n = 0;
    if (LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &n)) {
        for (int i = 1; i < n; i++) args.push_back(kare::narrow(argv[i]));
        LocalFree(argv);
    }
#endif
    for (size_t i = 0; i < args.size(); i++) {
        const std::string& a = args[i];
        bool has_next = i + 1 < args.size();
        if (a == "--shot" && has_next) {
            launch.shot = args[++i];
        } else if (a == "--page" && has_next) {
            launch.page = args[++i];
        } else if (a == "--lang" && has_next) {
            launch.lang = args[++i];
        } else if (a == "--size" && has_next) {
            std::sscanf(args[++i].c_str(), "%dx%d", &launch.width, &launch.height);
        } else if (a == "--scale" && has_next) {
            launch.scale = static_cast<float>(std::atof(args[++i].c_str()));
        } else if (a == "--at" && has_next) {
            launch.play_at = std::atof(args[++i].c_str());
        } else if (a == "--hover" && has_next) {
            launch.inputs.push_back(parse_point(args[++i], -1));
        } else if (a == "--click" && has_next) {
            launch.inputs.push_back(parse_point(args[++i], 0));
        } else if (a == "--rclick" && has_next) {
            launch.inputs.push_back(parse_point(args[++i], 1));
        } else if (launch.open_path.empty() && a.rfind("--", 0) != 0) {
            launch.open_path = a;
        }
    }
    kare::run_app(launch);
    return 0;
}
