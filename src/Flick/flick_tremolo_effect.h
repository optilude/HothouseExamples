#pragma once
#ifndef FLICK_TREMOLO_EFFECT_H
#define FLICK_TREMOLO_EFFECT_H

#include "flick_effect.h"
#include "flick_knob_takeover.h"
#include "flick_oscillator.h"

namespace flick {

// Forward declare the takeover helper
struct KnobTakeover;

/**
 * @brief Low-pass filter for harmonic tremolo
 */
struct LowPassFilter {
    float alpha;
    float prev_y;

    LowPassFilter() : alpha(0.0f), prev_y(0.0f) {}

    void Init(float fc, float fs) {
        alpha = expf(-2.0f * M_PI * fc / fs);
        prev_y = 0.0f;
    }

    float Process(float x) {
        float y = (1.0f - alpha) * x + alpha * prev_y;
        prev_y = y;
        return y;
    }
};

/**
 * @brief High-pass filter for harmonic tremolo
 */
struct HighPassFilter {
    float alpha;
    float prev_x;
    float prev_y;

    HighPassFilter() : alpha(0.0f), prev_x(0.0f), prev_y(0.0f) {}

    void Init(float fc, float fs) {
        alpha = expf(-2.0f * M_PI * fc / fs);
        prev_x = 0.0f;
        prev_y = 0.0f;
    }

    float Process(float x) {
        float y = (1.0f + alpha) * 0.5f * (x - prev_x) + alpha * prev_y;
        prev_x = x;
        prev_y = y;
        return y;
    }
};

/**
 * @brief Encapsulates the tremolo effect with three modes
 * 
 * Modes (controlled by SW2 in normal mode):
 * - Sine wave tremolo
 * - Harmonic tremolo (splits signal into low/high bands with opposite modulation)
 * - Square wave tremolo (opto-style)
 */
class TremoloEffect : public Effect {
public:
    enum Mode {
        MODE_SINE,      // Standard sine wave tremolo
        MODE_HARMONIC,  // Fender-style harmonic tremolo
        MODE_SQUARE,    // Opto/square wave tremolo
    };

    enum MakeupGain {
        MAKEUP_NONE,    // No makeup gain
        MAKEUP_NORMAL,  // Standard makeup gain
        MAKEUP_HEAVY,   // Heavy makeup gain
    };

    TremoloEffect();
    ~TremoloEffect() override {}

    void Init(float sample_rate) override;
    void ProcessSample(float in_l, float in_r, float& out_l, float& out_r) override;
    void ProcessControls(FlickCore* core, int sw1, int sw2, int sw3) override;

    float GetLedModulationValue() const override { return last_trem_value_; }

    // Speed/depth control via tap tempo
    void SetFrequency(float freq_hz);
    float GetFrequency() const { return osc_.GetFreq(); }
    
    void SetMakeupGain(MakeupGain gain) { makeup_gain_ = gain; }
    MakeupGain GetMakeupGain() const { return makeup_gain_; }

    // For tap tempo soft takeover
    void CaptureSpeedKnob();
    bool CheckSpeedKnobTakeover(float current_value);

    // Initialize parameter controls
    void InitControls(daisy::AnalogControl* speed_knob, daisy::AnalogControl* depth_knob);

    // Constants
    static constexpr float kSpeedMin = 0.2f;    // Minimum speed in Hz
    static constexpr float kSpeedMax = 16.0f;   // Maximum speed in Hz
    static constexpr float kLedBrightness = 0.4f; // LED brightness multiplier when only trem active

private:
    FlickOscillator osc_;
    
    // Harmonic tremolo filters (Fender 6G12-A style)
    LowPassFilter low_pass_l_;
    LowPassFilter low_pass_r_;
    HighPassFilter high_pass_l_;
    HighPassFilter high_pass_r_;

    // Parameters
    daisy::Parameter p_speed_;
    daisy::Parameter p_depth_;

    // State
    Mode current_mode_;
    MakeupGain makeup_gain_;
    float dc_offset_;
    float last_trem_value_;
    float current_lfo_sample_;

    // For tap tempo soft takeover
    KnobTakeover speed_knob_takeover_;

    // Constants for harmonic tremolo filter cutoffs (from Fender 6G12-A schematic)
    static constexpr float kHarmonicLPFCutoff = 144.0f;  // 220K and 5nF
    static constexpr float kHarmonicHPFCutoff = 636.0f;  // 1M and 250pF
};

} // namespace flick

#endif // FLICK_TREMOLO_EFFECT_H
