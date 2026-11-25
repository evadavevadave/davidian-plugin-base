/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian */
/* House-style control panel: drawing, hit-testing and mouse handling for knobs, trims,
 * toggle switches, footswitches and stepped selectors, independent of any windowing
 * system or plugin format. Also a radio-button list (W_LIST) and an indicator lamp driven
 * by an output parameter (W_LAMP), both from Davidian Canary Plus.
 *
 * A plugin describes its panel as data (a PanelSpec: size, name, sections, widgets) and
 * the panel draws into a 0x00RRGGBB pixel buffer. The plugin's UI wrapper shows that
 * buffer (for DPF, as an OpenGL texture), forwards mouse events, and receives value
 * changes through the PanelWrite callback.
 *
 *     static const Widget WIDGETS[] = { { W_KNOB, PARAM_GAIN, 80, 150, 30, 0, 10, 5,
 *                                         "GAIN", "GAIN", "0", "10" }, ... };
 *     static const PanelSection SECTIONS[] = { { 16, 76, 276, 246, "PREAMP" }, ... };
 *     static const PanelSpec SPEC = { 960, 470, "MY AMP", "SUBTITLE", "MY AMP VALVE HEAD",
 *                                     0, { 616, 14, 944, 56 }, 590,
 *                                     SECTIONS, 3, WIDGETS, 8 };
 *     Panel p;  panel_init(&p, &SPEC, write_cb, self);
 *
 * Generalized from Davidian Brownstone 100's panel; with Brownstone's layout as the spec
 * it draws the same pixels. Everything runs on the GUI thread; never include this (or
 * anything in common/ui/) from DSP code. */
#ifndef DAVIDIAN_UI_PANEL_H
#define DAVIDIAN_UI_PANEL_H

#include <stdio.h>
#include <stdlib.h>

#include "canvas.h"
#include "font.h"
#include "style.h"

/* W_SELECT: a stepped rotary selector whose pointer aims at the chosen name in a list.
 * W_LIST:   radio-button rows; x, y = centre of the first row, r = half the row width.
 *           Each option is "LABEL" or "LABEL|TAG" (TAG is drawn dim at the right end).
 * W_LAMP:   indicator lamp lit 0..1 by a (usually output) parameter; not clickable. */
enum { W_KNOB, W_TRIM, W_SWITCH, W_FOOT, W_SELECT, W_LIST, W_LAMP };

#define MAX_OPTS          8
#define LIST_ROW          34.0f /* W_LIST row spacing */
#define PANEL_MAX_PARAMS  64
#define SEL_LIST_DX       32.0f /* list starts this far right of the selector's centre */
#define SEL_ROW           20.0f /* list row spacing */
#define KNOB_SPAN         (0.75f * CV_PI) /* +/-135 degrees */

typedef struct {
    int kind;
    int param;          /* index into Panel.val, normally the plugin's parameter index */
    float x, y, r;
    float min, max, def;
    const char* label;  /* on the panel */
    const char* name;   /* in the readout */
    const char* lo;     /* scale end labels (knobs) */
    const char* hi;
    const char* opts[MAX_OPTS]; /* position names (switches, footswitch, selector) */
    int nopts;
    const char* fmt;    /* printf format for the readout value; NULL = "%.1f" */
    int log;            /* knobs and trims: logarithmic between min and max (both > 0) */
} Widget;

typedef struct {
    float x0, y0, x1, y1;
    const char* title;
} PanelSection;

typedef struct {
    float x0, y0, x1, y1;
} PanelRect;

typedef struct Panel Panel;

typedef struct {
    int w, h;
    const char* name;       /* drawn as "DAVIDIAN <name>" */
    const char* subtitle;
    const char* idle_title; /* readout text while no control is selected */
    const char* idle_hint;  /* NULL = "DRAG KNOBS - DOUBLE-CLICK RESETS" */
    PanelRect readout;      /* readout window, top right */
    float rule_x1;          /* right end of the amber rule under the title */
    const PanelSection* sections;
    int n_sections;
    const Widget* widgets;
    int n_widgets;
    /* Optional: extra static artwork (after sections, before widgets) and extra moving
     * parts (after widgets), for anything the standard widgets don't cover. */
    void (*draw_bg_extra)(Canvas* c, const Panel* p);
    void (*draw_fg_extra)(Canvas* c, const Panel* p);
    /* Optional: readout text for a widget; return 0 to fall back to the default. */
    int (*format)(const Panel* p, const Widget* w, char* buf, size_t n);
} PanelSpec;

