/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian */
/* Measurement harness: an offline host that runs a plugin's DSP directly (no DAW, no
 * plugin format) and checks it the way docs/ENVIRONMENT.md section 11 describes.
 *
 * Fails on
 *   - NaN or infinity, output above the ceiling, denormal output        (robustness)
 *     at every sample rate and block size, with silence, sine, full-scale noise,
 *     impulses and square waves, and with random block sizes
 *   - the plugin changing the host thread's flush-to-zero mode
 *   - DC offset, with silence and with a sine at the input                (dc)
 *   - clicks when any parameter jumps between its default, maximum and minimum (clicks):
 *     content above 4 kHz right after the change, over the settled sound and above -50 dBFS
 *   - a sample-rate change giving different output from a fresh instance (rate_change)
 *   - processing slower than --min-realtime times real time               (cpu)
 *   - THD or aliasing above --max-thd-db / --max-alias-db, when given     (thd, aliasing)
 * Reports THD and harmonic profile, aliasing (non-harmonic energy in the audible band
 * from a hot high sine), latency and CPU load.
 *
 * Every block is processed inside rt_process(), marked [[clang::nonblocking]]: in a
 * RealtimeSanitizer build any allocation, lock or system call made by the plugin while
 * processing or applying a parameter change aborts the run.
 *
 * Usage: harness_<plugin> [options]   (see usage() below) */
#include "analysis.h"
#include "dut.h"

#include "denormal.h"
#include "filters.h"

#include <algorithm>
#include <cfloat>
#include <cstdarg>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#if defined(__has_feature)
#  if __has_feature(realtime_sanitizer)
#    define HARNESS_RTSAN 1
#  endif
#endif
#ifdef HARNESS_RTSAN
#  define RT_NONBLOCKING [[clang::nonblocking]]
#else
#  define RT_NONBLOCKING
#endif

static const uint32_t MAX_BLOCK = 4096;
static const uint32_t MAX_CH = 16;

/* ------------------------------------------------------------ options */

struct Options {
    std::vector<double> rates;
    std::vector<uint32_t> blocks; /* 0 = random sizes */
    double ceiling_db = 12.0;
    double dc_db = -60.0;
    double click_db = 6.0;
    double click_floor_db = -50.0;
    double max_alias_db = NAN;
    double max_thd_db = NAN;
    double min_realtime = 1.5;
    std::vector<std::string> allow_step;
    std::string report;
};

static void usage()
{
    std::printf(
        "usage: harness_<plugin> [options]\n"
        "  --rates 44100,48000,...   sample rates (default 44.1, 48, 88.2, 96, 176.4, 192 kHz)\n"
        "  --blocks 1,7,64,...       block sizes, 0 = random sizes (default 1,7,64,512,4096,0)\n"
        "  --ceiling-db X            fail above X dBFS output (default +12)\n"
        "  --dc-db X                 fail on DC above X dBFS (default -60)\n"
        "  --click-db X              a parameter change clicks when its content above 4 kHz is\n"
        "                            more than X dB over the settled sound (default 6)...\n"
        "  --click-floor-db X        ...and louder than X dBFS (default -50)\n"
        "  --allow-step SYMBOL       don't check this parameter for clicks (repeatable)\n"
        "  --max-thd-db X            fail on THD above X dB (default: report only)\n"
        "  --max-alias-db X          fail on aliasing above X dB (default: report only)\n"
        "  --min-realtime X          fail below X times real time; 0 = report only (default 1.5)\n"
        "  --report FILE             also write the results as a Markdown table\n");
}

static std::vector<std::string> split(const char* s)
{
    std::vector<std::string> v;
    std::string cur;
    for (; *s; ++s) {
        if (*s == ',') { v.push_back(cur); cur.clear(); }
        else cur += *s;
    }
    if (!cur.empty()) v.push_back(cur);
    return v;
}

