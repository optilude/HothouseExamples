#include "flick_reverb_effect.h"
#include "flick_core.h"

namespace flick {

ReverbEffect::ReverbEffect()
    : verb_(48000, 16, 4.0),
      in_edit_mode_(false),
      diffusion_enabled_(true),
      pre_delay_(0.0f),
      plate_dry_(1.0f),
      plate_wet_(0.5f),
      decay_(0.8f),
      time_scale_(1.0075f),
      tank_diffusion_(0.85f),
      input_damp_low_(2.87f),
      input_damp_high_(7.25f),
      tank_damp_low_(2.87f),
      tank_damp_high_(7.25f),
      tank_mod_speed_(0.1f),
      tank_mod_depth_(0.1f),
      tank_mod_shape_(0.25f),
      reverb_dry_scale_factor_(1.0f),
      reverb_reverse_scale_factor_(1.0f),
      knob_mode_(KNOB_DRY_WET_MIX),
      mono_stereo_mode_(MS_MODE_MIMO)
{
    bypassed_ = true;
    saved_settings_ = GetSettings();
}

void ReverbEffect::Init(float sample_rate) {
    Effect::Init(sample_rate);
    
    verb_.setSampleRate(sample_rate);
    verb_.setTimeScale(time_scale_);
    verb_.enableInputDiffusion(diffusion_enabled_);
    verb_.setInputFilterLowCutoffPitch(input_damp_low_);
    verb_.setTankFilterLowCutFrequency(tank_damp_low_);
    
    // Apply current settings
    verb_.setDecay(decay_);
    verb_.setTankDiffusion(tank_diffusion_);
    verb_.setInputFilterHighCutoffPitch(input_damp_high_);
    verb_.setTankFilterHighCutFrequency(tank_damp_high_);
    verb_.setTankModSpeed(tank_mod_speed_ * 8);
    verb_.setTankModDepth(tank_mod_depth_ * 15);
    verb_.setTankModShape(tank_mod_shape_);
    verb_.setPreDelay(pre_delay_);
    
    updateReverbScales();
}

void ReverbEffect::InitControls(daisy::AnalogControl* k1, daisy::AnalogControl* k2,
                                daisy::AnalogControl* k3, daisy::AnalogControl* k4,
                                daisy::AnalogControl* k5, daisy::AnalogControl* k6) {
    p_wet_amount_.Init(*k1, 0.0f, 1.0f, daisy::Parameter::LINEAR);
    p_knob_2_.Init(*k2, 0.0f, 1.0f, daisy::Parameter::LINEAR);
    p_knob_3_.Init(*k3, 0.0f, 1.0f, daisy::Parameter::LINEAR);
    p_knob_4_.Init(*k4, 0.0f, 1.0f, daisy::Parameter::LINEAR);
    p_knob_5_.Init(*k5, 0.0f, 1.0f, daisy::Parameter::LINEAR);
    p_knob_6_.Init(*k6, 0.0f, 1.0f, daisy::Parameter::LINEAR);
}

void ReverbEffect::ProcessWetKnob() {
    if (!in_edit_mode_) {
        plate_wet_ = p_wet_amount_.Process();
    }
}

void ReverbEffect::ProcessControls(FlickCore* core, int sw1, int sw2, int sw3) {
    if (in_edit_mode_) {
        // Edit mode: all knobs and switches control reverb parameters
        plate_dry_ = 1.0f;  // Always 100% dry in edit mode for monitoring
        
        // Knobs with soft takeover
        if (edit_wet_knob_.checkTakeover(p_wet_amount_.Process())) {
            plate_wet_ = p_wet_amount_.Value();
        }
        if (edit_pre_delay_knob_.checkTakeover(p_knob_2_.Process())) {
            pre_delay_ = p_knob_2_.Value() * 0.25f;
        }
        if (edit_decay_knob_.checkTakeover(p_knob_3_.Process())) {
            decay_ = p_knob_3_.Value();
        }
        if (edit_diffusion_knob_.checkTakeover(p_knob_4_.Process())) {
            tank_diffusion_ = p_knob_4_.Value();
        }
        if (edit_input_cut_knob_.checkTakeover(p_knob_5_.Process())) {
            input_damp_high_ = p_knob_5_.Value() * 10.0f;
        }
        if (edit_tank_cut_knob_.checkTakeover(p_knob_6_.Process())) {
            tank_damp_high_ = p_knob_6_.Value() * 10.0f;
        }
        
        // Switches for modulation parameters
        static const float speed_vals[] = {0.5f, 0.25f, 0.1f};
        if (edit_mod_speed_switch_.checkChange(sw1)) {
            tank_mod_speed_ = speed_vals[sw1];
        }
        
        static const float depth_vals[] = {0.5f, 0.25f, 0.1f};
        if (edit_mod_depth_switch_.checkChange(sw2)) {
            tank_mod_depth_ = depth_vals[sw2];
        }
        
        static const float shape_vals[] = {0.5f, 0.25f, 0.1f};
        if (edit_mod_shape_switch_.checkChange(sw3)) {
            tank_mod_shape_ = shape_vals[sw3];
        }
        
        // Apply settings to verb
        verb_.setDecay(decay_);
        verb_.setTankDiffusion(tank_diffusion_);
        verb_.setInputFilterHighCutoffPitch(input_damp_high_);
        verb_.setTankFilterHighCutFrequency(tank_damp_high_);
        verb_.setTankModSpeed(tank_mod_speed_ * 8);
        verb_.setTankModDepth(tank_mod_depth_ * 15);
        verb_.setTankModShape(tank_mod_shape_);
        verb_.setPreDelay(pre_delay_);
    } else {
        // Normal mode: SW1 controls knob mode
        static const KnobMode kKnobModeMap[] = {KNOB_ALL_WET, KNOB_DRY_WET_MIX, KNOB_ALL_DRY};
        knob_mode_ = kKnobModeMap[sw1];
        
        switch (knob_mode_) {
            case KNOB_ALL_DRY:
                plate_dry_ = 1.0f;
                break;
            case KNOB_DRY_WET_MIX:
                plate_dry_ = 1.0f - plate_wet_;
                break;
            case KNOB_ALL_WET:
                plate_dry_ = 0.0f;
                break;
        }
    }
}

void ReverbEffect::ProcessSample(float in_l, float in_r, float& out_l, float& out_r) {
    // Apply dry scaling
    float left_input = in_l * reverb_dry_scale_factor_;
    float right_input = in_r * reverb_dry_scale_factor_;
    
    // Limit input
    left_input = (left_input > 1.0f) ? 1.0f : ((left_input < -1.0f) ? -1.0f : left_input);
    right_input = (right_input > 1.0f) ? 1.0f : ((right_input < -1.0f) ? -1.0f : right_input);
    
    // Always process verb (even when bypassed) to keep tails ready
    float gain = kMinus18dbGain * kMinus20dbGain * (1.0f + kInputAmplification * 7.0f);
    verb_.process(left_input * gain, right_input * gain);
    
    if (!bypassed_) {
        // Mix dry and wet
        out_l = (left_input * plate_dry_ * reverb_reverse_scale_factor_) + 
                (verb_.getLeftOutput() * plate_wet_);
        out_r = (right_input * plate_dry_ * reverb_reverse_scale_factor_) + 
                (verb_.getRightOutput() * plate_wet_);
    } else {
        // Bypassed: pass through scaled input
        out_l = in_l;
        out_r = in_r;
    }
}

void ReverbEffect::OnBypassEnabled() {
    verb_.clear();
}

void ReverbEffect::EnterEditMode() {
    in_edit_mode_ = true;
    bypassed_ = false;  // Force reverb on during edit
    
    // Capture current knob positions for soft takeover
    edit_wet_knob_.capture(p_wet_amount_.Value());
    edit_pre_delay_knob_.capture(p_knob_2_.Value());
    edit_decay_knob_.capture(p_knob_3_.Value());
    edit_diffusion_knob_.capture(p_knob_4_.Value());
    edit_input_cut_knob_.capture(p_knob_5_.Value());
    edit_tank_cut_knob_.capture(p_knob_6_.Value());
    
    // Reset switch detectors (use -1 to force first read to detect)
    edit_mod_speed_switch_.capture(-1);
    edit_mod_depth_switch_.capture(-1);
    edit_mod_shape_switch_.capture(-1);
    
    // Save current settings for potential cancellation
    saved_settings_ = GetSettings();
}

void ReverbEffect::ExitEditMode(bool save) {
    in_edit_mode_ = false;
    
    if (!save) {
        restoreFromSaved();
    } else {
        // Update saved settings to current
        saved_settings_ = GetSettings();
    }
}

void ReverbEffect::SetMonoStereoMode(MonoStereoMode mode) {
    mono_stereo_mode_ = mode;
    updateReverbScales();
}

void ReverbEffect::updateReverbScales() {
    switch (mono_stereo_mode_) {
        case MS_MODE_MIMO:
            reverb_dry_scale_factor_ = 5.0f;
            reverb_reverse_scale_factor_ = 0.2f;
            break;
        case MS_MODE_MISO:
        case MS_MODE_SISO:
            reverb_dry_scale_factor_ = 2.5f;
            reverb_reverse_scale_factor_ = 0.4f;
            break;
    }
}

void ReverbEffect::restoreFromSaved() {
    SetSettings(saved_settings_);
}

ReverbEffect::Settings ReverbEffect::GetSettings() const {
    Settings s;
    s.decay = decay_;
    s.diffusion = tank_diffusion_;
    s.input_cutoff_freq = input_damp_high_;
    s.tank_cutoff_freq = tank_damp_high_;
    s.tank_mod_speed = tank_mod_speed_;
    s.tank_mod_depth = tank_mod_depth_;
    s.tank_mod_shape = tank_mod_shape_;
    s.pre_delay = pre_delay_;
    s.mono_stereo_mode = static_cast<int>(mono_stereo_mode_);
    return s;
}

void ReverbEffect::SetSettings(const Settings& s) {
    decay_ = s.decay;
    tank_diffusion_ = s.diffusion;
    input_damp_high_ = s.input_cutoff_freq;
    tank_damp_high_ = s.tank_cutoff_freq;
    tank_mod_speed_ = s.tank_mod_speed;
    tank_mod_depth_ = s.tank_mod_depth;
    tank_mod_shape_ = s.tank_mod_shape;
    pre_delay_ = s.pre_delay;
    mono_stereo_mode_ = static_cast<MonoStereoMode>(s.mono_stereo_mode);
    
    // Apply to verb
    verb_.setDecay(decay_);
    verb_.setTankDiffusion(tank_diffusion_);
    verb_.setInputFilterHighCutoffPitch(input_damp_high_);
    verb_.setTankFilterHighCutFrequency(tank_damp_high_);
    verb_.setTankModSpeed(tank_mod_speed_ * 8);
    verb_.setTankModDepth(tank_mod_depth_ * 15);
    verb_.setTankModShape(tank_mod_shape_);
    verb_.setPreDelay(pre_delay_);
    
    updateReverbScales();
    saved_settings_ = s;
}

void ReverbEffect::RestoreDefaults() {
    decay_ = 0.8f;
    tank_diffusion_ = 0.85f;
    input_damp_high_ = 7.25f;
    tank_damp_high_ = 7.25f;
    tank_mod_speed_ = 0.1f;
    tank_mod_depth_ = 0.1f;
    tank_mod_shape_ = 0.25f;
    pre_delay_ = 0.0f;
    mono_stereo_mode_ = MS_MODE_MIMO;
    
    verb_.setDecay(decay_);
    verb_.setTankDiffusion(tank_diffusion_);
    verb_.setInputFilterHighCutoffPitch(input_damp_high_);
    verb_.setTankFilterHighCutFrequency(tank_damp_high_);
    verb_.setTankModSpeed(tank_mod_speed_ * 8);
    verb_.setTankModDepth(tank_mod_depth_ * 15);
    verb_.setTankModShape(tank_mod_shape_);
    verb_.setPreDelay(pre_delay_);
    
    updateReverbScales();
    saved_settings_ = GetSettings();
}

} // namespace flick
