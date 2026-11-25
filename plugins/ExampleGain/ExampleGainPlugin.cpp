/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian */
/* Example Gain, DSP side: a stereo gain with a smoothed parameter, built for every format
 * by DPF. The pattern every Davidian plugin follows:
 *   - parameters declared once in initParameter(); their order and symbols are the LV2
 *     control ports, so never reorder or rename them after a release
 *   - per-sample work uses common/ code only, with no allocation, locks or I/O
 *   - sampleRateChanged() rebuilds anything that depends on the rate
 *   - every parameter change is smoothed */
#include "DistrhoPlugin.hpp"

#include "ExampleGainParams.h"
#include "davidian_dsp.h"

START_NAMESPACE_DISTRHO

class ExampleGainPlugin : public Plugin
{
public:
    ExampleGainPlugin()
        : Plugin(kParamCount, 0, 0),
          fGainDb(kGainDefault)
    {
        sampleRateChanged(getSampleRate());
    }

protected:
    const char* getLabel() const override { return "ExampleGain"; }
    const char* getDescription() const override
    {
        return "Stereo gain: the example plugin of the Davidian plugin base.";
    }
    const char* getMaker() const override { return "Davidian"; }
    const char* getHomePage() const override { return "https://github.com/evadavevadave"; }
    const char* getLicense() const override { return "GPL-3.0-or-later"; }
    uint32_t getVersion() const override { return d_version(0, 1, 0); }

    void initAudioPort(bool input, uint32_t index, AudioPort& port) override
    {
        port.groupId = kPortGroupStereo;
        Plugin::initAudioPort(input, index, port);
    }

    void initParameter(uint32_t index, Parameter& p) override
    {
        switch (index)
        {
        case kParamGain:
            p.hints = kParameterIsAutomatable;
            p.name = "Gain";
            p.shortName = "Gain";
            p.symbol = "gain";
            p.unit = "dB";
            p.ranges.min = kGainMin;
            p.ranges.max = kGainMax;
            p.ranges.def = kGainDefault;
            break;
        }
    }

    float getParameterValue(uint32_t index) const override
    {
        return index == kParamGain ? fGainDb : 0.0f;
    }

    void setParameterValue(uint32_t index, float value) override
    {
        if (index == kParamGain)
            fGainDb = value;
    }

    void sampleRateChanged(double rate) override
    {
        smoother_init(&fGain, 0.020, rate > 0.0 ? rate : 48000.0);
    }

    void activate() override
    {
        smoother_reset(&fGain); /* start at the current value, no fade from silence */
    }

    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        DenormalGuard guard;
        denormal_enter(&guard);
        const double db = clampd(fGainDb, kGainMin, kGainMax);
        smoother_set(&fGain, db <= kGainMin ? 0.0 : db_to_gain(db)); /* bottom of the range is off */
        for (uint32_t i = 0; i < frames; ++i)
        {
            const double g = smoother_next(&fGain);
            outputs[0][i] = (float)(inputs[0][i] * g);
            outputs[1][i] = (float)(inputs[1][i] * g);
        }
        denormal_leave(&guard);
    }

private:
    float fGainDb;
    Smoother fGain;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ExampleGainPlugin)
};

Plugin* createPlugin()
{
    return new ExampleGainPlugin();
}

END_NAMESPACE_DISTRHO