static bool parse(int argc, char** argv, Options& o)
{
    o.rates = { 44100, 48000, 88200, 96000, 176400, 192000 };
    o.blocks = { 1, 7, 64, 512, 4096, 0 };
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        const bool has = i + 1 < argc;
        if (a == "--rates" && has) {
            o.rates.clear();
            for (const std::string& s : split(argv[++i])) o.rates.push_back(std::atof(s.c_str()));
        } else if (a == "--blocks" && has) {
            o.blocks.clear();
            for (const std::string& s : split(argv[++i])) o.blocks.push_back((uint32_t)std::atoi(s.c_str()));
        } else if (a == "--ceiling-db" && has) o.ceiling_db = std::atof(argv[++i]);
        else if (a == "--dc-db" && has) o.dc_db = std::atof(argv[++i]);
        else if (a == "--click-db" && has) o.click_db = std::atof(argv[++i]);
        else if (a == "--click-floor-db" && has) o.click_floor_db = std::atof(argv[++i]);
        else if (a == "--allow-step" && has) o.allow_step.push_back(argv[++i]);
        else if (a == "--max-thd-db" && has) o.max_thd_db = std::atof(argv[++i]);
        else if (a == "--max-alias-db" && has) o.max_alias_db = std::atof(argv[++i]);
        else if (a == "--min-realtime" && has) o.min_realtime = std::atof(argv[++i]);
        else if (a == "--report" && has) o.report = argv[++i];
        else { usage(); return false; }
    }
    for (uint32_t b : o.blocks)
        if (b > MAX_BLOCK) { std::printf("block size %u above %u\n", b, MAX_BLOCK); return false; }
    return !o.rates.empty() && !o.blocks.empty();
}

/* ------------------------------------------------------------ results */

struct Result {
    std::string test, detail;
    double rate;
    bool pass;
};

static std::vector<Result> g_results;

static void record(const char* test, double rate, bool pass, const char* fmt, ...)
#if defined(__MINGW32__)
    __attribute__((format(gnu_printf, 4, 5))) /* MinGW: plain printf means Microsoft's */
#elif defined(__GNUC__)
    __attribute__((format(printf, 4, 5)))
#endif
    ;

static void record(const char* test, double rate, bool pass, const char* fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    Result r;
    r.test = test;
    r.rate = rate;
    r.pass = pass;
    r.detail = buf;
    g_results.push_back(r);
    std::printf("%-5s %-12s %6.1f kHz  %s\n", pass ? "ok" : "FAIL", test, rate / 1000.0, buf);
    std::fflush(stdout);
}

/* ------------------------------------------------------------ signals */

typedef std::vector<float> Signal;

static void add_sine(Signal& s, double secs, double rate, double freq, double amp)
{
    const size_t n = (size_t)(secs * rate);
    for (size_t i = 0; i < n; ++i) s.push_back((float)(amp * std::sin(2.0 * analysis::PI * freq * (double)i / rate)));
}

static void add_silence(Signal& s, double secs, double rate)
{
    s.insert(s.end(), (size_t)(secs * rate), 0.0f);
}

static void add_noise(Signal& s, double secs, double rate, double amp)
{
    uint32_t seed = 12345;
    const size_t n = (size_t)(secs * rate);
    for (size_t i = 0; i < n; ++i) {
        seed = seed * 1664525u + 1013904223u;
        s.push_back((float)(amp * ((double)(seed >> 8) / 8388608.0 - 1.0)));
    }
}

static void add_impulses(Signal& s, double secs, double rate, double every)
{
    const size_t n = (size_t)(secs * rate), step = (size_t)(every * rate);
    for (size_t i = 0; i < n; ++i) s.push_back(i % step == 0 ? 1.0f : 0.0f);
}

static void add_square(Signal& s, double secs, double rate, double freq, double amp)
{
    const size_t n = (size_t)(secs * rate);
    for (size_t i = 0; i < n; ++i)
        s.push_back(std::fmod(freq * (double)i / rate, 1.0) < 0.5 ? (float)amp : (float)-amp);
}

/* A sine exactly on FFT bin k of an n-point frame, continuing for `total` samples. */
static Signal coherent_sine(size_t k, size_t n, size_t total, double amp)
{
    Signal s(total);
    for (size_t i = 0; i < total; ++i)
        s[i] = (float)(amp * std::sin(2.0 * analysis::PI * (double)k * (double)(i % n) / (double)n));
    return s;
}

/* ------------------------------------------------------------ processing */

struct ParamEvent {
    uint32_t index;
    float value;
};

struct TimedEvent {
    size_t at; /* applied at the first block starting at or after this sample */
    ParamEvent ev;
};

/* The audio-thread call: pending parameter changes, then one block, as a host does it. */
#if defined(__clang__)
#  pragma clang diagnostic push
#  pragma clang diagnostic ignored "-Wunknown-warning-option"
#  pragma clang diagnostic ignored "-Wfunction-effects"
#endif
static void rt_process(Dut* d, const ParamEvent* ev, uint32_t nev, const float** in, float** out,
                       uint32_t frames) noexcept RT_NONBLOCKING
{
    for (uint32_t i = 0; i < nev; ++i) d->set_param(ev[i].index, ev[i].value);
    d->run(in, out, frames);
}
#if defined(__clang__)
#  pragma clang diagnostic pop
#endif

