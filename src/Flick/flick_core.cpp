#include "flick_core.h"

FlickCore::FlickCore()
    : sample_rate_(48000.0f),
      pedal_mode_(PEDAL_MODE_NORMAL),
      led_left_brightness_(0.0f),
      led_right_brightness_(0.0f),
      tap_tempo_active_(false),
      tap_tempo_last_tap_time_(0),
      tap_tempo_interval_ms_(0),
      tap_tempo_delay_samples_(0.0f),
      tap_tempo_controls_delay_(false),
      tap_tempo_tremolo_freq_hz_(0.0f),
      tap_tempo_controls_tremolo_(false),
      tap_tempo_samples_min_(0.0f),
      tap_tempo_samples_max_(0.0f),
      trigger_settings_save_(false),
      current_makeup_gain_(MAKEUP_GAIN_NORMAL),
      is_factory_reset_mode_(false),
      factory_reset_stage_(0),
      reset_blink_interval_(1000),
      last_led_toggle_time_(0),
      led_toggle_state_(false)
{
}

FlickCore::~FlickCore() {
}

void FlickCore::Init(float sample_rate,
                     DelayLine<float, MAX_DELAY_SIZE>* delay_line_l,
                     DelayLine<float, MAX_DELAY_SIZE>* delay_line_r) {
    sample_rate_ = sample_rate;

    // Calculate tap tempo limits
    tap_tempo_samples_min_ = (TAP_TEMPO_MIN_INTERVAL_MS / MS_PER_SECOND) * sample_rate_;
    tap_tempo_samples_max_ = (TAP_TEMPO_MAX_INTERVAL_MS / MS_PER_SECOND) * sample_rate_;

    // Initialize effects
    reverb_.Init(sample_rate);
    tremolo_.Init(sample_rate);
    
    delay_.SetDelayLines(delay_line_l, delay_line_r);
    delay_.Init(sample_rate);
}

void FlickCore::InitControls(daisy::AnalogControl* k1, daisy::AnalogControl* k2,
                             daisy::AnalogControl* k3, daisy::AnalogControl* k4,
                             daisy::AnalogControl* k5, daisy::AnalogControl* k6) {
    // Initialize effect-specific controls
    reverb_.InitControls(k1, k2, k3, k4, k5, k6);
    tremolo_.InitControls(k2, k3);  // Speed (k2), Depth (k3)
    delay_.InitControls(k4, k5, k6); // Time (k4), Feedback (k5), Mix (k6)

    // Generic knob for factory reset
    p_knob_1_.Init(*k1, 0.0f, 1.0f, daisy::Parameter::LINEAR);
}

