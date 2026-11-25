/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian */
/* State restore test for the built CLAP and VST3 binaries, loaded the way a host loads
 * them. A host may hand saved state to the plugin in pieces of any size; the restored
 * parameters must not depend on that.
 *
 *   plugin_state <clap|vst3> <plugin binary>
 *
 * 1. Save the default state of a fresh instance.
 * 2. Build a state with every parameter moved off its default.
 * 3. Load it into one instance in a single read, and into another in reads of 1, 2, 3,
 *    5, 7, 11 and 13 bytes, leaving zeros in the rest of the plugin's read buffer.
 * 4. Save both again: they must be byte-identical, and differ from the defaults.
 *
 * This caught a DPF bug (state parameters cut at a read boundary), fixed by
 * patches/dpf-state-chunked-read.patch. */
#include "clap/entry.h"
#include "clap/ext/state.h"
#include "clap/host.h"
#include "clap/plugin-factory.h"

#include "travesty/base.h"
#include "travesty/bstream.h"
#include "travesty/component.h"
#include "travesty/factory.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#  include <windows.h>
static void* open_lib(const char* p) { return (void*)LoadLibraryA(p); }
static void* lib_sym(void* l, const char* s) { return (void*)GetProcAddress((HMODULE)l, s); }
#else
#  include <dlfcn.h>
static void* open_lib(const char* p) { return dlopen(p, RTLD_NOW | RTLD_LOCAL); }
static void* lib_sym(void* l, const char* s) { return dlsym(l, s); }
#endif

typedef std::vector<std::pair<std::string, std::string>> Params;

static const char BEGIN[] = "__dpf_parameters_begin__";
static const char END[] = "__dpf_parameters_end__";

/* Parameter symbol/value pairs from a DPF state blob (strings separated by '\0'). */
static Params parse(const std::string& blob)
{
    Params p;
    std::vector<std::string> s;
    for (size_t i = 0; i < blob.size();) {
        const size_t e = blob.find('\0', i);
        s.push_back(blob.substr(i, (e == std::string::npos ? blob.size() : e) - i));
        if (e == std::string::npos) break;
        i = e + 1;
    }
    size_t i = 0;
    while (i < s.size() && s[i] != BEGIN) ++i;
    for (++i; i + 1 < s.size() && s[i] != END; i += 2) p.push_back(std::make_pair(s[i], s[i + 1]));
    return p;
}

static std::string build(const Params& p)
{
    std::string b(BEGIN);
    b += '\0';
    for (size_t i = 0; i < p.size(); ++i) {
        b += p[i].first; b += '\0';
        b += p[i].second; b += '\0';
    }
    b += END; b += '\0';
    b += '\xfe'; b += '\0';
    return b;
}

/* The same parameters, every value moved; long values cross more reads. (Integer
 * parameters read the moved value with atoi, so they move too.) */
static Params moved(const Params& def)
{
    Params p = def;
    for (size_t i = 0; i < p.size(); ++i) {
        char v[64];
        std::snprintf(v, sizeof v, "%.6f", std::atof(p[i].second.c_str()) - 3.141592);
        p[i].second = v;
    }
    return p;
}

/* A state source that returns `data` either whole or in small prime-sized pieces,
 * leaving zeros in the rest of the reader's buffer (as a host's reused buffer may). */
struct Source {
    std::string data;
    size_t pos = 0;
    bool chunked = false;
    size_t k = 0;

    size_t read(void* buffer, size_t size)
    {
        static const size_t sizes[] = { 1, 2, 3, 5, 7, 11, 13 };
        size_t n = data.size() - pos;
        if (n > size) n = size;
        if (chunked && n > sizes[k % 7]) n = sizes[k++ % 7];
        std::memcpy(buffer, data.data() + pos, n);
        if (chunked) std::memset((char*)buffer + n, 0, size - n);
        pos += n;
        return n;
    }
};

/* ------------------------------------------------------------------ CLAP */

static const clap_plugin_entry_t* g_clap;
static const clap_plugin_factory_t* g_clap_factory;