struct Render {
    std::vector<Signal> out;  /* one per output channel */
    double cpu_seconds = 0.0;
    bool fp_mode_changed = false;
};

/* Runs `sig` into every input; block 0 = random block sizes. All buffers are allocated
 * before processing starts. */
static Render render(Dut* d, const Signal& sig, uint32_t block, const std::vector<TimedEvent>& events)
{
    Render r;
    const uint32_t ni = d->inputs(), no = d->outputs();
    std::vector<Signal> inbuf(ni, Signal(MAX_BLOCK)), outbuf(no, Signal(MAX_BLOCK));
    const float* in[MAX_CH];
    float* out[MAX_CH];
    for (uint32_t c = 0; c < ni && c < MAX_CH; ++c) in[c] = inbuf[c].data();
    for (uint32_t c = 0; c < no && c < MAX_CH; ++c) out[c] = outbuf[c].data();
    r.out.assign(no, Signal(sig.size()));
    std::vector<ParamEvent> pending;
    pending.reserve(events.size() + 1);

    uint32_t seed = 777;
    size_t next_ev = 0;
    const int ftz0 = denormal_ftz_active();
    const std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
    for (size_t pos = 0; pos < sig.size();) {
        uint32_t n = block;
        if (n == 0) {
            seed = seed * 1664525u + 1013904223u;
            n = 1 + (seed >> 8) % (1u << ((seed >> 4) % 13)); /* 1 .. 4096, log-spread */
        }
        if (n > sig.size() - pos) n = (uint32_t)(sig.size() - pos);
        pending.clear();
        while (next_ev < events.size() && events[next_ev].at <= pos) pending.push_back(events[next_ev++].ev);
        for (uint32_t c = 0; c < ni; ++c) std::memcpy(inbuf[c].data(), &sig[pos], n * sizeof(float));
        rt_process(d, pending.data(), (uint32_t)pending.size(), in, out, n);
        for (uint32_t c = 0; c < no; ++c) std::memcpy(&r.out[c][pos], outbuf[c].data(), n * sizeof(float));
        if (denormal_ftz_active() != ftz0) r.fp_mode_changed = true;
        pos += n;
    }
    r.cpu_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    return r;
}

static Dut* fresh(double rate)
{
    Dut* d = Dut::create(rate, MAX_BLOCK);
    if (d) d->activate();
    return d;
}

static std::string block_name(uint32_t b)
{
    return b == 0 ? std::string("random") : std::to_string(b);
}

/* ------------------------------------------------------------ tests */

/* NaN/inf, ceiling, denormals, FP mode; returns the real-time factor. */
static double test_robustness(const Options& o, double rate, uint32_t block)
{
    Signal s;
    add_silence(s, 0.2, rate);
    add_sine(s, 0.4, rate, 220.0, 0.5);
    add_noise(s, 0.3, rate, 1.0);
    add_impulses(s, 0.2, rate, 0.01);
    add_square(s, 0.2, rate, 110.0, 1.0);
    add_silence(s, 1.0, rate);

    Dut* d = fresh(rate);
    const Render r = render(d, s, block, std::vector<TimedEvent>());
    delete d;

    const double ceiling = std::pow(10.0, o.ceiling_db / 20.0);
    size_t bad = 0, over = 0, denorm = 0;
    double peak = 0.0;
    for (const Signal& ch : r.out)
        for (float y : ch) {
            if (dsp_bad(y)) { ++bad; continue; }
            const double a = std::fabs((double)y);
            if (a > peak) peak = a;
            if (a > ceiling) ++over;
            if (y != 0.0f && a < FLT_MIN) ++denorm;
        }
    const double rt = (double)s.size() / rate / (r.cpu_seconds > 1e-9 ? r.cpu_seconds : 1e-9);
    const bool pass = !bad && !over && !denorm && !r.fp_mode_changed;
    record("robustness", rate, pass, "block %s: %zu NaN/inf, %zu over %+.0f dBFS, %zu denormal%s, peak %+.1f dBFS, x%.0f real time",
           block_name(block).c_str(), bad, over, o.ceiling_db, denorm,
           r.fp_mode_changed ? ", FTZ mode left changed" : "", analysis::to_db(peak * peak), rt);
    return rt;
}