typedef void (*PanelWrite)(void* host, int param, float value);

struct Panel {
    const PanelSpec* spec;
    uint32_t* px; /* what the window shows */
    uint32_t* bg; /* static artwork */
    Canvas c;
    float val[PANEL_MAX_PARAMS];
    int drag;      /* widget being dragged, or -1 */
    float drag_x, drag_y, drag_norm;
    int shown;     /* widget in the readout, or -1 */
    int dirty;
    PanelWrite write;
    void* host;
};

/* ---- Values ---- */

static inline float w_norm(const Widget* w, float v)
{
    float n = w->log ? logf(v / w->min) / logf(w->max / w->min) : (v - w->min) / (w->max - w->min);
    return n < 0.0f ? 0.0f : (n > 1.0f ? 1.0f : n);
}

static inline float w_value(const Widget* w, float n)
{
    n = n < 0.0f ? 0.0f : (n > 1.0f ? 1.0f : n);
    return w->log ? w->min * powf(w->max / w->min, n) : w->min + n * (w->max - w->min);
}

static inline int w_index(const Panel* p, const Widget* w)
{
    int i = (int)(p->val[w->param] + 0.5f);
    int n = w->kind == W_FOOT ? 2 : w->nopts;
    return i < 0 ? 0 : (i >= n ? n - 1 : i);
}

/* Selector geometry: list row i and the pointer angle that aims at it. */
static inline float sel_row_y(const Widget* w, int i)
{
    return w->y + SEL_ROW * (i - 0.5f * (w->nopts - 1));
}

static inline float sel_angle(const Widget* w, int i)
{
    return atan2f(SEL_LIST_DX - 6.0f, -(sel_row_y(w, i) - w->y));
}

/* The label part of a "LABEL|TAG" option, into buf. */
static inline void opt_label(const char* opt, char* buf, size_t n)
{
    size_t i = 0;
    for (; opt[i] && opt[i] != '|' && i + 1 < n; ++i) buf[i] = opt[i];
    buf[i] = 0;
}

static inline void w_format(const Panel* p, const Widget* w, char* buf, size_t n)
{
    const float v = p->val[w->param];
    if (p->spec->format && p->spec->format(p, w, buf, n))
        return;
    switch (w->kind) {
    case W_SWITCH:
    case W_FOOT:
    case W_SELECT:
        snprintf(buf, n, "%s", w->opts[w_index(p, w)]);
        break;
    case W_LIST:
        opt_label(w->opts[w_index(p, w)], buf, n);
        break;
    default:
        snprintf(buf, n, w->fmt ? w->fmt : "%.1f", v);
    }
}

/* ---- Static artwork ---- */

static inline void draw_screw(Canvas* c, float x, float y)
{
    cv_circle(c, x + 1, y + 1.5f, 6, 0x000000, 0.5f);
    cv_shaded_circle(c, x, y, 6, 0xb8bbc2, 0x4a4d54, -0.4f, -0.5f, 1);
    cv_line(c, x - 4, y + 2, x + 4, y - 2, 1.6f, 0x2a2c30, 0.9f);
}

static inline void draw_section(Canvas* c, const PanelSpec* s, const PanelSection* sec)
{
    const float x0 = sec->x0, y0 = sec->y0, x1 = sec->x1, y1 = sec->y1;
    const float tw = text_width(sec->title, TXT_SECTION, 2.5f);
    const float cx = 0.5f * (x0 + x1);
    cv_rrect(c, x0, y0, x1, y1, 10, 0x000000, 0.18f);
    cv_rrect_line(c, x0, y0, x1, y1, 10, 1.4f, COL_LINE, 0.9f);
    /* Title sits in a gap in the top edge. */
    cv_rrect(c, cx - 0.5f * tw - 8, y0 - 6, cx + 0.5f * tw + 8, y0 + 6, 3,
             mix(COL_PANEL_TOP, COL_PANEL_BOT, y0 / s->h), 1);
    text_center(c, cx, y0 - 4.5f, sec->title, TXT_SECTION, 2.5f, 1.5f, COL_AMBER, 1);
}

