#include "core/encode.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

#include "core/audio.h"
#include "core/font.h"
#include "core/render.h"
#include "core/utf8.h"

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#endif

namespace kare {

namespace {

bool ends_with_ci(const std::string& path, const char* ext) {
    std::string f = fold_tr(path);
    std::string e = fold_tr(ext);
    return f.size() >= e.size() && f.compare(f.size() - e.size(), e.size(), e) == 0;
}

class FrameSource {
public:
    FrameSource(const Storyboard& board, int frames) : board_(board), frames_(frames) {
        total_ = std::max(0.001, total_seconds(board));
        unsigned hw = std::thread::hardware_concurrency();
        threads_ = std::clamp(static_cast<int>(hw > 1 ? hw - 1 : 1), 1, 12);
    }

    const Frame& get(int i) {
        if (i < base_ || i >= base_ + static_cast<int>(batch_.size())) fill(i);
        return batch_[static_cast<size_t>(i - base_)];
    }

private:
    void fill(int start) {
        int n = std::min(threads_ * 2, frames_ - start);
        batch_.resize(static_cast<size_t>(std::max(0, n)));
        base_ = start;
        std::atomic<int> next{0};
        auto work = [&] {
            for (;;) {
                int k = next++;
                if (k >= n) break;
                double ts = std::min(total_ - 1e-4, (start + k + 0.5) / board_.fps);
                render_frame(board_, ts, batch_[static_cast<size_t>(k)]);
            }
        };
        std::vector<std::thread> pool;
        for (int t = 1; t < threads_ && t < n; t++) pool.emplace_back(work);
        work();
        for (auto& th : pool) th.join();
    }