static void test_dc(const Options& o, double rate)
{
    const double limit = std::pow(10.0, o.dc_db / 20.0);
    const size_t sec = (size_t)rate;
    double worst = 0.0;
    bool nan = false;

    Signal sil;
    add_silence(sil, 1.0, rate);
    Signal sine;
    add_sine(sine, 2.0, rate, 1000.0, 0.25); /* the last second holds a whole number of cycles */

    for (int pass = 0; pass < 2; ++pass) {
        const Signal& s = pass ? sine : sil;
        Dut* d = fresh(rate);
        const Render r = render(d, s, 512, std::vector<TimedEvent>());
        delete d;
        const size_t from = s.size() - (pass ? sec : sec / 2);
        for (const Signal& ch : r.out) {
            double m = 0.0;
            for (size_t i = from; i < s.size(); ++i) m += ch[i];
            m = std::fabs(m / (double)(s.size() - from));
            if (dsp_bad(m)) nan = true;
            else if (m > worst) worst = m;
        }
    }
    record("dc", rate, !nan && worst <= limit, "worst DC %.1f dBFS (limit %.0f), silence and 1 kHz sine%s",
           analysis::to_db(worst * worst), o.dc_db, nan ? ", NaN in output" : "");
}

/* Content above 4 kHz (4th-order Butterworth high-pass). A click, a sudden jump in the
 * waveform, is broadband and shows up here strongly; a properly smoothed change only
 * modulates the signal slowly and adds next to nothing, at any sample rate. */
static Signal high_band(const Signal& y, double rate)
{
    Biquad q[2];
    bq_highpass(&q[0], 4000.0, 0.54119610, rate);
    bq_highpass(&q[1], 4000.0, 1.30656296, rate);
    bq_reset(&q[0]);
    bq_reset(&q[1]);
    Signal h(y.size());
    for (size_t i = 0; i < y.size(); ++i) h[i] = (float)bq_run(&q[1], bq_run(&q[0], y[i]));
    return h;
}

static double peak_in(const Signal& h, size_t a, size_t b)
{
    double m = 0.0;
    for (size_t i = a; i < b && i < h.size(); ++i) m = std::max(m, std::fabs((double)h[i]));
    return m;
}

static void test_clicks(const Options& o, double rate)
{
    Dut* probe = Dut::create(rate, MAX_BLOCK);
    const uint32_t np = probe->param_count();
    std::vector<DutParam> params;
    for (uint32_t i = 0; i < np; ++i) params.push_back(probe->param(i));
    delete probe;

    const double seg = 0.4;
    Signal s;
    add_sine(s, 0.5 + 3 * seg + 0.1, rate, 220.0, 0.25);
    const size_t c[3] = { (size_t)(0.5 * rate), (size_t)((0.5 + seg) * rate), (size_t)((0.5 + 2 * seg) * rate) };
    const double gain = std::pow(10.0, o.click_db / 20.0), floor = std::pow(10.0, o.click_floor_db / 20.0);

    int tested = 0;
    for (uint32_t i = 0; i < np; ++i) {
        const DutParam& p = params[i];
        if (p.output || p.trigger || p.max <= p.min) continue;
        if (std::find(o.allow_step.begin(), o.allow_step.end(), p.symbol) != o.allow_step.end()) {
            record("clicks", rate, true, "%s: not checked (--allow-step)", p.symbol.c_str());
            continue;
        }
        std::vector<TimedEvent> ev;
        TimedEvent e;
        e.ev.index = i;
        e.at = c[0]; e.ev.value = p.max; ev.push_back(e);
        e.at = c[1]; e.ev.value = p.min; ev.push_back(e);
        e.at = c[2]; e.ev.value = p.def; ev.push_back(e);

        Dut* d = fresh(rate);
        const Render r = render(d, s, 64, ev);
        delete d;

        double worst = -300.0;
        bool pass = true;
        double worst_abs = 0.0;
        for (const Signal& y : r.out) {
            const Signal h = high_band(y, rate);
            for (int k = 0; k < 3; ++k) {
                const size_t at = c[k];
                const double before = peak_in(h, at - (size_t)(0.1 * rate), at);
                const double settled = peak_in(h, at + (size_t)(0.25 * rate), at + (size_t)(seg * rate));
                const double trans = peak_in(h, at, at + (size_t)(0.05 * rate));
                const double ref = std::max(before, settled);
                /* A click must stand out from the settled sound AND be loud enough to hear. */
                const bool click = trans > ref * gain && trans > floor;
                worst = std::max(worst, 20.0 * std::log10((trans + 1e-12) / (ref + 1e-12)));
                worst_abs = std::max(worst_abs, trans);
                if (click || dsp_bad(trans)) pass = false;
            }
        }
        ++tested;
        record("clicks", rate, pass,
               "%s: default -> max -> min -> default, transient above 4 kHz %.1f dBFS, %+.1f dB over settled (limits %.0f dBFS, %+.0f dB)",
               p.symbol.c_str(), analysis::to_db(worst_abs * worst_abs), std::min(worst, 99.9), o.click_floor_db, o.click_db);
    }
    if (tested == 0) record("clicks", rate, true, "no input parameters");
}

