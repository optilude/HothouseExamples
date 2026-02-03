#include "flick_tremolo_effect.h"
#include "flick_core.h"
#include "daisysp.h"

namespace flick {

TremoloEffect::TremoloEffect()
    : current_mode_(MODE_SINE),
      makeup_gain_(MAKEUP_NORMAL),
      dc_offset_(1.0f),
      last_trem_value_(0.0f),
      current_lfo_sample_(0.0f),
      speed_knob_takeover_()
{
    bypassed_ = true;
}

void TremoloEffect::Init(float sample_rate) {
    Effect::Init(sample_rate);
    
    osc_.Init(sample_rate);
    osc_.SetFreq(2.0f);  // Default 2Hz
    osc_.SetAmp(0.5f);
    osc_.SetWaveform(FlickOscillator::WAVE_SIN);
    
    // Initialize harmonic tremolo filters
    low_pass_l_.Init(kHarmonicLPFCutoff, sample_rate);
    low_pass_r_.Init(kHarmonicLPFCutoff, sample_rate);
    high_pass_l_.Init(kHarmonicHPFCutoff, sample_rate);
    high_pass_r_.Init(kHarmonicHPFCutoff, sample_rate);
}

void TremoloEffect::InitControls(daisy::AnalogControl* speed_knob, daisy::AnalogControl* depth_knob) {
    p_speed_.Init(*speed_knob, kSpeedMin, kSpeedMax, daisy::Parameter::LOGARITHMIC);
    p_depth_.Init(*depth_knob, 0.0f, 1.0f, daisy::Parameter::LINEAR);
}

void TremoloEffect::ProcessControls(FlickCore* core, int sw1, int sw2, int sw3) {
    // Process speed - check if we're under tap tempo control
    float speed_val = p_speed_.Process();
    if (speed_knob_takeover_.hasTakenOver()) {
        osc_.SetFreq(speed_val);
    }
    // If not taken over, frequency is set externally (tap tempo)
    
    // Process depth
    float depth = daisysp::fclamp(p_depth_.Process(), 0.0f, 1.0f);
    
    // Determine mode from switch 2
    // Map: 0=DOWN, 1=MIDDLE, 2=UP
    // Original mapping: DOWN=Sine, MIDDLE=Harmonic, UP=Square
    static const Mode kModeMap[] = {MODE_SINE, MODE_HARMONIC, MODE_SQUARE};
    current_mode_ = kModeMap[sw2];
    
    // Adjust depth based on mode
    if (current_mode_ == MODE_HARMONIC) {
        depth *= 1.25f;
    } else {
        depth *= 0.5f;
    }
    
    osc_.SetAmp(depth);
    dc_offset_ = 1.0f - depth;
    
    // Set waveform
    if (current_mode_ == MODE_SQUARE) {
        osc_.SetWaveform(FlickOscillator::WAVE_SQUARE_ROUNDED);
    } else {
        osc_.SetWaveform(FlickOscillator::WAVE_SIN);
    }
}

void TremoloEffect::ProcessSample(float in_l, float in_r, float& out_l, float& out_r) {
    if (bypassed_) {
        out_l = in_l;
        out_r = in_r;
        last_trem_value_ = 0.0f;
        return;
    }
    
    // Generate LFO
    current_lfo_sample_ = osc_.Process();
    float trem_val = dc_offset_ + current_lfo_sample_;
    last_trem_value_ = trem_val;
    
    // Determine makeup gain
    float makeup = 1.0f;
    switch (makeup_gain_) {
        case MAKEUP_NORMAL: makeup = 1.2f; break;
        case MAKEUP_HEAVY:  makeup = 1.6f; break;
        default: break;
    }
    
    if (current_mode_ == MODE_HARMONIC) {
        // Harmonic tremolo: split into low and high bands, modulate inversely
        float low_l = low_pass_l_.Process(in_l);
        float high_l = high_pass_l_.Process(in_l);
        float low_mod_l = low_l * (1.0f + current_lfo_sample_);
        float high_mod_l = high_l * (1.0f - current_lfo_sample_);
        out_l = (low_mod_l + high_mod_l) * makeup;
        
        float low_r = low_pass_r_.Process(in_r);
        float high_r = high_pass_r_.Process(in_r);
        float low_mod_r = low_r * (1.0f + current_lfo_sample_);
        float high_mod_r = high_r * (1.0f - current_lfo_sample_);
        out_r = (low_mod_r + high_mod_r) * makeup;
    } else {
        // Standard tremolo (sine or square)
        out_l = in_l * trem_val * makeup;
        out_r = in_r * trem_val * makeup;
    }
}

void TremoloEffect::SetFrequency(float freq_hz) {
    freq_hz = daisysp::fclamp(freq_hz, kSpeedMin, kSpeedMax);
    osc_.SetFreq(freq_hz);
}

void TremoloEffect::CaptureSpeedKnob() {
    speed_knob_takeover_.capture(p_speed_.Value());
}

bool TremoloEffect::CheckSpeedKnobTakeover(float current_value) {
    return speed_knob_takeover_.checkTakeover(current_value);
}

} // namespace flick