static inline void draw_knob_scale(Canvas* c, const Widget* w)
{
    const int big = w->kind == W_KNOB;
    const int ticks = big ? 11 : 7;
    int i;
    for (i = 0; i < ticks; ++i) {
        const float a = -KNOB_SPAN + 2.0f * KNOB_SPAN * i / (ticks - 1);
        const float r0 = w->r + (big ? 9.0f : 6.0f), r1 = r0 + ((i % 5 == 0 || !big) ? 6.0f : 3.5f);
        cv_line(c, cv_ax(w->x, r0, a), cv_ay(w->y, r0, a), cv_ax(w->x, r1, a), cv_ay(w->y, r1, a),
                1.3f, COL_INK, 0.8f);
    }
    /* Dim track under the value arc. */
    cv_arc(c, w->x, w->y, w->r + 4.5f, 2.2f, -KNOB_SPAN, KNOB_SPAN, 0x000000, 0.55f);
    if (w->lo) {
        const float rr = w->r + 22.0f;
        text_center(c, cv_ax(w->x, rr, -KNOB_SPAN) - 2, cv_ay(w->y, rr, -KNOB_SPAN) - 3, w->lo, TXT_SMALL, 1, 1.1f, COL_DIM, 1);
        text_center(c, cv_ax(w->x, rr, KNOB_SPAN) + 2, cv_ay(w->y, rr, KNOB_SPAN) - 3, w->hi, TXT_SMALL, 1, 1.1f, COL_DIM, 1);
    }
    text_center(c, w->x, w->y + w->r + (big ? 20.0f : 14.0f), w->label, big ? TXT_LABEL : 8.0f, 2.0f,
                big ? 1.7f : 1.4f, COL_INK, 1);
}

static inline void draw_switch_static(Canvas* c, const Widget* w)
{
    text_center(c, w->x + 14, w->y - 44, w->label, 8, 2.0f, 1.4f, COL_INK, 1);
    /* Mounting plate and nut. */
    cv_rrect(c, w->x - 9, w->y - 25, w->x + 9, w->y + 25, 4, 0x000000, 0.35f);
    cv_rrect_line(c, w->x - 9, w->y - 25, w->x + 9, w->y + 25, 4, 1, 0x71757d, 0.7f);
    cv_circle(c, w->x + 1, w->y + 2, 8.5f, 0x000000, 0.5f);
    cv_shaded_circle(c, w->x, w->y, 8.5f, COL_METAL_LIGHT, COL_METAL_DARK, -0.4f, -0.6f, 1);
    cv_circle(c, w->x, w->y, 5, 0x2d2f33, 1);
}

static inline void draw_foot_static(Canvas* c, const Widget* w)
{
    cv_circle(c, w->x + 2, w->y + 4, w->r + 9, 0x000000, 0.5f);
    cv_shaded_circle(c, w->x, w->y, w->r + 8, 0xa9adb5, 0x3c3f45, -0.5f, -0.7f, 1);
    cv_ring(c, w->x, w->y, w->r + 8, 1.2f, 0x202226, 0.8f);
    text_center(c, w->x, w->y + w->r + 18, w->label, TXT_LABEL, 2.5f, 1.7f, COL_INK, 1);
    /* LED bezel */
    cv_circle(c, w->x, w->y - w->r - 26, 7.5f, 0x0e0e10, 1);
    cv_ring(c, w->x, w->y - w->r - 26, 7.5f, 1.5f, 0x8a8e96, 0.8f);
}

static inline void draw_select_static(Canvas* c, const Widget* w)
{
    int i;
    /* Detent marks, one per position, each aiming at its name. */
    for (i = 0; i < w->nopts; ++i) {
        const float a = sel_angle(w, i);
        cv_line(c, cv_ax(w->x, w->r + 5.0f, a), cv_ay(w->y, w->r + 5.0f, a), cv_ax(w->x, w->r + 10.0f, a),
                cv_ay(w->y, w->r + 10.0f, a), 1.3f, COL_INK, 0.8f);
    }
    text_center(c, w->x, w->y + w->r + 14.0f, w->label, 8, 2.0f, 1.4f, COL_INK, 1);
}

