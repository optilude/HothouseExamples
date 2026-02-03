#include "flick_delay_effect.h"
#include "flick_core.h"

namespace flick {

DelayEffect::DelayEffect()
    : master_delay_samples_(0.0f),
      dry_wet_percent_(50),
      makeup_gain_(MAKEUP_NORMAL),
      current_subdivision_(SUBDIV_NORMAL),
      time_knob_takeover_(),
      min_delay_samples_(0.0f),
      max_delay_samples_(0.0f)
{
    bypassed_ = true;
}

void DelayEffect::Init(float sample_rate) {
    Effect::Init(sample_rate);
    
    min_delay_samples_ = kMinDelaySeconds * sample_rate;
    max_delay_samples_ = static_cast<float>(kMaxDelaySize);
    
    if (delay_l_.del) delay_l_.del->Init();
    if (delay_r_.del) delay_r_.del->Init();
}

void DelayEffect::SetDelayLines(daisysp::DelayLine<float, kMaxDelaySize>* left,
                                 daisysp::DelayLine<float, kMaxDelaySize>* right) {
    delay_l_.del = left;
    delay_r_.del = right;
}

void DelayEffect::InitControls(daisy::AnalogControl* time_knob,
                                daisy::AnalogControl* feedback_knob,
                                daisy::AnalogControl* mix_knob) {
    // Time uses logarithmic scaling for musical feel
    p_time_.Init(*time_knob, min_delay_samples_, max_delay_samples_, daisy::Parameter::LOGARITHMIC);
    p_feedback_.Init(*feedback_knob, 0.0f, 1.0f, daisy::Parameter::LINEAR);
    p_mix_.Init(*mix_knob, 0.0f, kMaxDryWet, daisy::Parameter::LINEAR);
}

void DelayEffect::ProcessControls(FlickCore* core, int sw1, int sw2, int sw3) {
    // Process delay time - check if under tap tempo control
    float time_val = p_time_.Process();
    if (time_knob_takeover_.hasTakenOver()) {
        master_delay_samples_ = time_val;
    }
    // If not taken over, delay time is set externally (tap tempo)
    
    // Map switch 3 to subdivision
    // Hothouse switch: UP=0, MIDDLE=1, DOWN=2
    static const Subdivision kSubdivMap[] = {
        SUBDIV_DOTTED_EIGHTH,    // UP (0): 0.75x - 3/4 of quarter note
        SUBDIV_NORMAL,           // MIDDLE (1): 1.0x - quarter note
        SUBDIV_QUARTER_TRIPLET,  // DOWN (2): 0.666x - 2/3 of quarter note
    };
    current_subdivision_ = kSubdivMap[sw3];
    ApplySubdivision(current_subdivision_);
    
    // Process feedback and mix
    delay_l_.feedback = delay_r_.feedback = p_feedback_.Process();
    dry_wet_percent_ = static_cast<int>(p_mix_.Process());
}

void DelayEffect::ApplySubdivision(Subdivision subdiv) {
    float multiplier = 1.0f;
    switch (subdiv) {
        case SUBDIV_QUARTER_TRIPLET:
            multiplier = 2.0f / 3.0f;
            break;
        case SUBDIV_DOTTED_EIGHTH:
            multiplier = 0.75f;
            break;
        default:
            multiplier = 1.0f;
            break;
    }
    
    float final_delay = master_delay_samples_ * multiplier;
    final_delay = daisysp::fclamp(final_delay, min_delay_samples_, max_delay_samples_);
    
    delay_l_.delay_target = final_delay;
    delay_r_.delay_target = final_delay;
}

void DelayEffect::ProcessSample(float in_l, float in_r, float& out_l, float& out_r) {
    if (bypassed_) {
        out_l = in_l;
        out_r = in_r;
        return;
    }
    
    // Process delay lines
    float wet_l = delay_l_.Process(in_l);
    float wet_r = delay_r_.Process(in_r);
    
    // Calculate mix
    float mix = static_cast<float>(dry_wet_percent_) / kMaxDryWet;
    
    // Determine makeup gain
    float makeup = 1.0f;
    switch (makeup_gain_) {
        case MAKEUP_NORMAL: makeup = 1.66f; break;
        case MAKEUP_HEAVY:  makeup = 2.0f;  break;
        default: break;
    }
    
    // Mix wet and dry signals
    // Wet signal is attenuated, dry signal gets makeup gain
    out_l = mix * wet_l * kWetMixAttenuation + (1.0f - mix) * in_l * makeup;
    out_r = mix * wet_r * kWetMixAttenuation + (1.0f - mix) * in_r * makeup;
}

void DelayEffect::SetDelayTime(float samples) {
    master_delay_samples_ = daisysp::fclamp(samples, min_delay_samples_, max_delay_samples_);
    ApplySubdivision(current_subdivision_);
}

void DelayEffect::CaptureTimeKnob() {
    time_knob_takeover_.capture(p_time_.Value());
}

bool DelayEffect::CheckTimeKnobTakeover(float current_value) {
    return time_knob_takeover_.checkTakeover(current_value);
}

} // namespace flick
