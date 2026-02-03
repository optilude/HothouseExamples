#pragma once
#ifndef FLICK_DELAY_EFFECT_H
#define FLICK_DELAY_EFFECT_H

#include "flick_effect.h"
#include "flick_knob_takeover.h"
#include "daisysp.h"

namespace flick {

// Maximum delay size (2 seconds at 48kHz)
constexpr size_t kMaxDelaySize = static_cast<size_t>(48000.0f * 2.0f);

/**
 * @brief Delay line wrapper with feedback and smooth time changes
 */
struct DelayProcessor {
    daisysp::DelayLine<float, kMaxDelaySize>* del;
    float current_delay;
    float delay_target;
    float feedback;

    DelayProcessor() : del(nullptr), current_delay(0.0f), delay_target(0.0f), feedback(0.0f) {}

    float Process(float in) {
        // Smooth delay time changes
        daisysp::fonepole(current_delay, delay_target, 0.0002f);
        del->SetDelay(current_delay);

        float read = del->Read();
        del->Write((feedback * read) + in);

        return read;
    }
};

/**
 * @brief Encapsulates the stereo delay effect with tap tempo support
 * 
 * Features:
 * - Stereo delay with independent processing
 * - Tap tempo support with subdivisions
 * - Smooth delay time transitions
 * - Feedback and dry/wet mix control
 */
class DelayEffect : public Effect {
public:
    enum Subdivision {
        SUBDIV_QUARTER_TRIPLET,  // 0.6666x (2/3 of quarter note)
        SUBDIV_NORMAL,           // 1.0x (quarter note)  
        SUBDIV_DOTTED_EIGHTH,    // 0.75x (3/4 of quarter note)
    };

    enum MakeupGain {
        MAKEUP_NONE,
        MAKEUP_NORMAL,
        MAKEUP_HEAVY,
    };

    DelayEffect();
    ~DelayEffect() override {}

    void Init(float sample_rate) override;
    void ProcessSample(float in_l, float in_r, float& out_l, float& out_r) override;
    void ProcessControls(FlickCore* core, int sw1, int sw2, int sw3) override;

    // Set delay line pointers (must be in SDRAM)
    void SetDelayLines(daisysp::DelayLine<float, kMaxDelaySize>* left,
                       daisysp::DelayLine<float, kMaxDelaySize>* right);

    // Initialize parameter controls
    void InitControls(daisy::AnalogControl* time_knob, 
                      daisy::AnalogControl* feedback_knob,
                      daisy::AnalogControl* mix_knob);

    // Tap tempo control
    void SetDelayTime(float samples);  // Set delay time in samples
    float GetDelayTime() const { return master_delay_samples_; }
    
    // For soft takeover when switching between knob and tap tempo
    void CaptureTimeKnob();
    bool CheckTimeKnobTakeover(float current_value);

    void SetMakeupGain(MakeupGain gain) { makeup_gain_ = gain; }
    MakeupGain GetMakeupGain() const { return makeup_gain_; }

    // Apply subdivision and update delay targets
    void ApplySubdivision(Subdivision subdiv);

    // Constants  
    static constexpr float kMinDelaySeconds = 0.02f;  // 20ms minimum
    static constexpr float kWetMixAttenuation = 0.333f;
    static constexpr float kMaxDryWet = 100.0f;

private:
    DelayProcessor delay_l_;
    DelayProcessor delay_r_;

    // Parameters
    daisy::Parameter p_time_;
    daisy::Parameter p_feedback_;
    daisy::Parameter p_mix_;

    // State
    float master_delay_samples_;
    int dry_wet_percent_;
    MakeupGain makeup_gain_;
    Subdivision current_subdivision_;

    // Tap tempo soft takeover
    KnobTakeover time_knob_takeover_;

    // Sample rate dependent limits
    float min_delay_samples_;
    float max_delay_samples_;
};

} // namespace flick

#endif // FLICK_DELAY_EFFECT_H