static inline void draw_list_static(Canvas* c, const Widget* w)
{
    const float x0 = w->x - w->r, x1 = w->x + w->r;
    int i;
    for (i = 0; i < w->nopts; ++i) {
        const float y = w->y + LIST_ROW * i;
        const char* tag = strchr(w->opts[i], '|');
        if (i > 0)
            cv_line(c, x0 + 8, y - 0.5f * LIST_ROW, x1 - 8, y - 0.5f * LIST_ROW, 1, COL_LINE, 0.35f);
        /* Indicator bezel */
        cv_circle(c, x0 + 16, y, 6.5f, 0x0e0e10, 1);
        cv_ring(c, x0 + 16, y, 6.5f, 1.3f, 0x8a8e96, 0.7f);
        if (tag)
            text_draw(c, x1 - 6 - text_width(tag + 1, 7, 1.4f), y - 3.5f, tag + 1, 7, 1.4f, 1.1f, COL_DIM, 1);
    }
}

static inline void draw_lamp_static(Canvas* c, const Widget* w)
{
    cv_circle(c, w->x + 1, w->y + 2, w->r, 0x000000, 0.5f);
    cv_shaded_circle(c, w->x, w->y, w->r, 0xb9bcc3, 0x45484e, -0.4f, -0.6f, 1);
    cv_circle(c, w->x, w->y, w->r - 4, 0x140c04, 1);
    text_center(c, w->x, w->y + w->r + 9, w->label, 8, 2.0f, 1.4f, COL_INK, 1);
}

static inline void draw_background(Panel* p)
{
    const PanelSpec* s = p->spec;
    const PanelRect* r = &s->readout;
    const int W = s->w, H = s->h;
    Canvas c;
    int i, y;
    cv_init(&c, p->bg, W, H);
    for (y = 0; y < H; ++y) {
        uint32_t col = mix(COL_PANEL_TOP, COL_PANEL_BOT, (float)y / H);
        int x;
        for (x = 0; x < W; ++x)
            p->bg[y * W + x] = col;
    }
    cv_texture(&c, PANEL_TEXTURE);
    /* Bevelled edge */
    cv_rrect_line(&c, 1.5f, 1.5f, W - 1.5f, H - 1.5f, 6, 1.5f, 0x55595f, 0.8f);
    cv_rrect_line(&c, 3.5f, 3.5f, W - 3.5f, H - 3.5f, 5, 1.5f, 0x000000, 0.4f);

    /* Header */
    {
        char title[96];
        snprintf(title, sizeof title, "DAVIDIAN %s", s->name);
        text_draw(&c, 30, 18, title, TXT_TITLE, 3.2f, 2.8f, COL_INK, 1);
    }
    if (s->subtitle)
        text_draw(&c, 32, 46, s->subtitle, TXT_SMALL, 2.0f, 1.1f, COL_DIM, 1);
    cv_line(&c, 30, 60, s->rule_x1, 60, 1.2f, COL_AMBER, 0.55f);
    /* Readout window */
    cv_rrect(&c, r->x0, r->y0, r->x1, r->y1, 6, 0x000000, 0.6f);
    cv_rrect(&c, r->x0 + 2, r->y0 + 2, r->x1 - 2, r->y1 - 2, 5, 0x0d0a06, 1);
    cv_rrect_line(&c, r->x0, r->y0, r->x1, r->y1, 6, 1.2f, 0x6d7179, 0.8f);

    for (i = 0; i < s->n_sections; ++i)
        draw_section(&c, s, &s->sections[i]);

    if (s->draw_bg_extra)
        s->draw_bg_extra(&c, p);

    for (i = 0; i < s->n_widgets; ++i) {
        const Widget* w = &s->widgets[i];
        switch (w->kind) {
        case W_KNOB:
        case W_TRIM: draw_knob_scale(&c, w); break;
        case W_SWITCH: draw_switch_static(&c, w); break;
        case W_FOOT: draw_foot_static(&c, w); break;
        case W_SELECT: draw_select_static(&c, w); break;
        case W_LIST: draw_list_static(&c, w); break;
        case W_LAMP: draw_lamp_static(&c, w); break;
        }
    }

    draw_screw(&c, 14, 14);
    draw_screw(&c, W - 14, 14);
    draw_screw(&c, 14, H - 14);
    draw_screw(&c, W - 14, H - 14);
}

/* ---- Moving parts ---- */

