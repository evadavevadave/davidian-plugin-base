/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian */
/* Minimal LV2 host (lilv): loads a built bundle the way a DAW does, instantiates the
 * plugin by URI, sets control ports by symbol and runs a sine through every audio input.
 *
 *   lv2_host <bundle_dir> <uri> [symbol=value ...] [--expect-gain G] [--rate R]
 *
 * Always checks: the bundle loads, the plugin instantiates, every output is finite, and
 * outputs aren't silent with a signal at the inputs (unless --expect-gain 0). With
 * --expect-gain, every output must equal input * G once smoothing has settled. */
#include <lilv/lilv.h>
#include <lv2/atom/atom.h>
#include <lv2/buf-size/buf-size.h>
#include <lv2/core/lv2.h>
#include <lv2/options/options.h>
#include <lv2/urid/urid.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BLOCK 256
#define MAX_PORTS 64
#define MAX_URIDS 256

/* URID map: the host feature every DPF plugin requires. URIDs start at 1. */
static char* g_uris[MAX_URIDS];
static uint32_t g_nuris;

static LV2_URID map_uri(LV2_URID_Map_Handle h, const char* uri)
{
    (void)h;
    for (uint32_t i = 0; i < g_nuris; ++i)
        if (!strcmp(g_uris[i], uri)) return i + 1;
    if (g_nuris == MAX_URIDS) return 0;
    g_uris[g_nuris] = (char*)malloc(strlen(uri) + 1);
    strcpy(g_uris[g_nuris], uri);
    return ++g_nuris;
}