    const Storyboard& board_;
    int frames_;
    double total_ = 0;
    int threads_ = 1;
    int base_ = 0;
    std::vector<Frame> batch_;
};

struct Mem {
    std::vector<uint8_t> bytes;
};

void write_cb(void* context, void* data, int size) {
    auto* m = static_cast<Mem*>(context);
    auto* p = static_cast<uint8_t*>(data);
    m->bytes.insert(m->bytes.end(), p, p + size);
}

std::vector<uint8_t> jpeg_of(const Frame& frame) {
    std::vector<uint8_t> rgb(static_cast<size_t>(frame.w) * frame.h * 3);
    for (int i = 0; i < frame.w * frame.h; i++) {
        rgb[static_cast<size_t>(i) * 3] = frame.rgba[static_cast<size_t>(i) * 4];
        rgb[static_cast<size_t>(i) * 3 + 1] = frame.rgba[static_cast<size_t>(i) * 4 + 1];
        rgb[static_cast<size_t>(i) * 3 + 2] = frame.rgba[static_cast<size_t>(i) * 4 + 2];
    }
    Mem mem;
    stbi_write_jpg_to_func(write_cb, &mem, frame.w, frame.h, 3, rgb.data(), 85);
    return mem.bytes;
}

struct Out {
    FILE* f = nullptr;
    bool u32(uint32_t v) {
        uint8_t b[4] = {static_cast<uint8_t>(v), static_cast<uint8_t>(v >> 8), static_cast<uint8_t>(v >> 16),
                        static_cast<uint8_t>(v >> 24)};
        return std::fwrite(b, 1, 4, f) == 4;
    }
    bool u16(uint16_t v) {
        uint8_t b[2] = {static_cast<uint8_t>(v), static_cast<uint8_t>(v >> 8)};
        return std::fwrite(b, 1, 2, f) == 2;
    }
    bool fcc(const char* s) { return std::fwrite(s, 1, 4, f) == 4; }
    bool raw(const void* p, size_t n) { return n == 0 || std::fwrite(p, 1, n, f) == n; }
    long pos() { return std::ftell(f); }
    bool patch(long at, uint32_t v) {
        long cur = pos();
        if (std::fseek(f, at, SEEK_SET) != 0) return false;
        bool ok = u32(v);
        if (std::fseek(f, cur, SEEK_SET) != 0) return false;
        return ok;
    }
};

std::string write_avi(const Storyboard& board, const std::string& path, const Progress& progress) {
    Storyboard b = board;
    normalize(b);
    std::string ferr;
    if (!fonts_load(ferr)) return ferr;
    int frames = std::max(1, static_cast<int>(std::lround(total_seconds(b) * b.fps)));
#ifdef _WIN32
    FILE* fp = _wfopen(widen(path).c_str(), L"wb");
#else
    FILE* fp = std::fopen(path.c_str(), "wb");
#endif
    if (!fp) return "could not create avi";
    std::vector<int16_t> pcm = render_soundtrack(b);
    const bool audio = !pcm.empty();
    const uint32_t audio_frames = static_cast<uint32_t>(pcm.size() / 2);
    Out o{fp};
    o.fcc("RIFF");
    long riff_size_at = o.pos();
    o.u32(0);
    o.fcc("AVI ");
    o.fcc("LIST");
    long hdrl_size_at = o.pos();
    o.u32(0);
    long hdrl_data = o.pos();
    o.fcc("hdrl");
    o.fcc("avih");
    o.u32(56);
    long avih = o.pos();
    uint32_t micro = static_cast<uint32_t>(1000000 / b.fps);
    o.u32(micro);
    o.u32(static_cast<uint32_t>(b.width * b.height * b.fps));
    o.u32(0);
    o.u32(0x110);
    o.u32(static_cast<uint32_t>(frames));
    o.u32(0);
    o.u32(audio ? 2 : 1);
    o.u32(static_cast<uint32_t>(b.width * b.height * 3));
    o.u32(static_cast<uint32_t>(b.width));
    o.u32(static_cast<uint32_t>(b.height));
    o.u32(0);
    o.u32(0);
    o.u32(0);
    o.u32(0);
    o.fcc("LIST");
    long strl_size_at = o.pos();
    o.u32(0);
    long strl_data = o.pos();
    o.fcc("strl");
    o.fcc("strh");
    o.u32(56);
    long strh = o.pos();
    o.fcc("vids");
    o.fcc("MJPG");
    o.u32(0);
    o.u32(0);
    o.u32(0);
    o.u32(1);
    o.u32(static_cast<uint32_t>(b.fps));
    o.u32(0);
    o.u32(static_cast<uint32_t>(frames));
    o.u32(static_cast<uint32_t>(b.width * b.height * 3));
    o.u32(0xFFFFFFFF);
    o.u32(0);
    o.u16(0);
    o.u16(0);
    o.u16(static_cast<uint16_t>(b.width));
    o.u16(static_cast<uint16_t>(b.height));
    o.fcc("strf");
    o.u32(40);
    o.u32(40);
    o.u32(static_cast<uint32_t>(b.width));
    o.u32(static_cast<uint32_t>(b.height));
    o.u16(1);
    o.u16(24);
    o.fcc("MJPG");
    o.u32(static_cast<uint32_t>(b.width * b.height * 3));
    o.u32(0);
    o.u32(0);
    o.u32(0);
    o.u32(0);
    long strl_end = o.pos();
    o.patch(strl_size_at, static_cast<uint32_t>(strl_end - strl_data));
    if (audio) {
        o.fcc("LIST");
        long astrl_size_at = o.pos();
        o.u32(0);
        long astrl_data = o.pos();
        o.fcc("strl");
        o.fcc("strh");
        o.u32(56);
        o.fcc("auds");
        o.u32(0);
        o.u32(0);
        o.u32(0);
        o.u32(0);
        o.u32(4);
        o.u32(kAudioRate * 4);
        o.u32(0);
        o.u32(audio_frames);
        o.u32(static_cast<uint32_t>(kAudioRate * 4 / b.fps + 64));
        o.u32(0xFFFFFFFF);
        o.u32(4);
        o.u16(0);
        o.u16(0);
        o.u16(0);
        o.u16(0);
        o.fcc("strf");
        o.u32(18);
        o.u16(1);
        o.u16(2);
        o.u32(kAudioRate);
        o.u32(kAudioRate * 4);
        o.u16(4);
        o.u16(16);
        o.u16(0);
        long astrl_end = o.pos();
        o.patch(astrl_size_at, static_cast<uint32_t>(astrl_end - astrl_data));
    }
    long hdrl_end = o.pos();
    o.patch(hdrl_size_at, static_cast<uint32_t>(hdrl_end - hdrl_data));
    (void)avih;
    (void)strh;
    o.fcc("LIST");
    long movi_size_at = o.pos();
    o.u32(0);
    long movi_fcc = o.pos();
    o.fcc("movi");

    struct Ix {
        uint32_t offset;
        uint32_t size;
        bool audio;
    };
    std::vector<Ix> index;
    index.reserve(static_cast<size_t>(frames) * 2);
    std::string error;
    uint32_t audio_done = 0;
    FrameSource source(b, frames);
    for (int i = 0; i < frames; i++) {
        if (progress && !progress((i + 0.2f) / static_cast<float>(frames))) {
            error = "cancelled";
            break;
        }
        auto jpg = jpeg_of(source.get(i));
        if (jpg.size() < 4 || jpg[0] != 0xFF || jpg[1] != 0xD8) {
            error = "jpeg encode failed";
            break;
        }
        long chunk = o.pos();
        o.fcc("00dc");
        o.u32(static_cast<uint32_t>(jpg.size()));
        o.raw(jpg.data(), jpg.size());
        if (jpg.size() & 1) o.raw("\0", 1);
        index.push_back({static_cast<uint32_t>(chunk - movi_fcc), static_cast<uint32_t>(jpg.size()), false});
        if (audio) {
            uint32_t target = std::min<uint32_t>(audio_frames, static_cast<uint32_t>(static_cast<double>(i + 1) * kAudioRate / b.fps));
            if (i + 1 == frames) target = audio_frames;
            if (target > audio_done) {
                uint32_t bytes = (target - audio_done) * 4;
                long achunk = o.pos();
                o.fcc("01wb");
                o.u32(bytes);
                o.raw(&pcm[static_cast<size_t>(audio_done) * 2], bytes);
                index.push_back({static_cast<uint32_t>(achunk - movi_fcc), bytes, true});
                audio_done = target;
            }
        }
    }
    long movi_end = o.pos();
    o.patch(movi_size_at, static_cast<uint32_t>(movi_end - movi_fcc));
    o.fcc("idx1");
    o.u32(static_cast<uint32_t>(index.size() * 16));
    for (const auto& ix : index) {
        o.fcc(ix.audio ? "01wb" : "00dc");
        o.u32(0x10);
        o.u32(ix.offset);
        o.u32(ix.size);
    }
    long end = o.pos();
    o.patch(riff_size_at, static_cast<uint32_t>(end - 8));
    std::fclose(fp);
    if (!error.empty()) return error;
    return {};
}

void rgba_to_nv12(const Frame& frame, std::vector<uint8_t>& nv12) {
    int w = frame.w, h = frame.h;
    nv12.assign(static_cast<size_t>(w) * h * 3 / 2, 0);
    uint8_t* y = nv12.data();
    uint8_t* uv = y + w * h;
    auto clamp = [](int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); };
    for (int row = 0; row < h; row++) {
        for (int col = 0; col < w; col++) {
            const uint8_t* p = &frame.rgba[(static_cast<size_t>(row) * w + col) * 4];
            int r = p[0], g = p[1], b = p[2];
            int Y = ((66 * r + 129 * g + 25 * b + 128) >> 8) + 16;
            y[row * w + col] = static_cast<uint8_t>(clamp(Y));
        }
    }
    for (int row = 0; row < h; row += 2) {
        for (int col = 0; col < w; col += 2) {
            int r = 0, g = 0, b = 0;
            for (int dy = 0; dy < 2; dy++) {
                for (int dx = 0; dx < 2; dx++) {
                    const uint8_t* p = &frame.rgba[(static_cast<size_t>(row + dy) * w + (col + dx)) * 4];
                    r += p[0];
                    g += p[1];
                    b += p[2];
                }
            }
            r /= 4;
            g /= 4;
            b /= 4;
            int U = ((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128;
            int V = ((112 * r - 94 * g - 18 * b + 128) >> 8) + 128;
            uint8_t* d = uv + (row / 2) * w + col;
            d[0] = static_cast<uint8_t>(clamp(U));
            d[1] = static_cast<uint8_t>(clamp(V));
        }
    }
}

#ifdef _WIN32
std::string herr(const char* what, HRESULT hr) {
    char b[96];
    std::snprintf(b, sizeof b, "%s (0x%08lX)", what, static_cast<unsigned long>(hr));
    return b;
}

template <typename T>
struct Rel {
    T* p = nullptr;
    ~Rel() {
        if (p) p->Release();
    }
    T** put() {
        if (p) {
            p->Release();
            p = nullptr;
        }
        return &p;
    }
    T* operator->() const { return p; }
    explicit operator bool() const { return p != nullptr; }
};

const char* kAudioSetupFailed = "audio setup failed";

std::string write_mp4(const Storyboard& board, const std::string& path, const Progress& progress, bool with_audio) {
    Storyboard b = board;
    normalize(b);
    std::string ferr;
    if (!fonts_load(ferr)) return ferr;
    if ((b.width % 2) || (b.height % 2)) return "width and height must be even";
    HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool own_co = co == S_OK;
    if (FAILED(co) && co != RPC_E_CHANGED_MODE) return herr("CoInitialize", co);
    HRESULT started = MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET);
    if (FAILED(started)) {
        if (own_co) CoUninitialize();
        return herr("MFStartup", started);
    }
    std::string error;
    Rel<IMFSinkWriter> writer;
    HRESULT hr = MFCreateSinkWriterFromURL(widen(path).c_str(), nullptr, nullptr, writer.put());
    if (FAILED(hr)) error = herr("create mp4", hr);
    DWORD stream = 0;
    if (error.empty()) {
        Rel<IMFMediaType> outType;
        hr = MFCreateMediaType(outType.put());
        if (SUCCEEDED(hr)) hr = outType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        if (SUCCEEDED(hr)) hr = outType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
        UINT32 bitrate = static_cast<UINT32>(std::max(4000000, b.width * b.height * b.fps / 7));
        if (SUCCEEDED(hr)) hr = outType->SetUINT32(MF_MT_AVG_BITRATE, bitrate);
        if (SUCCEEDED(hr)) hr = outType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        if (SUCCEEDED(hr)) hr = MFSetAttributeSize(outType.p, MF_MT_FRAME_SIZE, b.width, b.height);
        if (SUCCEEDED(hr)) hr = MFSetAttributeRatio(outType.p, MF_MT_FRAME_RATE, b.fps, 1);
        if (SUCCEEDED(hr)) hr = MFSetAttributeRatio(outType.p, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
        if (SUCCEEDED(hr)) hr = writer->AddStream(outType.p, &stream);
        if (FAILED(hr)) error = herr("output type", hr);
    }
    if (error.empty()) {
        Rel<IMFMediaType> inType;
        hr = MFCreateMediaType(inType.put());
        if (SUCCEEDED(hr)) hr = inType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        if (SUCCEEDED(hr)) hr = inType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
        if (SUCCEEDED(hr)) hr = inType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        if (SUCCEEDED(hr)) hr = MFSetAttributeSize(inType.p, MF_MT_FRAME_SIZE, b.width, b.height);
        if (SUCCEEDED(hr)) hr = MFSetAttributeRatio(inType.p, MF_MT_FRAME_RATE, b.fps, 1);
        if (SUCCEEDED(hr)) hr = MFSetAttributeRatio(inType.p, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
        if (SUCCEEDED(hr)) hr = inType->SetUINT32(MF_MT_DEFAULT_STRIDE, static_cast<UINT32>(b.width));
        if (SUCCEEDED(hr)) hr = writer->SetInputMediaType(stream, inType.p, nullptr);
        if (FAILED(hr)) error = herr("input type", hr);
    }
    std::vector<int16_t> pcm;
    DWORD astream = 0;
    if (error.empty() && with_audio) {
        pcm = render_soundtrack(b);
        if (!pcm.empty()) {
            Rel<IMFMediaType> aout;
            hr = MFCreateMediaType(aout.put());
            if (SUCCEEDED(hr)) hr = aout->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
            if (SUCCEEDED(hr)) hr = aout->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC);
            if (SUCCEEDED(hr)) hr = aout->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
            if (SUCCEEDED(hr)) hr = aout->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, kAudioRate);
            if (SUCCEEDED(hr)) hr = aout->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2);
            if (SUCCEEDED(hr)) hr = aout->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 24000);
            if (SUCCEEDED(hr)) hr = writer->AddStream(aout.p, &astream);
            Rel<IMFMediaType> ain;
            if (SUCCEEDED(hr)) hr = MFCreateMediaType(ain.put());
            if (SUCCEEDED(hr)) hr = ain->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
            if (SUCCEEDED(hr)) hr = ain->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
            if (SUCCEEDED(hr)) hr = ain->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
            if (SUCCEEDED(hr)) hr = ain->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, kAudioRate);
            if (SUCCEEDED(hr)) hr = ain->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2);
            if (SUCCEEDED(hr)) hr = ain->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, 4);
            if (SUCCEEDED(hr)) hr = ain->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, kAudioRate * 4);
            if (SUCCEEDED(hr)) hr = writer->SetInputMediaType(astream, ain.p, nullptr);
            if (FAILED(hr)) error = kAudioSetupFailed;
        }
    }
    if (error.empty()) {
        hr = writer->BeginWriting();
        if (FAILED(hr)) error = herr("begin writing", hr);
    }
    int frames = std::max(1, static_cast<int>(std::lround(total_seconds(b) * b.fps)));
    std::vector<uint8_t> nv12;
    size_t audio_done = 0;
    if (error.empty()) {
        FrameSource source(b, frames);
        for (int i = 0; i < frames; i++) {
            if (progress && !progress((i + 0.2f) / static_cast<float>(frames))) {
                error = "cancelled";
                break;
            }
            rgba_to_nv12(source.get(i), nv12);
            Rel<IMFMediaBuffer> buffer;
            Rel<IMFSample> sample;
            hr = MFCreateMemoryBuffer(static_cast<DWORD>(nv12.size()), buffer.put());
            BYTE* data = nullptr;
            if (SUCCEEDED(hr)) hr = buffer->Lock(&data, nullptr, nullptr);
            if (SUCCEEDED(hr)) {
                std::memcpy(data, nv12.data(), nv12.size());
                hr = buffer->SetCurrentLength(static_cast<DWORD>(nv12.size()));
                buffer->Unlock();
            }
            if (SUCCEEDED(hr)) hr = MFCreateSample(sample.put());
            if (SUCCEEDED(hr)) hr = sample->AddBuffer(buffer.p);
            LONGLONG t0 = static_cast<LONGLONG>(static_cast<double>(i) * 10000000.0 / b.fps);
            LONGLONG t1 = static_cast<LONGLONG>(static_cast<double>(i + 1) * 10000000.0 / b.fps);
            if (SUCCEEDED(hr)) hr = sample->SetSampleTime(t0);
            if (SUCCEEDED(hr)) hr = sample->SetSampleDuration(t1 - t0);
            if (SUCCEEDED(hr)) hr = writer->WriteSample(stream, sample.p);
            if (FAILED(hr)) {
                error = herr("write sample", hr);
                break;
            }
            if (!pcm.empty()) {
                size_t frames_total = pcm.size() / 2;
                size_t target = std::min(frames_total, static_cast<size_t>(static_cast<double>(i + 1) * kAudioRate / b.fps));
                if (i + 1 == frames) target = frames_total;
                if (target > audio_done) {
                    DWORD bytes = static_cast<DWORD>((target - audio_done) * 4);
                    Rel<IMFMediaBuffer> abuf;
                    Rel<IMFSample> asample;
                    hr = MFCreateMemoryBuffer(bytes, abuf.put());
                    BYTE* adata = nullptr;
                    if (SUCCEEDED(hr)) hr = abuf->Lock(&adata, nullptr, nullptr);
                    if (SUCCEEDED(hr)) {
                        std::memcpy(adata, &pcm[audio_done * 2], bytes);
                        hr = abuf->SetCurrentLength(bytes);
                        abuf->Unlock();
                    }
                    if (SUCCEEDED(hr)) hr = MFCreateSample(asample.put());
                    if (SUCCEEDED(hr)) hr = asample->AddBuffer(abuf.p);
                    LONGLONG a0 = static_cast<LONGLONG>(static_cast<double>(audio_done) * 10000000.0 / kAudioRate);
                    LONGLONG a1 = static_cast<LONGLONG>(static_cast<double>(target) * 10000000.0 / kAudioRate);
                    if (SUCCEEDED(hr)) hr = asample->SetSampleTime(a0);
                    if (SUCCEEDED(hr)) hr = asample->SetSampleDuration(a1 - a0);
                    if (SUCCEEDED(hr)) hr = writer->WriteSample(astream, asample.p);
                    if (FAILED(hr)) {
                        error = herr("write audio", hr);
                        break;
                    }
                    audio_done = target;
                }
            }
        }
    }
    if (error.empty()) {
        hr = writer->Finalize();
        if (FAILED(hr)) error = herr("finalize", hr);
    }
    writer.p ? writer.p->Release() : 0;
    writer.p = nullptr;
    MFShutdown();
    if (own_co) CoUninitialize();
    if (!error.empty()) {
#ifdef _WIN32
        _wremove(widen(path).c_str());
#endif
        return error;
    }
    return {};
}
#endif

}  // namespace

