#include "core/tts.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <unordered_map>

#include "core/audio.h"
#include "core/utf8.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <objbase.h>
#include <sapi.h>
#endif

namespace kare {

std::string speakable(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        if (c == '*' || c == '\n' || c == '\r' || c == '\t') {
            if (c != '*' && !out.empty() && out.back() != ' ') out.push_back(' ');
            continue;
        }
        out.push_back(c);
    }
    return tidy(out);
}

std::shared_ptr<const VoiceClip> scene_voice(const Storyboard& board, int index, bool synthesize_now) {
    if (index < 0 || index >= static_cast<int>(board.scenes.size())) return nullptr;
    if (board.voice.empty() || board.voice == "none") return nullptr;
    const std::string& line = board.scenes[static_cast<size_t>(index)].narration;
    if (line.empty()) return nullptr;
    return narrate(speakable(line), board.voice, board.voice_rate, synthesize_now);
}

void prepare_voices(const Storyboard& board) {
    for (int i = 0; i < static_cast<int>(board.scenes.size()); i++) scene_voice(board, i);
}

void fit_to_voice(Storyboard& board, int index) {
    bool shorts = board.height > board.width;
    for (int i = 0; i < static_cast<int>(board.scenes.size()); i++) {
        if (index >= 0 && i != index) continue;
        auto clip = scene_voice(board, i);
        if (!clip || clip->seconds <= 0) continue;
        double pad = shorts ? 0.22 : 0.5;
        board.scenes[static_cast<size_t>(i)].seconds = std::clamp(kVoiceLead + clip->seconds + pad, 1.2, kMaxSceneSeconds);
    }
}

#ifndef _WIN32

std::vector<VoiceInfo> list_voices() { return {}; }
std::string voice_language(const std::string& voice) { return voice == "tr" || voice == "en" ? voice : ""; }
bool voice_available(const std::string&) { return false; }
std::shared_ptr<const VoiceClip> narrate(const std::string&, const std::string&, int, bool) { return nullptr; }

#else

namespace {

// GUIDs spelled out so no import library has to provide them.
const CLSID kSpVoice = {0x96749377, 0x3391, 0x11D2, {0x9E, 0xE3, 0x00, 0xC0, 0x4F, 0x79, 0x73, 0x96}};
const CLSID kSpCategory = {0xA910187F, 0x0C7A, 0x45AC, {0x92, 0xCC, 0x59, 0xED, 0xAF, 0xB7, 0x7B, 0x53}};
const CLSID kSpStream = {0x715D9C59, 0x4442, 0x11D2, {0x96, 0x05, 0x00, 0xC0, 0x4F, 0x8E, 0xE6, 0x28}};
const GUID kWaveFormatEx = {0xC31ADBAE, 0x527F, 0x4FF5, {0xA2, 0x30, 0xF6, 0x2B, 0xB6, 0x1F, 0xF7, 0x0C}};
const ULONGLONG kWordInterest = (1ULL << SPEI_WORD_BOUNDARY) | (1ULL << SPEI_RESERVED1) | (1ULL << SPEI_RESERVED2);

const wchar_t* const kCategories[] = {L"HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Speech_OneCore\\Voices",
                                      L"HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Speech\\Voices"};

struct Com {
    bool owned = false;
    Com() { owned = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)); }
    ~Com() {
        if (owned) CoUninitialize();
    }
};

template <class T>
struct Ptr {
    T* p = nullptr;
    Ptr() = default;
    Ptr(const Ptr&) = delete;
    Ptr& operator=(const Ptr&) = delete;
    ~Ptr() {
        if (p) p->Release();
    }
    T* operator->() const { return p; }
    explicit operator bool() const { return p != nullptr; }
};

std::string token_value(ISpDataKey* key, const wchar_t* name) {
    LPWSTR v = nullptr;
    if (FAILED(key->GetStringValue(name, &v)) || !v) return {};
    std::string out = narrow(v);
    CoTaskMemFree(v);
    return out;
}

