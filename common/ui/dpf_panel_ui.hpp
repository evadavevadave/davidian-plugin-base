/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian */
/* DPF binding for the house-style panel: one UI class shared by every plugin and every
 * format (LV2, VST3, CLAP, AU, standalone).
 *
 * panel.h draws into a pixel buffer, which is uploaded as an OpenGL texture and drawn
 * scaled to the window; mouse events go back to panel.h, and value changes go to the
 * host with begin/end edit gestures. Panel value index = DPF parameter index.
 *
 * In a plugin's UI file:
 *
 *     #include "ui/dpf_panel_ui.hpp"
 *     static const dui::Widget WIDGETS[] = { ... };
 *     static const dui::PanelSpec SPEC = { ... };
 *     START_NAMESPACE_DISTRHO
 *     UI* createUI() { return new DavidianPanelUI(SPEC); }
 *     END_NAMESPACE_DISTRHO
 *
 * The panel code lives in namespace dui, because DGL has its own Widget class.
 * Generalized from Davidian Brownstone 100's BrownstoneUI.cpp. */
#ifndef DAVIDIAN_UI_DPF_PANEL_UI_HPP
#define DAVIDIAN_UI_DPF_PANEL_UI_HPP

/* System headers first, so the panel's includes inside the namespace below are no-ops. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace dui {
#include "panel.h"
}

#include "DistrhoUI.hpp"
#include "OpenGL.hpp"

#ifndef GL_BGRA
#    define GL_BGRA 0x80E1
#endif
#ifndef GL_CLAMP_TO_EDGE
#    define GL_CLAMP_TO_EDGE 0x812F
#endif

START_NAMESPACE_DISTRHO

class DavidianPanelUI : public UI
{
public:
    explicit DavidianPanelUI(const dui::PanelSpec& spec)
        : UI((uint)spec.w, (uint)spec.h, true),
          fSpec(spec),
          fTexture(0),
          fUpload(true),
          fEditing(-1),
          fLastClick(0),
          fLastX(-100.0f),
          fLastY(-100.0f),
          fReady(false)
    {
        fReady = dui::panel_init(&fPanel, &fSpec, writeCallback, this) != 0;
    }

    ~DavidianPanelUI() override
    {
        if (fTexture != 0)
            glDeleteTextures(1, &fTexture);
        if (fReady)
            dui::panel_free(&fPanel);
    }

protected:
    /* For subclasses that draw extra moving parts (meters): mark the panel for redraw. */
    void panelChanged()
    {
        fPanel.dirty = 1;
        repaint();
    }

    dui::Panel& panel() { return fPanel; }

    void parameterChanged(uint32_t index, float value) override
    {
        if (!fReady)
            return;
        dui::panel_set(&fPanel, (int)index, value);
        repaintIfDirty();
    }

    void uiIdle() override
    {
        repaintIfDirty();
    }

    void onDisplay() override
    {
        if (!fReady)
            return;
        if (fPanel.dirty)
        {
            dui::panel_draw(&fPanel);
            fUpload = true;
        }

        glEnable(GL_TEXTURE_2D);
        if (fTexture == 0)
        {
            glGenTextures(1, &fTexture);
            glBindTexture(GL_TEXTURE_2D, fTexture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
            /* 0x00RRGGBB words are B,G,R,X bytes in memory; the unused byte is ignored. */
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, fSpec.w, fSpec.h, 0, GL_BGRA, GL_UNSIGNED_BYTE,
                         fPanel.px);
            fUpload = false;
        }
        else
        {
            glBindTexture(GL_TEXTURE_2D, fTexture);
            if (fUpload)
            {
                glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, fSpec.w, fSpec.h, GL_BGRA, GL_UNSIGNED_BYTE,
                                fPanel.px);
                fUpload = false;
            }
        }

        const double w = getWidth(), h = getHeight();
        glDisable(GL_BLEND);
        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
        glBegin(GL_QUADS);
        glTexCoord2f(0.0f, 0.0f); glVertex2d(0.0, 0.0);
        glTexCoord2f(1.0f, 0.0f); glVertex2d(w, 0.0);
        glTexCoord2f(1.0f, 1.0f); glVertex2d(w, h);
        glTexCoord2f(0.0f, 1.0f); glVertex2d(0.0, h);
        glEnd();
        glBindTexture(GL_TEXTURE_2D, 0);
        glDisable(GL_TEXTURE_2D);
    }

    bool onMouse(const MouseEvent& ev) override
    {
        if (!fReady || ev.button != 1)
            return false;
        const float x = toPanelX(ev.pos.getX()), y = toPanelY(ev.pos.getY());
        if (ev.press)
        {
            const bool dbl = ev.time - fLastClick < kDoubleClickMs
                             && fabsf(x - fLastX) < 4.0f && fabsf(y - fLastY) < 4.0f;
            fLastClick = dbl ? 0 : ev.time;
            fLastX = x;
            fLastY = y;
            beginEdit(dui::panel_hit(&fPanel, x, y));
            dui::panel_mouse_down(&fPanel, x, y, dbl ? 1 : 0);
            /* a click that doesn't start a drag (switch, footswitch, list) is a whole edit */
            if (fPanel.drag < 0)
                endEdit();
        }
        else
        {
            dui::panel_mouse_up(&fPanel);
            endEdit();
        }
        repaintIfDirty();
        return true;
    }

    bool onMotion(const MotionEvent& ev) override
    {
        if (!fReady || fPanel.drag < 0)
            return false;
        dui::panel_mouse_move(&fPanel, toPanelX(ev.pos.getX()), toPanelY(ev.pos.getY()),
                              (ev.mod & DGL_NAMESPACE::kModifierShift) != 0);
        repaintIfDirty();
        return true;
    }

    bool onScroll(const ScrollEvent& ev) override
    {
        if (!fReady)
            return false;
        const float x = toPanelX(ev.pos.getX()), y = toPanelY(ev.pos.getY());
        const double d = ev.delta.getY();
        if (d == 0.0)
            return false;
        beginEdit(dui::panel_hit(&fPanel, x, y));
        dui::panel_wheel(&fPanel, x, y, d > 0.0 ? 1.0f : -1.0f,
                         (ev.mod & DGL_NAMESPACE::kModifierShift) != 0);
        endEdit();
        repaintIfDirty();
        return true;
    }

private:
    static constexpr uint kDoubleClickMs = 350;

    static void writeCallback(void* host, int param, float value)
    {
        static_cast<DavidianPanelUI*>(host)->setParameterValue((uint32_t)param, value);
    }

    void beginEdit(int widget)
    {
        endEdit();
        if (widget >= 0)
        {
            fEditing = fSpec.widgets[widget].param;
            editParameter((uint32_t)fEditing, true);
        }
    }

    void endEdit()
    {
        if (fEditing >= 0)
        {
            editParameter((uint32_t)fEditing, false);
            fEditing = -1;
        }
    }

    float toPanelX(double x) const { return (float)(x * fSpec.w / (double)getWidth()); }
    float toPanelY(double y) const { return (float)(y * fSpec.h / (double)getHeight()); }

    void repaintIfDirty()
    {
        if (fPanel.dirty)
            repaint();
    }

    const dui::PanelSpec& fSpec;
    dui::Panel fPanel;
    GLuint fTexture;
    bool fUpload;
    int fEditing;
    uint fLastClick;
    float fLastX, fLastY;
    bool fReady;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DavidianPanelUI)
};

END_NAMESPACE_DISTRHO

#endif
