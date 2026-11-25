/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian */
/* The device under test, as the measurement harness sees it: a plugin's DSP with no
 * format, host or GUI around it. dut_dpf.cpp implements this for a DPF plugin by
 * driving it through DPF's own PluginExporter, the same object every format wrapper
 * (LV2, VST3, CLAP, AU, standalone) uses, so the harness tests the code all formats run. */
#ifndef DAVIDIAN_HARNESS_DUT_H
#define DAVIDIAN_HARNESS_DUT_H

#include <stdint.h>
#include <string>

struct DutParam {
    std::string symbol, name;
    float min, max, def;
    bool integer, boolean, output, trigger;
};

class Dut {
public:
    /* A new instance at `rate`, prepared for blocks up to `max_block`; parameters at
     * their defaults; not yet activated. Returns nullptr on failure. */
    static Dut* create(double rate, uint32_t max_block);
    virtual ~Dut() {}

    virtual const char* name() const = 0;
    virtual uint32_t inputs() const = 0;
    virtual uint32_t outputs() const = 0;
    virtual uint32_t param_count() const = 0;
    virtual DutParam param(uint32_t index) const = 0;
    virtual uint32_t latency() const = 0;

    virtual void activate() = 0;
    virtual void deactivate() = 0;
    /* Host sample-rate change: deactivate, sampleRateChanged(), activate. */
    virtual void set_rate(double rate) = 0;

    /* Audio-thread calls: must be real-time safe in the plugin. */
    virtual void set_param(uint32_t index, float value) = 0;
    virtual void run(const float** in, float** out, uint32_t frames) = 0;
};

#endif
