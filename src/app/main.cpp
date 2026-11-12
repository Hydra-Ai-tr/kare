#include "app/ui.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

int main() {
#ifdef _WIN32
    SetProcessDPIAware();
#endif
    kare::run_app();
    return 0;
}