void FlickCore::ProcessControls(int sw1, int sw2, int sw3) {
    if (is_factory_reset_mode_) {
        UpdateFactoryReset();
        return;
    }

    // Update LED states based on mode
    updateLeds();

    // Process based on current mode
    switch (pedal_mode_) {
        case PEDAL_MODE_NORMAL: {
            // Normal mode: process all effects with their normal controls
            
            // Reverb wet knob processed in audio callback, but we set mode here
            reverb_.ProcessControls(this, sw1, sw2, sw3);
            
            // Tremolo (SW2 controls mode)
            if (tap_tempo_controls_tremolo_) {
                // Under tap tempo control
                float speed_val = tremolo_.GetFrequency();  // Keep current tap tempo frequency
                tremolo_.ProcessControls(this, sw1, sw2, sw3);
                // If knob hasn't taken over, restore tap tempo frequency
                if (!tremolo_.CheckSpeedKnobTakeover(speed_val)) {
                    tremolo_.SetFrequency(tap_tempo_tremolo_freq_hz_);
                } else {
                    tap_tempo_controls_tremolo_ = false;
                }
            } else {
                tremolo_.ProcessControls(this, sw1, sw2, sw3);
            }

            // Delay (SW3 controls subdivision)
            if (tap_tempo_controls_delay_) {
                float time_val = delay_.GetDelayTime();
                delay_.ProcessControls(this, sw1, sw2, sw3);
                if (!delay_.CheckTimeKnobTakeover(time_val)) {
                    delay_.SetDelayTime(tap_tempo_delay_samples_);
                } else {
                    tap_tempo_controls_delay_ = false;
                }
            } else {
                delay_.ProcessControls(this, sw1, sw2, sw3);
            }

            // Makeup gain from SW2 mapped for both effects
            static const TremDelMakeUpGain kMakeupMap[] = {MAKEUP_GAIN_HEAVY, MAKEUP_GAIN_NORMAL, MAKEUP_GAIN_NONE};
            current_makeup_gain_ = kMakeupMap[sw2];
            tremolo_.SetMakeupGain(static_cast<flick::TremoloEffect::MakeupGain>(current_makeup_gain_));
            delay_.SetMakeupGain(static_cast<flick::DelayEffect::MakeupGain>(current_makeup_gain_));
            break;
        }

        case PEDAL_MODE_EDIT_REVERB:
            // Reverb edit mode: all controls go to reverb
            reverb_.ProcessControls(this, sw1, sw2, sw3);
            break;

        case PEDAL_MODE_EDIT_MONO_STEREO: {
            // Mono/stereo edit mode
            // SW3 controls mono/stereo mode
            static const MonoStereoMode kMsMap[] = {MS_MODE_MIMO, MS_MODE_MISO, MS_MODE_SISO};
            reverb_.SetMonoStereoMode(kMsMap[sw3]);
            
            // SW2 controls makeup gain
            static const TremDelMakeUpGain kMakeupMap[] = {MAKEUP_GAIN_HEAVY, MAKEUP_GAIN_NORMAL, MAKEUP_GAIN_NONE};
            current_makeup_gain_ = kMakeupMap[sw2];
            break;
        }

        case PEDAL_MODE_TAP_TEMPO:
            // In tap tempo mode, apply delay subdivision
            if (tap_tempo_controls_delay_) {
                delay_.SetDelayTime(tap_tempo_delay_samples_);
                delay_.ApplySubdivision(static_cast<flick::DelayEffect::Subdivision>(sw3));
            }
            if (tap_tempo_controls_tremolo_) {
                tremolo_.SetFrequency(tap_tempo_tremolo_freq_hz_);
            }
            break;
    }
}

void FlickCore::ProcessAudio(const float* in_l, const float* in_r,
                              float* out_l, float* out_r, size_t size) {
    // Process reverb wet knob in normal mode (needs per-block processing)
    if (pedal_mode_ == PEDAL_MODE_NORMAL) {
        reverb_.ProcessWetKnob();
    }

    MonoStereoMode ms_mode = reverb_.GetMonoStereoMode();

    for (size_t i = 0; i < size; ++i) {
        float s_l = in_l[i];
        float s_r;
        
        // Handle mono/stereo input
        if (ms_mode == MS_MODE_MIMO || ms_mode == MS_MODE_MISO) {
            s_r = s_l;
        } else {
            s_r = in_r[i];
        }

        // Effect chain: Delay -> Tremolo -> Reverb
        float d_l = s_l, d_r = s_r;
        delay_.ProcessSample(s_l, s_r, d_l, d_r);

        float t_l = d_l, t_r = d_r;
        tremolo_.ProcessSample(d_l, d_r, t_l, t_r);

        float r_l = t_l, r_r = t_r;
        reverb_.ProcessSample(t_l, t_r, r_l, r_r);

        // Handle mono/stereo output
        if (ms_mode == MS_MODE_MIMO) {
            out_l[i] = (r_l + r_r) * 0.5f;
            out_r[i] = 0.0f;
        } else {
            out_l[i] = r_l;
            out_r[i] = r_r;
        }
    }

    // Update right LED in normal mode based on tremolo/delay activity
    if (pedal_mode_ == PEDAL_MODE_NORMAL) {
        float trem_val = tremolo_.GetLedModulationValue();
        bool trem_bypassed = tremolo_.IsBypassed();
        bool delay_bypassed = delay_.IsBypassed();

        if (trem_bypassed) {
            led_right_brightness_ = delay_bypassed ? 0.0f : 1.0f;
        } else {
            constexpr float kTremLedBrightness = 0.4f;
            led_right_brightness_ = delay_bypassed ? trem_val * kTremLedBrightness : trem_val;
        }
    }
}