static const void* host_ext(const clap_host_t*, const char*) { return nullptr; }
static void host_noop(const clap_host_t*) {}
static const clap_host_t g_host = { CLAP_VERSION_INIT, nullptr, "davidian plugin_state", "Davidian", "", "1",
                                    host_ext, host_noop, host_noop, host_noop };

static int64_t clap_in_read(const clap_istream_t* s, void* buffer, uint64_t size)
{
    return (int64_t)((Source*)s->ctx)->read(buffer, (size_t)size);
}

static int64_t clap_out_write(const clap_ostream_t* s, const void* buffer, uint64_t size)
{
    ((std::string*)s->ctx)->append((const char*)buffer, (size_t)size);
    return (int64_t)size;
}

/* Loads `in` (when given) into a fresh instance, then returns its saved state. */
static bool clap_roundtrip(Source* in, std::string& out)
{
    const clap_plugin_descriptor_t* d = g_clap_factory->get_plugin_descriptor(g_clap_factory, 0);
    const clap_plugin_t* p = d ? g_clap_factory->create_plugin(g_clap_factory, &g_host, d->id) : nullptr;
    if (!p || !p->init(p)) return false;
    const clap_plugin_state_t* st = (const clap_plugin_state_t*)p->get_extension(p, CLAP_EXT_STATE);
    bool ok = st != nullptr;
    if (ok && in) {
        clap_istream_t is = { in, clap_in_read };
        ok = st->load(p, &is);
    }
    if (ok) {
        clap_ostream_t os = { &out, clap_out_write };
        ok = st->save(p, &os);
    }
    p->destroy(p);
    return ok;
}

/* ------------------------------------------------------------------ VST3 */

static v3_plugin_factory** g_vst3;
static v3_tuid g_vst3_class;

/* A VST3 object: a pointer to it is what the plugin receives as v3_bstream**. */
struct Stream {
    v3_bstream_cpp* vtbl;
    Source* in;
    std::string* out;
};

static v3_result V3_API bs_query(void* self, const v3_tuid iid, void** obj)
{
    if (v3_tuid_match(iid, v3_funknown_iid) || v3_tuid_match(iid, v3_bstream_iid)) {
        *obj = self;
        return V3_OK;
    }
    *obj = nullptr;
    return V3_NO_INTERFACE;
}
static uint32_t V3_API bs_ref(void*) { return 1; }
static v3_result V3_API bs_read(void* self, void* buffer, int32_t n, int32_t* got)
{
    Stream* s = (Stream*)self;
    const int32_t r = s->in ? (int32_t)s->in->read(buffer, (size_t)n) : 0;
    if (got) *got = r;
    return V3_OK;
}
static v3_result V3_API bs_write(void* self, void* buffer, int32_t n, int32_t* put)
{
    Stream* s = (Stream*)self;
    if (s->out) s->out->append((const char*)buffer, (size_t)n);
    if (put) *put = n;
    return V3_OK;
}
static v3_result V3_API bs_seek(void*, int64_t, int32_t, int64_t* r) { if (r) *r = 0; return V3_OK; }
static v3_result V3_API bs_tell(void*, int64_t* p) { if (p) *p = 0; return V3_OK; }

static bool vst3_roundtrip(Source* in, std::string& out)
{
    static v3_bstream_cpp vtbl;
    vtbl.query_interface = bs_query;
    vtbl.ref = bs_ref;
    vtbl.unref = bs_ref;
    vtbl.stream.read = bs_read;
    vtbl.stream.write = bs_write;
    vtbl.stream.seek = bs_seek;
    vtbl.stream.tell = bs_tell;

    v3_component** comp = nullptr;
    if (v3_cpp_obj(g_vst3)->create_instance(g_vst3, g_vst3_class, v3_component_iid, (void**)&comp) != V3_OK || !comp)
        return false;
    bool ok = v3_cpp_obj_initialize(comp, nullptr) == V3_OK;
    if (ok && in) {
        Stream s = { &vtbl, in, nullptr };
        ok = v3_cpp_obj(comp)->set_state(comp, (v3_bstream**)&s) == V3_OK;
    }
    if (ok) {
        Stream s = { &vtbl, nullptr, &out };
        ok = v3_cpp_obj(comp)->get_state(comp, (v3_bstream**)&s) == V3_OK;
    }
    v3_cpp_obj_terminate(comp);
    v3_cpp_obj_unref(comp);
    return ok;
}