static inline void draw_knob(Canvas* c, const Widget* w, float n, int active)
{
    const float r = w->r, x = w->x, y = w->y;
    const float ang = -KNOB_SPAN + 2.0f * KNOB_SPAN * n;
    const int knurls = w->kind == W_KNOB ? 36 : 20;
    int i;
    cv_arc(c, x, y, r + 4.5f, 2.2f, -KNOB_SPAN, ang, active ? 0xffd27f : COL_AMBER, 0.95f);
    cv_circle(c, x + 2, y + 4, r + 1, 0x000000, 0.5f);
    cv_shaded_circle(c, x, y, r, COL_KNOB_LIGHT, COL_KNOB_DARK, -0.45f, -0.65f, 1);
    for (i = 0; i < knurls; ++i) {
        const float a = ang + 2.0f * CV_PI * i / knurls;
        cv_line(c, cv_ax(x, r * 0.84f, a), cv_ay(y, r * 0.84f, a), cv_ax(x, r * 0.98f, a),
                cv_ay(y, r * 0.98f, a), w->kind == W_KNOB ? 1.6f : 1.2f, 0x000000, 0.55f);
    }
    cv_shaded_circle(c, x, y, r * 0.74f, 0x3c3e44, 0x18191c, -0.35f, -0.55f, 1);
    cv_ring(c, x, y, r * 0.74f, 1.0f, 0x6a6d74, 0.5f);
    cv_line(c, cv_ax(x, r * 0.2f, ang), cv_ay(y, r * 0.2f, ang), cv_ax(x, r * 0.93f, ang),
            cv_ay(y, r * 0.93f, ang), w->kind == W_KNOB ? 3.0f : 2.2f, COL_INK, 1);
}

static inline void draw_switch(Canvas* c, const Widget* w, int idx)
{
    const float x = w->x, y = w->y;
    float ey;
    int i;
    for (i = 0; i < w->nopts; ++i) {
        const float oy = w->nopts == 3 ? y - 20.0f + 20.0f * i : y - 20.0f + 40.0f * i;
        text_draw(c, x + 15, oy - 3.5f, w->opts[i], TXT_SMALL, 1.6f, 1.25f, i == idx ? COL_AMBER : COL_DIM, 1);
    }
    if (w->nopts == 3 && idx == 1) {
        /* Centre: the lever points at the viewer. */
        cv_circle(c, x + 1, y + 2, 5.5f, 0x000000, 0.5f);
        cv_shaded_circle(c, x, y, 5.5f, 0xf2f3f5, 0x8d9097, -0.3f, -0.4f, 1);
        return;
    }
    ey = (idx == 0) ? y - 19.0f : y + 19.0f;
    cv_line(c, x + 1.5f, y + 2, x + 1.5f, ey + 2, 6, 0x000000, 0.45f);
    cv_line(c, x, y, x, ey, 5.5f, 0xb6b9bf, 1);
    cv_line(c, x - 1.2f, y, x - 1.2f, ey, 1.6f, 0xf0f1f3, 0.7f);
    cv_shaded_circle(c, x, ey, 4.8f, 0xf4f5f7, 0x7c7f86, -0.35f, -0.45f, 1);
}

static inline void draw_select(Canvas* c, const Widget* w, int idx, int active)
{
    const float r = w->r, x = w->x, y = w->y;
    const float ang = sel_angle(w, idx);
    int i;
    for (i = 0; i < w->nopts; ++i)
        text_draw(c, x + SEL_LIST_DX, sel_row_y(w, i) - 3.5f, w->opts[i], TXT_SMALL, 1.6f, 1.25f,
                  i == idx ? COL_AMBER : COL_DIM, 1);
    /* Lit detent mark for the chosen position. */
    cv_line(c, cv_ax(x, r + 5.0f, ang), cv_ay(y, r + 5.0f, ang), cv_ax(x, r + 10.0f, ang),
            cv_ay(y, r + 10.0f, ang), 2.2f, active ? 0xffd27f : COL_AMBER, 1);
    /* Black knurled knob, same finish as the others. */
    cv_circle(c, x + 2, y + 4, r + 1, 0x000000, 0.5f);
    cv_shaded_circle(c, x, y, r, COL_KNOB_LIGHT, COL_KNOB_DARK, -0.45f, -0.65f, 1);
    for (i = 0; i < 20; ++i) {
        const float a = ang + 2.0f * CV_PI * i / 20;
        cv_line(c, cv_ax(x, r * 0.84f, a), cv_ay(y, r * 0.84f, a), cv_ax(x, r * 0.98f, a),
                cv_ay(y, r * 0.98f, a), 1.2f, 0x000000, 0.55f);
    }
    cv_shaded_circle(c, x, y, r * 0.74f, 0x3c3e44, 0x18191c, -0.35f, -0.55f, 1);
    cv_ring(c, x, y, r * 0.74f, 1.0f, 0x6a6d74, 0.5f);
    cv_line(c, cv_ax(x, r * 0.2f, ang), cv_ay(y, r * 0.2f, ang), cv_ax(x, r * 0.93f, ang),
            cv_ay(y, r * 0.93f, ang), 2.2f, COL_INK, 1);
}