void FlickCore::updateLeds() {
    uint32_t now = daisy::System::GetNow();

    switch (pedal_mode_) {
        case PEDAL_MODE_NORMAL:
            led_left_brightness_ = reverb_.IsBypassed() ? 0.0f : 1.0f;
            // Right LED handled in ProcessAudio
            break;

        case PEDAL_MODE_EDIT_REVERB:
            // Both LEDs blink together
            if ((now % 500) < 250) {
                led_left_brightness_ = 1.0f;
                led_right_brightness_ = 1.0f;
            } else {
                led_left_brightness_ = 0.0f;
                led_right_brightness_ = 0.0f;
            }
            break;

        case PEDAL_MODE_EDIT_MONO_STEREO:
            // LEDs alternate
            if ((now % 500) < 250) {
                led_left_brightness_ = 1.0f;
                led_right_brightness_ = 0.0f;
            } else {
                led_left_brightness_ = 0.0f;
                led_right_brightness_ = 1.0f;
            }
            break;

        case PEDAL_MODE_TAP_TEMPO: {
            // Left LED slow pulse
            bool slow_pulse = (now % 1000) < 500;
            led_left_brightness_ = slow_pulse ? 1.0f : 0.1f;

            // Right LED blinks at tap tempo rate
            if (tap_tempo_interval_ms_ > 0) {
                uint32_t blink_phase = now % tap_tempo_interval_ms_;
                float threshold = tap_tempo_interval_ms_ * TAP_TEMPO_BLINK_DUTY_CYCLE;
                led_right_brightness_ = (blink_phase < threshold) ? 1.0f : 0.1f;
            } else {
                led_right_brightness_ = slow_pulse ? 1.0f : 0.1f;
            }
            break;
        }
    }
}

void FlickCore::HandleNormalPress(int fs_idx) {
    switch (pedal_mode_) {
        case PEDAL_MODE_TAP_TEMPO:
            if (fs_idx == 0) {
                exitTapTempoMode();
            } else {
                handleTapTempoTap();
            }
            break;

        case PEDAL_MODE_EDIT_REVERB:
            if (fs_idx == 1) {
                // Save changes
                reverb_.ExitEditMode(true);
                saved_state_ = GetSettings();
                trigger_settings_save_ = true;
            } else {
                // Cancel changes
                reverb_.ExitEditMode(false);
            }
            pedal_mode_ = PEDAL_MODE_NORMAL;
            break;

        case PEDAL_MODE_EDIT_MONO_STEREO:
            if (fs_idx == 1) {
                saveMonoStereoSettings();
            } else {
                restoreMonoStereoSettings();
            }
            pedal_mode_ = PEDAL_MODE_NORMAL;
            break;

        default:  // PEDAL_MODE_NORMAL
            if (fs_idx == 0) {
                reverb_.ToggleBypass();
            } else {
                delay_.ToggleBypass();
            }
            saveBypassStates();
            break;
    }
}

void FlickCore::HandleDoublePress(int fs_idx) {
    if (pedal_mode_ != PEDAL_MODE_NORMAL) return;

    // Reverse the normal press that was already processed
    HandleNormalPress(fs_idx);

    if (fs_idx == 0) {
        enterTapTempoMode();
    } else {
        tremolo_.ToggleBypass();
        saveBypassStates();
    }
}

void FlickCore::HandleLongPress(int fs_idx) {
    if (fs_idx == 0) {
        // Enter reverb edit mode
        reverb_.SetBypassed(false);
        reverb_.EnterEditMode();
        pedal_mode_ = PEDAL_MODE_EDIT_REVERB;
    } else {
        // Enter mono/stereo edit mode
        reverb_.SetBypassed(false);
        delay_.SetBypassed(true);
        tremolo_.SetBypassed(true);
        pedal_mode_ = PEDAL_MODE_EDIT_MONO_STEREO;
    }
}