/* ------------------------------------------------------------------ main */

int main(int argc, char** argv)
{
    if (argc != 3 || (std::strcmp(argv[1], "clap") && std::strcmp(argv[1], "vst3"))) {
        std::printf("usage: plugin_state <clap|vst3> <plugin binary>\n");
        return 2;
    }
    const bool clap = !std::strcmp(argv[1], "clap");
    void* lib = open_lib(argv[2]);
    if (!lib) {
        std::printf("FAIL: cannot load %s\n", argv[2]);
        return 1;
    }

    bool (*roundtrip)(Source*, std::string&);
    if (clap) {
        g_clap = (const clap_plugin_entry_t*)lib_sym(lib, "clap_entry");
        if (!g_clap || !g_clap->init(argv[2])) { std::printf("FAIL: clap_entry\n"); return 1; }
        g_clap_factory = (const clap_plugin_factory_t*)g_clap->get_factory(CLAP_PLUGIN_FACTORY_ID);
        if (!g_clap_factory) { std::printf("FAIL: no plugin factory\n"); return 1; }
        roundtrip = clap_roundtrip;
    } else {
#if defined(__APPLE__)
        typedef bool (*Entry)(void*);
        Entry entry = (Entry)lib_sym(lib, "bundleEntry");
        if (entry) entry(nullptr);
#elif defined(_WIN32)
        typedef bool (*Entry)(void);
        Entry entry = (Entry)lib_sym(lib, "InitDll");
        if (entry) entry();
#else
        typedef bool (*Entry)(void*);
        Entry entry = (Entry)lib_sym(lib, "ModuleEntry");
        if (entry) entry(nullptr);
#endif
        typedef v3_plugin_factory** (*GetFactory)(void);
        GetFactory gf = (GetFactory)lib_sym(lib, "GetPluginFactory");
        g_vst3 = gf ? gf() : nullptr;
        bool found = false;
        for (int32_t i = 0; g_vst3 && i < v3_cpp_obj(g_vst3)->num_classes(g_vst3); ++i) {
            v3_class_info info;
            if (v3_cpp_obj(g_vst3)->get_class_info(g_vst3, i, &info) == V3_OK &&
                !std::strcmp(info.category, "Audio Module Class")) {
                std::memcpy(g_vst3_class, info.class_id, sizeof g_vst3_class);
                found = true;
                break;
            }
        }
        if (!found) { std::printf("FAIL: no VST3 audio module class\n"); return 1; }
        roundtrip = vst3_roundtrip;
    }

    std::string def;
    if (!roundtrip(nullptr, def)) { std::printf("FAIL: default state\n"); return 1; }
    const Params defaults = parse(def);
    if (defaults.empty()) { std::printf("FAIL: no parameters in the saved state\n"); return 1; }
    const std::string state = build(moved(defaults));

    Source whole; whole.data = state;
    Source pieces; pieces.data = state; pieces.chunked = true;
    std::string a, b;
    if (!roundtrip(&whole, a) || !roundtrip(&pieces, b)) { std::printf("FAIL: load/save\n"); return 1; }

    const Params pa = parse(a), pb = parse(b);
    int fail = 0;
    for (size_t i = 0; i < pa.size(); ++i) {
        const std::string vb = i < pb.size() ? pb[i].second : std::string("(missing)");
        const bool same = i < pb.size() && pa[i] == pb[i];
        std::printf("  %-20s default %-12s whole read %-12s small reads %-12s %s\n", pa[i].first.c_str(),
                    defaults[i].second.c_str(), pa[i].second.c_str(), vb.c_str(), same ? "ok" : "MISMATCH");
        fail |= !same;
    }
    if (a != b) fail = 1;
    if (pa == defaults) { std::printf("FAIL: the loaded state changed nothing\n"); fail = 1; }
    std::printf("%s %s: state restore %s\n", argv[1], argv[2], fail ? "depends on read size: FAIL" : "ok");
    if (clap) g_clap->deinit();
    return fail;
}