static inline void draw_foot(Canvas* c, const Widget* w, int on)
{
    const float lx = w->x, ly = w->y - w->r - 26;
    cv_shaded_circle(c, w->x, w->y, w->r, 0xe3e5e9, 0x6b6e75, -0.4f, -0.55f, 1);
    cv_ring(c, w->x, w->y, w->r * 0.62f, 1.2f, 0x8e9198, 0.6f);
    cv_ring(c, w->x, w->y, w->r, 1.0f, 0x34363b, 0.8f);
    if (on) {
        cv_glow(c, lx, ly, 5, 16, COL_RED, 0.35f);
        cv_shaded_circle(c, lx, ly, 5.5f, 0xffd0c8, COL_RED, -0.3f, -0.4f, 1);
    } else {
        cv_shaded_circle(c, lx, ly, 5.5f, 0x5a1f1b, 0x240a08, -0.3f, -0.4f, 1);
    }
}

static inline void draw_list(Canvas* c, const Widget* w, int idx)
{
    const float x0 = w->x - w->r;
    char label[64];
    int i;
    for (i = 0; i < w->nopts; ++i) {
        const float y = w->y + LIST_ROW * i;
        const int on = i == idx;
        if (on) {
            cv_glow(c, x0 + 16, y, 3, 12, COL_AMBER, 0.35f);
            cv_shaded_circle(c, x0 + 16, y, 4.5f, 0xfff0d0, COL_AMBER, -0.3f, -0.4f, 1);
        } else {
            cv_circle(c, x0 + 16, y, 4.5f, 0x2a1a08, 1);
        }
        opt_label(w->opts[i], label, sizeof label);
        text_draw(c, x0 + 32, y - 4, label, 8, 1.8f, 1.35f, on ? COL_AMBER : COL_INK, on ? 1.0f : 0.75f);
    }
}

static inline void draw_lamp(Canvas* c, const Widget* w, float b)
{
    const float r = w->r - 4;
    b = b < 0.0f ? 0.0f : (b > 1.0f ? 1.0f : b);
    cv_circle(c, w->x, w->y, r, mix(0x1e1206, COL_AMBER, b), 1);
    if (b > 0.02f) {
        cv_glow(c, w->x, w->y, 5, 22, COL_AMBER, 0.45f * b);
        cv_circle(c, w->x - 2.5f, w->y - 2.5f, 2.6f, 0xfff2d6, 0.8f * b);
    }
}

static inline void draw_readout(Panel* p)
{
    const PanelSpec* s = p->spec;
    const float tx = s->readout.x0 + 16, ty = s->readout.y0 + 8;
    char val[64];
    Canvas* c = &p->c;
    if (p->shown < 0) {
        if (s->idle_title)
            text_draw(c, tx, ty, s->idle_title, 8, 2.2f, 1.3f, COL_AMBER_DK, 1);
        text_draw(c, tx, ty + 14, s->idle_hint ? s->idle_hint : "DRAG KNOBS - DOUBLE-CLICK RESETS",
                  TXT_SMALL, 1.6f, 1.2f, 0x9c6a24, 1);
        return;
    }
    w_format(p, &s->widgets[p->shown], val, sizeof val);
    text_draw(c, tx, ty, s->widgets[p->shown].name, 8, 2.2f, 1.3f, 0xc58b33, 1);
    text_draw(c, tx, ty + 13, val, 13, 2.0f, 2.0f, COL_AMBER, 1);
}