void FlickCore::enterTapTempoMode() {
    tap_tempo_active_ = true;
    tap_tempo_last_tap_time_ = daisy::System::GetNow();

    bool delay_active = !delay_.IsBypassed();
    bool tremolo_active = !tremolo_.IsBypassed();

    // Determine what tap tempo should control
    if (!delay_active && !tremolo_active) {
        tap_tempo_controls_delay_ = true;
        tap_tempo_controls_tremolo_ = true;
    } else if (delay_active && tremolo_active) {
        tap_tempo_controls_delay_ = true;
        tap_tempo_controls_tremolo_ = true;
    } else if (delay_active && !tremolo_active) {
        tap_tempo_controls_delay_ = true;
        tap_tempo_controls_tremolo_ = false;
    } else {
        tap_tempo_controls_delay_ = false;
        tap_tempo_controls_tremolo_ = true;
    }

    // Capture current knob positions for soft takeover
    delay_.CaptureTimeKnob();
    tremolo_.CaptureSpeedKnob();

    pedal_mode_ = PEDAL_MODE_TAP_TEMPO;
}

void FlickCore::exitTapTempoMode() {
    tap_tempo_active_ = false;
    pedal_mode_ = PEDAL_MODE_NORMAL;
}

void FlickCore::handleTapTempoTap() {
    uint32_t current_time = daisy::System::GetNow();
    
    if (tap_tempo_last_tap_time_ > 0) {
        uint32_t interval = current_time - tap_tempo_last_tap_time_;
        
        if (interval >= TAP_TEMPO_MIN_INTERVAL_MS && interval <= TAP_TEMPO_MAX_INTERVAL_MS) {
            tap_tempo_interval_ms_ = interval;
            
            // Calculate delay time in samples
            tap_tempo_delay_samples_ = (interval / MS_PER_SECOND) * sample_rate_;
            tap_tempo_delay_samples_ = daisysp::fclamp(tap_tempo_delay_samples_,
                                                       tap_tempo_samples_min_,
                                                       tap_tempo_samples_max_);
            
            // Calculate tremolo frequency
            tap_tempo_tremolo_freq_hz_ = MS_PER_SECOND / interval;
            tap_tempo_tremolo_freq_hz_ = daisysp::fclamp(tap_tempo_tremolo_freq_hz_,
                                                         flick::TremoloEffect::kSpeedMin,
                                                         flick::TremoloEffect::kSpeedMax);

            // Apply to effects
            if (tap_tempo_controls_delay_) {
                delay_.SetDelayTime(tap_tempo_delay_samples_);
            }
            if (tap_tempo_controls_tremolo_) {
                tremolo_.SetFrequency(tap_tempo_tremolo_freq_hz_);
            }
        }
    }
    
    tap_tempo_last_tap_time_ = current_time;
}

void FlickCore::CheckTapTempoTimeout() {
    if (pedal_mode_ == PEDAL_MODE_TAP_TEMPO) {
        uint32_t current_time = daisy::System::GetNow();
        if ((current_time - tap_tempo_last_tap_time_) >= TAP_TEMPO_TIMEOUT_MS) {
            exitTapTempoMode();
        }
    }
}

Settings FlickCore::GetSettings() const {
    Settings s;
    s.version = SETTINGS_VERSION;
    
    // Get reverb settings
    flick::ReverbEffect::Settings rs = reverb_.GetSettings();
    s.decay = rs.decay;
    s.diffusion = rs.diffusion;
    s.input_cutoff_freq = rs.input_cutoff_freq;
    s.tank_cutoff_freq = rs.tank_cutoff_freq;
    s.tank_mod_speed = rs.tank_mod_speed;
    s.tank_mod_depth = rs.tank_mod_depth;
    s.tank_mod_shape = rs.tank_mod_shape;
    s.pre_delay = rs.pre_delay;
    s.mono_stereo_mode = rs.mono_stereo_mode;
    
    s.makeup_gain_mode = static_cast<int>(current_makeup_gain_);
    s.bypass_reverb = reverb_.IsBypassed();
    s.bypass_delay = delay_.IsBypassed();
    s.bypass_tremolo = tremolo_.IsBypassed();
    
    return s;
}

