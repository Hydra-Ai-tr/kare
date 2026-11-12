#include "core/audio.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "core/render.h"

namespace kare {

namespace {

constexpr float kTau = 6.28318531f;
constexpr int kTable = 2048;

struct Rng {
    uint32_t s;
    float next() {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return static_cast<float>(s & 0xFFFFFF) / 8388608.f - 1.f;
    }
};

float mtof(float m) { return 440.f * std::pow(2.f, (m - 69.f) / 12.f); }

struct Table {
    std::vector<float> t;
    float at(float ph) const {
        float x = ph * kTable;
        int i = static_cast<int>(x);
        float f = x - static_cast<float>(i);
        float a = t[static_cast<size_t>(i & (kTable - 1))];
        float b = t[static_cast<size_t>((i + 1) & (kTable - 1))];
        return a + (b - a) * f;
    }
};

const Table& saw_table() {
    static const Table tb = [] {
        Table t;
        t.t.resize(kTable);
        for (int i = 0; i < kTable; i++) {
            float v = 0;
            for (int k = 1; k <= 14; k++) v += std::sin(kTau * k * i / kTable) / static_cast<float>(k);
            t.t[static_cast<size_t>(i)] = v * 0.55f;
        }
        return t;
    }();
    return tb;
}

const Table& soft_table() {
    static const Table tb = [] {
        Table t;
        t.t.resize(kTable);
        for (int i = 0; i < kTable; i++) {
            float ph = kTau * i / kTable;
            t.t[static_cast<size_t>(i)] = std::sin(ph) * 0.8f + std::sin(ph * 2.f) * 0.18f + std::sin(ph * 3.f) * 0.09f +
                                          std::sin(ph * 4.f) * 0.04f;
        }
        return t;
    }();
    return tb;
}

struct Mood {
    double bpm;
    int kick, hat, snare, bass, arp;
    float pad_amp, pad_cut;
    bool sevenths;
    float swing;
    bool duck;
    bool crackle;
};

Mood mood_of(const std::string& id) {
    if (id == "ambient") return {84, 0, 0, 0, 1, 1, 0.34f, 0.10f, true, 0.f, false, false};
    if (id == "drive") return {122, 2, 2, 2, 3, 3, 0.20f, 0.14f, false, 0.f, true, false};
    if (id == "lofi") return {82, 3, 3, 3, 4, 4, 0.22f, 0.06f, true, 0.17f, false, true};
    return {108, 1, 1, 1, 2, 2, 0.24f, 0.11f, false, 0.f, true, false};
}

struct Bus {
    std::vector<float> l, r;
    void init(size_t n) {
        l.assign(n, 0.f);
        r.assign(n, 0.f);
    }
    void add(size_t i, float v, float pan) {
        if (i >= l.size()) return;
        float pl = std::sqrt(0.5f * (1.f - pan)), pr = std::sqrt(0.5f * (1.f + pan));
        l[i] += v * pl;
        r[i] += v * pr;
    }
};

size_t at(double sec) { return sec <= 0 ? 0 : static_cast<size_t>(sec * kAudioRate); }

void pad_chord(Bus& bus, Bus& send, double t0, double dur, const std::vector<int>& notes, float amp, float cut, Rng& rng) {
    const Table& saw = saw_table();
    const float att = 0.35f, rel = 0.9f;
    size_t s0 = at(t0);
    size_t n = at(dur + rel);
    float detune[3] = {-0.07f, 0.f, 0.07f};
    float pans[3] = {-0.55f, 0.f, 0.55f};
    struct Voice {
        float ph, inc, pan;
    };
    std::vector<Voice> voices;
    for (int m : notes)
        for (int k = 0; k < 3; k++) {
            Voice v;
            v.ph = (rng.next() + 1.f) * 0.5f;
            v.inc = mtof(static_cast<float>(m) + detune[k]) / kAudioRate;
            v.pan = pans[k] * (0.6f + 0.4f * ((m % 3) / 2.f));
            voices.push_back(v);
        }
    float scale = amp / std::sqrt(static_cast<float>(voices.size()));
    float lpl = 0.f, lpr = 0.f;
    for (size_t i = 0; i < n; i++) {
        float tt = static_cast<float>(i) / kAudioRate;
        float env = std::min(1.f, tt / att);
        if (tt > dur) env *= std::max(0.f, 1.f - (tt - static_cast<float>(dur)) / rel);
        if (env <= 0.f) continue;
        float sl = 0.f, sr = 0.f;
        for (auto& v : voices) {
            float s = saw.at(v.ph);
            v.ph += v.inc;
            if (v.ph >= 1.f) v.ph -= 1.f;
            sl += s * (1.f - v.pan) * 0.5f;
            sr += s * (1.f + v.pan) * 0.5f;
        }
        float c = cut * (0.85f + 0.15f * std::sin(tt * 0.9f));
        lpl += (sl - lpl) * c;
        lpr += (sr - lpr) * c;
        size_t idx = s0 + i;
        if (idx >= bus.l.size()) break;
        bus.l[idx] += lpl * env * scale;
        bus.r[idx] += lpr * env * scale;
        send.l[idx] += lpl * env * scale * 0.5f;
        send.r[idx] += lpr * env * scale * 0.5f;
    }
}

void bass_note(Bus& bus, double t0, double dur, int midi, float amp) {
    size_t s0 = at(t0);
    size_t n = at(dur + 0.06);
    float inc = mtof(static_cast<float>(midi)) / kAudioRate;
    float ph = 0.f;
    for (size_t i = 0; i < n; i++) {
        float tt = static_cast<float>(i) / kAudioRate;
        float env = std::min(1.f, tt / 0.006f) * (0.7f + 0.3f * std::exp(-tt / 0.12f));
        if (tt > dur) env *= std::max(0.f, 1.f - (tt - static_cast<float>(dur)) / 0.06f);
        float s = std::sin(kTau * ph) + 0.28f * std::sin(2.f * kTau * ph) + 0.08f * std::sin(3.f * kTau * ph);
        s = std::tanh(s * 1.4f) * 0.8f;
        ph += inc;
        if (ph >= 1.f) ph -= 1.f;
        bus.add(s0 + i, s * env * amp, 0.f);
    }
}

void pluck(Bus& bus, Bus& send, double t0, int midi, float amp, float pan, float decay, bool soft) {
    const Table& tb = soft ? soft_table() : saw_table();
    size_t s0 = at(t0);
    size_t n = at(decay * 5.0);
    float inc = mtof(static_cast<float>(midi)) / kAudioRate;
    float ph = 0.f, lp = 0.f;
    for (size_t i = 0; i < n; i++) {
        float tt = static_cast<float>(i) / kAudioRate;
        float env = std::min(1.f, tt / 0.003f) * std::exp(-tt / decay);
        float c = 0.06f + 0.5f * std::exp(-tt / (decay * 0.5f));
        float s = tb.at(ph);
        ph += inc;
        if (ph >= 1.f) ph -= 1.f;
        lp += (s - lp) * c;
        float v = lp * env * amp;
        bus.add(s0 + i, v, pan);
        send.add(s0 + i, v * 0.6f, -pan);
    }
}

void kick(Bus& bus, double t0, float amp, Rng& rng) {
    size_t s0 = at(t0);
    size_t n = at(0.45);
    float ph = 0.f;
    for (size_t i = 0; i < n; i++) {
        float tt = static_cast<float>(i) / kAudioRate;
        float f = 48.f + 120.f * std::exp(-tt / 0.028f);
        ph += f / kAudioRate;
        float s = std::sin(kTau * ph) * std::exp(-tt / 0.26f) + rng.next() * std::exp(-tt / 0.002f) * 0.25f;
        bus.add(s0 + i, std::tanh(s * 1.6f) * amp, 0.f);
    }
}

void hat(Bus& bus, double t0, float amp, float decay, float pan, Rng& rng) {
    size_t s0 = at(t0);
    size_t n = at(decay * 6.0);
    float prev = 0.f, hp = 0.f;
    for (size_t i = 0; i < n; i++) {
        float tt = static_cast<float>(i) / kAudioRate;
        float x = rng.next();
        hp = 0.6f * (hp + x - prev);
        prev = x;
        bus.add(s0 + i, hp * std::exp(-tt / decay) * amp, pan);
    }
}

void clap(Bus& bus, Bus& send, double t0, float amp, Rng& rng, bool snare) {
    size_t s0 = at(t0);
    size_t n = at(0.4);
    float lp = 0.f, prev = 0.f, hp = 0.f, ph = 0.f;
    for (size_t i = 0; i < n; i++) {
        float tt = static_cast<float>(i) / kAudioRate;
        float x = rng.next();
        hp = 0.85f * (hp + x - prev);
        prev = x;
        lp += (hp - lp) * 0.35f;
        float env;
        if (snare) env = std::exp(-tt / 0.11f);
        else {
            float burst = 0.f;
            for (int k = 0; k < 3; k++) {
                float d = tt - k * 0.011f;
                if (d >= 0.f) burst = std::max(burst, std::exp(-d / 0.006f));
            }
            env = std::max(burst, 0.6f * std::exp(-std::max(0.f, tt - 0.022f) / 0.1f) * (tt > 0.022f ? 1.f : 0.f));
        }
        float v = lp * env * amp;
        if (snare) {
            ph += 185.f / kAudioRate;
            v += std::sin(kTau * ph) * std::exp(-tt / 0.06f) * amp * 0.5f;
        }
        bus.add(s0 + i, v, 0.f);
        send.add(s0 + i, v * 0.5f, 0.f);
    }
}

void whoosh(Bus& bus, double t_cut, double len, float amp, Rng& rng) {
    double t0 = std::max(0.0, t_cut - len);
    size_t s0 = at(t0);
    size_t n = at((t_cut - t0) + 0.25);
    float lp1 = 0.f, lp2 = 0.f;
    float total = static_cast<float>(t_cut - t0);
    for (size_t i = 0; i < n; i++) {
        float tt = static_cast<float>(i) / kAudioRate;
        float u = total > 0.f ? std::min(1.f, tt / total) : 1.f;
        float env = tt <= total ? u * u : std::exp(-(tt - total) / 0.07f);
        float c = 0.01f + 0.35f * u * u;
        float x = rng.next();
        lp1 += (x - lp1) * c;
        lp2 += (lp1 - lp2) * c;
        float pan = -0.8f + 1.6f * u;
        bus.add(s0 + i, lp2 * env * amp * 2.2f, pan);
    }
}

void boom(Bus& bus, Bus& send, double t0, float amp, Rng& rng) {
    size_t s0 = at(t0);
    size_t n = at(1.2);
    float ph = 0.f, lp = 0.f;
    for (size_t i = 0; i < n; i++) {
        float tt = static_cast<float>(i) / kAudioRate;
        float f = 36.f + 34.f * std::exp(-tt / 0.15f);
        ph += f / kAudioRate;
        float x = rng.next();
        lp += (x - lp) * 0.04f;
        float v = (std::sin(kTau * ph) * std::exp(-tt / 0.5f) + lp * 1.5f * std::exp(-tt / 0.3f)) * amp;
        bus.add(s0 + i, std::tanh(v * 1.3f), 0.f);
        send.add(s0 + i, v * 0.3f, 0.f);
    }
}

void riser(Bus& bus, double t_end, double len, float amp, Rng& rng) {
    double t0 = std::max(0.0, t_end - len);
    size_t s0 = at(t0);
    size_t n = at(t_end - t0);
    float total = static_cast<float>(t_end - t0);
    float ph = 0.f, lp = 0.f;
    for (size_t i = 0; i < n; i++) {
        float tt = static_cast<float>(i) / kAudioRate;
        float u = total > 0.f ? tt / total : 1.f;
        float f = 200.f * std::pow(8.f, u);
        ph += f / kAudioRate;
        float x = rng.next();
        lp += (x - lp) * (0.02f + 0.4f * u);
        float v = (lp * 1.4f + 0.18f * saw_table().at(ph - std::floor(ph))) * u * u * amp;
        bus.add(s0 + i, v, std::sin(tt * 9.f) * 0.4f);
    }
}

void reverb(const Bus& in, Bus& out, float wet) {
    const int combs[4] = {1557, 1617, 1491, 1422};
    const int aps[2] = {225, 556};
    size_t n = in.l.size();
    for (int ch = 0; ch < 2; ch++) {
        const std::vector<float>& src = ch ? in.r : in.l;
        std::vector<float>& dst = ch ? out.r : out.l;
        std::vector<float> acc(n, 0.f);
        for (int c = 0; c < 4; c++) {
            int d = combs[c] + ch * 23;
            std::vector<float> buf(static_cast<size_t>(d), 0.f);
            size_t pos = 0;
            float damp = 0.f;
            for (size_t i = 0; i < n; i++) {
                float y = buf[pos];
                damp = y * 0.6f + damp * 0.4f;
                buf[pos] = src[i] + damp * 0.82f;
                acc[i] += y;
                if (++pos >= buf.size()) pos = 0;
            }
        }
        for (int a = 0; a < 2; a++) {
            int d = aps[a] + ch * 11;
            std::vector<float> buf(static_cast<size_t>(d), 0.f);
            size_t pos = 0;
            for (size_t i = 0; i < n; i++) {
                float b = buf[pos];
                float x = acc[i];
                float y = -x + b;
                buf[pos] = x + b * 0.5f;
                acc[i] = y;
                if (++pos >= buf.size()) pos = 0;
            }
        }
        for (size_t i = 0; i < n; i++) dst[i] += acc[i] * wet * 0.25f;
    }
}

void delay_pingpong(const Bus& in, Bus& out, double seconds, float fb, float wet) {
    size_t d = std::max<size_t>(1, at(seconds));
    size_t n = in.l.size();
    std::vector<float> bl(d, 0.f), br(d, 0.f);
    size_t pos = 0;
    for (size_t i = 0; i < n; i++) {
        float yl = bl[pos], yr = br[pos];
        bl[pos] = in.r[i] + yr * fb;
        br[pos] = in.l[i] + yl * fb;
        out.l[i] += yl * wet;
        out.r[i] += yr * wet;
        if (++pos >= d) pos = 0;
    }
}

uint32_t seed_of(const Storyboard& b) {
    uint32_t h = 2166136261u;
    for (char c : b.prompt.empty() ? b.title : b.prompt) {
        h ^= static_cast<uint8_t>(c);
        h *= 16777619u;
    }
    return h ? h : 1u;
}

}  // namespace

std::string resolve_music(const Storyboard& board) {
    if (board.music != "auto" && known_music(board.music)) return board.music;
    const std::string& s = board.style;
    if (s == "neon") return "drive";
    if (s == "sea" || s == "forest") return "ambient";
    if (s == "paper" || s == "minimal") return "lofi";
    return "pulse";
}

double music_bpm(const std::string& mood) { return mood_of(mood).bpm; }

double beat_seconds(const Storyboard& board) {
    std::string mood = resolve_music(board);
    double base = 60.0 / music_bpm(mood == "none" ? "pulse" : mood);
    double total = total_seconds(board);
    if (total <= 0) return base;
    double units = std::max(1.0, static_cast<double>(std::lround(total / base)));
    double fitted = total / units;
    return std::abs(fitted / base - 1.0) <= 0.12 ? fitted : base;
}

void snap_to_beat(Storyboard& board, double target_seconds) {
    std::string mood = resolve_music(board);
    if (mood == "none" || board.scenes.empty()) return;
    double base = 60.0 / music_bpm(mood);
    double target = target_seconds > 0 ? target_seconds : total_seconds(board);
    int n = static_cast<int>(board.scenes.size());
    int units = static_cast<int>(std::lround(target / base));
    double unit = units > 0 ? target / units : base;
    if (units < 2 * n || std::abs(unit / base - 1.0) > 0.12) {
        units = std::max(units, 2 * n);
        unit = base;
    }
    double sum = std::max(1e-6, total_seconds(board));
    std::vector<int> give(static_cast<size_t>(n), 2);
    std::vector<double> want(static_cast<size_t>(n));
    int used = 2 * n;
    for (int i = 0; i < n; i++) want[static_cast<size_t>(i)] = board.scenes[static_cast<size_t>(i)].seconds / sum * units;
    while (used < units) {
        int best = 0;
        double gap = -1e9;
        for (int i = 0; i < n; i++) {
            double g = want[static_cast<size_t>(i)] - give[static_cast<size_t>(i)];
            if (g > gap) {
                gap = g;
                best = i;
            }
        }
        give[static_cast<size_t>(best)]++;
        used++;
    }
    for (int i = 0; i < n; i++) board.scenes[static_cast<size_t>(i)].seconds = std::min(20.0, give[static_cast<size_t>(i)] * unit);
}

std::vector<int16_t> render_soundtrack(const Storyboard& input) {
    Storyboard b = input;
    normalize(b);
    std::string mood_id = resolve_music(b);
    if (mood_id == "none") return {};
    Mood m = mood_of(mood_id);
    double total = total_seconds(b);
    size_t n = at(total);
    if (n < 16) return {};
    uint32_t seed = seed_of(b);
    Rng rng{seed};

    Bus music, drums, fx, send, echo;
    music.init(n);
    drums.init(n);
    fx.init(n);
    send.init(n);
    echo.init(n);

    static const int keys[] = {57, 55, 52, 50, 53};
    int key = keys[seed % 5];
    static const int progs[3][4][4] = {
        {{0, 3, 7, 10}, {-4, 0, 3, 7}, {3, 7, 10, 14}, {-2, 2, 5, 9}},
        {{0, 3, 7, 10}, {-2, 2, 5, 9}, {-4, 0, 3, 7}, {-5, -1, 2, 5}},
        {{0, 3, 7, 10}, {-7, -4, 0, 3}, {-4, 0, 3, 7}, {-2, 2, 5, 9}},
    };
    const int(*prog)[4] = progs[(seed >> 8) % 3];

    double beat = beat_seconds(b);
    double bar = beat * 4.0;
    int bars = static_cast<int>(std::ceil(total / bar)) + 1;
    bool drums_on_from_start = total < bar * 3.0;
    int last_full = static_cast<int>(std::floor(total / bar)) - 1;

    for (int bi = 0; bi < bars; bi++) {
        double t0 = bi * bar;
        if (t0 >= total) break;
        const int* ch = prog[bi % 4];
        std::vector<int> notes;
        for (int k = 0; k < (m.sevenths ? 4 : 3); k++) notes.push_back(key + ch[k]);
        if (!m.sevenths) notes.push_back(key + ch[0] + 12);
        pad_chord(music, send, t0, bar, notes, m.pad_amp, m.pad_cut, rng);

        int root = key - 12 + ch[0];
        while (root > 50) root -= 12;
        float bamp = 0.32f;
        switch (m.bass) {
            case 1:
                bass_note(music, t0, bar * 0.98, root, bamp * 0.8f);
                break;
            case 2:
                for (int k = 0; k < 8; k++) bass_note(music, t0 + k * beat * 0.5, beat * 0.42, root + (k == 7 ? 7 : 0), bamp * (k % 2 ? 0.7f : 0.9f));
                break;
            case 3:
                for (int k = 0; k < 16; k++)
                    if (k % 4 != 0) bass_note(music, t0 + k * beat * 0.25, beat * 0.2, root + (k % 8 == 6 ? 12 : 0), bamp * 0.85f);
                break;
            case 4: {
                const double hits[] = {0.0, 1.5, 2.5, 3.0};
                for (double h : hits) bass_note(music, t0 + h * beat, beat * 0.7, root + (h == 3.0 ? 7 : 0), bamp);
                break;
            }
        }

        std::vector<int> arp;
        for (int k = 0; k < 3; k++) arp.push_back(key + 12 + ch[k]);
        arp.push_back(key + 24 + ch[0]);
        switch (m.arp) {
            case 1:
                for (int k = 0; k < 4; k++)
                    if ((k + bi) % 2 == 0) pluck(music, echo, t0 + k * beat, arp[static_cast<size_t>((k + bi) % 4)] + 12, 0.10f, (k % 2) ? 0.4f : -0.4f, 0.9f, true);
                break;
            case 2:
                for (int k = 0; k < 8; k++)
                    pluck(music, echo, t0 + k * beat * 0.5, arp[static_cast<size_t>((k * 3 + bi) % 4)], 0.075f, (k % 2) ? 0.35f : -0.35f, 0.22f, false);
                break;
            case 3:
                for (int k = 0; k < 16; k++)
                    pluck(music, echo, t0 + k * beat * 0.25, arp[static_cast<size_t>(k % 4)] + (k % 8 >= 4 ? 12 : 0), 0.055f,
                          (k % 2) ? 0.3f : -0.3f, 0.12f, false);
                break;
            case 4: {
                const double stabs[] = {0.5, 2.0, 3.5};
                for (double s : stabs)
                    for (int k = 0; k < 4; k++) pluck(music, echo, t0 + s * beat + k * 0.012, key + ch[k], 0.07f, -0.2f + 0.13f * k, 0.6f, true);
                break;
            }
        }

        bool drums_here = (bi >= 1 || drums_on_from_start) && !(bi > last_full && bars > 4 && bi > 1);
        if (!drums_here) continue;
        for (int k = 0; k < 16; k++) {
            double step = t0 + k * beat * 0.25;
            if (m.swing > 0.f && k % 2 == 1) step += beat * 0.25 * m.swing;
            if (step >= total) break;
            switch (m.kick) {
                case 1:
                    if (k % 4 == 0) kick(drums, step, 0.55f, rng);
                    break;
                case 2:
                    if (k % 4 == 0) kick(drums, step, 0.75f, rng);
                    break;
                case 3:
                    if (k == 0 || k == 10 || (k == 8 && bi % 2)) kick(drums, step, 0.6f, rng);
                    break;
            }
            switch (m.hat) {
                case 1:
                    if (k % 4 == 2) hat(drums, step, 0.16f, 0.05f, 0.25f, rng);
                    break;
                case 2:
                    hat(drums, step, k % 4 == 2 ? 0.17f : 0.07f, k % 4 == 2 ? 0.06f : 0.025f, 0.25f, rng);
                    break;
                case 3:
                    if (k % 2 == 0) hat(drums, step, k % 4 == 0 ? 0.08f : 0.12f, 0.04f, 0.2f, rng);
                    break;
            }
            switch (m.snare) {
                case 1:
                    if (k == 4 || k == 12) clap(drums, send, step, 0.22f, rng, false);
                    break;
                case 2:
                    if (k == 4 || k == 12) clap(drums, send, step, 0.32f, rng, false);
                    break;
                case 3:
                    if (k == 4 || k == 12) clap(drums, send, step, 0.2f, rng, true);
                    break;
            }
        }
    }

    double start = 0;
    int ns = static_cast<int>(b.scenes.size());
    for (int i = 0; i < ns; i++) {
        if (i > 0) {
            std::string tr = resolve_transition(b, i);
            std::string layout = resolve_layout(b, i);
            if (layout == "end" && start > 2.5) {
                riser(fx, start, std::min(1.8, start - 0.5), 0.16f, rng);
                boom(fx, send, start, 0.42f, rng);
            } else if (tr != "cut") {
                whoosh(fx, start, std::min(0.6, b.scenes[static_cast<size_t>(i) - 1].seconds * 0.5), 0.16f, rng);
                if (tr == "flash" || tr == "zoom") boom(fx, send, start, 0.22f, rng);
            }
        }
        start += b.scenes[static_cast<size_t>(i)].seconds;
    }
    boom(fx, send, 0.0, 0.28f, rng);

    if (m.crackle) {
        float lp = 0.f;
        for (size_t i = 0; i < n; i++) {
            float x = rng.next();
            lp += (x - lp) * 0.08f;
            float v = lp * 0.012f;
            if (((rng.s >> 7) & 0x3FFF) == 0) v += rng.next() * 0.25f;
            fx.add(i, v, 0.f);
        }
    }

    delay_pingpong(echo, music, beat * 0.75, 0.38f, 0.32f);
    for (size_t i = 0; i < n; i++) {
        send.l[i] += echo.l[i] * 0.5f;
        send.r[i] += echo.r[i] * 0.5f;
        music.l[i] += echo.l[i];
        music.r[i] += echo.r[i];
    }
    Bus wet;
    wet.init(n);
    reverb(send, wet, mood_id == "ambient" ? 1.0f : 0.7f);

    std::vector<float> duck(n, 1.f);
    if (m.duck) {
        double t = (drums_on_from_start ? 0.0 : bar);
        for (; t < total; t += beat) {
            size_t s0 = at(t);
            size_t len = at(beat);
            for (size_t i = 0; i < len && s0 + i < n; i++) {
                float tt = static_cast<float>(i) / kAudioRate;
                duck[s0 + i] = std::min(duck[s0 + i], 1.f - 0.45f * std::exp(-tt / 0.11f));
            }
        }
    }

    std::vector<float> L(n), R(n), mags;
    mags.reserve(n / 8 + 1);
    for (size_t i = 0; i < n; i++) {
        float g = duck[i];
        L[i] = (music.l[i] + wet.l[i]) * g + drums.l[i] + fx.l[i];
        R[i] = (music.r[i] + wet.r[i]) * g + drums.r[i] + fx.r[i];
        if (i % 8 == 0) mags.push_back(std::max(std::fabs(L[i]), std::fabs(R[i])));
    }
    size_t q = mags.size() * 995 / 1000;
    std::nth_element(mags.begin(), mags.begin() + static_cast<std::ptrdiff_t>(q), mags.end());
    float gain = 0.72f / std::max(1e-5f, mags[q]);
    double fade_in = 0.12, fade_out = std::min(1.4, total * 0.25);
    std::vector<int16_t> out(n * 2);
    for (size_t i = 0; i < n; i++) {
        double t = static_cast<double>(i) / kAudioRate;
        float f = 1.f;
        if (t < fade_in) f = static_cast<float>(t / fade_in);
        if (total - t < fade_out) f = std::min(f, static_cast<float>((total - t) / fade_out));
        float l = std::tanh(L[i] * gain * 1.15f) * f;
        float r = std::tanh(R[i] * gain * 1.15f) * f;
        out[i * 2] = static_cast<int16_t>(std::clamp(l, -1.f, 1.f) * 32000.f);
        out[i * 2 + 1] = static_cast<int16_t>(std::clamp(r, -1.f, 1.f) * 32000.f);
    }
    return out;
}

std::vector<uint8_t> wav_bytes(const std::vector<int16_t>& stereo, size_t start_frame) {
    size_t frames = stereo.size() / 2;
    if (start_frame > frames) start_frame = frames;
    uint32_t data = static_cast<uint32_t>((frames - start_frame) * 4);
    std::vector<uint8_t> w(44 + data);
    auto u32 = [&](size_t o, uint32_t v) {
        for (int k = 0; k < 4; k++) w[o + static_cast<size_t>(k)] = static_cast<uint8_t>(v >> (8 * k));
    };
    auto u16 = [&](size_t o, uint16_t v) {
        w[o] = static_cast<uint8_t>(v);
        w[o + 1] = static_cast<uint8_t>(v >> 8);
    };
    std::memcpy(&w[0], "RIFF", 4);
    u32(4, 36 + data);
    std::memcpy(&w[8], "WAVEfmt ", 8);
    u32(16, 16);
    u16(20, 1);
    u16(22, 2);
    u32(24, kAudioRate);
    u32(28, kAudioRate * 4);
    u16(32, 4);
    u16(34, 16);
    std::memcpy(&w[36], "data", 4);
    u32(40, data);
    if (data) std::memcpy(&w[44], &stereo[start_frame * 2], data);
    return w;
}

std::vector<float> audio_peaks(const std::vector<int16_t>& stereo, int buckets) {
    std::vector<float> out(static_cast<size_t>(std::max(0, buckets)), 0.f);
    size_t frames = stereo.size() / 2;
    if (!frames || buckets <= 0) return out;
    for (int b = 0; b < buckets; b++) {
        size_t s0 = frames * static_cast<size_t>(b) / static_cast<size_t>(buckets);
        size_t s1 = frames * static_cast<size_t>(b + 1) / static_cast<size_t>(buckets);
        int peak = 0;
        for (size_t i = s0; i < s1; i += 4) peak = std::max(peak, std::abs(static_cast<int>(stereo[i * 2])));
        out[static_cast<size_t>(b)] = peak / 32768.f;
    }
    return out;
}

}  // namespace kare