std::string encode_video(const Storyboard& board, const std::string& path, const Progress& progress) {
    if (path.empty()) return "output path is empty";
    if (ends_with_ci(path, ".avi")) return write_avi(board, path, progress);
#ifdef _WIN32
    if (ends_with_ci(path, ".mp4")) {
        std::string err = write_mp4(board, path, progress, true);
        if (err == kAudioSetupFailed) err = write_mp4(board, path, progress, false);
        return err;
    }
    return "use a .mp4 or .avi path";
#else
    if (ends_with_ci(path, ".mp4")) return "mp4 export needs Windows; use .avi";
    return "use an .avi path";
#endif
}

bool write_preview_png(const std::string& path, const Storyboard& board, double time_sec, std::string& err) {
    if (!fonts_load(err)) return false;
    Frame frame;
    render_frame(board, time_sec, frame);
    return write_png(path, frame.w, frame.h, frame.rgba.data(), err);
}

bool write_png(const std::string& path, int w, int h, const uint8_t* rgba, std::string& err) {
    Mem mem;
    if (!stbi_write_png_to_func(write_cb, &mem, w, h, 4, rgba, w * 4) || mem.bytes.empty()) {
        err = "png encode failed";
        return false;
    }
    if (!write_bytes(path, mem.bytes.data(), mem.bytes.size())) {
        err = "could not write png";
        return false;
    }
    err.clear();
    return true;
}

