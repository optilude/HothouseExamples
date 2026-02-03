#pragma once
#ifndef FLICK_REVERB_EFFECT_H
#define FLICK_REVERB_EFFECT_H

#include "flick_effect.h"
#include "flick_knob_takeover.h"
#include "Dattorro.hpp"

namespace flick {

/**
 * @brief Encapsulates the Dattorro plate reverb effect
 * 
 * Modes:
 * - Normal mode: Knob 1 controls wet/dry mix, SW1 controls mix behavior
 * - Edit mode: All knobs control reverb parameters (decay, diffusion, etc.)
 *              All switches control modulation parameters
 */
class ReverbEffect : public Effect {
public:
    enum KnobMode {
        KNOB_ALL_DRY,      // Wet knob only controls wet level, dry stays at 100%
        KNOB_DRY_WET_MIX,  // Wet and dry are inversely proportional
        KNOB_ALL_WET,      // Dry at 0%, only wet signal
    };

    // Mono/Stereo mode affects reverb scaling
    enum MonoStereoMode {
        MS_MODE_MIMO,  // Mono In, Mono Out
        MS_MODE_MISO,  // Mono In, Stereo Out
        MS_MODE_SISO   // Stereo In, Stereo Out
    };

    ReverbEffect();
    ~ReverbEffect() override {}

    void Init(float sample_rate) override;
    void ProcessSample(float in_l, float in_r, float& out_l, float& out_r) override;
    void ProcessControls(FlickCore* core, int sw1, int sw2, int sw3) override;
    
    void EnterEditMode() override;
    void ExitEditMode(bool save) override;
    bool IsInEditMode() const override { return in_edit_mode_; }
    void OnBypassEnabled() override;

    // Parameter accessors for external control
    void SetWetAmount(float wet) { plate_wet_ = wet; }
    float GetWetAmount() const { return plate_wet_; }
    
    void SetKnobMode(KnobMode mode) { knob_mode_ = mode; }
    void SetMonoStereoMode(MonoStereoMode mode);
    MonoStereoMode GetMonoStereoMode() const { return mono_stereo_mode_; }

    // Settings for persistence
    struct Settings {
        float decay;
        float diffusion;
        float input_cutoff_freq;
        float tank_cutoff_freq;
        float tank_mod_speed;
        float tank_mod_depth;
        float tank_mod_shape;
        float pre_delay;
        int mono_stereo_mode;
    };

    Settings GetSettings() const;
    void SetSettings(const Settings& settings);
    void RestoreDefaults();

    // Initialize parameter controls (called by FlickCore)
    void InitControls(daisy::AnalogControl* k1, daisy::AnalogControl* k2,
                      daisy::AnalogControl* k3, daisy::AnalogControl* k4,
                      daisy::AnalogControl* k5, daisy::AnalogControl* k6);

    // Process wet knob (Knob 1) - called in normal mode from audio callback
    void ProcessWetKnob();

    // Scaling factors for mono/stereo modes  
    float GetDryScaleFactor() const { return reverb_dry_scale_factor_; }
    float GetReverseScaleFactor() const { return reverb_reverse_scale_factor_; }

private:
    void updateReverbScales();
    void restoreFromSaved();

    Dattorro verb_;
    bool in_edit_mode_;
    
    // Parameters
    daisy::Parameter p_wet_amount_;
    daisy::Parameter p_knob_2_, p_knob_3_, p_knob_4_, p_knob_5_, p_knob_6_;

    // Soft takeover for edit mode
    KnobTakeover edit_wet_knob_;
    KnobTakeover edit_pre_delay_knob_;
    KnobTakeover edit_decay_knob_;
    KnobTakeover edit_diffusion_knob_;
    KnobTakeover edit_input_cut_knob_;
    KnobTakeover edit_tank_cut_knob_;
    SwitchChangeDetector edit_mod_speed_switch_;
    SwitchChangeDetector edit_mod_depth_switch_;
    SwitchChangeDetector edit_mod_shape_switch_;

    // Reverb internal state
    bool diffusion_enabled_;
    float pre_delay_;
    float plate_dry_;
    float plate_wet_;
    float decay_;
    float time_scale_;
    float tank_diffusion_;
    float input_damp_low_;
    float input_damp_high_;
    float tank_damp_low_;
    float tank_damp_high_;
    float tank_mod_speed_;
    float tank_mod_depth_;
    float tank_mod_shape_;

    // Scaling factors
    float reverb_dry_scale_factor_;
    float reverb_reverse_scale_factor_;

    // Mode settings
    KnobMode knob_mode_;
    MonoStereoMode mono_stereo_mode_;

    // Saved state for canceling edits
    Settings saved_settings_;

    // Constants
    static constexpr float kMinus18dbGain = 0.12589254f;
    static constexpr float kMinus20dbGain = 0.1f;
    static constexpr float kInputAmplification = 1.0f;
};

} // namespace flick

#endif // FLICK_REVERB_EFFECT_H
