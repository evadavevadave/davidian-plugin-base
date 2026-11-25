/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian */
/* House-style panel test: renders a demo layout with every widget kind to a .bmp (no
 * window needed, any platform) and checks drawing, value handling and mouse behaviour.
 * Usage: test_panel [out.bmp] */
#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>

#include "ui/panel.h"
#include "check.h"

enum { P_GAIN, P_TONE, P_LEVEL, P_MODE, P_BYPASS, P_TYPE, P_TRIM, P_VOICE, P_METER, P_VOLTS, P_COUNT };

static const Widget WIDGETS[] = {
    { W_KNOB, P_GAIN, 81, 150, 30, 0, 10, 5, "GAIN", "GAIN", "0", "10", {0}, 0, 0, 0 },
    { W_KNOB, P_TONE, 211, 150, 30, 0, 10, 5, "TONE", "TONE", "0", "10", {0}, 0, 0, 0 },
    { W_KNOB, P_LEVEL, 341, 150, 30, 0, 10, 5, "LEVEL", "LEVEL", "0", "10", {0}, 0, 0, 0 },
    { W_SWITCH, P_MODE, 50, 376, 0, 0, 2, 1, "MODE", "MODE", 0, 0, { "CLEAN", "CRUNCH", "LEAD" }, 3, 0, 0 },
    { W_FOOT, P_BYPASS, 200, 386, 25, 0, 1, 1, "ACTIVE", "EFFECT", 0, 0, { "BYPASSED", "ON" }, 2, 0, 0 },
    { W_SELECT, P_TYPE, 284, 372, 19, 0, 3, 0, "TYPE", "TYPE", 0, 0, { "ONE", "TWO", "THREE", "FOUR" }, 4, 0, 0 },
    { W_TRIM, P_TRIM, 445, 372, 17, -24, 24, 0, "TRIM", "OUTPUT TRIM", 0, 0, {0}, 0, "%+.1f DB", 0 },
    { W_LIST, P_VOICE, 619, 106, 115, 0, 3, 1, "VOICE", "VOICE", 0, 0,
      { "1N270 GERMANIUM|ORIGINAL", "1N4148 SILICON|BUZZ", "RED LED|CRUNCH", "PLAIN ROW" }, 4, 0, 0 },
    { W_LAMP, P_METER, 560, 350, 13, 0, 1, 0, "CLIP", "CLIP", 0, 0, {0}, 0, 0, 0 },
    { W_TRIM, P_VOLTS, 680, 372, 17, 0.1f, 10.0f, 1.0f, "INPUT V", "VOLTS AT 0 DBFS", 0, 0, {0}, 0, 0, 1 },
};

static const PanelSection SECTIONS[] = {
    { 16, 76, 406, 246, "CONTROLS" },
    { 16, 266, 136, 456, "VOICE" },
    { 146, 266, 400, 456, "SWITCHES" },
    { 410, 266, 484, 456, "OUT" },
    { 494, 76, 744, 246, "LIST" },
    { 494, 266, 744, 456, "LAMP AND LOG" },
};

/* Custom readout for the log trim; everything else uses the default. */
static int format_volts(const Panel* p, const Widget* w, char* buf, size_t n)
{
    if (w->param != P_VOLTS) return 0;
    snprintf(buf, n, "%.2f V", p->val[w->param]);
    return 1;
}

static const PanelSpec SPEC = {
    760, 470, "BASE", "HOUSE-STYLE PANEL TEST", "PLUGIN BASE PANEL", NULL,
    { 496, 14, 744, 56 }, 470,
    SECTIONS, (int)(sizeof SECTIONS / sizeof SECTIONS[0]),
    WIDGETS, (int)(sizeof WIDGETS / sizeof WIDGETS[0]),
    NULL, NULL, format_volts
};

static int write_bmp(const char* path, const uint32_t* px, int w, int h)
{
    unsigned char hdr[54] = { 'B', 'M' };
    const unsigned size = 54u + 4u * (unsigned)(w * h);
    FILE* f = fopen(path, "wb");
    int i;
    if (!f)
        return 0;
    for (i = 0; i < 4; ++i) {
        hdr[2 + i] = (unsigned char)(size >> (8 * i));
        hdr[18 + i] = (unsigned char)((unsigned)w >> (8 * i));
        hdr[22 + i] = (unsigned char)((unsigned)(-h) >> (8 * i)); /* negative: top-down */
    }
    hdr[10] = 54;
    hdr[14] = 40;
    hdr[26] = 1;
    hdr[28] = 32;
    fwrite(hdr, 1, 54, f);
    fwrite(px, 4, (size_t)(w * h), f); /* 0x00RRGGBB is BGRA in memory on little-endian */
    fclose(f);
    return 1;
}

