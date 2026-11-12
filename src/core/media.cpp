#include "core/media.h"

#include <algorithm>
#include <map>
#include <mutex>

#include "core/audio.h"
#include "core/utf8.h"

#ifdef _WIN32
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#endif

namespace kare {

namespace {

constexpr double kMaxSeconds = 600.0;

std::vector<int16_t> to_stereo_44k(const std::vector<int16_t>& in, int channels, int rate) {
    std::vector<int16_t> out;
    if (channels <= 0 || rate <= 0 || in.empty()) return out;
    size_t frames = in.size() / static_cast<size_t>(channels);
    auto sample = [&](size_t f, int ch) -> float {
        f = std::min(f, frames - 1);
        const int16_t* p = &in[f * static_cast<size_t>(channels)];
        if (channels == 1) return p[0];
        return p[std::min(ch, channels - 1)];
    };
    if (rate == kAudioRate) {
        out.resize(frames * 2);
        for (size_t f = 0; f < frames; f++) {
            out[f * 2] = static_cast<int16_t>(sample(f, 0));
            out[f * 2 + 1] = static_cast<int16_t>(sample(f, 1));
        }
        return out;
    }
    double step = static_cast<double>(rate) / kAudioRate;
    size_t n = static_cast<size_t>(static_cast<double>(frames) / step);
    out.resize(n * 2);
    for (size_t i = 0; i < n; i++) {
        double pos = static_cast<double>(i) * step;
        size_t f0 = static_cast<size_t>(pos);
        float t = static_cast<float>(pos - static_cast<double>(f0));
        for (int ch = 0; ch < 2; ch++) {
            float a = sample(f0, ch), b = sample(f0 + 1, ch);
            out[i * 2 + static_cast<size_t>(ch)] = static_cast<int16_t>(a + (b - a) * t);
        }
    }
    return out;
}

#ifdef _WIN32
template <class T>
struct Rel {
    T* p = nullptr;
    ~Rel() {
        if (p) p->Release();
    }
    T** put() { return &p; }
    T* operator->() const { return p; }
};

std::string decode(const std::string& path, std::vector<int16_t>& out) {
    HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool own_co = co == S_OK || co == S_FALSE;
    if (FAILED(MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET))) {
        if (own_co) CoUninitialize();
        return "Media Foundation is unavailable";
    }
    std::string err;
    std::vector<int16_t> pcm;
    UINT32 channels = 0, rate = 0;
    {
        Rel<IMFSourceReader> reader;
        HRESULT hr = MFCreateSourceReaderFromURL(widen(path).c_str(), nullptr, reader.put());
        if (FAILED(hr)) err = "could not open the audio file";
        if (err.empty()) {
            reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS), FALSE);
            hr = reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), TRUE);
            if (FAILED(hr)) err = "the file has no audio track";
        }
        if (err.empty()) {
            Rel<IMFMediaType> want;
            hr = MFCreateMediaType(want.put());
            if (SUCCEEDED(hr)) hr = want->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
            if (SUCCEEDED(hr)) hr = want->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
            if (SUCCEEDED(hr)) hr = want->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
            if (SUCCEEDED(hr)) hr = reader->SetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), nullptr, want.p);
            if (FAILED(hr)) err = "this audio format cannot be decoded";
        }
        if (err.empty()) {
            Rel<IMFMediaType> got;
            hr = reader->GetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), got.put());
            if (SUCCEEDED(hr)) hr = got->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &channels);
            if (SUCCEEDED(hr)) hr = got->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &rate);
            if (FAILED(hr) || !channels || !rate) err = "unknown audio format";
        }
        size_t limit = err.empty() ? static_cast<size_t>(kMaxSeconds * rate * channels) : 0;
        while (err.empty() && pcm.size() < limit) {
            DWORD flags = 0;
            Rel<IMFSample> sample;
            hr = reader->ReadSample(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), 0, nullptr, &flags, nullptr, sample.put());
            if (FAILED(hr)) {
                if (pcm.empty()) err = "decoding failed";
                break;
            }
            if (flags & MF_SOURCE_READERF_ENDOFSTREAM) break;
            if (!sample.p) continue;
            Rel<IMFMediaBuffer> buf;
            if (FAILED(sample->ConvertToContiguousBuffer(buf.put()))) continue;
            BYTE* data = nullptr;
            DWORD len = 0;
            if (SUCCEEDED(buf->Lock(&data, nullptr, &len))) {
                const int16_t* s = reinterpret_cast<const int16_t*>(data);
                pcm.insert(pcm.end(), s, s + len / 2);
                buf->Unlock();
            }
        }
    }
    MFShutdown();
    if (own_co) CoUninitialize();
    if (err.empty() && pcm.empty()) err = "the audio file is empty";
    if (err.empty()) out = to_stereo_44k(pcm, static_cast<int>(channels), static_cast<int>(rate));
    return err;
}
#else
std::string decode(const std::string&, std::vector<int16_t>&) { return "audio import needs Windows"; }
#endif

}  // namespace

std::shared_ptr<const std::vector<int16_t>> load_audio(const std::string& path, std::string* err) {
    static std::mutex mu;
    static std::map<std::string, std::pair<std::shared_ptr<const std::vector<int16_t>>, std::string>> cache;
    if (path.empty()) return nullptr;
    {
        std::lock_guard<std::mutex> lock(mu);
        auto it = cache.find(path);
        if (it != cache.end()) {
            if (err) *err = it->second.second;
            return it->second.first;
        }
    }
    auto pcm = std::make_shared<std::vector<int16_t>>();
    std::string e = decode(path, *pcm);
    std::shared_ptr<const std::vector<int16_t>> result = e.empty() ? pcm : nullptr;
    std::lock_guard<std::mutex> lock(mu);
    cache[path] = {result, e};
    if (err) *err = e;
    return result;
}

}  // namespace kare
