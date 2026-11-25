/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian */
/* DPF plugins are C++: every shared header must compile, warning-free, as C++ too.
 * This also runs a few calls so the linker sees the C++ instantiation. */
#include "davidian_dsp.h"
#include "ui/panel.h"

#include <cstdio>

int main()
{
    static Triode t;
    static CabSim cab;
    static LinNet n;
    Oversampler os;
    Glide g;
    DenormalGuard dg;
    double buf[OS_MAX_FACTOR];

    denormal_enter(&dg);
    triode_build(&t, 300.0, 100e3, 1e6, 1.5e3, 1, 0.4);
    os_init(&os, 2, 48000.0);
    cab_init(&cab, 48000.0, 0);
    glide_init(&g, 0.015, 32, 48000.0);
    glide_step(&g, 1.0);
    linnet_init(&n, 1, 48000.0);
    linnet_source(&n, 1, 1e3);
    linnet_r(&n, 1, 0, 1e3);
    linnet_prepare(&n);
    os_up(&os, 0.1, buf);
    for (int i = 0; i < os.factor; ++i) buf[i] = triode_run(&t, buf[i]);
    const double y = cab_run(&cab, linnet_step(&n, os_down(&os, buf), 1)) * g.value;
    denormal_leave(&dg);

    static const Widget w[] = { { W_KNOB, 0, 40, 40, 20, 0, 10, 5, "K", "K", "0", "10", {0}, 0, 0, 0 } };
    static const PanelSpec spec = { 120, 100, "CXX", 0, 0, 0, { 60, 10, 110, 30 }, 50, 0, 0, w, 1, 0, 0, 0 };
    Panel p;
    const int ok = panel_init(&p, &spec, 0, 0);
    if (ok)
        panel_free(&p);

    std::printf("test_cxx: %s (y = %g)\n", ok && !dsp_bad(y) ? "ok" : "FAIL", y);
    return ok && !dsp_bad(y) ? 0 : 1;
}