void FlickCore::SetSettings(const Settings& s) {
    if (s.version != SETTINGS_VERSION) {
        RestoreDefaults();
        saved_state_ = GetSettings();
        return;
    }
    
    saved_state_ = s;
    
    // Set reverb settings
    flick::ReverbEffect::Settings rs;
    rs.decay = s.decay;
    rs.diffusion = s.diffusion;
    rs.input_cutoff_freq = s.input_cutoff_freq;
    rs.tank_cutoff_freq = s.tank_cutoff_freq;
    rs.tank_mod_speed = s.tank_mod_speed;
    rs.tank_mod_depth = s.tank_mod_depth;
    rs.tank_mod_shape = s.tank_mod_shape;
    rs.pre_delay = s.pre_delay;
    rs.mono_stereo_mode = s.mono_stereo_mode;
    reverb_.SetSettings(rs);
    
    current_makeup_gain_ = static_cast<TremDelMakeUpGain>(s.makeup_gain_mode);
    
    reverb_.SetBypassed(s.bypass_reverb);
    delay_.SetBypassed(s.bypass_delay);
    tremolo_.SetBypassed(s.bypass_tremolo);
}

void FlickCore::RestoreDefaults() {
    reverb_.RestoreDefaults();
    reverb_.SetBypassed(true);
    delay_.SetBypassed(true);
    tremolo_.SetBypassed(true);
    current_makeup_gain_ = MAKEUP_GAIN_NORMAL;
}

void FlickCore::saveBypassStates() {
    saved_state_.bypass_reverb = reverb_.IsBypassed();
    saved_state_.bypass_delay = delay_.IsBypassed();
    saved_state_.bypass_tremolo = tremolo_.IsBypassed();
    trigger_settings_save_ = true;
}

void FlickCore::saveMonoStereoSettings() {
    saved_state_.mono_stereo_mode = static_cast<int>(reverb_.GetMonoStereoMode());
    saved_state_.makeup_gain_mode = static_cast<int>(current_makeup_gain_);
    trigger_settings_save_ = true;
}

void FlickCore::restoreMonoStereoSettings() {
    reverb_.SetMonoStereoMode(static_cast<MonoStereoMode>(saved_state_.mono_stereo_mode));
    current_makeup_gain_ = static_cast<TremDelMakeUpGain>(saved_state_.makeup_gain_mode);
}

void FlickCore::UpdateFactoryReset() {
    constexpr float low_threshold = 0.05f;
    constexpr float high_threshold = 0.95f;
    constexpr uint32_t blink_faster_amount = 300;

    uint32_t now = daisy::System::GetNow();
    if (now - last_led_toggle_time_ >= reset_blink_interval_) {
        last_led_toggle_time_ = now;
        led_toggle_state_ = !led_toggle_state_;
        led_left_brightness_ = led_toggle_state_ ? 1.0f : 0.0f;
        led_right_brightness_ = led_toggle_state_ ? 0.0f : 1.0f;
    }

    float knob_value = p_knob_1_.Process();

    bool advance = false;
    if (factory_reset_stage_ == 0 && knob_value >= high_threshold) advance = true;
    else if (factory_reset_stage_ == 1 && knob_value <= low_threshold) advance = true;
    else if (factory_reset_stage_ == 2 && knob_value >= high_threshold) advance = true;
    else if (factory_reset_stage_ == 3 && knob_value <= low_threshold) {
        // Factory reset complete
        RestoreDefaults();
        trigger_settings_save_ = true;
        is_factory_reset_mode_ = false;
        factory_reset_stage_ = 0;
        pedal_mode_ = PEDAL_MODE_NORMAL;
        led_left_brightness_ = 1.0f;
        led_right_brightness_ = 1.0f;
        return;
    }

    if (advance) {
        factory_reset_stage_++;
        if (reset_blink_interval_ > blink_faster_amount) {
            reset_blink_interval_ -= blink_faster_amount;
        }
        led_left_brightness_ = 1.0f;
        led_right_brightness_ = 1.0f;
    }
}
