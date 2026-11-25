/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian */
/* Dut for a DPF plugin. This file is compiled once per plugin, with that plugin's
 * DistrhoPluginInfo.h, as a minimal DPF "format": DPF's plugin core (the static target)
 * plus a PluginExporter, set up the way DPF's own wrappers do it. */
#define DISTRHO_PLUGIN_TARGET_STATIC 1
#include "DistrhoPluginMain.cpp"

#include "dut.h"

START_NAMESPACE_DISTRHO

class DpfDut : public Dut
{
public:
    DpfDut(double rate, uint32_t max_block)
    {
        d_nextBufferSize = max_block;
        d_nextSampleRate = rate;
        fPlugin = new PluginExporter(nullptr, nullptr, nullptr, nullptr);
        d_nextBufferSize = 0;
        d_nextSampleRate = 0.0;
        for (uint32_t i = 0; i < fPlugin->getParameterCount(); ++i)
            if (! fPlugin->isParameterOutput(i))
                fPlugin->setParameterValue(i, fPlugin->getParameterDefault(i));
    }

    ~DpfDut() override
    {
        fPlugin->deactivateIfNeeded();
        delete fPlugin;
    }

    bool valid() const { return fPlugin->getInstancePointer() != nullptr; }

    const char* name() const override { return fPlugin->getName(); }
    uint32_t inputs() const override { return DISTRHO_PLUGIN_NUM_INPUTS; }
    uint32_t outputs() const override { return DISTRHO_PLUGIN_NUM_OUTPUTS; }
    uint32_t param_count() const override { return fPlugin->getParameterCount(); }
   #if DISTRHO_PLUGIN_WANT_LATENCY
    uint32_t latency() const override { return fPlugin->getLatency(); }
   #else
    uint32_t latency() const override { return 0; }
   #endif

    DutParam param(uint32_t i) const override
    {
        const ParameterRanges& r(fPlugin->getParameterRanges(i));
        const uint32_t h = fPlugin->getParameterHints(i);
        DutParam p;
        p.symbol = fPlugin->getParameterSymbol(i).buffer();
        p.name = fPlugin->getParameterName(i).buffer();
        p.min = r.min;
        p.max = r.max;
        p.def = r.def;
        p.integer = (h & kParameterIsInteger) != 0;
        p.boolean = (h & kParameterIsBoolean) != 0;
        p.output = (h & kParameterIsOutput) != 0;
        p.trigger = (h & kParameterIsTrigger) == kParameterIsTrigger;
        return p;
    }

    void activate() override { fPlugin->activate(); }
    void deactivate() override { fPlugin->deactivateIfNeeded(); }

    void set_rate(double rate) override
    {
        const bool was_active = fPlugin->isActive();
        fPlugin->deactivateIfNeeded();
        fPlugin->setSampleRate(rate, true);
        if (was_active)
            fPlugin->activate();
    }

    void set_param(uint32_t index, float value) override
    {
        fPlugin->setParameterValue(index, value);
    }

    void run(const float** in, float** out, uint32_t frames) override
    {
       #if DISTRHO_PLUGIN_WANT_MIDI_INPUT
        fPlugin->run(in, out, frames, nullptr, 0);
       #else
        fPlugin->run(in, out, frames);
       #endif
    }

private:
    PluginExporter* fPlugin;
};

END_NAMESPACE_DISTRHO

Dut* Dut::create(double rate, uint32_t max_block)
{
    DISTRHO_NAMESPACE::DpfDut* d = new DISTRHO_NAMESPACE::DpfDut(rate, max_block);
    if (! d->valid())
    {
        delete d;
        return nullptr;
    }
    return d;
}