VoiceInfo describe(ISpObjectToken* tok) {
    VoiceInfo info;
    Ptr<ISpDataKey> attrs;
    if (SUCCEEDED(tok->OpenKey(L"Attributes", &attrs.p)) && attrs) {
        info.name = token_value(attrs.p, L"Name");
        std::string lang = token_value(attrs.p, L"Language");
        unsigned long lcid = std::strtoul(lang.c_str(), nullptr, 16);
        switch (lcid & 0x3FF) {
            case 0x1F: info.lang = "tr"; break;
            case 0x09: info.lang = "en"; break;
            case 0x07: info.lang = "de"; break;
            case 0x0C: info.lang = "fr"; break;
            case 0x0A: info.lang = "es"; break;
            case 0x10: info.lang = "it"; break;
            case 0x19: info.lang = "ru"; break;
            case 0x01: info.lang = "ar"; break;
            default: break;
        }
    }
    if (info.name.empty()) info.name = token_value(tok, nullptr);
    return info;
}

// Visits every installed voice token; OneCore voices first, they sound better.
template <class F>
void each_voice(F visit) {
    std::vector<std::string> seen;
    for (const wchar_t* category : kCategories) {
        Ptr<ISpObjectTokenCategory> cat;
        if (FAILED(CoCreateInstance(kSpCategory, nullptr, CLSCTX_ALL, __uuidof(ISpObjectTokenCategory),
                                    reinterpret_cast<void**>(&cat.p))))
            continue;
        if (FAILED(cat->SetId(category, FALSE))) continue;
        Ptr<IEnumSpObjectTokens> tokens;
        if (FAILED(cat->EnumTokens(nullptr, nullptr, &tokens.p)) || !tokens) continue;
        ULONG count = 0;
        tokens->GetCount(&count);
        for (ULONG i = 0; i < count; i++) {
            Ptr<ISpObjectToken> tok;
            if (FAILED(tokens->Item(i, &tok.p)) || !tok) continue;
            VoiceInfo info = describe(tok.p);
            if (std::find(seen.begin(), seen.end(), info.name) != seen.end()) continue;
            seen.push_back(info.name);
            if (visit(tok.p, info)) return;
        }
    }
}

bool matches(const VoiceInfo& info, const std::string& voice) {
    if (voice == "tr" || voice == "en") return info.lang == voice;
    return fold_tr(info.name) == fold_tr(voice);
}

std::shared_ptr<VoiceClip> synthesize(const std::wstring& text, ISpObjectToken* tok, int rate) {
    Ptr<ISpVoice> voice;
    if (FAILED(CoCreateInstance(kSpVoice, nullptr, CLSCTX_ALL, __uuidof(ISpVoice), reinterpret_cast<void**>(&voice.p))))
        return nullptr;
    voice->SetVoice(tok);
    voice->SetRate(std::clamp(rate, -10, 10));
    Ptr<IStream> mem;
    if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &mem.p))) return nullptr;
    Ptr<ISpStream> stream;
    if (FAILED(CoCreateInstance(kSpStream, nullptr, CLSCTX_ALL, __uuidof(ISpStream), reinterpret_cast<void**>(&stream.p))))
        return nullptr;
    WAVEFORMATEX wfx{};
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nChannels = 1;
    wfx.nSamplesPerSec = kAudioRate;
    wfx.wBitsPerSample = 16;
    wfx.nBlockAlign = 2;
    wfx.nAvgBytesPerSec = kAudioRate * 2;
    if (FAILED(stream->SetBaseStream(mem.p, kWaveFormatEx, &wfx))) return nullptr;
    if (FAILED(voice->SetOutput(stream.p, TRUE))) return nullptr;
    voice->SetInterest(kWordInterest, kWordInterest);
    if (FAILED(voice->Speak(text.c_str(), SPF_DEFAULT | SPF_IS_NOT_XML, nullptr))) return nullptr;

    struct Mark {
        double at;
        size_t pos, len;
    };
    std::vector<Mark> marks;
    for (;;) {
        SPEVENT ev{};
        ULONG got = 0;
        if (FAILED(voice->GetEvents(1, &ev, &got)) || got == 0) break;
        if (ev.eEventId == SPEI_WORD_BOUNDARY)
            marks.push_back({static_cast<double>(ev.ullAudioStreamOffset) / (kAudioRate * 2.0), static_cast<size_t>(ev.lParam),
                             static_cast<size_t>(ev.wParam)});
    }
    voice->SetOutput(nullptr, FALSE);

    STATSTG st{};
    if (FAILED(mem->Stat(&st, STATFLAG_NONAME))) return nullptr;
    HGLOBAL h = nullptr;
    if (FAILED(GetHGlobalFromStream(mem.p, &h)) || !h) return nullptr;
    auto clip = std::make_shared<VoiceClip>();
    size_t bytes = static_cast<size_t>(st.cbSize.QuadPart);
    if (const void* data = GlobalLock(h)) {
        clip->pcm.resize(bytes / 2);
        std::memcpy(clip->pcm.data(), data, clip->pcm.size() * 2);
        GlobalUnlock(h);
    }
    if (clip->pcm.empty()) return nullptr;
    size_t last = clip->pcm.size();
    while (last > 0 && std::abs(static_cast<int>(clip->pcm[last - 1])) < 260) last--;
    clip->seconds = std::min(static_cast<double>(clip->pcm.size()), last + kAudioRate * 0.06) / kAudioRate;

    std::sort(marks.begin(), marks.end(), [](const Mark& a, const Mark& b) { return a.pos < b.pos; });
    for (size_t i = 0; i < marks.size(); i++) {
        const Mark& m = marks[i];
        if (m.pos >= text.size()) continue;
        VoiceWord w;
        w.text = tidy(narrow(text.substr(m.pos, m.len)));
        if (w.text.empty()) continue;
        w.start = m.at;
        clip->words.push_back(std::move(w));
    }
    for (size_t i = 0; i < clip->words.size(); i++) {
        double next = i + 1 < clip->words.size() ? clip->words[i + 1].start : clip->seconds;
        clip->words[i].end = std::max(clip->words[i].start + 0.05, next);
    }
    return clip;
}