bool probe_video(const std::string& path, std::string& err, bool* has_audio) {
    if (has_audio) *has_audio = false;
#ifndef _WIN32
    err = "probe is only implemented on Windows";
    return false;
#else
    HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool own_co = co == S_OK;
    if (FAILED(co) && co != RPC_E_CHANGED_MODE) {
        err = "CoInitialize failed";
        return false;
    }
    HRESULT started = MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET);
    if (FAILED(started)) {
        err = "MFStartup failed";
        if (own_co) CoUninitialize();
        return false;
    }
    IMFSourceReader* reader = nullptr;
    HRESULT hr = MFCreateSourceReaderFromURL(widen(path).c_str(), nullptr, &reader);
    bool ok = false;
    if (SUCCEEDED(hr) && reader) {
        IMFSample* sample = nullptr;
        DWORD flags = 0;
        hr = reader->ReadSample(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), 0, nullptr, &flags, nullptr, &sample);
        ok = SUCCEEDED(hr) && sample && (flags & MF_SOURCE_READERF_ENDOFSTREAM) == 0;
        if (sample) sample->Release();
        if (has_audio) {
            IMFSample* asample = nullptr;
            DWORD aflags = 0;
            HRESULT ahr = reader->ReadSample(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), 0, nullptr, &aflags, nullptr, &asample);
            *has_audio = SUCCEEDED(ahr) && asample && (aflags & MF_SOURCE_READERF_ENDOFSTREAM) == 0;
            if (asample) asample->Release();
        }
        reader->Release();
    }
    MFShutdown();
    if (own_co) CoUninitialize();
    if (!ok) err = "could not read the video back";
    else err.clear();
    return ok;
#endif
}

}  // namespace kare