/* Redraws the visible buffer from the static artwork and the current values. */
static inline void panel_draw(Panel* p)
{
    const PanelSpec* s = p->spec;
    int i;
    memcpy(p->px, p->bg, sizeof(uint32_t) * (size_t)s->w * (size_t)s->h);
    for (i = 0; i < s->n_widgets; ++i) {
        const Widget* w = &s->widgets[i];
        switch (w->kind) {
        case W_KNOB:
        case W_TRIM: draw_knob(&p->c, w, w_norm(w, p->val[w->param]), p->drag == i); break;
        case W_SWITCH: draw_switch(&p->c, w, w_index(p, w)); break;
        case W_FOOT: draw_foot(&p->c, w, w_index(p, w)); break;
        case W_SELECT: draw_select(&p->c, w, w_index(p, w), p->drag == i); break;
        case W_LIST: draw_list(&p->c, w, w_index(p, w)); break;
        case W_LAMP: draw_lamp(&p->c, w, p->val[w->param]); break;
        }
    }
    if (s->draw_fg_extra)
        s->draw_fg_extra(&p->c, p);
    draw_readout(p);
    p->dirty = 0;
}

/* ---- Setup and host values ---- */

/* Allocates the two pixel buffers (GUI thread only). Returns 0 on failure, including a
 * widget whose param index is outside 0 .. PANEL_MAX_PARAMS - 1. */
static inline int panel_init(Panel* p, const PanelSpec* spec, PanelWrite write, void* host)
{
    const size_t n = (size_t)spec->w * (size_t)spec->h;
    int i;
    memset(p, 0, sizeof *p);
    for (i = 0; i < spec->n_widgets; ++i)
        if (spec->widgets[i].param < 0 || spec->widgets[i].param >= PANEL_MAX_PARAMS)
            return 0;
    p->spec = spec;
    p->px = (uint32_t*)malloc(sizeof(uint32_t) * n);
    p->bg = (uint32_t*)malloc(sizeof(uint32_t) * n);
    if (!p->px || !p->bg) {
        free(p->px);
        free(p->bg);
        p->px = p->bg = NULL;
        return 0;
    }
    cv_init(&p->c, p->px, spec->w, spec->h);
    for (i = 0; i < spec->n_widgets; ++i)
        p->val[spec->widgets[i].param] = spec->widgets[i].def;
    p->drag = -1;
    p->shown = -1;
    p->write = write;
    p->host = host;
    draw_background(p);
    panel_draw(p);
    return 1;
}

static inline void panel_free(Panel* p)
{
    free(p->px);
    free(p->bg);
    p->px = p->bg = NULL;
}

/* A value from the host. Ignores out-of-range indices and non-finite values. */
static inline void panel_set(Panel* p, int param, float v)
{
    if (param < 0 || param >= PANEL_MAX_PARAMS || !isfinite(v))
        return;
    if (p->val[param] != v) {
        p->val[param] = v;
        p->dirty = 1;
    }
}

/* ---- Mouse ---- */

static inline void set_value(Panel* p, int wi, float v)
{
    const Widget* w = &p->spec->widgets[wi];
    if (w->kind == W_SWITCH || w->kind == W_FOOT || w->kind == W_SELECT || w->kind == W_LIST)
        v = floorf(v + 0.5f);
    if (v < w->min) v = w->min;
    if (v > w->max) v = w->max;
    p->shown = wi;
    p->dirty = 1;
    if (p->val[w->param] != v) {
        p->val[w->param] = v;
        if (p->write)
            p->write(p->host, w->param, v);
    }
}

/* The widget under (x, y) in panel pixels, or -1. */
static inline int panel_hit(const Panel* p, float x, float y)
{
    int i;
    for (i = 0; i < p->spec->n_widgets; ++i) {
        const Widget* w = &p->spec->widgets[i];
        const float dx = x - w->x, dy = y - w->y;
        switch (w->kind) {
        case W_KNOB:
        case W_TRIM:
        case W_FOOT:
            if (dx * dx + dy * dy <= (w->r + 8) * (w->r + 8))
                return i;
            break;
        case W_SWITCH:
            if (dx >= -14 && dx <= 62 && dy >= -30 && dy <= 30)
                return i;
            break;
        case W_LIST:
            if (fabsf(dx) <= w->r && dy >= -0.5f * LIST_ROW && dy <= (w->nopts - 0.5f) * LIST_ROW)
                return i;
            break;
        case W_SELECT:
            if (dx * dx + dy * dy <= (w->r + 10) * (w->r + 10))
                return i;
            if (dx >= SEL_LIST_DX - 4 && dx <= SEL_LIST_DX + 90 &&
                fabsf(dy) <= SEL_ROW * 0.5f * w->nopts)
                return i;
            break;
        }
    }
    return -1;
}