int main(int argc, char** argv)
{
    double rate = 48000.0, expect = -1.0;
    const char* sets[MAX_PORTS];
    int nsets = 0, fail = 0;
    if (argc < 3) {
        fprintf(stderr, "usage: lv2_host <bundle_dir> <uri> [symbol=value ...] [--expect-gain G] [--rate R]\n");
        return 2;
    }
    for (int i = 3; i < argc; ++i) {
        if (!strcmp(argv[i], "--expect-gain") && i + 1 < argc) expect = atof(argv[++i]);
        else if (!strcmp(argv[i], "--rate") && i + 1 < argc) rate = atof(argv[++i]);
        else if (nsets < MAX_PORTS) sets[nsets++] = argv[i];
    }

    LilvWorld* world = lilv_world_new();
    char bundle_uri[4096];
    snprintf(bundle_uri, sizeof bundle_uri, "file://%s/", argv[1]);
    LilvNode* bnode = lilv_new_uri(world, bundle_uri);
    lilv_world_load_bundle(world, bnode);
    LilvNode* uri = lilv_new_uri(world, argv[2]);
    const LilvPlugin* plug = lilv_plugins_get_by_uri(lilv_world_get_all_plugins(world), uri);
    if (!plug) {
        printf("FAIL: %s not found in %s\n", argv[2], argv[1]);
        return 1;
    }

    LilvNode* c_audio = lilv_new_uri(world, LV2_CORE__AudioPort);
    LilvNode* c_control = lilv_new_uri(world, LV2_CORE__ControlPort);
    LilvNode* c_input = lilv_new_uri(world, LV2_CORE__InputPort);
    const uint32_t nports = lilv_plugin_get_num_ports(plug);
    if (nports > MAX_PORTS) {
        printf("FAIL: %u ports\n", nports);
        return 1;
    }

    static float ctl[MAX_PORTS];
    static float audio[MAX_PORTS][BLOCK];
    float* defaults = (float*)calloc(nports, sizeof(float));
    lilv_plugin_get_port_ranges_float(plug, NULL, NULL, defaults);

    /* Host features: URID map and options (block length, sample rate), as in a DAW. */
    LV2_URID_Map map = { NULL, map_uri };
    const int32_t block = BLOCK;
    const float frate = (float)rate;
    const LV2_URID t_int = map_uri(NULL, LV2_ATOM__Int), t_float = map_uri(NULL, LV2_ATOM__Float);
    LV2_Options_Option opts[] = {
        { LV2_OPTIONS_INSTANCE, 0, map_uri(NULL, LV2_BUF_SIZE__maxBlockLength), sizeof block, t_int, &block },
        { LV2_OPTIONS_INSTANCE, 0, map_uri(NULL, LV2_BUF_SIZE__nominalBlockLength), sizeof block, t_int, &block },
        { LV2_OPTIONS_INSTANCE, 0, map_uri(NULL, "http://lv2plug.in/ns/ext/parameters#sampleRate"), sizeof frate, t_float, &frate },
        { LV2_OPTIONS_INSTANCE, 0, 0, 0, 0, NULL },
    };
    LV2_Feature f_map = { LV2_URID__map, &map }, f_opts = { LV2_OPTIONS__options, opts };
    LV2_Feature f_bounded = { LV2_BUF_SIZE__boundedBlockLength, NULL };
    const LV2_Feature* features[] = { &f_map, &f_opts, &f_bounded, NULL };

    LilvInstance* inst = lilv_plugin_instantiate(plug, rate, features);
    if (!inst) {
        printf("FAIL: instantiate\n");
        return 1;
    }

    int nin = 0, nout = 0;
    for (uint32_t p = 0; p < nports; ++p) {
        const LilvPort* port = lilv_plugin_get_port_by_index(plug, p);
        const char* sym = lilv_node_as_string(lilv_port_get_symbol(plug, port));
        if (lilv_port_is_a(plug, port, c_control)) {
            ctl[p] = isnan(defaults[p]) ? 0.0f : defaults[p];
            for (int s = 0; s < nsets; ++s) {
                size_t n = strlen(sym);
                if (!strncmp(sets[s], sym, n) && sets[s][n] == '=') ctl[p] = (float)atof(sets[s] + n + 1);
            }
            lilv_instance_connect_port(inst, p, &ctl[p]);
        } else if (lilv_port_is_a(plug, port, c_audio)) {
            lilv_instance_connect_port(inst, p, audio[p]);
            if (lilv_port_is_a(plug, port, c_input)) ++nin; else ++nout;
        } else {
            lilv_instance_connect_port(inst, p, NULL);
        }
        printf("  port %u %-14s %s\n", p, sym,
               lilv_port_is_a(plug, port, c_control) ? "control" :
               lilv_port_is_a(plug, port, c_audio) ? "audio" : "other");
    }

    lilv_instance_activate(inst);
    const int blocks = (int)(rate / BLOCK); /* one second */
    double peak = 0.0, maxerr = 0.0;
    long t = 0;
    for (int b = 0; b < blocks; ++b) {
        for (uint32_t p = 0; p < nports; ++p) {
            const LilvPort* port = lilv_plugin_get_port_by_index(plug, p);
            if (lilv_port_is_a(plug, port, c_audio) && lilv_port_is_a(plug, port, c_input))
                for (int i = 0; i < BLOCK; ++i)
                    audio[p][i] = (float)(0.5 * sin(2.0 * M_PI * 440.0 * (t + i) / rate));
        }
        lilv_instance_run(inst, BLOCK);
        for (uint32_t p = 0; p < nports; ++p) {
            const LilvPort* port = lilv_plugin_get_port_by_index(plug, p);
            if (!lilv_port_is_a(plug, port, c_audio) || lilv_port_is_a(plug, port, c_input))
                continue;
            for (int i = 0; i < BLOCK; ++i) {
                const float y = audio[p][i];
                if (!isfinite(y)) fail = 1;
                if (fabs(y) > peak) peak = fabs(y);
                if (expect >= 0.0 && b > blocks / 2) {
                    const double want = expect * 0.5 * sin(2.0 * M_PI * 440.0 * (t + i) / rate);
                    if (fabs(y - want) > maxerr) maxerr = fabs(y - want);
                }
            }
        }
        t += BLOCK;
    }
    lilv_instance_deactivate(inst);
    lilv_instance_free(inst);

    printf("%s: %d in, %d out, %.0f Hz, peak %.4f", argv[2], nin, nout, rate, peak);
    if (expect >= 0.0) printf(", gain error %.2e (expected x%.4f)", maxerr, expect);
    printf("\n");
    if (fail) printf("FAIL: non-finite output\n");
    if (nout == 0) { printf("FAIL: no audio outputs\n"); fail = 1; }
    if (expect < 0.0 && nin > 0 && peak < 1e-6) { printf("FAIL: silent output\n"); fail = 1; }
    if (expect >= 0.0 && maxerr > 1e-5) { printf("FAIL: gain\n"); fail = 1; }

    free(defaults);
    lilv_node_free(c_audio);
    lilv_node_free(c_control);
    lilv_node_free(c_input);
    lilv_node_free(uri);
    lilv_node_free(bnode);
    lilv_world_free(world);
    for (uint32_t i = 0; i < g_nuris; ++i) free(g_uris[i]);
    return fail;
}