static int g_writes, g_last_param;
static float g_last_value;

static void on_write(void* host, int param, float value)
{
    (void)host;
    ++g_writes;
    g_last_param = param;
    g_last_value = value;
}

static int count_diff(const uint32_t* a, const uint32_t* b, int n)
{
    int d = 0;
    for (int i = 0; i < n; ++i) d += a[i] != b[i];
    return d;
}

int main(int argc, char** argv)
{
    static uint32_t before[760 * 470];
    const int npx = SPEC.w * SPEC.h;
    Panel p;

    CHECK(panel_init(&p, &SPEC, on_write, NULL), "panel_init");
    CHECK(p.val[P_GAIN] == 5.0f && p.val[P_TRIM] == 0.0f && p.val[P_BYPASS] == 1.0f, "defaults applied");

    /* Background is textured, not flat; corners carry the panel gradient. */
    {
        int distinct = 0;
        for (int i = 1; i < npx; ++i) distinct += p.bg[i] != p.bg[i - 1];
        CHECK(distinct > npx / 4, "powder-coat texture present (%d changes)", distinct);
        CHECK(((p.bg[SPEC.w * 30 + 250] >> 16) & 255) < 0x60, "panel is dark graphite");
    }

    /* Host value changes redraw only what moves. */
    memcpy(before, p.px, sizeof(uint32_t) * npx);
    panel_set(&p, P_GAIN, 9.0f);
    CHECK(p.dirty, "panel_set marks dirty");
    panel_draw(&p);
    {
        int d = count_diff(before, p.px, npx), outside = 0;
        for (int y = 0; y < SPEC.h; ++y)
            for (int x = 0; x < SPEC.w; ++x)
                if (before[y * SPEC.w + x] != p.px[y * SPEC.w + x] &&
                    (x < 81 - 40 || x > 81 + 40 || y < 150 - 40 || y > 150 + 40))
                    ++outside;
        CHECK(d > 100, "gain knob redrawn (%d px)", d);
        CHECK(outside == 0, "nothing outside the gain knob changed (%d px)", outside);
    }
    p.dirty = 0;
    panel_set(&p, P_GAIN, NAN);
    panel_set(&p, -1, 1.0f);
    panel_set(&p, PANEL_MAX_PARAMS, 1.0f);
    CHECK(!p.dirty && p.val[P_GAIN] == 9.0f, "NaN and out-of-range indices ignored");
    CHECK(g_writes == 0, "host values are not echoed back to the host");

    /* Hit testing. */
    CHECK(panel_hit(&p, 81, 150) == 0 && panel_hit(&p, 81 + 37, 150) == 0, "knob hit");
    CHECK(panel_hit(&p, 81 + 40, 150) == -1, "outside knob");
    CHECK(panel_hit(&p, 284 + SEL_LIST_DX + 10, 372) == 5, "selector list hit");

    /* Knob drag: 90 px up is half the range; Shift is 5x finer; double-click resets. */
    panel_set(&p, P_TONE, 2.0f);
    panel_mouse_down(&p, 211, 150, 0);
    CHECK(p.drag == 1 && p.shown == 1, "drag starts");
    panel_mouse_move(&p, 211, 150 - 90, 0);
    CHECK(fabsf(p.val[P_TONE] - 7.0f) < 1e-4f, "drag up 90 px = +5, got %g", p.val[P_TONE]);
    CHECK(g_writes > 0 && g_last_param == P_TONE, "drag writes to the host");
    panel_mouse_move(&p, 211, 150 - 90 - 90, 1);
    CHECK(fabsf(p.val[P_TONE] - 8.0f) < 1e-4f, "fine drag 90 px = +1, got %g", p.val[P_TONE]);
    panel_mouse_move(&p, 211, -5000, 0);
    CHECK(p.val[P_TONE] == 10.0f, "drag clamps at max");
    panel_mouse_up(&p);
    CHECK(p.drag == -1, "drag ends");
    panel_mouse_down(&p, 211, 150, 1);
    CHECK(p.val[P_TONE] == 5.0f && g_last_value == 5.0f, "double-click resets to default");

    /* Switch, footswitch, selector, wheel. */
    panel_mouse_down(&p, 50, 376 - 20, 0);
    CHECK(p.val[P_MODE] == 0.0f, "switch up = first position");
    panel_mouse_down(&p, 50, 376 + 20, 0);
    CHECK(p.val[P_MODE] == 2.0f, "switch down = last position");
    panel_mouse_down(&p, 200, 386, 0);
    CHECK(p.val[P_BYPASS] == 0.0f, "footswitch toggles off");
    panel_mouse_down(&p, 200, 386, 0);
    CHECK(p.val[P_BYPASS] == 1.0f, "footswitch toggles on");
    panel_mouse_down(&p, 284 + SEL_LIST_DX + 10, sel_row_y(&WIDGETS[5], 2), 0);
    CHECK(p.val[P_TYPE] == 2.0f && p.drag == -1, "click a list name selects it");
    panel_wheel(&p, 284, 372, -1.0f, 0);
    CHECK(p.val[P_TYPE] == 3.0f, "wheel down steps the selector down the list");
    panel_wheel(&p, 284, 372, -1.0f, 0);
    CHECK(p.val[P_TYPE] == 3.0f, "selector stops at the end");
    panel_wheel(&p, 445, 372, 1.0f, 0);
    CHECK(fabsf(p.val[P_TRIM] - 1.2f) < 1e-4f, "wheel on a trim: 2.5%% of range, got %g", p.val[P_TRIM]);

    /* Readout text formatting. */
    {
        char buf[64];
        w_format(&p, &WIDGETS[6], buf, sizeof buf);
        CHECK(strcmp(buf, "+1.2 DB") == 0, "trim readout '%s'", buf);
        w_format(&p, &WIDGETS[3], buf, sizeof buf);
        CHECK(strcmp(buf, "LEAD") == 0, "switch readout '%s'", buf);
    }


    /* Radio list: click a row, wheel steps rows, readout drops the tag. */
    {
        char buf[64];
        CHECK(panel_hit(&p, 619, 106 + 2 * LIST_ROW) == 7, "list row hit");
        CHECK(panel_hit(&p, 619, 106 + 4 * LIST_ROW) == -1, "below the last row misses");
        panel_mouse_down(&p, 560, 106 + 2 * LIST_ROW + 5, 0);
        CHECK(p.val[P_VOICE] == 2.0f && p.drag == -1, "click selects row 2, got %g", p.val[P_VOICE]);
        panel_wheel(&p, 619, 106, 1.0f, 0);
        CHECK(p.val[P_VOICE] == 1.0f, "wheel up moves up the list");
        w_format(&p, &WIDGETS[7], buf, sizeof buf);
        CHECK(strcmp(buf, "1N4148 SILICON") == 0, "list readout '%s'", buf);
    }
    /* Lamp: lit by its parameter, not clickable. */
    {
        panel_set(&p, P_METER, 0.0f);
        panel_draw(&p);
        const uint32_t dark = p.px[350 * SPEC.w + 560];
        panel_set(&p, P_METER, 1.0f);
        panel_draw(&p);
        const uint32_t lit = p.px[350 * SPEC.w + 560];
        CHECK(((lit >> 16) & 255) > ((dark >> 16) & 255) + 100, "lamp lights (%06x -> %06x)", dark, lit);
        CHECK(panel_hit(&p, 560, 350) == -1, "lamp is not clickable");
    }
    /* Log trim: 1 V is the middle of 0.1-10 V; dragging 90 px up is half the range. */
    {
        char buf[64];
        CHECK(fabsf(w_norm(&WIDGETS[9], 1.0f) - 0.5f) < 1e-6f, "log knob: 1 V at mid travel");
        panel_mouse_down(&p, 680, 372, 0);
        panel_mouse_move(&p, 680, 372 - 90, 0);
        panel_mouse_up(&p);
        CHECK(fabsf(p.val[P_VOLTS] - 10.0f) < 1e-3f, "log drag up half travel: 1 V -> 10 V, got %g", p.val[P_VOLTS]);
        w_format(&p, &WIDGETS[9], buf, sizeof buf);
        CHECK(strcmp(buf, "10.00 V") == 0, "custom formatter '%s'", buf);
    }

    /* A spec with a parameter index the panel can't hold is refused. */
    {
        static const Widget bad_w[] = { { W_KNOB, PANEL_MAX_PARAMS, 50, 50, 20, 0, 1, 0, "X", "X", 0, 0, {0}, 0, 0, 0 } };
        PanelSpec bad = SPEC;
        Panel q;
        bad.widgets = bad_w;
        bad.n_widgets = 1;
        CHECK(!panel_init(&q, &bad, NULL, NULL), "out-of-range param index rejected");
    }

    /* Screenshot for CI artifacts and for reviewing the house style. */
    p.shown = 0;
    panel_draw(&p);
    if (argc > 1) {
        CHECK(write_bmp(argv[1], p.px, SPEC.w, SPEC.h), "write %s", argv[1]);
        printf("wrote %s (%dx%d)\n", argv[1], SPEC.w, SPEC.h);
    }
    panel_free(&p);
    return check_summary("test_panel");
}