static void test_spectrum(const Options& o, double rate, uint32_t inputs)
{
    if (inputs == 0) {
        record("thd", rate, true, "no audio inputs: not measured");
        return;
    }
    const size_t n = rate > 100000.0 ? 131072 : 65536;
    const size_t warm = (size_t)(0.5 * rate);

    /* THD: 1 kHz at -12 dBFS */
    {
        const size_t k = analysis::coherent_bin(1000.0, rate, n);
        const Signal s = coherent_sine(k, n, warm + n, 0.25);
        Dut* d = fresh(rate);
        const Render r = render(d, s, 512, std::vector<TimedEvent>());
        delete d;
        double worst = -300.0;
        analysis::Distortion dist = analysis::Distortion();
        bool any = false;
        for (const Signal& y : r.out) {
            const std::vector<double> p = analysis::power_spectrum(&y[warm], n);
            const analysis::Distortion dd = analysis::distortion(p, k);
            if (dd.fund_power < 1e-20) continue;
            if (!any || dd.thd_db > worst) { worst = dd.thd_db; dist = dd; }
            any = true;
        }
        if (!any) {
            record("thd", rate, true, "1 kHz -12 dBFS: no output at the test frequency");
        } else {
            const bool pass = std::isnan(o.max_thd_db) || worst <= o.max_thd_db;
            record("thd", rate, pass, "1 kHz -12 dBFS: THD %.1f dB (H2 %.1f, H3 %.1f, H4 %.1f, H5 %.1f)%s",
                   worst, dist.h_db[2], dist.h_db[3], dist.h_db[4], dist.h_db[5],
                   std::isnan(o.max_thd_db) ? "" : (" limit " + std::to_string((int)o.max_thd_db) + " dB").c_str());
        }
    }

    /* Aliasing: 6 kHz at -6 dBFS; non-harmonic energy up to 20 kHz */
    {
        const size_t k = analysis::coherent_bin(6000.0, rate, n);
        const Signal s = coherent_sine(k, n, warm + n, 0.5);
        Dut* d = fresh(rate);
        const Render r = render(d, s, 512, std::vector<TimedEvent>());
        delete d;
        double worst = -300.0;
        bool any = false;
        for (const Signal& y : r.out) {
            const std::vector<double> p = analysis::power_spectrum(&y[warm], n);
            if (analysis::tone_power(p, k) < 1e-20) continue;
            worst = std::max(worst, analysis::non_harmonic_db(p, k, rate, std::min(20000.0, rate / 2)));
            any = true;
        }
        if (!any) {
            record("aliasing", rate, true, "6 kHz -6 dBFS: no output at the test frequency");
        } else {
            const bool pass = std::isnan(o.max_alias_db) || worst <= o.max_alias_db;
            record("aliasing", rate, pass, "6 kHz -6 dBFS: non-harmonic energy to 20 kHz %.1f dB%s", worst,
                   std::isnan(o.max_alias_db) ? "" : (" (limit " + std::to_string((int)o.max_alias_db) + " dB)").c_str());
        }
    }
}

/* A host sample-rate change must leave the plugin as a fresh instance would be. Every
 * parameter moves during the comparison, so stale rate-dependent state (smoothing
 * coefficients, filter designs) shows up. */
