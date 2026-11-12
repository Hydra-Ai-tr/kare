cmake_minimum_required(VERSION 3.20)
project(Kare VERSION 0.2.0 LANGUAGES CXX)

option(KARE_BUILD_GUI "Build the Kare desktop application" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

if(MSVC)
  add_compile_options(/utf-8 /W3)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
  add_compile_options(-finput-charset=UTF-8 -fexec-charset=UTF-8)
endif()

add_library(karecore STATIC
  src/core/utf8.cpp
  src/core/json.cpp
  src/core/story.cpp
  src/core/director.cpp
  src/core/http.cpp
  src/core/github.cpp
  src/core/brain.cpp
  src/core/config.cpp
  src/core/font.cpp
  src/core/render.cpp
  src/core/audio.cpp
  src/core/encode.cpp)
target_include_directories(karecore PUBLIC src PRIVATE ${CMAKE_SOURCE_DIR}/third_party)
target_compile_definitions(karecore PUBLIC KARE_VERSION="${PROJECT_VERSION}")
if(WIN32)
  target_compile_definitions(karecore PUBLIC UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)
  target_link_libraries(karecore PUBLIC winhttp ole32 uuid shell32 mfplat mfreadwrite mfuuid)
endif()
find_package(Threads REQUIRED)
target_link_libraries(karecore PUBLIC Threads::Threads)
if(NOT MSVC)
  target_compile_options(karecore PRIVATE -Wall -Wextra -Wno-unused-function)
endif()

add_executable(kare-cli src/cli/main.cpp)
target_link_libraries(kare-cli PRIVATE karecore)
if(MINGW)
  target_link_options(kare-cli PRIVATE -static)
endif()
add_custom_command(TARGET kare-cli POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_SOURCE_DIR}/assets/fonts $<TARGET_FILE_DIR:kare-cli>/fonts)

enable_testing()
add_test(NAME selftest COMMAND kare-cli selftest)

if(KARE_BUILD_GUI)
  include(FetchContent)
  set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
    GIT_SHALLOW TRUE)
  FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.5
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(glfw imgui)
  find_package(OpenGL REQUIRED)

  add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
    ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp)
  target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends ${imgui_SOURCE_DIR}/misc/cpp)
  target_link_libraries(imgui PUBLIC glfw OpenGL::GL)
  if(NOT MSVC)
    target_compile_options(imgui PRIVATE -w)
  endif()

  add_executable(kare WIN32 src/app/main.cpp src/app/ui.cpp)
  target_link_libraries(kare PRIVATE karecore imgui)
  if(WIN32)
    target_link_libraries(kare PRIVATE comdlg32 gdi32 dwmapi pdh winmm)
  endif()
  if(MSVC)
    target_link_options(kare PRIVATE /ENTRY:mainCRTStartup)
  endif()
  if(MINGW)
    target_link_options(kare PRIVATE -static)
  endif()
  add_dependencies(kare kare-cli)
endif()