/* The list row under y, or -1 when y is outside the list. */
static inline int sel_row_at(const Widget* w, float x, float y)
{
    int i;
    if (x - w->x < SEL_LIST_DX - 4)
        return -1;
    for (i = 0; i < w->nopts; ++i)
        if (fabsf(y - sel_row_y(w, i)) <= 0.5f * SEL_ROW)
            return i;
    return -1;
}

static inline int switch_pos_at(const Widget* w, float y)
{
    const float dy = y - w->y;
    if (w->nopts == 3)
        return dy < -8.0f ? 0 : (dy > 8.0f ? 2 : 1);
    return dy < 0.0f ? 0 : 1;
}

static inline void panel_mouse_down(Panel* p, float x, float y, int dbl)
{
    const int i = panel_hit(p, x, y);
    const Widget* w;
    if (i < 0)
        return;
    w = &p->spec->widgets[i];
    switch (w->kind) {
    case W_KNOB:
    case W_TRIM:
        if (dbl) {
            set_value(p, i, w->def);
            return;
        }
        p->drag = i;
        p->drag_y = y;
        p->drag_norm = w_norm(w, p->val[w->param]);
        p->shown = i;
        p->dirty = 1;
        break;
    case W_SWITCH:
        set_value(p, i, (float)switch_pos_at(w, y));
        break;
    case W_FOOT:
        set_value(p, i, w_index(p, w) ? 0.0f : 1.0f);
        break;
    case W_LIST: {
        const int row = (int)floorf((y - w->y) / LIST_ROW + 0.5f);
        set_value(p, i, (float)(row < 0 ? 0 : (row >= w->nopts ? w->nopts - 1 : row)));
        break;
    }
    case W_SELECT: {
        /* Click a name to pick it; on the knob, drag up/down to step. */
        const int row = sel_row_at(w, x, y);
        if (row >= 0) {
            set_value(p, i, (float)row);
            return;
        }
        if (dbl) {
            set_value(p, i, w->def);
            return;
        }
        p->drag = i;
        p->drag_y = y;
        p->drag_norm = (float)w_index(p, w);
        p->shown = i;
        p->dirty = 1;
        break;
    }
    }
}

/* fine: 5x finer knob movement (Shift held). */
static inline void panel_mouse_move(Panel* p, float x, float y, int fine)
{
    const Widget* w;
    (void)x;
    if (p->drag < 0)
        return;
    w = &p->spec->widgets[p->drag];
    if (w->kind == W_SELECT) {
        /* One step per 24 px from where the drag started; dragging down moves down the list. */
        set_value(p, p->drag, p->drag_norm + floorf((y - p->drag_y) / 24.0f + 0.5f));
        return;
    }
    {
        const float n = p->drag_norm + (p->drag_y - y) / (fine ? 900.0f : 180.0f);
        /* Fine mode needs a fresh reference so switching modes mid-drag doesn't jump. */
        set_value(p, p->drag, w_value(w, n));
        p->drag_norm = w_norm(w, p->val[w->param]);
        p->drag_y = y;
    }
}

static inline void panel_mouse_up(Panel* p)
{
    if (p->drag >= 0)
        p->dirty = 1;
    p->drag = -1;
}

static inline void panel_wheel(Panel* p, float x, float y, float steps, int fine)
{
    const int i = panel_hit(p, x, y);
    const Widget* w;
    if (i < 0)
        return;
    w = &p->spec->widgets[i];
    if (w->kind == W_SWITCH || w->kind == W_SELECT || w->kind == W_LIST) {
        /* Wheel up moves up the list. */
        int n = w_index(p, w) - (steps > 0 ? 1 : -1);
        set_value(p, i, (float)(n < 0 ? 0 : (n >= w->nopts ? w->nopts - 1 : n)));
    } else if (w->kind != W_FOOT && w->kind != W_LAMP) {
        set_value(p, i, w_value(w, w_norm(w, p->val[w->param]) + steps * (fine ? 0.004f : 0.025f)));
    }
}

#endif