static void test_rate_change(double rate, double from)
{
    if (rate == from) return;
    Signal warm;
    add_sine(warm, 0.2, from, 330.0, 0.5);
    Signal s;
    add_sine(s, 0.6, rate, 440.0, 0.25);

    std::vector<TimedEvent> ev;
    {
        Dut* probe = Dut::create(rate, MAX_BLOCK);
        for (uint32_t i = 0; i < probe->param_count(); ++i) {
            const DutParam p = probe->param(i);
            if (p.output || p.trigger || p.max <= p.min) continue;
            TimedEvent e;
            e.ev.index = i;
            e.at = (size_t)(0.1 * rate); e.ev.value = p.max; ev.push_back(e);
            e.at = (size_t)(0.3 * rate); e.ev.value = p.min; ev.push_back(e);
            e.at = (size_t)(0.45 * rate); e.ev.value = p.def; ev.push_back(e);
        }
        delete probe;
        std::stable_sort(ev.begin(), ev.end(), [](const TimedEvent& a, const TimedEvent& b) { return a.at < b.at; });
    }

    Dut* a = fresh(from);
    render(a, warm, 512, std::vector<TimedEvent>());
    a->set_rate(rate);
    const Render ra = render(a, s, 512, ev);
    delete a;
    Dut* b = fresh(rate);
    const Render rb = render(b, s, 512, ev);
    delete b;

    double diff = 0.0;
    bool bad = false;
    for (size_t c = 0; c < ra.out.size(); ++c)
        for (size_t i = 0; i < s.size(); ++i) {
            const double d = std::fabs((double)ra.out[c][i] - rb.out[c][i]);
            if (dsp_bad(d)) bad = true;
            else diff = std::max(diff, d);
        }
    record("rate_change", rate, !bad && diff <= 1e-5,
           "%.1f -> %.1f kHz vs fresh instance, all parameters moving: max difference %.2e%s",
           from / 1000.0, rate / 1000.0, diff, bad ? ", NaN" : "");
}

/* ------------------------------------------------------------ main */

static void write_report(const Options& o, const char* name, uint32_t latency)
{
    if (o.report.empty()) return;
    FILE* f = std::fopen(o.report.c_str(), "w");
    if (!f) return;
    int fails = 0;
    for (const Result& r : g_results) fails += !r.pass;
    std::fprintf(f, "### Harness: %s\n\n%zu checks, %d failed. Latency %u samples.\n\n", name,
                 g_results.size(), fails, latency);
    std::fprintf(f, "| | Test | Rate | Result |\n| --- | --- | --- | --- |\n");
    for (const Result& r : g_results)
        std::fprintf(f, "| %s | %s | %.1f kHz | %s |\n", r.pass ? "ok" : "**FAIL**", r.test.c_str(),
                     r.rate / 1000.0, r.detail.c_str());
    std::fclose(f);
}

int main(int argc, char** argv)
{
    Options o;
    if (!parse(argc, argv, o)) return 2;

    Dut* info = Dut::create(o.rates[0], MAX_BLOCK);
    if (!info) {
        std::printf("FAIL: the plugin could not be created\n");
        return 1;
    }
    const std::string name = info->name();
    const uint32_t ins = info->inputs(), outs = info->outputs(), latency = info->latency();
    const uint32_t nparams = info->param_count();
    delete info;
    if (ins > MAX_CH || outs > MAX_CH) {
        std::printf("FAIL: more than %u channels\n", MAX_CH);
        return 1;
    }
#ifdef HARNESS_RTSAN
    std::printf("%s: %u in, %u out, %u parameters, latency %u samples (RealtimeSanitizer active)\n\n",
                name.c_str(), ins, outs, nparams, latency);
#else
    std::printf("%s: %u in, %u out, %u parameters, latency %u samples\n\n", name.c_str(), ins, outs, nparams, latency);
#endif

    double worst_rt = 1e30, worst_rt_rate = 0.0;
    for (double rate : o.rates) {
        for (uint32_t b : o.blocks) {
            const double rt = test_robustness(o, rate, b);
            if (b == 512 && rt < worst_rt) { worst_rt = rt; worst_rt_rate = rate; }
        }
        test_dc(o, rate);
        test_spectrum(o, rate, ins);
        test_rate_change(rate, 48000.0);
    }
    test_clicks(o, o.rates.front());
    if (o.rates.back() != o.rates.front()) test_clicks(o, o.rates.back());

    if (worst_rt < 1e30) {
        const bool pass = o.min_realtime <= 0.0 || worst_rt >= o.min_realtime;
        record("cpu", worst_rt_rate, pass, "slowest rate runs at x%.1f real time (block 512)%s", worst_rt,
               o.min_realtime > 0.0 ? (", limit x" + std::to_string(o.min_realtime).substr(0, 4)).c_str() : ", not checked");
    }

    write_report(o, name.c_str(), latency);
    int fails = 0;
    for (const Result& r : g_results) fails += !r.pass;
    std::printf("\n%s: %zu checks, %d failed\n", name.c_str(), g_results.size(), fails);
    return fails ? 1 : 0;
}