std::mutex g_cache_mu, g_synth_mu;
std::unordered_map<std::string, std::shared_ptr<const VoiceClip>> g_cache;

}  // namespace

std::vector<VoiceInfo> list_voices() {
    Com com;
    std::vector<VoiceInfo> out;
    each_voice([&](ISpObjectToken*, const VoiceInfo& info) {
        out.push_back(info);
        return false;
    });
    return out;
}

std::string voice_language(const std::string& voice) {
    if (voice == "tr" || voice == "en") return voice;
    static std::mutex mu;
    static std::unordered_map<std::string, std::string> known;
    {
        std::lock_guard<std::mutex> lock(mu);
        auto it = known.find(voice);
        if (it != known.end()) return it->second;
    }
    std::string lang;
    for (const auto& v : list_voices())
        if (matches(v, voice)) {
            lang = v.lang;
            break;
        }
    std::lock_guard<std::mutex> lock(mu);
    known[voice] = lang;
    return lang;
}

bool voice_available(const std::string& voice) {
    if (voice.empty() || voice == "none") return false;
    for (const auto& v : list_voices())
        if (matches(v, voice)) return true;
    return false;
}

std::shared_ptr<const VoiceClip> narrate(const std::string& text, const std::string& voice, int rate, bool synthesize_now) {
    if (text.empty() || voice.empty() || voice == "none") return nullptr;
    std::string key = voice + "|" + std::to_string(rate) + "|" + text;
    {
        std::lock_guard<std::mutex> lock(g_cache_mu);
        auto it = g_cache.find(key);
        if (it != g_cache.end()) return it->second;
    }
    if (!synthesize_now) return nullptr;
    std::lock_guard<std::mutex> synth_lock(g_synth_mu);
    {
        std::lock_guard<std::mutex> lock(g_cache_mu);
        auto it = g_cache.find(key);
        if (it != g_cache.end()) return it->second;
    }
    Com com;
    std::shared_ptr<VoiceClip> clip;
    std::wstring wide = widen(text);
    each_voice([&](ISpObjectToken* tok, const VoiceInfo& info) {
        if (!matches(info, voice)) return false;
        clip = synthesize(wide, tok, rate);
        return true;
    });
    std::lock_guard<std::mutex> lock(g_cache_mu);
    if (g_cache.size() > 400) g_cache.clear();
    g_cache[key] = clip;
    return clip;
}

#endif

}  // namespace kare
